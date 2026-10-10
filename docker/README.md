# Shairport Sync Docker Image

Available at: https://hub.docker.com/r/mikebrady/shairport-sync

Shairport Sync provides limited AirPlay 2 functionality. Versions with tags including `-classic` only provide classic AirPlay (aka "AirPlay 1"), the same as Shairport Sync versions up to 3.3.9.

### Tags
* `latest` and `latest-classic` are images of the most recent releases. These should be the most stable and updated only when a new release is made.
* `rolling` and `rolling-classic` are images of the latest updates in the `master` branch that have not yet been incorporated in a release. These may be updated frequently but should be stable.
* `development` and `development-classic` are images of the latest pushes to the `development` branch. These will be less stable and may be actually faulty. They will be updated frequently.
* Version Tags, e.g. `4.1` are images of specific releases.


## Example Docker Compose File
See the `docker-compose.yaml` file in this folder for an example.

## Docker Run

To run the latest release of Shairport Sync, which provides AirPlay 2 service:

```
$ docker run -d --cap-add=SYS_NICE --restart unless-stopped --net host --device /dev/snd \
    mikebrady/shairport-sync:latest
```
To run the classic version:

```
$ docker run -d --cap-add=SYS_NICE --restart unless-stopped --net host --device /dev/snd \
    mikebrady/shairport-sync:latest-classic
```

### Options

Command line options will be passed to Shairport Sync. Here is an example:

```
$ docker run -d --cap-add=SYS_NICE --restart unless-stopped --net host --device /dev/snd \
    mikebrady/shairport-sync:latest \
    -v --statistics -a DenSystem -- -d hw:0 -c PCM
```
This will send audio to alsa hardware device `hw:0` and make use of the that device's mixer control called `PCM`. The service will be visible as `DenSystem` on the network.

The image is built with PipeWire and PulseAudio backend support. To use it, refer to [`docker-compose.yaml`](docker-compose.yaml) for required environment variables and mounts.

To use the PipeWire backend, set the backend to `pipewire` via either command line option `-o pipewire ` or the `output_backend` field in the `general` section of the configuration file.

Similarly, to use the PulseAudio backend, set the backend to `pulseaudio` via either command line option `-o pulseaudio ` or the `output_backend` field in the `general` section of the configuration file.
For use with PulseAudio, you might need to adjust authentication on your PulseAudio server ([PA documentation](https://www.freedesktop.org/wiki/Software/PulseAudio/Documentation/User/Modules/#module-native-protocol-unixtcp)).

The container defaults `PULSE_SERVER` to `unix:/tmp/pulseaudio.socket` and `PULSE_COOKIE` to `/tmp/pulseaudio.cookie`. You can override either value without rebuilding the image by passing them at runtime:

```
# docker run
$ docker run ... -e PULSE_SERVER=unix:/run/user/1000/pulse/native -e PULSE_COOKIE=/run/user/1000/pulse/cookie ...
```

```yaml
# docker compose
environment:
  PULSE_SERVER: unix:/run/user/1000/pulse/native
  PULSE_COOKIE: /run/user/1000/pulse/cookie
```

The `${VAR:-default}` fallbacks in `run.sh` apply only when the variable is unset or empty, so any value you pass via `-e` or `environment:` takes precedence automatically. The image does not use s6-overlay (the entrypoint is a plain shell script invoked directly by Docker), so environment variables reach `run.sh` through the normal Docker mechanism without any additional configuration. `S6_KEEP_ENV: 1` is included in the example `docker-compose.yaml` as a precaution should an s6-based init ever be introduced; it has no effect in the current image.

## Configuration File

To get access to the full range of configuration options, pass the configuration file to `/etc/shairport-sync.conf` in the container using the `-v` option or docker compose.

## Building
### Build Example (for arm7 devices)

```
docker buildx build --platform linux/arm/v7 -f ./docker/Dockerfile --build-arg SHAIRPORT_SYNC_BRANCH=development --build-arg NQPTP_BRANCH=development --no-cache -t shairport-sync:development .
```

`SHAIRPORT_SYNC_BRANCH` and `NQPTP_BRANCH` are required to ensure the image is built using the expected branch.
`--no-cache` needs to be used to force buildx to pull the NQPTP branch for new updates. This slows down the build time though so can be removed when it is not beneficial during testing.

### "Classic" AirPlay

The "Classic" AirPlay only dockerfile is in the `classic` folder. This also includes the `start.sh` script used by the container.

### GitHub Action Builds

Requires the following secrets to be set in the repo:
- `DOCKER_REGISTRY` - docker.io if using Docker Hub, else set to your registry URL.
- `DOCKER_REGISTRY_TOKEN` - Access token for your registry.
- `DOCKER_REGISTRY_USER` - Login user for your registry.
- `DOCKER_IMAGE_NAME` - The name of the image, for example `your-registry.com/shairport-sync` or just `your-username/shairport-sync` if using Docker Hub.
