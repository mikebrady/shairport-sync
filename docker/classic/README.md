# Classic (aka AirPlay 1 Only) Docker Image

See the [Shairport Sync Docker Hub Repo](https://hub.docker.com/r/mikebrady/shairport-sync) for available tags.

The container defaults `PULSE_SERVER` to `unix:/tmp/pulseaudio.socket` and `PULSE_COOKIE` to `/tmp/pulseaudio.cookie`. You can override either value without rebuilding the image by passing them at runtime via `docker run -e` or the `environment:` key in a Compose file.