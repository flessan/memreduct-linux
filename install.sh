#!/usr/bin/env bash
#
# Mem Reduct for Linux - one-command installer
#
# Safe by design: this script only
#   1. installs 'gcc'/'make' with your distro's package manager (if missing)
#   2. builds the program from source with 'make'
#   3. runs 'make install' (binary, config, man page, systemd unit)
# Nothing else is downloaded or executed. Read it before running - please do!
#
# Usage (any shell - bash, zsh, fish):
#   curl -fsSL https://raw.githubusercontent.com/flessan/memreduct-linux/master/install.sh | bash
#
# Non-interactive flags:
#   ... | bash -s -- --install      install
#   ... | bash -s -- --daemon      install + enable auto-clean service
#   ... | bash -s -- --static      build a portable static binary only
#   ... | bash -s -- --uninstall   remove everything
#
# Environment overrides:
#   MEMREDUCT_REF=master           git branch/tag to download
#   PREFIX=/usr/local              install prefix

set -eu

REPO="flessan/memreduct-linux"
REF="${MEMREDUCT_REF:-master}"
PREFIX="${PREFIX:-/usr/local}"

# ---------------------------------------------------------------- colors ---
if [ -t 1 ] || [ -e /dev/tty ]; then
	C_RESET=$'\033[0m';  C_BOLD=$'\033[1m';   C_DIM=$'\033[2m'
	C_GREEN=$'\033[92m'; C_YELLOW=$'\033[93m'; C_RED=$'\033[91m'; C_CYAN=$'\033[96m'
else
	C_RESET=""; C_BOLD=""; C_DIM=""; C_GREEN=""; C_YELLOW=""; C_RED=""; C_CYAN=""
fi

say ()  { printf '%s\n' "$1"; }
ok ()   { printf '%s\n' "  ${C_GREEN}[ok]${C_RESET} $1"; }
warn () { printf '%s\n' "  ${C_YELLOW}[!!]${C_RESET} $1"; }
die ()  { printf '%s\n' "  ${C_RED}[error]${C_RESET} $1" >&2; exit 1; }

# ------------------------------------------------------------------ sudo ---
SUDO=""
if [ "$(id -u)" -ne 0 ]; then
	if command -v sudo >/dev/null 2>&1; then
		SUDO="sudo"
	elif command -v doas >/dev/null 2>&1; then
		SUDO="doas"
	else
		die "root privileges are needed to install; install sudo/doas or run as root"
	fi
fi

# ------------------------------------------------------- package manager ---
install_build_deps () {
	if command -v cc >/dev/null 2>&1 || command -v gcc >/dev/null 2>&1; then
		if command -v make >/dev/null 2>&1; then
			ok "build tools already present"
			return
		fi
	fi

	say "  installing build tools (gcc, make)..."

	if command -v apt-get >/dev/null 2>&1; then
		$SUDO apt-get update -qq && $SUDO apt-get install -y -qq gcc make
	elif command -v dnf >/dev/null 2>&1; then
		$SUDO dnf install -y -q gcc make
	elif command -v yum >/dev/null 2>&1; then
		$SUDO yum install -y -q gcc make
	elif command -v pacman >/dev/null 2>&1; then
		$SUDO pacman -Sy --noconfirm --needed gcc make
	elif command -v zypper >/dev/null 2>&1; then
		$SUDO zypper --non-interactive install gcc make
	elif command -v apk >/dev/null 2>&1; then
		$SUDO apk add --no-progress gcc make musl-dev
	elif command -v xbps-install >/dev/null 2>&1; then
		$SUDO xbps-install -Sy gcc make
	elif command -v emerge >/dev/null 2>&1; then
		warn "Gentoo detected - gcc/make are normally already installed"
	else
		die "no supported package manager found - install 'gcc' and 'make' manually, then re-run"
	fi

	ok "build tools ready"
}

# ---------------------------------------------------------------- source ---
SRCDIR=""
TMPDIR_DL=""
ORIG_PWD="$PWD"

cleanup () { if [ -n "$TMPDIR_DL" ]; then rm -rf "$TMPDIR_DL"; fi; }
trap cleanup EXIT

locate_source () {
	# already inside a checkout? (repo dir or script dir)
	script_dir="$(cd "$(dirname "$0")" 2>/dev/null && pwd || true)"

	for candidate in "$PWD" "$script_dir"; do
		if [ -n "$candidate" ] && [ -f "$candidate/src/memreduct.c" ] && [ -f "$candidate/Makefile" ]; then
			SRCDIR="$candidate"
			ok "using local source: $SRCDIR"
			return
		fi
	done

	say "  downloading source (${REPO} @ ${REF})..."

	TMPDIR_DL="$(mktemp -d)"

	if command -v curl >/dev/null 2>&1; then
		curl -fsSL "https://codeload.github.com/${REPO}/tar.gz/refs/heads/${REF}" | tar -xz -C "$TMPDIR_DL" \
			|| curl -fsSL "https://codeload.github.com/${REPO}/tar.gz/refs/tags/${REF}" | tar -xz -C "$TMPDIR_DL" \
			|| die "download failed - check your network"
	elif command -v wget >/dev/null 2>&1; then
		wget -qO- "https://codeload.github.com/${REPO}/tar.gz/refs/heads/${REF}" | tar -xz -C "$TMPDIR_DL" \
			|| die "download failed - check your network"
	else
		die "neither curl nor wget found - install one of them, then re-run"
	fi

	SRCDIR="$(find "$TMPDIR_DL" -mindepth 1 -maxdepth 1 -type d | head -n 1)"

	[ -f "$SRCDIR/src/memreduct.c" ] || die "downloaded archive looks wrong"

	ok "source ready"
}

# --------------------------------------------------------------- actions ---
memory_line () {
	awk '/MemTotal/ {t=$2} /MemAvailable/ {a=$2} END {
		if (t) printf "%.1f GB used of %.1f GB (%.0f%%)", (t-a)/1048576, t/1048576, 100*(t-a)/t
	}' /proc/meminfo 2>/dev/null || true
}

do_build () {
	install_build_deps
	locate_source
	say "  building..."
	make -s -C "$SRCDIR" clean >/dev/null 2>&1 || true
	make -s -C "$SRCDIR"
	ok "built: $SRCDIR/memreduct"
}

do_static () {
	install_build_deps
	locate_source
	say "  building static portable binary..."
	make -s -C "$SRCDIR" clean >/dev/null 2>&1 || true
	if ! make -s -C "$SRCDIR" static 2>/dev/null; then
		warn "static linking unavailable on this system, building normal binary"
		make -s -C "$SRCDIR"
	fi
	if [ "$SRCDIR/memreduct" != "$ORIG_PWD/memreduct" ]; then
		cp "$SRCDIR/memreduct" "$ORIG_PWD/memreduct"
	fi
	ok "portable binary saved to: $ORIG_PWD/memreduct"
	say ""
	say "  run it with:  ${C_BOLD}./memreduct${C_RESET}"
}

do_install () {
	do_build
	say "  installing to ${PREFIX}..."
	$SUDO make -s -C "$SRCDIR" PREFIX="$PREFIX" install >/dev/null
	ok "installed: ${PREFIX}/bin/memreduct"

	hash -r 2>/dev/null || true

	say ""
	say "  ${C_BOLD}Quick start${C_RESET}"
	say "    memreduct                   show memory usage"
	say "    sudo memreduct clean        clean memory now"
	say "    sudo memreduct monitor      live monitor ('c' = clean, 'q' = quit)"
	say "    man memreduct               full manual"
}

enable_daemon () {
	if command -v systemctl >/dev/null 2>&1 && [ -d /run/systemd/system ]; then
		$SUDO systemctl daemon-reload
		$SUDO systemctl enable --now memreduct
		ok "auto-clean daemon enabled (systemctl status memreduct)"
	else
		warn "systemd not detected - start the daemon manually with: sudo memreduct daemon"
	fi
}

do_uninstall () {
	if command -v systemctl >/dev/null 2>&1 && [ -d /run/systemd/system ]; then
		$SUDO systemctl disable --now memreduct 2>/dev/null || true
	fi

	if [ -f "$PWD/Makefile" ] && [ -f "$PWD/src/memreduct.c" ]; then
		$SUDO make -s -C "$PWD" PREFIX="$PREFIX" uninstall >/dev/null
	else
		$SUDO rm -f "$PREFIX/bin/memreduct" \
			"$PREFIX/lib/systemd/system/memreduct.service" \
			"$PREFIX/share/man/man1/memreduct.1" /etc/memreduct.conf
		$SUDO rm -rf "$PREFIX/share/doc/memreduct"
	fi

	ok "Mem Reduct removed"
}

# ------------------------------------------------------------------- tui ---
banner () {
	say ""
	say "  ${C_CYAN}${C_BOLD}Mem Reduct for Linux${C_RESET} ${C_DIM}- installer${C_RESET}"
	memline="$(memory_line)"
	if [ -n "$memline" ]; then say "  ${C_DIM}memory: ${memline}${C_RESET}"; fi
	say ""
}

tui_menu () {
	# interactive arrow-key menu on /dev/tty; prints selected index to stdout
	options=("$@")
	count=${#options[@]}
	selected=0
	drawn=0

	draw () {
		[ "$drawn" -eq 1 ] && printf '\033[%dA' "$count" > /dev/tty
		for i in "${!options[@]}"; do
			if [ "$i" -eq "$selected" ]; then
				printf '  %s>%s %s%s%s\033[K\n' "$C_GREEN" "$C_RESET" "$C_BOLD" "${options[$i]}" "$C_RESET" > /dev/tty
			else
				printf '    %s\033[K\n' "${options[$i]}" > /dev/tty
			fi
		done
		drawn=1
	}

	printf '\033[?25l' > /dev/tty   # hide cursor
	trap 'printf "\033[?25h" > /dev/tty' RETURN 2>/dev/null || true

	while true; do
		draw
		IFS= read -rsn1 key < /dev/tty || { selected=$((count - 1)); break; }

		case "$key" in
			$'\x1b')
				read -rsn2 -t 0.05 seq < /dev/tty || seq=""
				case "$seq" in
					'[A') selected=$(( (selected + count - 1) % count )) ;;
					'[B') selected=$(( (selected + 1) % count )) ;;
					'')   selected=$((count - 1)); break ;;  # bare Esc = quit
				esac
				;;
			'') break ;;                                     # Enter
			[1-9])
				n=$((key - 1))
				[ "$n" -lt "$count" ] && { selected=$n; break; }
				;;
			q|Q) selected=$((count - 1)); break ;;
		esac
	done

	printf '\033[?25h' > /dev/tty   # show cursor
	echo "$selected"
}

run_tui () {
	banner
	say "  ${C_DIM}up/down + Enter, or press a number${C_RESET}"
	say ""

	choice="$(tui_menu \
		"Install                    (recommended)" \
		"Install + enable auto-clean daemon" \
		"Build portable static binary only (no install)" \
		"Uninstall" \
		"Quit")"

	say ""

	case "$choice" in
		0) do_install ;;
		1) do_install; enable_daemon ;;
		2) do_static ;;
		3) do_uninstall ;;
		*) say "  bye!" ;;
	esac
}

# ------------------------------------------------------------------ main ---
case "${1:-}" in
	--install)   banner; do_install ;;
	--daemon)    banner; do_install; enable_daemon ;;
	--static)    banner; do_static ;;
	--uninstall) banner; do_uninstall ;;
	--help|-h)
		grep '^#' "$0" | sed -n '3,20p' | sed 's/^# \{0,1\}//'
		;;
	*)
		if [ -e /dev/tty ] && ( : < /dev/tty ) 2>/dev/null; then
			run_tui
		else
			# no terminal at all (CI, container build) - default to install
			banner
			warn "no interactive terminal detected, running default install"
			do_install
		fi
		;;
esac

say ""
