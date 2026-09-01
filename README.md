<h1 align="center">Mem Reduct for Linux</h1>

<p align="center">
Lightweight real-time memory management application to monitor<br/>
and clean system memory on your computer.
</p>

-------

### Install - just copy & paste

One command, works from **bash**, **zsh** and **fish**:

```sh
curl -fsSL https://raw.githubusercontent.com/flessan/memreduct-linux/master/install.sh | bash
```

It opens a tiny menu - pick what you want with the arrow keys (or press a number):

```
  Mem Reduct for Linux - installer
  memory: 1.2 GB used of 3.8 GB (31%)

  > Install                    (recommended)
    Install + enable auto-clean daemon
    Build portable static binary only (no install)
    Uninstall
    Quit
```

**Is it safe?** Yes - but don't take our word for it, take 30 seconds and
[read the script](install.sh) before running it. It's ~250 lines of plain
shell that only does three things: installs `gcc`/`make` with your distro's
package manager if missing, builds the program from this repository's source,
and runs `make install`. Nothing else is downloaded or executed. If you
prefer, inspect first and run after:

```sh
curl -fsSL https://raw.githubusercontent.com/flessan/memreduct-linux/master/install.sh -o install.sh
less install.sh        # check it - it's short and readable
bash install.sh
```

Skip the menu with flags: `... | bash -s -- --install` / `--daemon` /
`--static` / `--uninstall`. No curl? Use
`wget -qO- <same url> | bash`. Uninstall any time with:

```sh
curl -fsSL https://raw.githubusercontent.com/flessan/memreduct-linux/master/install.sh | bash -s -- --uninstall
```

-------

Linux port of [Mem Reduct](https://github.com/henrypp/memreduct) by Henry++.
Where the Windows version uses undocumented Native API calls to clear the
working sets and standby page lists, this port uses the official Linux kernel
interfaces to achieve the same result:

| Windows original            | Linux port                                    |
|-----------------------------|-----------------------------------------------|
| Standby page lists          | Page cache (`/proc/sys/vm/drop_caches` = 1)   |
| System working set          | Dentry/inode slab caches (`drop_caches` = 2)  |
| Combined memory lists       | Memory compaction (`compact_memory`)          |
| Modified page lists         | `sync()` + swap reload (`swapoff`/`swapon`)   |

### Features
- **Status** - colored overview of physical memory, reclaimable cache, swap
  and PSI memory pressure, with `--json` output for scripting
- **Clean** - one-shot memory cleaning with selectable areas
- **Top** - processes using the most memory (`memreduct top`)
- **Monitor** - interactive full-screen live monitor with usage history
  graph, memory pressure and top consumers (`c` = clean, `q` = quit)
- **Daemon** - automatic cleaning at a usage threshold and/or on a timer,
  high-usage alerts, systemd integration, config reload on `SIGHUP`,
  syslog/journal logging and optional desktop notifications
- **Shell completions** for bash and fish, man page, systemd unit
- Zero dependencies: plain C99 + libc + `/proc` - no GUI toolkit, no libraries

### Supported distributions
All of them. The program only needs a C library and a Linux kernel (≥ 2.6.16
for `drop_caches`); it builds and runs identically on glibc and musl:

Debian, Ubuntu, Mint, Pop!_OS, Fedora, RHEL, CentOS, AlmaLinux, Rocky,
openSUSE, Arch, Manjaro, EndeavourOS, CachyOS, Alpine, Void, Gentoo, Slackware, NixOS,
postmarketOS, Raspberry Pi OS - x86_64, i686, ARM, RISC-V, anything the
kernel runs on. A `make static` build produces a single portable binary that
runs on any distro without installing anything.

### Build & install (manual way)
```sh
make                 # build (any C99 compiler: gcc, clang, tcc)
sudo make install    # install binary, config, man page, systemd unit
```

Optional:
```sh
make static                        # fully static, distro-independent binary
./packaging/debian/build-deb.sh    # build a .deb  (Debian/Ubuntu/Mint/...)
rpmbuild -ba packaging/rpm/memreduct.spec   # build an .rpm (Fedora/RHEL/openSUSE)
cd packaging/arch && makepkg -si            # Arch Linux package
```

### Usage
```sh
memreduct                      # show memory usage
memreduct status --json        # ...as JSON, for scripts
memreduct top                  # biggest memory consumers
sudo memreduct clean           # clean memory now
sudo memreduct clean --all     # ...including swap reload
sudo memreduct monitor         # live monitor; press 'c' to clean
sudo memreduct daemon -t 85    # auto-clean at 85% usage (foreground)
```

Run it automatically at boot:
```sh
sudo systemctl enable --now memreduct
```

### Configuration
System-wide `/etc/memreduct.conf`, per-user
`~/.config/memreduct/memreduct.conf`; command-line options override both.
The daemon reloads its configuration on `systemctl reload memreduct`.

```ini
clean_threshold = 90    # auto-clean when memory usage reaches 90% (0 = off)
clean_interval = 0      # additionally clean every N minutes (0 = off)
cooldown = 60           # min seconds between threshold-triggered cleans
clean_pagecache = yes   # drop clean page cache
clean_dentries = yes    # drop dentry/inode slab caches
clean_compact = yes     # compact physical memory
clean_swap = no         # flush swap back to RAM
notifications = yes     # desktop notifications via notify-send
```

See `man memreduct` for the full reference.

### Landing page
A static Vite + React landing page (with live GitHub API stats) lives in
[`site/`](site/) and can auto-deploy to GitHub Pages - copy `site/deploy/deploy-pages.yml` to `.github/workflows/`
(enable it once: Settings → Pages → Source: GitHub Actions).

### Notes
- Cleaning requires root; monitoring does not.
- Dropping caches is non-destructive - `sync()` is called first and the
  kernel only discards clean, reclaimable pages. Expect a short period of
  slower disk access while hot caches repopulate.
- Swap reload is skipped automatically when the swapped data would not fit
  into available RAM.

---
- Original Windows version: [github.com/henrypp/memreduct](https://github.com/henrypp/memreduct)
---
(c) 2011-2026 Henry++ - GPL v3
