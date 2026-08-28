# Security Policy

## Reporting a Vulnerability

Please report security vulnerabilities **privately**, so a fix can be prepared
before the issue is public.

The preferred channel is GitHub's private vulnerability reporting: go to the
[**Security** tab](https://github.com/mikebrady/shairport-sync/security) of this
repository and click **Report a vulnerability**. This opens a private advisory
visible only to the maintainer and you.

<!-- Optional: if you'd like an email fallback, add it here, e.g.
Alternatively, email <address>. -->

Please include as much as you can:

- the affected version or commit — the output of `shairport-sync --displayConfig` or, failing that, `shairport-sync -V` is ideal;
- how the instance is built and configured (AirPlay 1 vs 2, relevant build
  options, mDNS backend, etc.) and how the issue is reachable;
- the impact (crash, memory disclosure, code execution, denial of service, …);
- steps to reproduce, ideally a minimal proof of concept;
- any suggested fix.

Please allow a reasonable opportunity to release a fix
before disclosing the issue publicly.

## Supported Versions

Security fixes are made on the `development` branch and included in the next
release. Please reproduce against the latest release or `development` before
reporting; older releases are not maintained.
<!-- Maintainer: adjust if you support specific release lines. -->

## Scope

In scope: the shairport-sync daemon's handling of untrusted network input —
RTSP/RTP, the AirPlay/HomeKit pairing exchange, mDNS/DNS-SD, and the
metadata/remote-control interfaces.

Out of scope: vulnerabilities in third-party dependencies (please report those to
their own projects), and issues that require privileges the operator already has.

## Credit

Reporters are credited in the release notes and any published advisory, unless
they ask to remain anonymous.
