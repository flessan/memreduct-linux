# RPM spec for Fedora / RHEL / CentOS / AlmaLinux / Rocky / openSUSE
#
# Build from a source tarball named memreduct-1.0.0.tar.gz containing
# the repository at top level directory memreduct-1.0.0/:
#   rpmbuild -ba packaging/rpm/memreduct.spec

Name:           memreduct
Version:        1.1.0
Release:        1%{?dist}
Summary:        Lightweight real-time memory monitoring and cleaning
License:        GPL-3.0-or-later
URL:            https://github.com/flessan/memreduct-linux
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc, make
Recommends:     libnotify

%description
Lightweight real-time memory management application to monitor and clean
system memory. Linux port of Mem Reduct: cleans the page cache, dentry and
inode slab caches, compacts physical memory, and can flush swap back to
RAM - on demand, at a usage threshold, or on a timer.

%prep
%setup -q

%build
make CFLAGS="%{optflags} -std=c99" %{?_smp_mflags}

%install
make DESTDIR=%{buildroot} PREFIX=/usr install

%post
%systemd_post memreduct.service

%preun
%systemd_preun memreduct.service

%postun
%systemd_postun_with_restart memreduct.service

%files
%license LICENSE
%doc README.md
%config(noreplace) /etc/memreduct.conf
/usr/bin/memreduct
/usr/lib/systemd/system/memreduct.service
/usr/share/man/man1/memreduct.1*
/usr/share/bash-completion/completions/memreduct
/usr/share/fish/vendor_completions.d/memreduct.fish
/usr/share/doc/memreduct/README.md

%changelog
* Mon Sep 01 2026 Mem Reduct for Linux contributors - 1.0.0-1
- Initial Linux release
