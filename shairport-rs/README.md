# shairport-rs

This crate is the Rust-native rewrite scaffold for Shairport Sync. It is not yet
a production AirPlay 2 receiver. The first implementation milestone provides:

- a daemon process and local control API,
- CPAL-backed audio device enumeration,
- a built-in PTP service skeleton for UDP ports 319 and 320,
- selectable mDNS advertisement backends,
- AirPlay TXT record generation shared by all mDNS backends,
- a minimal web UI shell for track, volume, audio, mDNS, and PTP state.

## Run

```powershell
cargo run --manifest-path shairport-rs/Cargo.toml -- --config shairport-rs/shairport-rs.toml
```

The API listens on `127.0.0.1:3689` by default.

## Windows ASIO

ASIO support is feature-gated through CPAL:

```powershell
cargo build --manifest-path shairport-rs/Cargo.toml --features asio
```

ASIO requires an ASIO driver for the audio device and the CPAL build-time ASIO
requirements, including LLVM/Clang for generated bindings.
