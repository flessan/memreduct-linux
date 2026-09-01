# Mem Reduct for Linux
#
# Builds with any C99 compiler (gcc, clang, tcc) against glibc or musl.
#
#   make                 build
#   make static          build a fully static binary (portable across distros)
#   make install         install binary, config, man page, systemd unit
#   make uninstall       remove everything installed by 'make install'

CC      ?= cc
CFLAGS  ?= -O2 -pipe
CFLAGS  += -std=c99 -Wall -Wextra -Wpedantic
LDFLAGS ?=

PREFIX     ?= /usr/local
BINDIR      = $(DESTDIR)$(PREFIX)/bin
MANDIR      = $(DESTDIR)$(PREFIX)/share/man/man1
DOCDIR      = $(DESTDIR)$(PREFIX)/share/doc/memreduct
UNITDIR     = $(DESTDIR)$(PREFIX)/lib/systemd/system
SYSCONFDIR  = $(DESTDIR)/etc

BIN = memreduct
SRC = src/memreduct.c

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

static: $(SRC)
	$(CC) $(CFLAGS) -static -o $(BIN) $(SRC) $(LDFLAGS)

install: $(BIN)
	install -Dm755 $(BIN) $(BINDIR)/$(BIN)
	install -Dm644 memreduct.conf $(SYSCONFDIR)/memreduct.conf
	sed 's|/usr/local/bin|$(PREFIX)/bin|' packaging/memreduct.service > .memreduct.service.tmp
	install -Dm644 .memreduct.service.tmp $(UNITDIR)/memreduct.service
	rm -f .memreduct.service.tmp
	install -Dm644 packaging/memreduct.1 $(MANDIR)/memreduct.1
	install -Dm644 packaging/completions/memreduct.bash $(DESTDIR)$(PREFIX)/share/bash-completion/completions/memreduct
	install -Dm644 packaging/completions/memreduct.fish $(DESTDIR)$(PREFIX)/share/fish/vendor_completions.d/memreduct.fish
	install -Dm644 README.md $(DOCDIR)/README.md
	@echo
	@echo "Installed. To enable automatic memory cleaning:"
	@echo "  systemctl daemon-reload && systemctl enable --now memreduct"

uninstall:
	rm -f $(BINDIR)/$(BIN)
	rm -f $(SYSCONFDIR)/memreduct.conf
	rm -f $(UNITDIR)/memreduct.service
	rm -f $(MANDIR)/memreduct.1
	rm -f $(DESTDIR)$(PREFIX)/share/bash-completion/completions/memreduct
	rm -f $(DESTDIR)$(PREFIX)/share/fish/vendor_completions.d/memreduct.fish
	rm -rf $(DOCDIR)

clean:
	rm -f $(BIN)

.PHONY: all static install uninstall clean
