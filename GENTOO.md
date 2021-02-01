# Gentoo Installation Guide

### Installation
---------
#### Add Overlay
shairport-sync is available in griffon_overlay:
```
# layman -L
# layman -a griffon_overlay
```
#### USE Flags
USE flags | Description
-----|------
`alac` | Include the Apple ALAC Decoder.
`alsa` | Add support for media-libs/alsa-lib (Advanced Linux Sound Architecture)
`convolution` | Add convolution filter support.
`mbedtls` | Uses mbedtls for encryption.
`openssl` | Uses openssl for encryption.
`pulseaudio` | Add support for PulseAudio sound server
`soundio` | Add support for soundio backend.
`soxr` | Add resample support use libsoxr
#### Emerge
```
# emerge media-sound/shairport-sync
```
### Configuration
--------
See [Configuring Shairport Sync](https://github.com/mikebrady/shairport-sync/blob/master/README.md#configuring-shairport-sync)

Remember start avahi-daemon if you run shairport-sync mannually:
```
# systemctl start avahi-daemon
# shairport-sync
```
##### What's more
It takes a little more work to let shairport-sync run as system service with pulseaudio. Here is an example:

First emerge pulseaudio with system-wide support. The `system-wide` USE flag is masked, so we have to unmask it(See [Pulseaudio](https://wiki.gentoo.org/wiki/PulseAudio#Headless_server)):
```
# mkdir -p /etc/portage/profile
# echo "-system-wide" >> /etc/portage/profile/use.mask
# echo "media-sound/pulseaudio system-wide" >> /etc/portage/package.use
# emerge --ask --oneshot pulseaudio
```
To allow multiple users to use PulseAudio concurrently, add following line to `/etc/pulse/system.pa` :
```
load-module module-native-protocol-unix auth-anonymous=1 socket=/tmp/pulse-socket
```
Configure pulseaudio backend in `/etc/shairport-sync.conf` :
```
pa =
{
        server = "unix:/tmp/pulse-socket";
};
```
Finally, start and enable services:
```
# systemctl start pulseaudio
# systemctl start shairport-sync
# systemctl enable pulseaudio
# systemctl enable shairport-sync
```
