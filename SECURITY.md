# Security Policy

## Supported Versions

Security fixes are provided for the latest release only.

## Reporting A Vulnerability

Please do not open a public issue for a suspected vulnerability.

Use GitHub's private vulnerability reporting for this repository when available, or email support@threethingsmedia.de with:

- the affected version or commit
- a concise summary of the issue
- reproduction steps or a proof of concept
- the practical impact

We will acknowledge credible reports as quickly as possible and coordinate disclosure once a fix is available.

## Scope

In scope:

- Radio Streamer source code and release packaging
- GitHub Actions workflows in this repository
- handling of stream credentials inside the plugin

Out of scope:

- vulnerabilities in OBS Studio, FFmpeg, Icecast, operating systems, or package managers
- denial of service from excessive local resource use
- social engineering or physical access attacks

## Release Integrity

Release assets are built locally by the maintainer from tagged commits, signed with Developer ID certificates, notarized by Apple, and then uploaded to GitHub Releases and the public R2 distribution bucket.
