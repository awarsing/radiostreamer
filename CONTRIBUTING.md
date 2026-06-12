# Contributing

Thanks for helping improve Radio Streamer.

## Ground Rules

- Keep changes focused and easy to review.
- Open an issue before large behavior changes.
- Do not include credentials, stream URLs with real passwords, certificates, or private keys in issues, commits, logs, or screenshots.
- Report security issues through the process in [SECURITY.md](SECURITY.md), not public issues.

## Development Setup

Radio Streamer targets OBS Studio 31.x and uses FFmpeg for Icecast output.

For an Apple Silicon macOS build:

```sh
cmake --preset macos-ci
cmake --build --preset macos-ci
ctest --test-dir build_macos --build-config RelWithDebInfo --output-on-failure
```

For a faster local Apple Silicon build, use the `macos` preset.

## Pull Requests

Before opening a pull request:

- run the relevant build and test commands for your platform
- keep formatting clean; CI runs clang-format and gersemi
- update README or release notes when behavior, installation paths, or packaging names change
- include enough context for maintainers to reproduce the issue or validate the change

Pull requests from first-time contributors require maintainer approval before GitHub Actions runs.
