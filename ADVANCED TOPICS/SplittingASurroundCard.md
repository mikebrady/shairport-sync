# Splitting a Surround Card into Per-Room Stereo Outputs

A single surround or multichannel sound card -- a 5.1/7.1 HDMI or analog card, or an 8-channel USB interface -- exposes many output channels on one physical device. Often you want to treat those channels as several independent stereo "rooms": each stereo pair of the card's outputs feeds one room's speakers, with a *different* program driving each pair at the same time.

This is the natural companion to running several Shairport Sync instances on one host (see [`RunningMultipleInstances.md`](RunningMultipleInstances.md)). That guide explains that each instance needs its own output device; this guide shows how to carve one multichannel card into several stereo ALSA PCMs so each instance can own one.

All of the work here is ALSA configuration. Shairport Sync itself needs no special build -- it just points at the named PCM you create.

## The problem

You cannot simply open the raw hardware device (`hw:0`) from several programs at once. A raw `hw` device is *exclusive*: the first program to open it locks it, and every other program gets a "device busy" error. To let several programs share one card you need a software layer that mixes or shares the device between clients, and some way to steer each client's two channels onto a specific pair of the card's output channels.

ALSA provides exactly these pieces:

- **`dshare`** -- lets several clients share one card, each writing to a *different* set of the card's channels. This is what gives each room its own pair.
- **`bindings`** -- inside a `dshare`, map this client's two channels onto chosen channels of the card.
- **`plug`** -- transparently converts rate, format, and channel count so that ordinary stereo sources work.
- **`dmix`** -- the sibling of `dshare` that *mixes* several clients onto the *same* channels. You don't need it to split a card into rooms, but it's the piece to reach for if you want two sources to share one output -- for example a chime or announcement played over whatever is already in a room. Point both sources at a `dmix` on those channels instead of a `dshare`.

You combine these into one named PCM per room.

## General ALSA configuration

Put the following in `/etc/asound.conf` (system-wide) or `~/.asoundrc` (per-user). The two files use identical syntax.

### Define the card once

Define the hardware as a named slave, so every room can refer to it without repeating the device, channel count and rate:

```
pcm_slave.card {
    pcm "hw:0,0"     # the real multichannel card
    channels 6       # the card's output channels (6 for 5.1, 8 for 7.1)
    rate 44100
}
```

### One stereo PCM per room

Now define a PCM per room. Each is a `dshare` bound to two of the card's channels -- `dshare` gives each room its own channels and, unlike `dmix`, does not mix them -- wrapped in `plug` so any stereo source is converted automatically. A 5.1 card is three stereo output jacks, so it gives three rooms; name each after the jack it drives:

```
pcm.room_front {
    type plug
    slave.pcm {
        type dshare
        ipc_key 4242          # same key for every room on this card
        ipc_key_add_uid false
        slave card
        bindings.0 0          # room left  -> channel 0 (front left)
        bindings.1 1          # room right -> channel 1 (front right)
    }
}

pcm.room_center {
    type plug
    slave.pcm {
        type dshare
        ipc_key 4242
        ipc_key_add_uid false
        slave card
        bindings.0 2          # -> channel 2 (centre)
        bindings.1 3          # -> channel 3 (LFE / subwoofer)
    }
}

pcm.room_rear {
    type plug
    slave.pcm {
        type dshare
        ipc_key 4242
        ipc_key_add_uid false
        slave card
        bindings.0 4          # -> channel 4 (rear left)
        bindings.1 5          # -> channel 5 (rear right)
    }
}
```

`bindings.0 4 / bindings.1 5` put the room's left and right onto the card's channels 4 and 5. Every room on the one card uses the **same `ipc_key`**, so they attach to the same shared device and play at once, each on its own channels -- the rooms do not need a key each. `ipc_key_add_uid false` keeps that key the same for every user, which matters when the rooms run as separate containers (see below). `room_center` just uses the centre/subwoofer jack (channels 2 and 3) as a plain stereo output.

> **Check your channel order.** The mapping above assumes the common layout `FL FR FC LFE RL RR (SL SR)`. Cards can differ, so confirm which physical output each channel drives -- e.g. `speaker-test -D hw:0,0 -c 6 -t wav`, which plays each channel in turn and names its position -- and adjust the `bindings` to match.

### Adding rooms on a 7.1 / 8-channel card

An 8-channel card has a fourth stereo pair -- the side channels. Set `channels 8` in `pcm_slave.card` (above), then add a fourth room alongside `room_front`, `room_center` and `room_rear`:

```
pcm.room_side {
    type plug
    slave.pcm {
        type dshare
        ipc_key 4242
        ipc_key_add_uid false
        slave card
        bindings.0 6          # -> channel 6 (side left)
        bindings.1 7          # -> channel 7 (side right)
    }
}
```

### Pointing Shairport Sync at a room

In the configuration file, name the PCM with the `output_device` setting in the `alsa` section:

```
alsa = {
    output_device = "room_front";
};
```

Or on the command line:

```
shairport-sync -o alsa -- -d room_front
```

Give each instance its own room PCM and they will play independently through the one card.

## Doing the split across Docker containers

The scheme above works unchanged on a bare host. In containers -- for example one Shairport Sync instance per container -- three things have to be true inside *each* container:

1. **The ALSA configuration must be visible.** Bind-mount your config read-only, either `/etc/asound.conf` or `~/.asoundrc`.
2. **The device nodes must be reachable.** Pass `--device /dev/snd` (or a device-cgroup rule), and make sure the container's user is in the `audio` group.
3. **A matching `libasound` must be present** in the image so the plugins above can be parsed and loaded.

There is one more, less obvious requirement. A `dshare` device coordinates its clients through **System V IPC** -- the shared-memory segment and semaphore named by `ipc_key`. System V IPC lives in an **IPC namespace**, and by default every container gets its own. So even with the same `ipc_key`, a client in container A and a client in container B land in *different* IPC namespaces, each creates its own private state, and they end up fighting over the card: expect xruns, "device busy", or one container silently winning the device.

The fix is to put the containers in a **shared IPC namespace** so that one `ipc_key` resolves to one IPC object for all of them:

- With `docker run`, give every container `--ipc host`, or start one "owner" and join the rest with `--ipc container:<name>`.
- In Compose, set `ipc: host` (or `ipc: "service:<name>"`) on each service.

Also keep `ipc_key_add_uid false` in each `dshare` definition (as above) and use an explicit `ipc_key`, so every container's client deterministically resolves to the same object regardless of UID.

Finally, `/dev/snd` must refer to the *same* physical card in each container. With host device access (`--device /dev/snd`) it does.

A runnable example combines the ALSA bits above (`/dev/snd`, the `audio` group, the shared `asound.conf`) with the AirPlay 2 essentials from the [companion guide](RunningMultipleInstances.md) -- host networking, a distinct name and port per room, and one shared NQPTP (`ENABLE_NQPTP=0` on the rooms):

```yaml
services:
  nqptp:                               # one shared nqptp for the whole host
    image: mikebrady/shairport-sync:latest
    entrypoint: ["/usr/local/bin/nqptp"]
    network_mode: host

  room_front:
    image: mikebrady/shairport-sync:latest
    network_mode: host                 # AirPlay discovery, and reaching nqptp
    ipc: host                          # shared /dev/shm for nqptp AND shared IPC for dshare
    environment: [ENABLE_NQPTP=0]      # use the shared nqptp above
    devices:
      - "/dev/snd:/dev/snd"
    group_add:
      - audio
    volumes:
      - "./asound.conf:/etc/asound.conf:ro"
    command: ["-a", "Front Room", "--port=7000", "--", "-d", "room_front"]
    depends_on: [nqptp]

  room_rear:
    image: mikebrady/shairport-sync:latest
    network_mode: host
    ipc: host
    environment: [ENABLE_NQPTP=0]
    devices:
      - "/dev/snd:/dev/snd"
    group_add:
      - audio
    volumes:
      - "./asound.conf:/etc/asound.conf:ro"
    command: ["-a", "Rear Room", "--port=7001", "--", "-d", "room_rear"]
    depends_on: [nqptp]
```

The equivalent for one room with plain `docker run` (with the shared `nqptp` already running):

```
docker run --network host --ipc host -e ENABLE_NQPTP=0 \
    --device /dev/snd --group-add audio \
    -v "$PWD/asound.conf:/etc/asound.conf:ro" \
    mikebrady/shairport-sync:latest -a "Front Room" --port=7000 -- -d room_front
```

`ipc: host` does double duty here: it shares the host's `/dev/shm` so the rooms read the one nqptp's clock, and it shares the System V IPC namespace so the per-room `dshare` devices resolve to one shared card. The same `asound.conf` (its shared card slave and per-room `dshare` PCMs) goes into every container.

## A note on multiple USB sound cards

If your rooms span several USB cards rather than one multichannel card, do not rely on `hw:0` / `hw:1`: USB devices enumerate in whatever order the kernel probes them, so the indices can change between boots. Pin each card by its *name* and reference it as `hw:CARD=<name>` in its slave -- the name is the one shown by `cat /proc/asound/cards` or `aplay -l`:

```
pcm_slave.card {
    pcm "hw:CARD=Device_1,0"
    channels 6
    rate 44100
}
```

A second card is just a second `pcm_slave` with its own `hw:CARD=...` and its own `ipc_key`; its rooms bind to it exactly the same way. Alternatively, fix each card's index with a `modprobe`/`modules-load.d` option for `snd-usb-audio`. See the [ALSA wiki](https://www.alsa-project.org/wiki/Asoundrc) for the full details.

---

See also [`RunningMultipleInstances.md`](RunningMultipleInstances.md), the companion guide on running several Shairport Sync instances -- one per room -- on a single host.
