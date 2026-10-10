# Classic (aka AirPlay 1 Only) Docker Image

See the [Shairport Sync Docker Hub Repo](https://hub.docker.com/r/mikebrady/shairport-sync) for available tags.

The container defaults `PULSE_SERVER` to `unix:/tmp/pulseaudio.socket` and `PULSE_COOKIE` to `/tmp/pulseaudio.cookie`. You can override either value without rebuilding the image by passing them at runtime via `docker run -e` or the `environment:` key in a Compose file.

The `${VAR:-default}` fallbacks in `run.sh` apply only when the variable is unset or empty, so any value you supply takes precedence automatically. The image does not use s6-overlay (the entrypoint is a plain shell script invoked directly by Docker), so the variables reach `run.sh` through the normal Docker mechanism. `S6_KEEP_ENV: 1` is not needed for the current image but may be added to a Compose file as a precaution should an s6-based init be introduced in future.