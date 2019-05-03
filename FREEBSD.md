Shairport Sync on FreeBSD using `sndio`
----
Shairport Sync runs natively on FreeBSD using the `sndio` back end, thanks to the work of [Tobias Kortkamp (t6)](https://github.com/t6).

[`sndio`](http://www.sndio.org) is *"a small audio and MIDI framework part of the OpenBSD project and ported to FreeBSD, Linux and NetBSD"* developed by Alexandre Ratchov (see also [this paper](http://www.openbsd.org/papers/asiabsdcon2010_sndio.pdf) for more details).

This is an initial note about installing Shairport Sync on FreeBSD.

The build instructions here install back ends both for `sndio` and ALSA. ALSA is, or course, the Advanced Linux Sound Architecture, so it is not "native" to FreeBSD, but has been ported to some architectures under FreeBSD. 

General
----
This build was done on a default build of `FreeBSD 11.0-RELEASE-p9`.

First, update everything:
```
# freebsd-update fetch
# freebsd-update install
```
Next, install the `pkg` package manager and update its lists:

```
# pkg
# pkg update
```

Subsystems
----
Install the Avahi subsystem. FYI, `avahi-app` is chosen because it doesn’t require X11. `nss_mdns` is included to allow FreeBSD to resolve mDNS-originated addresses – it's not actually needed by Shairport Sync. Thanks to [reidransom](https://gist.github.com/reidransom/6033227) for this.

```
# pkg install avahi-app nss_mdns
```
Add these lines to `/etc/rc.conf`:
```
dbus_enable="YES"
avahi_daemon_enable="YES"
```
Next, change the `hosts:` line in `/etc/nsswitch.conf` to
```
hosts: files dns mdns
```
Reboot for these changes to take effect.

Building
----

Install the packages that are needed for Shairport Sync to be downloaded and built successfully:
```
# pkg install git autotools pkgconf popt libconfig openssl sndio alsa-utils
```
Omit `alsa-utils` if you're not using ALSA. Likewise, omit `sndio` if you don't intend to use the `sndio` subsystem.

Now, download Shairport Sync from GitHub:
```
$ git clone https://github.com/mikebrady/shairport-sync.git
$ cd shairport-sync
```
Next, configure the build and compile it:

```
$ autoreconf -i -f
$ ./configure  --with-avahi --with-ssl=openssl --with-alsa --with-sndio --with-os=freebsd --with-freebsd-service --with-libdaemon
$ make
```
Omit `--with-alsa` if you don't want to include the ALSA back end. Omit the `--with-sndio` if you don't want the `sndio` back end. Omit the `--with-freebsd-service` if you don't want to install a FreeBSD startup script, runtime folder and user and group -- see below for more details.

Installation
----

Enter the superuser mode and do a `make install`:

```
$ su
# make install
```

With the `./configure` options shown above, this will install the `shairport-sync` program along with a sample configuration file at `/usr/local/etc/shairport-sync.conf`. A service startup script will also be installed to launch Shairport Sync as a daemon. In addition, a `shairport-sync` user and group will be added and a directory will be created at `/var/run/shairport-sync` owned by the user `shairport-sync`. This will be used to hold the daemon's PID file.

Finally, edit `/usr/local/etc/shairport-sync.conf` to customise your installation, e.g. service name, etc. To make the `shairport-sync` daemon load at startup, add the following line to `/etc/rc.conf`:

```
shairport_sync_enable="YES"
```
You can launch the service as superuser, or simply reboot the machine.

Using the `sndio` backend
----

The `sndio` back end does not yet have a hardware volume control facility. You should set the volume to maximum before use, using, for example, the `mixer` command described below.

Setting Overall  Volume
----
The `mixer` command can be used for setting the output device's volume settings. You may have to experiment to figure out which settings are appropriate.

```
$ mixer vol 100 # sets overall volume
```
If you've installed `alsa-utils`, then `alsamixer` and friends will also be available.
