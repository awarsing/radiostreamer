# Radio Streamer

Radio Streamer is an OBS plugin for sending an audio-only Icecast stream without using OBS's recording output. It adds a `Radio Streamer` dock with dedicated start/stop controls, codec selection, bitrate, audio track selection, reconnects, and lightweight Icecast health polling.

## Features

- Dedicated OBS audio output for radio streaming
- MP3, AAC, and Opus via FFmpeg
- Icecast URL input in the form `icecast://source:password@server:8000/mountpoint`
- OBS audio track selection from track 1 through 6
- Automatic reconnect with backoff after unexpected disconnects
- Listener/server status polling through Icecast `status-json.xsl`
- Apple Silicon macOS release build

## Requirements

- OBS Studio 31.x
- Apple Silicon Mac
- FFmpeg on `PATH`, `/opt/homebrew/bin/ffmpeg`, `/usr/local/bin/ffmpeg`, or `/usr/bin/ffmpeg`
- For local builds: CMake 3.28+ and Xcode on macOS

FFmpeg must include the `icecast` protocol plus the `libmp3lame`, `aac`, and `libopus` encoders.

## Build On Apple Silicon macOS

For the supported Apple Silicon macOS build:

```sh
cmake --preset macos-ci
cmake --build --preset macos-ci
ctest --test-dir build_macos --build-config RelWithDebInfo --output-on-failure
```

The built plugin bundle is:

```text
build_macos/RelWithDebInfo/radiostreamer.plugin
```

For local development without CI warning settings, use the `macos` preset instead.

## Install Locally On macOS

Release downloads are attached to GitHub Releases. Use the `.pkg` installer for the standard install path, or the `.zip` if you want to copy `radiostreamer.plugin` manually.

For a local build or zip install, quit OBS, then copy the plugin bundle into OBS's user plugin folder:

```sh
mkdir -p "$HOME/Library/Application Support/obs-studio/plugins"
ditto build_macos/RelWithDebInfo/radiostreamer.plugin \
  "$HOME/Library/Application Support/obs-studio/plugins/radiostreamer.plugin"
```

Restart OBS and enable the dock from `Docks > Radio Streamer` if it is not already visible.

## Icecast URL

Most Icecast servers accept:

```text
icecast://source:password@server:port/mountpoint
```

Use URL encoding for special characters in the username, password, or mount. For example, `@` in a password should be written as `%40`.

## Notes

For MP3, Radio Streamer runs FFmpeg with `libmp3lame`, `-f mp3`, and `audio/mpeg`.

For AAC, it runs FFmpeg with native `aac`, `-f adts`, and `audio/aac`.

For Opus, it runs FFmpeg with `libopus`, `-f ogg`, `audio/ogg`, and a 48 kHz Opus output rate.

The output uses OBS's active audio sample rate, converts the selected OBS audio track to stereo `s16le`, and streams it through FFmpeg stdin.

If reconnect is enabled, an unexpected disconnect retries after 2, 5, 10, 20, and then 30 seconds. Pressing Stop cancels reconnect.

Use a dedicated Icecast source password for the mount you stream to.

## License

Radio Streamer is licensed under the GNU General Public License v2.0 or later. See [LICENSE](LICENSE).
