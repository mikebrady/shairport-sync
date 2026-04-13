# Patches for companion software

## `nqptp-darwin.patch`

Adds macOS (Darwin) support to [NQPTP](https://github.com/mikebrady/nqptp) so Shairport Sync can use full AirPlay 2 timing on a Mac **after** you disable **AirPlay Receiver** in System Settings (otherwise the OS keeps UDP 319/320).

Apply from an NQPTP source tree:

```sh
patch -p1 < /path/to/shairport-sync/patches/nqptp-darwin.patch
```

Then build and run NQPTP per [BUILD.md](../BUILD.md) (*AirPlay 2 on macOS*).
