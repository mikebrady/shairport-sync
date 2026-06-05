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

## Discovery

The built-in mDNS backend publishes `_raop._tcp.local.` and
`_airplay._tcp.local.` and uses automatic LAN address selection. On hosts with
multiple active adapters, set `mdns.interface` in `shairport-rs.toml` to the
interface that is on the same network as the Apple sender. On Windows, ensure
the firewall allows inbound UDP 5353 and TCP 7000 for the daemon.

Examples:

```toml
[mdns]
backend = "builtin"
interface = "Ethernet"
```

If the built-in backend is blocked by the OS or another mDNS responder, use the
native Bonjour publisher:

```toml
[mdns]
backend = "dns-sd"
```

## Windows ASIO

ASIO support is feature-gated through CPAL:

```powershell
cargo build --manifest-path shairport-rs/Cargo.toml --features asio
```

ASIO requires an ASIO driver for the audio device and the CPAL build-time ASIO
requirements, including LLVM/Clang for generated bindings.
