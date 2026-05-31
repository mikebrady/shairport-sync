Installing Shairport Sync into Cygwin
====

This guide is based on installing onto a fresh installation of Cygwin 2.895 (64-bit installation) running in Windows 10
inside VMWare Fusion on a Mac.

The end result is an AirPlay service by which iOS devices or other AirPlay sources on the network can play audio through the Windows device. Classic AirPlay can be run as a Cygwin service as before. AirPlay 2 support is intended for manual launch: start `nqptp` first, then start `shairport-sync` with the `ao` backend.

Windows Firewall
----
While getting everything working, it is suggested that you temporarily disable the Windows Firewall. Shairport Sync uses port 5000 for TCP and uses three ports for UDP, so you should leave a minimum of three, and preferably at least 10, open from 6001 upwards. The Bonjour Service advertises Shairport Sync over the local network. Once everything is working, the firewall can be re-enabled gradually.

Setting up Windows
----
Set up Windows 10 and install all updates. Install the `Bonjour Service`, available from Apple in an installer called "Bonjour Print Services for Windows v2.0.2".

* Download and run `Bonjour Print Services for Windows v2.0.2`
* After accepting conditions and clicking the `Install` button, the installer will do a preliminary installation, installing   just the Bonjour Service. It will then pause, inviting you to install Bonjour Print Services. You can decline this, as the Bonjour Service will have been installed during the first part of the installation.

* Check Bonjour Service is running. In Windows, open the `Services` desktop application and ensure that you can see `Bonjour Service` running.

Setting up Cygwin
----
* Download the Cygwin installer from the [official website](https://cygwin.com/install.html). Save the installer in the Downloads folder.

* Open a Windows `Command Prompt` window and enter the following multi-line command, omitting the `C:\Users\mike>` prompt:
```
C:\Users\mike> Downloads\setup-x86_64.exe -P cygrunsrv,libdns_sd-devel,^
libglib2.0-devel,openssl,pkg-config,autoconf,automake,clang,popt-devel,^
make,libao-devel,openssl-devel,libtool,git,wget,flex,bison,libplist-devel,libsodium-devel,^
libgcrypt-devel,libuuid-devel,ffmpeg-devel,vim-common
```
This will do a complete installation of Cygwin and all necessary packages.
The AirPlay 2 Cygwin path uses Bonjour / DNS-SD, not Avahi. Check that the Windows `Bonjour Service` is running before starting Shairport Sync.

The `libconfig` Library
----
Shairport Sync relies on a library – `libconfig` – that is not a Cygwin package, so it must be downloaded, compiled and installed:
* Download, configure, compile and install `libconfig`:
```
$ git clone https://github.com/hyperrealm/libconfig.git
$ cd libconfig
$ autoreconf -fi
$ ./configure
$ make
$ make install
$ cd ..
```

NQPTP for AirPlay 2
----
AirPlay 2 requires the companion `nqptp` program for timing. Build it from its own repository and run it before starting Shairport Sync:
```
$ git clone https://github.com/mikebrady/nqptp.git
$ cd nqptp
$ autoreconf -fi
$ ./configure
$ make
$ ./nqptp -vv
```

Leave `nqptp` running in that terminal while testing. It must be able to use UDP ports `319` and `320` and its control port `9000`; run the Cygwin terminal as Administrator if Windows blocks those ports.

Shairport Sync
----
* Download, configure and compile Shairport Sync:
```
$ git clone https://github.com/mikebrady/shairport-sync.git
$ cd shairport-sync
$ autoreconf -fi
$ PKG_CONFIG_PATH=/usr/local/lib/pkgconfig ./configure --with-ao --with-ssl=openssl \
    --with-dns_sd --with-airplay-2 --with-os=cygwin --sysconfdir=/etc
$ make
$ make install
```
* The last step above installs the `shairport-sync` application into `/usr/local/bin` and also installs a configuration file.

Manual AirPlay 2 Test
----
Start `nqptp` first and then run:
```
$ shairport-sync -vv -o ao
```

An AirPlay player on the local network should now be able to see an AirPlay output device bearing the computer's Device Name, e.g. `DESKTOP-0RHGN0`. You can set a different name by changing the settings in the Shairport Sync configuration file, installed at `/etc/shairport-sync.conf`. To make libao the default backend, set `general.output_backend = "ao";`.

Classic AirPlay Service
----
For classic AirPlay service installation, add `--with-cygwin-startup` or its alias `--with-cygwin-service` to the `configure` command and then run:
```
$ shairport-sync-config
```
Answer `yes` to all queries. Open the Windows `Services` desktop application (if it's already open, refresh the screen contents: `Actions > Refresh`) and look for the `CYGWIN Shairport Sync` service. Open it and start it.

Known Issues
----
* Shairport Sync cannot access the D-Bus system bus to make its D-Bus interface available. The cause of this problem is unknown. (While the Avahi daemon can access the D-Bus system bus, Shairport Sync can not. The two applications use different D-Bus libraries, so perhaps the issue lies there.)
