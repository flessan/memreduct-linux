/*
 * Mem Reduct for Linux
 *
 * Lightweight real-time memory management application to monitor
 * and clean system memory.
 *
 * Linux port of Mem Reduct (https://github.com/henrypp/memreduct)
 * Original Windows version copyright (c) 2011-2026 Henry++
 *
 * Uses only POSIX/libc and the /proc filesystem, so it runs on every
 * Linux distribution (glibc and musl alike) on any kernel >= 2.6.16.
 *
 * Licensed under the GNU General Public License v3 (see LICENSE).
 */

#define _GNU_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syslog.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define APP_NAME        "Mem Reduct"
#define APP_VERSION     "1.3.0"
#define APP_BINARY      "memreduct"
#define APP_COPYRIGHT   "(c) 2011-2026 Henry++, Linux port contributors"

#define PROC_MEMINFO       "/proc/meminfo"
#define PROC_DROP_CACHES   "/proc/sys/vm/drop_caches"
#define PROC_COMPACT       "/proc/sys/vm/compact_memory"
#define PROC_PRESSURE      "/proc/pressure/memory"
#define PROC_PRESSURE_CPU   "/proc/pressure/cpu"
#define PROC_PRESSURE_IO    "/proc/pressure/io"
#define PROC_VMSTAT         "/proc/vmstat"
#define SYSTEM_CONFIG      "/etc/memreduct.conf"

/* cleaning flags (roughly equivalent to the Windows "memory areas") */
#define CLEAN_PAGECACHE  0x01  /* drop_caches 1: page cache (standby lists)   */
#define CLEAN_SLAB       0x02  /* drop_caches 2: dentries/inodes (system ws)  */
#define CLEAN_COMPACT    0x04  /* compact_memory: defragment physical memory  */
#define CLEAN_SWAP       0x08  /* swapoff/swapon: flush swap back to RAM      */
#define CLEAN_MODIFIEDCACHE 0x10 /* sync dirty file-backed pages to storage */
#define CLEAN_DEFAULT    (CLEAN_PAGECACHE | CLEAN_SLAB | CLEAN_COMPACT | CLEAN_MODIFIEDCACHE)

/*
 * Memory statistics (all values in KiB, taken from /proc/meminfo)
 */
typedef struct
{
	uint64_t total;
	uint64_t free;
	uint64_t available;
	uint64_t buffers;
	uint64_t cached;
	uint64_t sreclaimable;
	uint64_t sunreclaim;
	uint64_t active;
	uint64_t inactive;
	uint64_t active_anon;
	uint64_t inactive_anon;
	uint64_t active_file;
	uint64_t inactive_file;
	uint64_t dirty;
	uint64_t writeback;
	uint64_t anon_pages;
	uint64_t mapped;
	uint64_t slab;
	uint64_t kernel_stack;
	uint64_t page_tables;
	uint64_t commit_limit;
	uint64_t committed_as;
	uint64_t hugepages_total;
	uint64_t hugepages_free;
	uint64_t hugepages_size;
	uint64_t shmem;
	uint64_t swap_total;
	uint64_t swap_free;

	/* derived */
	uint64_t used;          /* total - available          */
	uint64_t cache;         /* reclaimable cache          */
	uint64_t swap_used;
	double   percent;       /* used physical memory, %    */
	double   swap_percent;
}
MEMORY_INFO;

/*
 * Configuration
 */
typedef struct
{
	int      clean_threshold;   /* auto-clean when usage >= N% (0 = off)    */
	int      clean_pressure;    /* auto-clean when PSI some avg10 >= N%      */
	int      clean_interval;    /* auto-clean every N minutes  (0 = off)    */
	int      check_interval;    /* daemon poll period, seconds              */
	int      cooldown;          /* min seconds between auto-cleans          */
	int      warning_level;     /* monitor: yellow at N%                    */
	int      danger_level;      /* monitor: red at N%                       */
	unsigned clean_flags;       /* what to clean                            */
	bool     clean_pressure_full; /* use PSI full avg10 instead of some       */
	bool     notifications;     /* desktop notifications (notify-send)      */
	bool     use_syslog;        /* daemon logs to syslog too                */
}
CONFIG;

static CONFIG config = {
	.clean_threshold = 90,
	.clean_pressure  = 0,
	.clean_pressure_full = false,
	.clean_interval  = 0,
	.check_interval  = 5,
	.cooldown        = 60,
	.warning_level   = 60,
	.danger_level    = 90,
	.clean_flags     = CLEAN_DEFAULT,
	.notifications   = true,
	.use_syslog      = true,
};

static volatile sig_atomic_t is_terminating = 0;
static volatile sig_atomic_t reload_requested = 0;

static struct termios saved_termios;
static bool termios_saved = false;

/*
 * -------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------
 */

static void signal_handler (int sig)
{
	if (sig == SIGHUP)
		reload_requested = 1;
	else
		is_terminating = 1;
}

static void install_signals (void)
{
	struct sigaction sa;

	memset (&sa, 0, sizeof (sa));
	sa.sa_handler = signal_handler;
	sigemptyset (&sa.sa_mask);

	sigaction (SIGINT, &sa, NULL);
	sigaction (SIGTERM, &sa, NULL);
	sigaction (SIGHUP, &sa, NULL);

	signal (SIGPIPE, SIG_IGN);
}

/* format KiB into a human readable string ("1.5 GB", "312.4 MB", ...) */
static const char *format_size (uint64_t kib, char *buffer, size_t length)
{
	double value = (double) kib;

	if (value >= 1024.0 * 1024.0)
		snprintf (buffer, length, "%.1f GB", value / (1024.0 * 1024.0));
	else if (value >= 1024.0)
		snprintf (buffer, length, "%.1f MB", value / 1024.0);
	else
		snprintf (buffer, length, "%.0f KB", value);

	return buffer;
}

static void timestamp_now (char *buffer, size_t length)
{
	time_t now = time (NULL);
	struct tm tm_now;

	localtime_r (&now, &tm_now);
	strftime (buffer, length, "%Y-%m-%d %H:%M:%S", &tm_now);
}

static bool command_exists (const char *name)
{
	const char *path_env = getenv ("PATH");
	char candidate[PATH_MAX];
	char *paths, *token, *saveptr = NULL;
	bool found = false;

	if (!path_env)
		path_env = "/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin";

	paths = strdup (path_env);

	if (!paths)
		return false;

	for (token = strtok_r (paths, ":", &saveptr); token; token = strtok_r (NULL, ":", &saveptr))
	{
		snprintf (candidate, sizeof (candidate), "%s/%s", token, name);

		if (access (candidate, X_OK) == 0)
		{
			found = true;
			break;
		}
	}

	free (paths);

	return found;
}

/* run an external command, return its exit code (or -1) */
static int run_command (char *const argv[])
{
	pid_t pid;
	int status;

	pid = fork ();

	if (pid < 0)
		return -1;

	if (pid == 0)
	{
		int devnull = open ("/dev/null", O_RDWR);

		if (devnull >= 0)
		{
			dup2 (devnull, STDOUT_FILENO);
			dup2 (devnull, STDERR_FILENO);

			if (devnull > STDERR_FILENO)
				close (devnull);
		}

		execvp (argv[0], argv);
		_exit (127);
	}

	if (waitpid (pid, &status, 0) < 0)
		return -1;

	if (WIFEXITED (status))
		return WEXITSTATUS (status);

	return -1;
}

/*
 * -------------------------------------------------------------------------
 * /proc/meminfo
 * -------------------------------------------------------------------------
 */

static bool memory_getinfo (MEMORY_INFO *info)
{
	FILE *fp;
	char line[256];
	char key[64];
	unsigned long long value;

	memset (info, 0, sizeof (*info));

	fp = fopen (PROC_MEMINFO, "r");

	if (!fp)
		return false;

	while (fgets (line, sizeof (line), fp))
	{
		if (sscanf (line, "%63[^:]: %llu", key, &value) != 2)
			continue;

		if (!strcmp (key, "MemTotal"))
			info->total = value;
		else if (!strcmp (key, "MemFree"))
			info->free = value;
		else if (!strcmp (key, "MemAvailable"))
			info->available = value;
		else if (!strcmp (key, "Buffers"))
			info->buffers = value;
		else if (!strcmp (key, "Cached"))
			info->cached = value;
		else if (!strcmp (key, "SReclaimable"))
			info->sreclaimable = value;
		else if (!strcmp (key, "SUnreclaim"))
			info->sunreclaim = value;
		else if (!strcmp (key, "Active"))
			info->active = value;
		else if (!strcmp (key, "Inactive"))
			info->inactive = value;
		else if (!strcmp (key, "Active(anon)"))
			info->active_anon = value;
		else if (!strcmp (key, "Inactive(anon)"))
			info->inactive_anon = value;
		else if (!strcmp (key, "Active(file)"))
			info->active_file = value;
		else if (!strcmp (key, "Inactive(file)"))
			info->inactive_file = value;
		else if (!strcmp (key, "Dirty"))
			info->dirty = value;
		else if (!strcmp (key, "Writeback"))
			info->writeback = value;
		else if (!strcmp (key, "AnonPages"))
			info->anon_pages = value;
		else if (!strcmp (key, "Mapped"))
			info->mapped = value;
		else if (!strcmp (key, "Slab"))
			info->slab = value;
		else if (!strcmp (key, "KernelStack"))
			info->kernel_stack = value;
		else if (!strcmp (key, "PageTables"))
			info->page_tables = value;
		else if (!strcmp (key, "CommitLimit"))
			info->commit_limit = value;
		else if (!strcmp (key, "Committed_AS"))
			info->committed_as = value;
		else if (!strcmp (key, "HugePages_Total"))
			info->hugepages_total = value;
		else if (!strcmp (key, "HugePages_Free"))
			info->hugepages_free = value;
		else if (!strcmp (key, "Hugepagesize"))
			info->hugepages_size = value;
		else if (!strcmp (key, "Shmem"))
			info->shmem = value;
		else if (!strcmp (key, "SwapTotal"))
			info->swap_total = value;
		else if (!strcmp (key, "SwapFree"))
			info->swap_free = value;
	}

	fclose (fp);

	if (!info->total)
		return false;

	/* pre-3.14 kernels have no MemAvailable, approximate it */
	if (!info->available)
		info->available = info->free + info->buffers + info->cached + info->sreclaimable;

	if (info->available > info->total)
		info->available = info->total;

	info->used = info->total - info->available;

	info->cache = info->buffers + info->cached + info->sreclaimable;

	if (info->cache > info->shmem)
		info->cache -= info->shmem; /* shmem is not reclaimable */

	info->swap_used = (info->swap_total > info->swap_free) ? (info->swap_total - info->swap_free) : 0;

	info->percent = 100.0 * (double) info->used / (double) info->total;
	info->swap_percent = info->swap_total ? (100.0 * (double) info->swap_used / (double) info->swap_total) : 0.0;

	return true;
}

/*
 * -------------------------------------------------------------------------
 * /proc/pressure/memory (PSI, kernel >= 4.20 - optional)
 * -------------------------------------------------------------------------
 */

typedef struct
{
	double some_avg10;
	double some_avg60;
	double some_avg300;
	uint64_t some_total_us;
	double full_avg10;
	double full_avg60;
	double full_avg300;
	uint64_t full_total_us;
	bool some_available;
	bool full_available;
}
PRESSURE_METRIC;

typedef struct
{
	PRESSURE_METRIC memory;
	PRESSURE_METRIC cpu;
	PRESSURE_METRIC io;
}
PRESSURE_INFO;

static void pressure_readmetric (const char *path, PRESSURE_METRIC *metric)
{
	FILE *fp;
	char line[256];

	fp = fopen (path, "r");

	if (!fp)
		return;

	while (fgets (line, sizeof (line), fp))
	{
		if (!strncmp (line, "some ", 5))
		{
			unsigned long long total = 0;

			if (sscanf (line, "some avg10=%lf avg60=%lf avg300=%lf total=%llu",
				&metric->some_avg10, &metric->some_avg60, &metric->some_avg300, &total) == 4)
			{
				metric->some_total_us = (uint64_t) total;
				metric->some_available = true;
			}
		}
		else if (!strncmp (line, "full ", 5))
		{
			unsigned long long total = 0;

			if (sscanf (line, "full avg10=%lf avg60=%lf avg300=%lf total=%llu",
				&metric->full_avg10, &metric->full_avg60, &metric->full_avg300, &total) == 4)
			{
				metric->full_total_us = (uint64_t) total;
				metric->full_available = true;
			}
		}
	}

	fclose (fp);
}

static void pressure_getinfo (PRESSURE_INFO *info)
{
	memset (info, 0, sizeof (*info));

	pressure_readmetric (PROC_PRESSURE, &info->memory);
	pressure_readmetric (PROC_PRESSURE_CPU, &info->cpu);
	pressure_readmetric (PROC_PRESSURE_IO, &info->io);
}

/*
 * -------------------------------------------------------------------------
 * Per-process memory usage (top by resident set size)
 * -------------------------------------------------------------------------
 */

typedef struct
{
	uint64_t rss;        /* KiB */
	pid_t pid;
	char name[32];
}
PROCESS_INFO;

static int process_gettop (PROCESS_INFO *list, int max_count)
{
	DIR *dir;
	struct dirent *entry;
	FILE *fp;
	char path[64];
	unsigned long size_pages, rss_pages;
	uint64_t rss, page_kib;
	int count = 0;

	page_kib = (uint64_t) sysconf (_SC_PAGESIZE) / 1024;

	dir = opendir ("/proc");

	if (!dir)
		return 0;

	while ((entry = readdir (dir)))
	{
		pid_t pid = (pid_t) atoi (entry->d_name);

		if (pid <= 0)
			continue;

		snprintf (path, sizeof (path), "/proc/%d/statm", pid);

		fp = fopen (path, "r");

		if (!fp)
			continue;

		if (fscanf (fp, "%lu %lu", &size_pages, &rss_pages) != 2)
			rss_pages = 0;

		fclose (fp);

		rss = (uint64_t) rss_pages * page_kib;

		if (!rss)
			continue; /* kernel thread */

		/* keep a sorted top-N list (insertion into a small array) */
		if (count < max_count || rss > list[count - 1].rss)
		{
			int pos = (count < max_count) ? count : (max_count - 1);

			while (pos > 0 && list[pos - 1].rss < rss)
			{
				list[pos] = list[pos - 1];
				pos -= 1;
			}

			list[pos].pid = pid;
			list[pos].rss = rss;
			list[pos].name[0] = 0;

			snprintf (path, sizeof (path), "/proc/%d/comm", pid);

			fp = fopen (path, "r");

			if (fp)
			{
				if (fgets (list[pos].name, sizeof (list[pos].name), fp))
					list[pos].name[strcspn (list[pos].name, "\n")] = 0;

				fclose (fp);
			}

			if (!list[pos].name[0])
				snprintf (list[pos].name, sizeof (list[pos].name), "[%d]", pid);

			if (count < max_count)
				count += 1;
		}
	}

	closedir (dir);

	return count;
}

/*
 * -------------------------------------------------------------------------
 * Memory cleaning
 * -------------------------------------------------------------------------
 */

static bool write_procfile (const char *path, const char *value)
{
	int fd;
	ssize_t written;

	fd = open (path, O_WRONLY);

	if (fd < 0)
		return false;

	written = write (fd, value, strlen (value));

	close (fd);

	return (written > 0);
}

static bool clean_swap_reload (const MEMORY_INFO *info)
{
	char *const swapoff_argv[] = {(char *) "swapoff", (char *) "-a", NULL};
	char *const swapon_argv[] = {(char *) "swapon", (char *) "-a", NULL};
	bool result;

	if (!info->swap_total || !info->swap_used)
		return true; /* nothing to do */

	/* refuse when swapped pages would not comfortably fit into free memory */
	if (info->swap_used >= (info->available * 9) / 10)
		return false;

	result = (run_command (swapoff_argv) == 0);

	/* always re-enable swap, even if swapoff failed halfway */
	run_command (swapon_argv);

	return result;
}

typedef struct
{
	uint64_t freed;          /* change in available memory (KiB) */
	uint64_t cache_before;
	uint64_t cache_after;
	double   percent_before;
	double   percent_after;
	bool     success;
}
CLEAN_RESULT;

static bool memory_clean (unsigned flags, CLEAN_RESULT *out)
{
	MEMORY_INFO before, after;
	CLEAN_RESULT result;
	char drop_value[4];
	unsigned drop = 0;
	bool ok = true;

	memset (&result, 0, sizeof (result));

	if (!memory_getinfo (&before))
		return false;

	result.cache_before = before.cache;
	result.percent_before = before.percent;

	/* Flush modified file-backed pages explicitly. Cache eviction also needs
	   this first so the kernel can discard the newly clean pages. */
	if (flags & (CLEAN_MODIFIEDCACHE | CLEAN_PAGECACHE | CLEAN_SLAB))
		sync ();

	if (flags & CLEAN_PAGECACHE)
		drop |= 1;

	if (flags & CLEAN_SLAB)
		drop |= 2;

	if (drop)
	{
		snprintf (drop_value, sizeof (drop_value), "%u\n", drop);

		if (!write_procfile (PROC_DROP_CACHES, drop_value))
			ok = false;
	}

	if (flags & CLEAN_COMPACT)
	{
		/* compact_memory appeared in 2.6.35 and needs CONFIG_COMPACTION;
		   treat absence as non-fatal */
		if (access (PROC_COMPACT, F_OK) == 0)
		{
			if (!write_procfile (PROC_COMPACT, "1\n"))
				ok = false;
		}
	}

	if (flags & CLEAN_SWAP)
	{
		if (!clean_swap_reload (&before))
			ok = false;
	}

	if (!memory_getinfo (&after))
		memcpy (&after, &before, sizeof (after));

	result.cache_after = after.cache;
	result.percent_after = after.percent;
	result.freed = (after.available > before.available) ? (after.available - before.available) : 0;
	result.success = ok;

	if (out)
		memcpy (out, &result, sizeof (result));

	return ok;
}

/*
 * -------------------------------------------------------------------------
 * Desktop notifications (best effort, optional)
 * -------------------------------------------------------------------------
 */

static void app_notify (const char *summary, const char *body)
{
	char *const argv[] = {
		(char *) "notify-send",
		(char *) "--app-name=" APP_NAME,
		(char *) "--icon=utilities-system-monitor",
		(char *) summary,
		(char *) body,
		NULL
	};

	if (!config.notifications)
		return;

	if (!command_exists ("notify-send"))
		return;

	run_command (argv);
}

/*
 * -------------------------------------------------------------------------
 * Configuration file
 * -------------------------------------------------------------------------
 */

static char *string_trim (char *string)
{
	char *end;

	while (isspace ((unsigned char) *string))
		string += 1;

	end = string + strlen (string);

	while (end > string && isspace ((unsigned char) end[-1]))
		end -= 1;

	*end = 0;

	return string;
}

static bool config_parsebool (const char *value)
{
	return (!strcasecmp (value, "yes") || !strcasecmp (value, "true") ||
			!strcasecmp (value, "on") || !strcmp (value, "1"));
}

static void config_setvalue (const char *key, const char *value)
{
	if (!strcasecmp (key, "clean_threshold"))
		config.clean_threshold = atoi (value);
	else if (!strcasecmp (key, "clean_pressure"))
		config.clean_pressure = atoi (value);
	else if (!strcasecmp (key, "clean_pressure_type"))
		config.clean_pressure_full = !strcasecmp (value, "full");
	else if (!strcasecmp (key, "clean_interval"))
		config.clean_interval = atoi (value);
	else if (!strcasecmp (key, "check_interval"))
		config.check_interval = atoi (value);
	else if (!strcasecmp (key, "cooldown"))
		config.cooldown = atoi (value);
	else if (!strcasecmp (key, "warning_level"))
		config.warning_level = atoi (value);
	else if (!strcasecmp (key, "danger_level"))
		config.danger_level = atoi (value);
	else if (!strcasecmp (key, "notifications"))
		config.notifications = config_parsebool (value);
	else if (!strcasecmp (key, "syslog"))
		config.use_syslog = config_parsebool (value);
	else if (!strcasecmp (key, "clean_pagecache"))
		config.clean_flags = config_parsebool (value) ? (config.clean_flags | CLEAN_PAGECACHE) : (config.clean_flags & ~CLEAN_PAGECACHE);
	else if (!strcasecmp (key, "clean_dentries"))
		config.clean_flags = config_parsebool (value) ? (config.clean_flags | CLEAN_SLAB) : (config.clean_flags & ~CLEAN_SLAB);
	else if (!strcasecmp (key, "clean_compact"))
		config.clean_flags = config_parsebool (value) ? (config.clean_flags | CLEAN_COMPACT) : (config.clean_flags & ~CLEAN_COMPACT);
	else if (!strcasecmp (key, "clean_swap"))
		config.clean_flags = config_parsebool (value) ? (config.clean_flags | CLEAN_SWAP) : (config.clean_flags & ~CLEAN_SWAP);
	else if (!strcasecmp (key, "clean_modifiedcache"))
		config.clean_flags = config_parsebool (value) ? (config.clean_flags | CLEAN_MODIFIEDCACHE) : (config.clean_flags & ~CLEAN_MODIFIEDCACHE);
}

static bool config_loadfile (const char *path)
{
	FILE *fp;
	char line[256];
	char *cursor, *separator;

	fp = fopen (path, "r");

	if (!fp)
		return false;

	while (fgets (line, sizeof (line), fp))
	{
		cursor = string_trim (line);

		if (!*cursor || *cursor == '#' || *cursor == ';')
			continue;

		separator = strchr (cursor, '=');

		if (!separator)
			continue;

		*separator = 0;

		config_setvalue (string_trim (cursor), string_trim (separator + 1));
	}

	fclose (fp);

	return true;
}

static void config_load (const char *explicit_path)
{
	char path[PATH_MAX];
	const char *home;

	if (explicit_path)
	{
		if (!config_loadfile (explicit_path))
			fprintf (stderr, "warning: cannot read config file: %s\n", explicit_path);

		return;
	}

	config_loadfile (SYSTEM_CONFIG);

	home = getenv ("HOME");

	if (!home || !*home)
	{
		struct passwd *pw = getpwuid (getuid ());

		if (pw)
			home = pw->pw_dir;
	}

	if (home && *home)
	{
		snprintf (path, sizeof (path), "%s/.config/memreduct/memreduct.conf", home);
		config_loadfile (path);
	}
}

static void config_sanitize (void)
{
	if (config.clean_threshold < 0 || config.clean_threshold > 99)
		config.clean_threshold = 0;

	if (config.clean_pressure < 0 || config.clean_pressure > 100)
		config.clean_pressure = 0;

	if (config.clean_interval < 0)
		config.clean_interval = 0;

	if (config.check_interval < 1)
		config.check_interval = 1;

	if (config.cooldown < 0)
		config.cooldown = 0;

	if (config.warning_level < 1 || config.warning_level > 100)
		config.warning_level = 60;

	if (config.danger_level < config.warning_level || config.danger_level > 100)
		config.danger_level = 90;

	if (!(config.clean_flags & (CLEAN_PAGECACHE | CLEAN_SLAB | CLEAN_COMPACT | CLEAN_SWAP | CLEAN_MODIFIEDCACHE)))
		config.clean_flags = CLEAN_DEFAULT;
}

/*
 * -------------------------------------------------------------------------
 * Output helpers
 * -------------------------------------------------------------------------
 */

static const char *usage_color (double percent, bool use_color)
{
	if (!use_color)
		return "";

	if (percent >= (double) config.danger_level)
		return "\033[91m"; /* red */

	if (percent >= (double) config.warning_level)
		return "\033[93m"; /* yellow */

	return "\033[92m"; /* green */
}

static void print_bar (double percent, int width, bool use_color)
{
	int filled = (int) ((percent / 100.0) * (double) width + 0.5);

	if (filled > width)
		filled = width;

	if (filled < 0)
		filled = 0;

	fputs ("[", stdout);
	fputs (usage_color (percent, use_color), stdout);

	for (int i = 0; i < width; i++)
		fputs ((i < filled) ? "|" : " ", stdout);

	if (use_color)
		fputs ("\033[0m", stdout);

	fputs ("]", stdout);
}

static void print_meminfo (const MEMORY_INFO *info, bool use_color)
{
	char used_str[32], total_str[32], cache_str[32];

	format_size (info->used, used_str, sizeof (used_str));
	format_size (info->total, total_str, sizeof (total_str));
	format_size (info->cache, cache_str, sizeof (cache_str));

	printf ("Physical memory  ");
	print_bar (info->percent, 30, use_color);
	printf (" %s%5.1f%%%s  %s / %s\n",
		usage_color (info->percent, use_color), info->percent, use_color ? "\033[0m" : "",
		used_str, total_str);

	printf ("Cache            ");
	print_bar (100.0 * (double) info->cache / (double) info->total, 30, use_color);
	printf (" %5.1f%%  %s\n", 100.0 * (double) info->cache / (double) info->total, cache_str);

	if (info->swap_total)
	{
		format_size (info->swap_used, used_str, sizeof (used_str));
		format_size (info->swap_total, total_str, sizeof (total_str));

		printf ("Swap             ");
		print_bar (info->swap_percent, 30, use_color);
		printf (" %s%5.1f%%%s  %s / %s\n",
			usage_color (info->swap_percent, use_color), info->swap_percent, use_color ? "\033[0m" : "",
			used_str, total_str);
	}
	else
	{
		printf ("Swap             (not configured)\n");
	}
}

static void print_pressure_metric (const char *label, const PRESSURE_METRIC *metric,
	bool include_full, bool use_color)
{
	if (!metric->some_available)
		return;

	printf ("%-15s some %s%5.2f%%%s  (10s / 60s / 5m: %.2f%% / %.2f%% / %.2f%%)",
		label, usage_color (metric->some_avg10 * 10.0, use_color),
		metric->some_avg10, use_color ? "\033[0m" : "",
		metric->some_avg10, metric->some_avg60, metric->some_avg300);

	if (include_full && metric->full_available)
		printf ("  full %5.2f%% / %.2f%% / %.2f%%",
			metric->full_avg10, metric->full_avg60, metric->full_avg300);

	printf ("\n");
}

static void print_pressure (const PRESSURE_INFO *pressure, bool use_color)
{
	print_pressure_metric ("Memory PSI", &pressure->memory, true, use_color);
	print_pressure_metric ("CPU PSI", &pressure->cpu, false, use_color);
	print_pressure_metric ("I/O PSI", &pressure->io, true, use_color);
}

typedef struct
{
	uint64_t original_bytes;
	uint64_t compressed_bytes;
	uint64_t memory_used_bytes;
	uint64_t memory_limit_bytes;
	uint64_t memory_used_max_bytes;
	uint64_t same_pages;
	uint64_t pages_compacted;
	uint64_t huge_pages;
	unsigned devices;
	bool available;
}
ZRAM_INFO;

static bool zram_name_valid (const char *name)
{
	const char *cursor;

	if (strncmp (name, "zram", 4) || !isdigit ((unsigned char) name[4]))
		return false;

	for (cursor = name + 4; *cursor; cursor++)
	{
		if (!isdigit ((unsigned char) *cursor))
			return false;
	}

	return true;
}

static void zram_getinfo (ZRAM_INFO *info)
{
	DIR *dir;
	struct dirent *entry;
	char path[PATH_MAX];

	memset (info, 0, sizeof (*info));

	dir = opendir ("/sys/block");

	if (!dir)
		return;

	while ((entry = readdir (dir)))
	{
		FILE *fp;
		unsigned long long original, compressed, used, limit, used_max;
		unsigned long long same, compacted, huge = 0;
		int fields;

		if (!zram_name_valid (entry->d_name))
			continue;

		if (snprintf (path, sizeof (path), "/sys/block/%s/mm_stat", entry->d_name) >= (int) sizeof (path))
			continue;

		fp = fopen (path, "r");

		if (!fp)
			continue;

		fields = fscanf (fp, "%llu %llu %llu %llu %llu %llu %llu %llu",
			&original, &compressed, &used, &limit, &used_max, &same, &compacted, &huge);
		fclose (fp);

		if (fields < 7)
			continue;

		info->original_bytes += original;
		info->compressed_bytes += compressed;
		info->memory_used_bytes += used;
		info->memory_limit_bytes += limit;
		info->memory_used_max_bytes += used_max;
		info->same_pages += same;
		info->pages_compacted += compacted;

		if (fields >= 8)
			info->huge_pages += huge;

		info->devices += 1;
		info->available = true;
	}

	closedir (dir);
}

typedef struct
{
	uint64_t page_faults;
	uint64_t major_faults;
	uint64_t pswpin;
	uint64_t pswpout;
	uint64_t pgscan_kswapd;
	uint64_t pgsteal_kswapd;
	uint64_t pgscan_direct;
	uint64_t pgsteal_direct;
	uint64_t compact_stall;
	uint64_t compact_success;
	uint64_t compact_fail;
	uint64_t oom_kill;
	bool available;
}
VMSTAT_INFO;

static void vmstat_getinfo (VMSTAT_INFO *info)
{
	FILE *fp;
	char line[256];
	char key[64];
	unsigned long long value;

	memset (info, 0, sizeof (*info));

	fp = fopen (PROC_VMSTAT, "r");

	if (!fp)
		return;

	while (fgets (line, sizeof (line), fp))
	{
		if (sscanf (line, "%63s %llu", key, &value) != 2)
			continue;

		if (!strcmp (key, "pgfault"))
			info->page_faults = value;
		else if (!strcmp (key, "pgmajfault"))
			info->major_faults = value;
		else if (!strcmp (key, "pswpin"))
			info->pswpin = value;
		else if (!strcmp (key, "pswpout"))
			info->pswpout = value;
		else if (!strcmp (key, "pgscan_kswapd"))
			info->pgscan_kswapd = value;
		else if (!strcmp (key, "pgsteal_kswapd"))
			info->pgsteal_kswapd = value;
		else if (!strcmp (key, "pgscan_direct"))
			info->pgscan_direct = value;
		else if (!strcmp (key, "pgsteal_direct"))
			info->pgsteal_direct = value;
		else if (!strcmp (key, "compact_stall"))
			info->compact_stall = value;
		else if (!strcmp (key, "compact_success"))
			info->compact_success = value;
		else if (!strcmp (key, "compact_fail"))
			info->compact_fail = value;
		else if (!strcmp (key, "oom_kill"))
			info->oom_kill = value;
	}

	fclose (fp);
	info->available = true;
}

typedef struct
{
	uint64_t current_bytes;
	uint64_t limit_bytes;
	uint64_t high_bytes;
	uint64_t swap_current_bytes;
	uint64_t high_events;
	uint64_t max_events;
	uint64_t oom_events;
	uint64_t oom_kill_events;
	bool available;
	bool limit_unlimited;
}
CGROUP_INFO;

static bool read_cgroup_value (const char *path, uint64_t *value, bool *unlimited)
{
	FILE *fp;
	char line[128];
	char *end;
	unsigned long long parsed;

	fp = fopen (path, "r");

	if (!fp)
		return false;

	if (!fgets (line, sizeof (line), fp))
	{
		fclose (fp);
		return false;
	}

	fclose (fp);

	line[strcspn (line, "\r\n")] = 0;

	if (!strcmp (line, "max"))
	{
		if (unlimited)
			*unlimited = true;
		*value = 0;
		return true;
	}

	errno = 0;
	parsed = strtoull (line, &end, 10);

	if (errno || end == line || *end)
		return false;

	*value = (uint64_t) parsed;

	if (unlimited)
		*unlimited = false;

	return true;
}

static void cgroup_getinfo (CGROUP_INFO *info)
{
	FILE *fp;
	char line[2048];
	char group[1024] = "";
	char directory[PATH_MAX];
	char path[PATH_MAX];

	memset (info, 0, sizeof (*info));

	fp = fopen ("/proc/self/cgroup", "r");

	if (!fp)
		return;

	while (fgets (line, sizeof (line), fp))
	{
		if (!strncmp (line, "0::", 3))
		{
			snprintf (group, sizeof (group), "%s", line + 3);
			group[strcspn (group, "\r\n")] = 0;
			break;
		}
	}

	fclose (fp);

	/* Refuse malformed paths rather than composing an unsafe sysfs path. */
	if (!group[0] || group[0] != '/' || strstr (group, "/../") ||
		!strcmp (group, "/..") || strstr (group, "//"))
		return;

	if (snprintf (directory, sizeof (directory), "/sys/fs/cgroup%s", group) >= (int) sizeof (directory))
		return;

	if (snprintf (path, sizeof (path), "%s/memory.current", directory) >= (int) sizeof (path))
		return;

	if (!read_cgroup_value (path, &info->current_bytes, NULL))
		return;

	info->available = true;

	if (snprintf (path, sizeof (path), "%s/memory.max", directory) < (int) sizeof (path))
		read_cgroup_value (path, &info->limit_bytes, &info->limit_unlimited);

	if (snprintf (path, sizeof (path), "%s/memory.high", directory) < (int) sizeof (path))
		read_cgroup_value (path, &info->high_bytes, NULL);

	if (snprintf (path, sizeof (path), "%s/memory.swap.current", directory) < (int) sizeof (path))
		read_cgroup_value (path, &info->swap_current_bytes, NULL);

	if (snprintf (path, sizeof (path), "%s/memory.events", directory) < (int) sizeof (path))
	{
		unsigned long long value;
		char key[64];

		fp = fopen (path, "r");

		if (fp)
		{
			while (fgets (line, sizeof (line), fp))
			{
				if (sscanf (line, "%63s %llu", key, &value) != 2)
					continue;

				if (!strcmp (key, "high"))
					info->high_events = value;
				else if (!strcmp (key, "max"))
					info->max_events = value;
				else if (!strcmp (key, "oom"))
					info->oom_events = value;
				else if (!strcmp (key, "oom_kill"))
					info->oom_kill_events = value;
			}

			fclose (fp);
		}
	}
}

static void print_memory_details (const MEMORY_INFO *info, const ZRAM_INFO *zram,
	const VMSTAT_INFO *vmstat, const CGROUP_INFO *cgroup)
{
	char value[32], other[32];

	printf ("\nMemory breakdown\n");
	format_size (info->anon_pages, value, sizeof (value));
	printf ("  Anonymous pages       %s\n", value);
	format_size (info->active_file + info->inactive_file, value, sizeof (value));
	printf ("  File-backed pages     %s\n", value);
	format_size (info->active, value, sizeof (value));
	format_size (info->inactive, other, sizeof (other));
	printf ("  Active / inactive     %s / %s\n", value, other);
	format_size (info->dirty, value, sizeof (value));
	format_size (info->writeback, other, sizeof (other));
	printf ("  Dirty / writeback     %s / %s\n", value, other);
	format_size (info->slab, value, sizeof (value));
	format_size (info->sreclaimable, other, sizeof (other));
	printf ("  Slab / reclaimable    %s / %s\n", value, other);
	format_size (info->sunreclaim, value, sizeof (value));
	printf ("  Unreclaimable slab    %s\n", value);
	format_size (info->mapped, value, sizeof (value));
	printf ("  Mapped                %s\n", value);
	format_size (info->kernel_stack, value, sizeof (value));
	format_size (info->page_tables, other, sizeof (other));
	printf ("  Kernel stack / tables %s / %s\n", value, other);
	if (info->commit_limit || info->committed_as)
	{
		format_size (info->committed_as, value, sizeof (value));
		format_size (info->commit_limit, other, sizeof (other));
		printf ("  Committed / limit     %s / %s\n", value, other);
	}
	if (info->hugepages_total)
		printf ("  Huge pages            %llu total, %llu free (%llu KiB each)\n",
			(unsigned long long) info->hugepages_total,
			(unsigned long long) info->hugepages_free,
			(unsigned long long) info->hugepages_size);

	if (zram->available)
	{
		double ratio = zram->compressed_bytes ?
			(double) zram->original_bytes / (double) zram->compressed_bytes : 0.0;

		format_size (zram->original_bytes / 1024, value, sizeof (value));
		format_size (zram->compressed_bytes / 1024, other, sizeof (other));
		printf ("  ZRAM devices          %u device(s), %s logical / %s compressed (%.2fx)\n",
			zram->devices, value, other, ratio);

		format_size (zram->memory_used_bytes / 1024, value, sizeof (value));
		printf ("  ZRAM allocated        %s", value);

		if (zram->memory_limit_bytes)
		{
			format_size (zram->memory_limit_bytes / 1024, other, sizeof (other));
			printf (" / %s limit", other);
		}

		printf ("\n");
	}

	if (vmstat->available)
	{
		printf ("\nKernel VM counters (since boot)\n");
		printf ("  Page faults           %llu total, %llu major\n",
			(unsigned long long) vmstat->page_faults,
			(unsigned long long) vmstat->major_faults);
		printf ("  Swap I/O              %llu pages in, %llu pages out\n",
			(unsigned long long) vmstat->pswpin,
			(unsigned long long) vmstat->pswpout);
		printf ("  Reclaim (kswapd)      %llu scanned, %llu reclaimed\n",
			(unsigned long long) vmstat->pgscan_kswapd,
			(unsigned long long) vmstat->pgsteal_kswapd);
		printf ("  Reclaim (direct)      %llu scanned, %llu reclaimed\n",
			(unsigned long long) vmstat->pgscan_direct,
			(unsigned long long) vmstat->pgsteal_direct);
		printf ("  Compaction            %llu stalls, %llu successes, %llu failures\n",
			(unsigned long long) vmstat->compact_stall,
			(unsigned long long) vmstat->compact_success,
			(unsigned long long) vmstat->compact_fail);
		printf ("  OOM kills             %llu\n",
			(unsigned long long) vmstat->oom_kill);
	}

	if (cgroup->available)
	{
		format_size (cgroup->current_bytes / 1024, value, sizeof (value));
		printf ("  Current cgroup        %s", value);

		if (cgroup->limit_unlimited)
			printf (" / unlimited");
		else
		{
			format_size (cgroup->limit_bytes / 1024, other, sizeof (other));
			printf (" / %s limit", other);
		}

		printf ("\n");
		format_size (cgroup->swap_current_bytes / 1024, value, sizeof (value));
		printf ("  Cgroup swap           %s\n", value);
		printf ("  Cgroup memory events  high %llu, max %llu, OOM %llu, OOM kills %llu\n",
			(unsigned long long) cgroup->high_events,
			(unsigned long long) cgroup->max_events,
			(unsigned long long) cgroup->oom_events,
			(unsigned long long) cgroup->oom_kill_events);
	}
}

static void print_json_meminfo (const MEMORY_INFO *info, const PRESSURE_INFO *pressure,
	const ZRAM_INFO *zram, const VMSTAT_INFO *vmstat, const CGROUP_INFO *cgroup)
{
	const PRESSURE_METRIC *memory = &pressure->memory;
	const PRESSURE_METRIC *cpu = &pressure->cpu;
	const PRESSURE_METRIC *io = &pressure->io;

	printf ("{\n"
		"  \"total_kb\": %llu,\n"
		"  \"used_kb\": %llu,\n"
		"  \"available_kb\": %llu,\n"
		"  \"free_kb\": %llu,\n"
		"  \"cache_kb\": %llu,\n"
		"  \"percent\": %.1f,\n"
		"  \"swap_total_kb\": %llu,\n"
		"  \"swap_used_kb\": %llu,\n"
		"  \"swap_percent\": %.1f,\n"
		"  \"active_kb\": %llu,\n"
		"  \"inactive_kb\": %llu,\n"
		"  \"active_anon_kb\": %llu,\n"
		"  \"inactive_anon_kb\": %llu,\n"
		"  \"active_file_kb\": %llu,\n"
		"  \"inactive_file_kb\": %llu,\n"
		"  \"dirty_kb\": %llu,\n"
		"  \"writeback_kb\": %llu,\n"
		"  \"anon_pages_kb\": %llu,\n"
		"  \"mapped_kb\": %llu,\n"
		"  \"slab_kb\": %llu,\n"
		"  \"slab_unreclaimable_kb\": %llu,\n"
		"  \"kernel_stack_kb\": %llu,\n"
		"  \"page_tables_kb\": %llu,\n"
		"  \"commit_limit_kb\": %llu,\n"
		"  \"committed_as_kb\": %llu,\n"
		"  \"hugepages_total\": %llu,\n"
		"  \"hugepages_free\": %llu,\n"
		"  \"hugepages_size_kb\": %llu,\n"
		"  \"psi_memory_available\": %s,\n"
		"  \"psi_memory_some_avg10\": %.2f,\n"
		"  \"psi_memory_some_avg60\": %.2f,\n"
		"  \"psi_memory_some_avg300\": %.2f,\n"
		"  \"psi_memory_some_total_us\": %llu,\n"
		"  \"psi_memory_full_available\": %s,\n"
		"  \"psi_memory_full_avg10\": %.2f,\n"
		"  \"psi_memory_full_avg60\": %.2f,\n"
		"  \"psi_memory_full_avg300\": %.2f,\n"
		"  \"psi_memory_full_total_us\": %llu,\n"
		"  \"psi_cpu_available\": %s,\n"
		"  \"psi_cpu_some_avg10\": %.2f,\n"
		"  \"psi_cpu_some_avg60\": %.2f,\n"
		"  \"psi_cpu_some_avg300\": %.2f,\n"
		"  \"psi_io_available\": %s,\n"
		"  \"psi_io_some_avg10\": %.2f,\n"
		"  \"psi_io_some_avg60\": %.2f,\n"
		"  \"psi_io_some_avg300\": %.2f,\n"
		"  \"zram_available\": %s,\n"
		"  \"zram_devices\": %u,\n"
		"  \"zram_original_bytes\": %llu,\n"
		"  \"zram_compressed_bytes\": %llu,\n"
		"  \"zram_memory_used_bytes\": %llu,\n"
		"  \"zram_memory_limit_bytes\": %llu,\n"
		"  \"zram_same_pages\": %llu,\n"
		"  \"zram_pages_compacted\": %llu,\n"
		"  \"zram_huge_pages\": %llu,\n"
		"  \"vmstat_available\": %s,\n"
		"  \"page_faults\": %llu,\n"
		"  \"major_page_faults\": %llu,\n"
		"  \"pswpin_pages\": %llu,\n"
		"  \"pswpout_pages\": %llu,\n"
		"  \"pgscan_kswapd\": %llu,\n"
		"  \"pgsteal_kswapd\": %llu,\n"
		"  \"pgscan_direct\": %llu,\n"
		"  \"pgsteal_direct\": %llu,\n"
		"  \"compact_stall\": %llu,\n"
		"  \"compact_success\": %llu,\n"
		"  \"compact_fail\": %llu,\n"
		"  \"oom_kill\": %llu,\n"
		"  \"cgroup_v2_available\": %s,\n"
		"  \"cgroup_memory_current_bytes\": %llu,\n"
		"  \"cgroup_memory_limit_bytes\": %llu,\n"
		"  \"cgroup_memory_limit_unlimited\": %s,\n"
		"  \"cgroup_memory_high_bytes\": %llu,\n"
		"  \"cgroup_memory_swap_current_bytes\": %llu,\n"
		"  \"cgroup_event_high\": %llu,\n"
		"  \"cgroup_event_max\": %llu,\n"
		"  \"cgroup_event_oom\": %llu,\n"
		"  \"cgroup_event_oom_kill\": %llu\n"
		"}\n",
		(unsigned long long) info->total,
		(unsigned long long) info->used,
		(unsigned long long) info->available,
		(unsigned long long) info->free,
		(unsigned long long) info->cache,
		info->percent,
		(unsigned long long) info->swap_total,
		(unsigned long long) info->swap_used,
		info->swap_percent,
		(unsigned long long) info->active,
		(unsigned long long) info->inactive,
		(unsigned long long) info->active_anon,
		(unsigned long long) info->inactive_anon,
		(unsigned long long) info->active_file,
		(unsigned long long) info->inactive_file,
		(unsigned long long) info->dirty,
		(unsigned long long) info->writeback,
		(unsigned long long) info->anon_pages,
		(unsigned long long) info->mapped,
		(unsigned long long) info->slab,
		(unsigned long long) info->sunreclaim,
		(unsigned long long) info->kernel_stack,
		(unsigned long long) info->page_tables,
		(unsigned long long) info->commit_limit,
		(unsigned long long) info->committed_as,
		(unsigned long long) info->hugepages_total,
		(unsigned long long) info->hugepages_free,
		(unsigned long long) info->hugepages_size,
		memory->some_available ? "true" : "false",
		memory->some_avg10, memory->some_avg60, memory->some_avg300,
		(unsigned long long) memory->some_total_us,
		memory->full_available ? "true" : "false",
		memory->full_avg10, memory->full_avg60, memory->full_avg300,
		(unsigned long long) memory->full_total_us,
		cpu->some_available ? "true" : "false",
		cpu->some_avg10, cpu->some_avg60, cpu->some_avg300,
		io->some_available ? "true" : "false",
		io->some_avg10, io->some_avg60, io->some_avg300,
		zram->available ? "true" : "false",
		zram->devices,
		(unsigned long long) zram->original_bytes,
		(unsigned long long) zram->compressed_bytes,
		(unsigned long long) zram->memory_used_bytes,
		(unsigned long long) zram->memory_limit_bytes,
		(unsigned long long) zram->same_pages,
		(unsigned long long) zram->pages_compacted,
		(unsigned long long) zram->huge_pages,
		vmstat->available ? "true" : "false",
		(unsigned long long) vmstat->page_faults,
		(unsigned long long) vmstat->major_faults,
		(unsigned long long) vmstat->pswpin,
		(unsigned long long) vmstat->pswpout,
		(unsigned long long) vmstat->pgscan_kswapd,
		(unsigned long long) vmstat->pgsteal_kswapd,
		(unsigned long long) vmstat->pgscan_direct,
		(unsigned long long) vmstat->pgsteal_direct,
		(unsigned long long) vmstat->compact_stall,
		(unsigned long long) vmstat->compact_success,
		(unsigned long long) vmstat->compact_fail,
		(unsigned long long) vmstat->oom_kill,
		cgroup->available ? "true" : "false",
		(unsigned long long) cgroup->current_bytes,
		(unsigned long long) cgroup->limit_bytes,
		cgroup->limit_unlimited ? "true" : "false",
		(unsigned long long) cgroup->high_bytes,
		(unsigned long long) cgroup->swap_current_bytes,
		(unsigned long long) cgroup->high_events,
		(unsigned long long) cgroup->max_events,
		(unsigned long long) cgroup->oom_events,
		(unsigned long long) cgroup->oom_kill_events);
}


static void json_print_string (const char *value)
{
	const unsigned char *cursor = (const unsigned char *) value;
	putchar ('"');
	for (; *cursor; cursor++)
	{
		switch (*cursor)
		{
			case '"':  fputs ("\\\"", stdout); break;
			case '\\': fputs ("\\\\", stdout); break;
			case '\b': fputs ("\\b", stdout); break;
			case '\f': fputs ("\\f", stdout); break;
			case '\n': fputs ("\\n", stdout); break;
			case '\r': fputs ("\\r", stdout); break;
			case '\t': fputs ("\\t", stdout); break;
			default:
				if (*cursor < 0x20)
					printf ("\\u%04x", (unsigned int) *cursor);
				else
					putchar (*cursor);
				break;
		}
	}
	putchar ('"');
}

static void print_processes (const PROCESS_INFO *list, int count, uint64_t total_kb)
{
	char rss_str[32];

	printf ("%7s %10s %6s  %s\n", "PID", "RSS", "MEM%", "NAME");

	for (int i = 0; i < count; i++)
	{
		format_size (list[i].rss, rss_str, sizeof (rss_str));

		printf ("%7d %10s %5.1f%%  %s\n",
			(int) list[i].pid, rss_str,
			total_kb ? (100.0 * (double) list[i].rss / (double) total_kb) : 0.0,
			list[i].name);
	}
}

static void print_cleanresult (const CLEAN_RESULT *result)
{
	char freed_str[32], cache_str[32];
	uint64_t cache_freed;

	cache_freed = (result->cache_before > result->cache_after) ? (result->cache_before - result->cache_after) : 0;

	format_size (result->freed, freed_str, sizeof (freed_str));
	format_size (cache_freed, cache_str, sizeof (cache_str));

	printf ("Memory cleaned: %s reclaimed, cache reduced by %s (%.1f%% -> %.1f%%)\n",
		freed_str, cache_str, result->percent_before, result->percent_after);
}

/*
 * -------------------------------------------------------------------------
 * Commands
 * -------------------------------------------------------------------------
 */

static int command_status (bool as_json)
{
	MEMORY_INFO info;
	PRESSURE_INFO pressure;
	ZRAM_INFO zram;
	VMSTAT_INFO vmstat;
	CGROUP_INFO cgroup;
	bool use_color = isatty (STDOUT_FILENO);

	if (!memory_getinfo (&info))
	{
		fprintf (stderr, "error: cannot read %s\n", PROC_MEMINFO);
		return 1;
	}

	pressure_getinfo (&pressure);
	zram_getinfo (&zram);
	vmstat_getinfo (&vmstat);
	cgroup_getinfo (&cgroup);

	if (as_json)
	{
		print_json_meminfo (&info, &pressure, &zram, &vmstat, &cgroup);
		return 0;
	}

	printf ("%s %s\n\n", APP_NAME, APP_VERSION);

	print_meminfo (&info, use_color);
	print_pressure (&pressure, use_color);

	return 0;
}

static int command_stats (bool as_json)
{
	MEMORY_INFO info;
	PRESSURE_INFO pressure;
	ZRAM_INFO zram;
	VMSTAT_INFO vmstat;
	CGROUP_INFO cgroup;
	PROCESS_INFO top_list[5];
	int count;

	if (!memory_getinfo (&info))
	{
		fprintf (stderr, "error: cannot read %s\n", PROC_MEMINFO);
		return 1;
	}
	pressure_getinfo (&pressure);
	zram_getinfo (&zram);
	vmstat_getinfo (&vmstat);
	cgroup_getinfo (&cgroup);
	if (as_json)
	{
		print_json_meminfo (&info, &pressure, &zram, &vmstat, &cgroup);
		return 0;
	}
	printf ("%s %s - detailed memory statistics\n\n", APP_NAME, APP_VERSION);
	print_meminfo (&info, isatty (STDOUT_FILENO));
	print_memory_details (&info, &zram, &vmstat, &cgroup);
	print_pressure (&pressure, isatty (STDOUT_FILENO));
	count = process_gettop (top_list, 5);
	if (count)
	{
		printf ("\nTop processes\n");
		print_processes (top_list, count, info.total);
	}
	return 0;
}

static int command_top (int limit, bool as_json)
{
	MEMORY_INFO info;
	PROCESS_INFO list[64];
	int count;

	if (limit < 1)
		limit = 10;

	if (limit > (int) (sizeof (list) / sizeof (list[0])))
		limit = (int) (sizeof (list) / sizeof (list[0]));

	if (!memory_getinfo (&info))
	{
		fprintf (stderr, "error: cannot read %s\n", PROC_MEMINFO);
		return 1;
	}

	count = process_gettop (list, limit);

	if (as_json)
	{
		printf ("[\n");

		for (int i = 0; i < count; i++)
		{
			printf ("  {\"pid\": %d, \"name\": ", (int) list[i].pid);
			json_print_string (list[i].name);
			printf (", \"rss_kb\": %llu}%s\n",
				(unsigned long long) list[i].rss,
				(i + 1 < count) ? "," : "");

		}

		printf ("]\n");
	}
	else
	{
		print_processes (list, count, info.total);
	}

	return 0;
}

static int require_root (void)
{
	if (geteuid () != 0)
	{
		fprintf (stderr,
			"error: memory cleaning requires root privileges.\n"
			"try: sudo " APP_BINARY " clean\n");

		return 1;
	}

	return 0;
}

static const char *describe_flags (unsigned flags, char *buffer, size_t length)
{
	buffer[0] = 0;

	if (flags & CLEAN_PAGECACHE)
		strncat (buffer, "page cache, ", length - strlen (buffer) - 1);

	if (flags & CLEAN_SLAB)
		strncat (buffer, "dentries/inodes, ", length - strlen (buffer) - 1);

	if (flags & CLEAN_COMPACT)
		strncat (buffer, "compaction, ", length - strlen (buffer) - 1);

	if (flags & CLEAN_SWAP)
		strncat (buffer, "swap reload, ", length - strlen (buffer) - 1);

	if (flags & CLEAN_MODIFIEDCACHE)
		strncat (buffer, "modified file cache flush, ", length - strlen (buffer) - 1);

	if (strlen (buffer) >= 2)
		buffer[strlen (buffer) - 2] = 0; /* strip trailing ", " */

	return buffer;
}

static int command_clean (unsigned flags)
{
	CLEAN_RESULT result;
	char description[256];

	if (require_root ())
		return 1;

	printf ("Cleaning memory (%s)...\n", describe_flags (flags, description, sizeof (description)));

	if (!memory_clean (flags, &result))
	{
		fprintf (stderr, "error: memory cleaning failed (%s)\n", strerror (errno));
		return 1;
	}

	print_cleanresult (&result);

	return 0;
}

/*
 * Daemon mode: poll memory, clean on threshold and/or by timer.
 */
static void daemon_log (int priority, const char *format, ...)
{
	char message[512];
	char stamp[32];
	va_list args;

	va_start (args, format);
	vsnprintf (message, sizeof (message), format, args);
	va_end (args);

	timestamp_now (stamp, sizeof (stamp));
	printf ("[%s] %s\n", stamp, message);
	fflush (stdout);

	if (config.use_syslog)
		syslog (priority, "%s", message);
}

static int command_daemon (void)
{
	MEMORY_INFO info;
	PRESSURE_INFO pressure;
	CLEAN_RESULT result;
	char freed_str[32], description[256];
	time_t last_clean = 0;
	time_t now;
	const char *reason;
	bool warned_high = false;

	if (require_root ())
		return 1;

	if (!config.clean_threshold && !config.clean_pressure && !config.clean_interval)
	{
		fprintf (stderr,
			"error: nothing to do: no auto-clean trigger is enabled (threshold, PSI pressure and interval are disabled).\n"
			"set at least one of them in " SYSTEM_CONFIG " or via --threshold/--pressure/--interval.\n");

		return 1;
	}

	install_signals ();

	if (config.use_syslog)
		openlog (APP_BINARY, LOG_PID, LOG_DAEMON);

	daemon_log (LOG_INFO, "%s %s daemon started (threshold: %d%%, PSI %s pressure: %d%%, interval: %d min, areas: %s)",
		APP_NAME, APP_VERSION,
		config.clean_threshold, config.clean_pressure_full ? "full" : "some", config.clean_pressure, config.clean_interval,
		describe_flags (config.clean_flags, description, sizeof (description)));

	while (!is_terminating)
	{
		if (reload_requested)
		{
			reload_requested = 0;

			config_load (NULL);
			config_sanitize ();

			daemon_log (LOG_INFO, "configuration reloaded (threshold: %d%%, PSI %s pressure: %d%%, interval: %d min)",
				config.clean_threshold, config.clean_pressure_full ? "full" : "some", config.clean_pressure, config.clean_interval);
		}

		if (!memory_getinfo (&info))
		{
			daemon_log (LOG_ERR, "cannot read " PROC_MEMINFO);
			break;
		}

		pressure_getinfo (&pressure);
		now = time (NULL);
		reason = NULL;

		/* one-shot warning when usage crosses the danger level */
		if (info.percent >= (double) config.danger_level)
		{
			if (!warned_high)
			{
				char body[96];

				warned_high = true;

				daemon_log (LOG_WARNING, "high memory usage: %.1f%%", info.percent);

				snprintf (body, sizeof (body), "Memory usage reached %.1f%%", info.percent);
				app_notify ("High memory usage", body);
			}
		}
		else if (info.percent < (double) config.warning_level)
		{
			warned_high = false;
		}

		if (config.clean_pressure &&
			((config.clean_pressure_full && pressure.memory.full_available &&
				pressure.memory.full_avg10 >= (double) config.clean_pressure) ||
			(!config.clean_pressure_full && pressure.memory.some_available &&
				pressure.memory.some_avg10 >= (double) config.clean_pressure)))
			reason = config.clean_pressure_full ? "pressure-full" : "pressure-some";
		else if (config.clean_threshold && info.percent >= (double) config.clean_threshold)
			reason = "threshold";
		else if (config.clean_interval && last_clean && (now - last_clean) >= (time_t) config.clean_interval * 60)
			reason = "timer";
		else if (config.clean_interval && !last_clean)
			last_clean = now; /* start the timer on first pass */

		if (reason && (!last_clean || (now - last_clean) >= (time_t) config.cooldown || !strcmp (reason, "timer")))
		{
			if (memory_clean (config.clean_flags, &result))
			{
				format_size (result.freed, freed_str, sizeof (freed_str));

				daemon_log (LOG_INFO, "memory cleaned (%s): %s reclaimed, %.1f%% -> %.1f%%",
					reason, freed_str, result.percent_before, result.percent_after);

				if (result.freed)
				{
					char body[128];

					snprintf (body, sizeof (body), "%s reclaimed (%.1f%% -> %.1f%%)",
						freed_str, result.percent_before, result.percent_after);

					app_notify ("Memory cleaned", body);
				}
			}
			else
			{
				daemon_log (LOG_ERR, "memory cleaning failed (%s)", strerror (errno));
			}

			last_clean = time (NULL);
		}

		for (int i = 0; i < config.check_interval && !is_terminating && !reload_requested; i++)
			sleep (1);
	}

	daemon_log (LOG_INFO, "daemon stopped");

	if (config.use_syslog)
		closelog ();

	return 0;
}

/*
 * Monitor mode: live full-screen view, 'c' to clean, 'q' to quit.
 */
static void terminal_restore (void)
{
	if (termios_saved)
		tcsetattr (STDIN_FILENO, TCSANOW, &saved_termios);

	/* show cursor, leave alternate screen */
	fputs ("\033[?25h\033[?1049l", stdout);
	fflush (stdout);
}

static bool terminal_setup (void)
{
	struct termios raw;

	if (!isatty (STDIN_FILENO) || !isatty (STDOUT_FILENO))
		return false;

	if (tcgetattr (STDIN_FILENO, &saved_termios) < 0)
		return false;

	termios_saved = true;

	raw = saved_termios;
	raw.c_lflag &= ~(tcflag_t) (ICANON | ECHO);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 0;

	if (tcsetattr (STDIN_FILENO, TCSANOW, &raw) < 0)
		return false;

	atexit (terminal_restore);

	/* enter alternate screen, hide cursor */
	fputs ("\033[?1049h\033[?25l", stdout);

	return true;
}

static int command_monitor (int refresh_seconds)
{
	MEMORY_INFO info;
	PRESSURE_INFO pressure;
	CLEAN_RESULT result;
	PROCESS_INFO top_list[5];
	int top_count;
	char message[160] = "";
	char stamp[32], freed_str[32];
	bool is_root = (geteuid () == 0);

	/* usage history for the sparkline graph */
	static const char *blocks[8] = {"\u2581", "\u2582", "\u2583", "\u2584", "\u2585", "\u2586", "\u2587", "\u2588"};
	double history[60];
	int history_count = 0;

	if (refresh_seconds < 1)
		refresh_seconds = 1;

	if (!terminal_setup ())
	{
		fprintf (stderr, "error: monitor mode requires an interactive terminal.\n");
		return 1;
	}

	install_signals ();

	while (!is_terminating)
	{
		if (!memory_getinfo (&info))
			break;

		pressure_getinfo (&pressure);
		top_count = process_gettop (top_list, 5);

		/* append to history (shift left when full) */
		if (history_count == 60)
		{
			memmove (history, history + 1, sizeof (double) * 59);
			history_count = 59;
		}

		history[history_count++] = info.percent;

		/* redraw */
		fputs ("\033[H\033[2J", stdout);

		printf ("\033[1m%s %s\033[0m - live monitor (refresh: %ds)\n\n", APP_NAME, APP_VERSION, refresh_seconds);

		print_meminfo (&info, true);
		print_pressure (&pressure, true);

		/* history sparkline */
		printf ("\nHistory          ");

		for (int i = 0; i < history_count; i++)
		{
			int level = (int) (history[i] / 12.5);

			if (level > 7)
				level = 7;

			if (level < 0)
				level = 0;

			printf ("%s%s\033[0m", usage_color (history[i], true), blocks[level]);
		}

		printf ("\n");

		/* top memory consumers */
		if (top_count)
		{
			char rss_str[32];

			printf ("\n\033[1mTop processes\033[0m\n");

			for (int i = 0; i < top_count; i++)
			{
				format_size (top_list[i].rss, rss_str, sizeof (rss_str));

				printf ("  %7d %10s %5.1f%%  %s\n",
					(int) top_list[i].pid, rss_str,
					100.0 * (double) top_list[i].rss / (double) info.total,
					top_list[i].name);
			}
		}

		printf ("\nAuto-clean threshold: %s", config.clean_threshold ? "" : "disabled");

		if (config.clean_threshold)
			printf ("%d%%", config.clean_threshold);

		printf ("   Areas: page cache%s%s%s%s\n",
			(config.clean_flags & CLEAN_SLAB) ? ", dentries/inodes" : "",
			(config.clean_flags & CLEAN_COMPACT) ? ", compaction" : "",
			(config.clean_flags & CLEAN_SWAP) ? ", swap" : "",
			(config.clean_flags & CLEAN_MODIFIEDCACHE) ? ", modified cache flush" : "");

		if (*message)
			printf ("\n%s\n", message);

		printf ("\n\033[2m[c] clean memory%s   [q] quit\033[0m\n", is_root ? "" : " (requires root)");
		fflush (stdout);

		/* wait for keypress or timeout */
		fd_set fds;
		struct timeval tv = {.tv_sec = refresh_seconds, .tv_usec = 0};

		FD_ZERO (&fds);
		FD_SET (STDIN_FILENO, &fds);

		if (select (STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0)
		{
			char key = 0;

			if (read (STDIN_FILENO, &key, 1) == 1)
			{
				if (key == 'q' || key == 'Q' || key == 27)
					break;

				if (key == 'c' || key == 'C')
				{
					if (!is_root)
					{
						snprintf (message, sizeof (message), "\033[91mCleaning requires root - restart with: sudo " APP_BINARY " monitor\033[0m");
					}
					else if (memory_clean (config.clean_flags, &result))
					{
						timestamp_now (stamp, sizeof (stamp));
						format_size (result.freed, freed_str, sizeof (freed_str));

						snprintf (message, sizeof (message), "\033[92m[%s] Cleaned: %s reclaimed (%.1f%% -> %.1f%%)\033[0m",
							stamp, freed_str, result.percent_before, result.percent_after);
					}
					else
					{
						snprintf (message, sizeof (message), "\033[91mCleaning failed: %s\033[0m", strerror (errno));
					}
				}
			}
		}
	}

	terminal_restore ();
	termios_saved = false;

	return 0;
}

/*
 * -------------------------------------------------------------------------
 * Entry point
 * -------------------------------------------------------------------------
 */

static void print_usage (void)
{
	printf (
		APP_NAME " " APP_VERSION " - real-time memory monitoring and cleaning for Linux\n"
		APP_COPYRIGHT "\n"
		"\n"
		"Usage: " APP_BINARY " [command] [options]\n"
		"\n"
		"Commands:\n"
		"  status                 show current memory usage (default)\n"
		"  stats                  show detailed memory and process statistics\n"
		"  clean                  clean memory now (requires root)\n"
		"  top                    show processes using the most memory\n"
		"  monitor                interactive live monitor ('c' = clean, 'q' = quit)\n"
		"  daemon                 run auto-clean daemon in the foreground\n"
		"\n"
		"Cleaning areas (for 'clean' and as defaults for 'daemon'/'monitor'):\n"
		"  --pagecache            drop clean page cache (standby lists)\n"
		"  --dentries             drop dentries and inodes (slab caches)\n"
		"  --compact              compact fragmented physical memory\n"
		"  --swap                 flush swap back into RAM (swapoff/swapon)\n"
		"  --modified-cache       flush modified file-backed pages (sync)\n"
		"  --all                  all of the above\n"
		"  (default: --pagecache --dentries --compact --modified-cache)\n"
		"\n"
		"Options:\n"
		"  -t, --threshold <pct>  auto-clean when memory usage >= pct (daemon)\n"
		"  -p, --pressure <pct>   auto-clean at the selected PSI memory pressure (daemon)\n"
		"      --pressure-type <mode>  PSI metric: some (default) or full\n"
		"  -n, --interval <min>   auto-clean every N minutes (daemon)\n"
		"  -i, --refresh <sec>    refresh/poll period (monitor/daemon)\n"
		"  -l, --limit <n>        number of processes to show (top, default 10)\n"
		"  -j, --json             machine-readable JSON output (status/stats/top)\n"
		"  -c, --config <file>    use an alternative config file\n"
		"  -q, --quiet            disable desktop notifications\n"
		"  -h, --help             show this help\n"
		"  -v, --version          show version\n"
		"\n"
		"Configuration: " SYSTEM_CONFIG ", ~/.config/memreduct/memreduct.conf\n"
		"Systemd:       systemctl enable --now memreduct\n");
}

int main (int argc, char *argv[])
{
	const char *command = "status";
	const char *config_path = NULL;
	unsigned flags_arg = 0;
	int threshold_arg = -1;
	int pressure_arg = -1;
	const char *pressure_type_arg = NULL;
	int interval_arg = -1;
	int refresh_arg = -1;
	int limit_arg = 10;
	bool json_arg = false;
	int i = 1;

	if (argc > 1 && argv[1][0] != '-')
	{
		command = argv[1];
		i = 2;
	}

	for (; i < argc; i++)
	{
		const char *arg = argv[i];

		if (!strcmp (arg, "-h") || !strcmp (arg, "--help"))
		{
			print_usage ();
			return 0;
		}
		else if (!strcmp (arg, "-v") || !strcmp (arg, "--version"))
		{
			printf (APP_NAME " " APP_VERSION "\n");
			return 0;
		}
		else if (!strcmp (arg, "--pagecache"))
		{
			flags_arg |= CLEAN_PAGECACHE;
		}
		else if (!strcmp (arg, "--dentries"))
		{
			flags_arg |= CLEAN_SLAB;
		}
		else if (!strcmp (arg, "--compact"))
		{
			flags_arg |= CLEAN_COMPACT;
		}
		else if (!strcmp (arg, "--swap"))
		{
			flags_arg |= CLEAN_SWAP;
		}
		else if (!strcmp (arg, "--modified-cache") || !strcmp (arg, "--sync"))
		{
			flags_arg |= CLEAN_MODIFIEDCACHE;
		}
		else if (!strcmp (arg, "--all"))
		{
			flags_arg |= CLEAN_PAGECACHE | CLEAN_SLAB | CLEAN_COMPACT | CLEAN_SWAP | CLEAN_MODIFIEDCACHE;
		}
		else if ((!strcmp (arg, "-t") || !strcmp (arg, "--threshold")) && i + 1 < argc)
		{
			threshold_arg = atoi (argv[++i]);
		}
		else if ((!strcmp (arg, "-p") || !strcmp (arg, "--pressure")) && i + 1 < argc)
		{
			pressure_arg = atoi (argv[++i]);
		}
		else if (!strcmp (arg, "--pressure-type") && i + 1 < argc)
		{
			pressure_type_arg = argv[++i];

			if (strcmp (pressure_type_arg, "some") && strcmp (pressure_type_arg, "full"))
			{
				fprintf (stderr, "error: --pressure-type must be 'some' or 'full'\n");
				return 1;
			}
		}
		else if ((!strcmp (arg, "-n") || !strcmp (arg, "--interval")) && i + 1 < argc)
		{
			interval_arg = atoi (argv[++i]);
		}
		else if ((!strcmp (arg, "-i") || !strcmp (arg, "--refresh")) && i + 1 < argc)
		{
			refresh_arg = atoi (argv[++i]);
		}
		else if ((!strcmp (arg, "-l") || !strcmp (arg, "--limit")) && i + 1 < argc)
		{
			limit_arg = atoi (argv[++i]);
		}
		else if (!strcmp (arg, "-j") || !strcmp (arg, "--json"))
		{
			json_arg = true;
		}
		else if ((!strcmp (arg, "-c") || !strcmp (arg, "--config")) && i + 1 < argc)
		{
			config_path = argv[++i];
		}
		else if (!strcmp (arg, "-q") || !strcmp (arg, "--quiet"))
		{
			config.notifications = false;
		}
		else
		{
			fprintf (stderr, "error: unknown option: %s (see --help)\n", arg);
			return 1;
		}
	}

	config_load (config_path);

	/* command line overrides config file */
	if (flags_arg)
		config.clean_flags = flags_arg;

	if (threshold_arg >= 0)
		config.clean_threshold = threshold_arg;

	if (pressure_arg >= 0)
		config.clean_pressure = pressure_arg;

	if (pressure_type_arg)
		config.clean_pressure_full = !strcmp (pressure_type_arg, "full");

	if (interval_arg >= 0)
		config.clean_interval = interval_arg;

	if (refresh_arg >= 1)
		config.check_interval = refresh_arg;

	config_sanitize ();

	if (!strcmp (command, "status"))
		return command_status (json_arg);

	if (!strcmp (command, "stats"))
		return command_stats (json_arg);

	if (!strcmp (command, "clean"))
		return command_clean (config.clean_flags);

	if (!strcmp (command, "top"))
		return command_top (limit_arg, json_arg);

	if (!strcmp (command, "monitor"))
		return command_monitor (config.check_interval);

	if (!strcmp (command, "daemon"))
		return command_daemon ();

	fprintf (stderr, "error: unknown command: %s (see --help)\n", command);

	return 1;
}
