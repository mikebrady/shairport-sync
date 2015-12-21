The Raspbian image at the time of writing is `2015-01-31-raspbian`. It does not include `libsoxr`, nor is `libsoxr` available as a package via `apt-get`.
Libsoxr is, however, easy to compile, and works well with Shairport Sync on the Raspberry Pi. Here are very brief instructions to download, compile and install it on the Raspberry Pi, so that it can be used with Shairport Sync.

* Install `cmake`. This is used in the building of libsoxr:
```
sudo apt-get install cmake
```
* Download the `libsoxr source`:
```
git clone git://git.code.sf.net/p/soxr/code libsoxr
```
* `cd` into the `libsoxr` directory and start the build process:
```
cd libsoxr
./go
```
Be patient! This takes a long time on a Raspberry Pi -- it looks like it gets stuck around 40% or 50%, but it will finish if you let it.

Having compiled `libsoxr`, it must now must be installed:
```
cd Release
sudo make install
```
Finally, for Shairport Sync to be able to locate `libsoxr-dev` during compilation, you need to tell `ld` to catalogue it:
```
sudo ldconfig -v
```
That's it. Now you can select the `--with-soxr` option when you're building Shairport Sync.
