Installing Shairport Sync into Cygwin [Airplay 2 Not Supported]
====

This guide is based on installing onto a fresh installation of Cygwin 2.895 (64-bit installation) running in Windows 10
inside VMWare Fusion on a Mac.

The end result is a new Windows Service called `CYGWIN Shairport Sync`, providing an AirPlay service by which iOS devices or other AirPlay sources on the network can play audio through the Windows device.

Windows Firewall
----
While getting everything working, it is suggested that you temporarily disable the Windows Firewall. Shairport Sync uses port 5000 for TCP and uses three ports for UDP, so you should leave a minimum of three, and preferably at least 10, open from 6001 upwards. The Bonjour Service, used in conjunction with the Avahi daemon, advertises Shairport Sync over a number of further ports. Once everything is working, the firewall can be re-enabled gradually.

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
C:\Users\mike> Downloads\setup-x86_64.exe -P cygrunsrv,dbus,avahi,avahi-tools,gnome-keyring,libavahi-client-devel,^
libglib2.0-devel,openssl,pkg-config,autoconf,automake,clang,libdaemon-devel,popt-devel,^
make,libao-devel,openssl-devel,libtool,git,wget,flex,bison
```
This will do a complete installation of Cygwin and all necessary packages.
* Set up the D-Bus and Avahi Services:

Open a `Cygwin64 Terminal` window in Administrator mode. Enter the following command:
```
$ messagebus-config
```
Answer `yes` to all queries. Open the Windows `Services` desktop application (if it's already open, refresh the screen contents: `Actions > Refresh`) and look for the `CYGWIN D-Bus system service`. Open it and start it.

Next, open (or return to) a `Cygwin64 Terminal` window in Administrator mode. Enter the following command:
```
$ /usr/sbin/avahi-daemon-config
```
Answer `yes` to all queries. Open the Windows `Services` desktop application (if it's already open, refresh the screen contents: `Actions > Refresh`) and look for the `CYGWIN Avahi service`. Open it and start it.

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

Shairport Sync
----
* Download, configure and compile Shairport Sync:
```
$ git clone https://github.com/mikebrady/shairport-sync.git
$ cd shairport-sync
$ git checkout development // this is temporary
$ autoreconf -fi
$ PKG_CONFIG_PATH=/usr/local/lib/pkgconfig ./configure --with-ao --with-ssl=openssl \
    --with-avahi --with-dbus-interface --with-libdaemon --sysconfdir=/etc --with-cygwin-service
$ make
$ make install
```
* The last step above installs the `shairport-sync` application into `/usr/local/bin` and also installs a configuration file, a service configuration script and two D-Bus policy files. 

Shairport Sync Service
----
* To install Shairport Sync as a Cygwin Service, open (or return to) a `Cygwin64 Terminal` window in Administrator mode. Enter the following command:
```
$ shairport-sync-config
```
Answer `yes` to all queries. Open the Windows `Services` desktop application (if it's already open, refresh the screen contents: `Actions > Refresh`) and look for the `CYGWIN Shairport Sync` service. Open it and start it.

An AirPlay player on the local network should now  be able to see an AirPlay output device bearing the computer's Device Name, e.g. `DESKTOP-0RHGN0`. You can set a different name by changing the settings in the Shairport Sync configuration file, installed at `/etc/shairport-sync.conf`.

Since Shairport Sync is now a Cygwin Service, you do not need to open Cygwin to launch it – it should launch automatically when Windows is booted up.

Known Issues
----
* Shairport Sync cannot access the D-Bus system bus to make its D-Bus interface available. The cause of this problem is unknown. (While the Avahi daemon can access the D-Bus system bus, Shairport Sync can not. The two applications use different D-Bus libraries, so perhaps the issue lies there.)
