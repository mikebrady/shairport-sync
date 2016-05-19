Name:           shairport-sync
Version:        2.8.3
Release:        1%{?dist}
Summary:        AirTunes emulator. Shairport Sync adds multi-room capability with Audio Synchronisation.

Group:          Applications/Multimedia
License:        GPL
URL:            https://github.com/mikebrady/shairport-sync
Source0:        https://github.com/mikebrady/%{name}/archive/%{version}.tar.gz#/%{name}-%{version}.tar.gz

BuildRequires:  autoconf
BuildRequires:  automake
BuildRequires:  libconfig-devel
BuildRequires:  popt-devel
BuildRequires:  openssl-devel
BuildRequires:  libdaemon-devel
BuildRequires:  avahi-devel
BuildRequires:  alsa-lib-devel
BuildRequires:  systemd-units
BuildRequires:  soxr-devel
Requires:       popt
Requires:       openssl
Requires:       avahi
Requires:       libdaemon
Requires:       alsa-lib
Requires:       soxr

%description
Shairport Sync emulates an AirPort Express for the purpose of streaming audio from iTunes, iPods, iPhones, iPads and AppleTVs. Audio played by a Shairport Sync-powered device stays synchronised with the source and hence with similar devices playing the same source. Thus, for example, synchronised multi-room audio is possible without difficulty. (Hence the name Shairport Sync, BTW.)

Shairport Sync does not support AirPlay video or photo streaming.

%prep
%setup -q

%build
autoreconf -i -f
%configure --with-avahi --with-alsa --with-ssl=openssl --with-soxr --with-systemd
make %{?_smp_mflags}

%install
make install DESTDIR=%{buildroot}
rm %{buildroot}/etc/shairport-sync.conf.sample

%pre
getent group %{name} &>/dev/null || groupadd --system %{name} >/dev/null
getent passwd %{name} &> /dev/null || useradd --system -c "%{name} User" \
        -d %{_localstatedir}/%{name} -m -g %{name} -s /sbin/nologin \
        -G audio %{name} >/dev/null

%files
%config /etc/shairport-sync.conf
/usr/bin/shairport-sync
/usr/share/man/man7/shairport-sync.7.gz
%{_unitdir}/%{name}.service
%doc AUTHORS LICENSES README.md

%changelog
* Fri Apr 15 2016 Mike Brady <mikebrady@eircom.net> 2.8.2
- Stability improvements, bug fixes and a few special-purpose settings 2.8.2
* Wed Mar 02 2016 Mike Brady <mikebrady@eircom.net> 2.8.1
- Stability improvements and important bug fixes 2.8.1
* Sat Jan 30 2016 Mike Brady <mikebrady@eircom.net> 2.8.0
- Enhancements and bug fixes 2.8.0
* Sun Oct 18 2015 Mike Brady <mikebrady@eircom.net> 2.6
- Important enhancements and bug fixes 2.6
* Thu Aug 27 2015 Mike Brady <mikebrady@eircom.net> 2.4.1
- Minor bug fixes 2.4.1
* Thu Aug 27 2015 Mike Brady <mikebrady@eircom.net> 2.4
- Prepare for stable release 2.4
* Wed Aug 26 2015 Mike Brady <mikebrady@eircom.net> 2.3.13.1-1
- Harmonise release numbers
* Fri Jul 24 2015 Bill Peck <bill@pecknet.com> 2.3.7-1
- Initial spec file
