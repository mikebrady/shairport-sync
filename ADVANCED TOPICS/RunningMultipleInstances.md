# Running Multiple AirPlay 2 Instances on One Host

Each Shairport Sync process appears on the network as a single AirPlay device. To
offer several independent zones — one per room — from a single machine (bare
metal, a VM, or a set of containers), run **one instance per room**. Each instance
needs a distinct AirPlay identity; Shairport Sync derives that identity from the
instance's **name**, so the setup is minimal.

If you want several rooms driven from a single multichannel sound card, also see
the companion guide [Splitting a Surround Card into Per-Room Outputs](SplittingASurroundCard.md).

## The short version

Give each instance its own **name** (`-a` / `general.name`) and its own network
endpoint — a distinct `--port`, a distinct `--address`, or both — and point it at
an output device. Shairport Sync generates a distinct AirPlay 2 identity from the
name, so the instances share one configuration file and differ only on the command
line:

```bash
# one shared configuration file; each room differs only by name, port, device
shairport-sync -c /etc/shairport-sync.conf -a "Kitchen"     --port=7000 -- -d room_kitchen
shairport-sync -c /etc/shairport-sync.conf -a "Living Room" --port=7001 -- -d room_livingroom
```

These examples share the host's IP and differ by port; giving each instance its own
`--address` instead (or as well) works the same way. Because the identity comes from
the name, it stays put if a room later moves to a different port or address.
(Everything after `--` is passed to the output backend; `-d` selects its output
device — run `shairport-sync -h` for the options.)

## What makes an instance distinct

1. **A name** — `general.name`, or `-a`. The AirPlay menu label, and the source of
   the derived identity. Each instance must have a distinct name.
2. **A socket** — a distinct `--port`, a distinct `--address`, or both. Two
   instances can't share one address and port.
3. **A timing clock** — each AirPlay 2 instance needs its own clock from the
   companion [NQPTP](https://github.com/mikebrady/nqptp) daemon, addressed by a
   shared-memory interface name derived from the instance's name (`/nqptp-<name>`).
4. **An output device** — `alsa.output_device`, or `-- -d <device>`. Usually its
   own, though instances can share a device that permits concurrent playback (their
   audio is then mixed). If several rooms come off one multichannel card, see
   [Splitting a Surround Card into Per-Room Outputs](SplittingASurroundCard.md).

## One NQPTP for the whole host

NQPTP is multi-client. Run **one** NQPTP for the machine — it needs exclusive use
of UDP ports 319 and 320 — and it keeps a separate clock per shared-memory
interface name, demultiplexing its clients by the name each one uses. **Do not run
one NQPTP per instance**; they would contend for ports 319/320. The
automatically-derived names give each instance its own clock with no configuration.

## Running in containers

The container case is the same model — a name and a port (or address) per room —
but the shared daemons (NQPTP and the mDNS responder) run **once**, and each room
is told not to start its own:

* **Networking.** The instances and the shared daemons must reach each other, and
  NQPTP needs ports 319/320, so run everything with host networking (or place it in
  one shared network namespace).
* **Shared memory / IPC (`ipc: host`).** NQPTP hands timing to Shairport Sync
  through a POSIX shared-memory object (an entry under `/dev/shm`). By default each
  Docker container gets its own private `/dev/shm`, so an instance can't see the
  object NQPTP created in another. `ipc: host` puts the containers on the same
  `/dev/shm`. (Splitting a single sound card across containers also needs a shared
  IPC namespace, for the separate reason that ALSA's `dmix` coordinates through
  System V IPC — see the surround-card guide.)
* **Only one NQPTP.** The image's launcher starts NQPTP *inside every container*.
  With several rooms they would all fight over ports 319/320, so run **one**
  dedicated NQPTP and set **`ENABLE_NQPTP=0`** on each room. (This needs a
  multi-client NQPTP — one that keeps a separate clock per shared-memory name.)
* **Only one mDNS responder (Avahi).** The launcher likewise starts Avahi in every
  container — and **multiple Avahi daemons in one network namespace collide**: mDNS
  allows only one responder per host, so with host networking only one room ends up
  discoverable and the rest silently do not. Run **one** Avahi (with its D-Bus) as a
  shared responder and set **`ENABLE_AVAHI=0`** on each room; the rooms register
  their AirPlay services through it over a shared D-Bus socket. One responder then
  advertises every room under one hostname — the normal multi-service-host pattern.

Compose sketch — shared NQPTP and Avahi sidecars plus two rooms:

```yaml
volumes:
  dbus:     # shared D-Bus system socket: mdns sidecar <-> rooms
  avahi:    # shared Avahi socket + pid

services:
  nqptp:
    image: mikebrady/shairport-sync:latest   # the image ships nqptp
    entrypoint: ["/usr/local/bin/nqptp"]
    network_mode: host                        # NQPTP owns UDP 319/320 for the host

  mdns:
    image: mikebrady/shairport-sync:latest    # the image ships avahi + dbus
    network_mode: host
    entrypoint:
      - /bin/sh
      - -c
      - |
        dbus-uuidgen --ensure
        dbus-daemon --system --fork
        sleep 1
        exec avahi-daemon --no-chroot
    volumes:
      - dbus:/run/dbus
      - avahi:/run/avahi-daemon

  kitchen:
    image: mikebrady/shairport-sync:latest
    network_mode: host
    ipc: host                                 # share /dev/shm with nqptp
    environment: ["ENABLE_NQPTP=0", "ENABLE_AVAHI=0"]
    volumes:                                  # reach the shared avahi + dbus
      - dbus:/run/dbus
      - avahi:/run/avahi-daemon
    command: ["-a", "Kitchen", "--port=7000", "--", "-d", "room_kitchen"]
    depends_on: [nqptp, mdns]

  livingroom:
    image: mikebrady/shairport-sync:latest
    network_mode: host
    ipc: host
    environment: ["ENABLE_NQPTP=0", "ENABLE_AVAHI=0"]
    volumes:
      - dbus:/run/dbus
      - avahi:/run/avahi-daemon
    command: ["-a", "Living Room", "--port=7001", "--", "-d", "room_livingroom"]
    depends_on: [nqptp, mdns]
```

(Add the audio-device access your output backend needs — for ALSA, `/dev/snd` and
the `audio` group. If the rooms share one card, the container-side ALSA setup,
including the shared-IPC requirement for `dmix`, is covered in
[Splitting a Surround Card into Per-Room Outputs](SplittingASurroundCard.md).)

## A note on mDNS advertisement

On a single host there is **one** mDNS responder, shared by every instance (the
Avahi sidecar above). Running one Avahi per instance in a shared network namespace
does **not** work — mDNS allows only one responder per host and they collide, so
only one instance stays discoverable. The shared responder advertises every
instance's service under one hostname; instances differ by their service name. If it
would otherwise advertise on interfaces you don't want (internal/container bridges,
for example), scope it to your LAN interface (`allow-interfaces=` in
`avahi-daemon.conf`) to keep discovery clean.
