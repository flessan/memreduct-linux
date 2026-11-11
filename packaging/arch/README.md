# Arch Linux packaging

The canonical AUR recipe is the repository-root `PKGBUILD`, with its
generated `.SRCINFO`. This directory mirrors the same recipe for discoverability.

## Build locally

Install the base build tools, Git and the optional smoke-test runtime:

```sh
sudo pacman -S --needed base-devel git python
makepkg -si
```

The package installs to standard Arch locations under `/usr`, keeps
`/etc/memreduct.conf` as a pacman backup file, and ships the systemd unit,
man page and Bash/Fish completions. It deliberately does not enable the
auto-clean daemon or alter kernel settings during installation.

Optional ZRAM support can be configured independently using the official
`zram-generator` package:

```sh
sudo pacman -S zram-generator
sudo install -d /etc/systemd
sudo tee /etc/systemd/zram-generator.conf >/dev/null <<'EOF'
[zram0]
zram-size = min(ram / 2, 4096)
compression-algorithm = zstd
EOF
sudo systemctl daemon-reload
sudo reboot
```

Mem Reduct detects and reports existing ZRAM devices; it never creates or
resets them. See the [Arch ZRAM guide](https://wiki.archlinux.org/title/ZRAM).

## Updating the AUR recipe

When changing package metadata or the pinned upstream Git commit, regenerate
`.SRCINFO` with:

```sh
makepkg --printsrcinfo > .SRCINFO
```

Commit both files together. The Git source is pinned to an immutable commit,
not a moving branch.
