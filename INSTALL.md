# Installing Radio Streamer

Radio Streamer is an OBS Studio plugin for macOS on Apple Silicon. It streams an
audio-only feed to an Icecast server through FFmpeg.

## Requirements

- macOS 12 or later on Apple Silicon (arm64)
- OBS Studio 31.x
- FFmpeg with the `icecast` protocol and the `libmp3lame`, `aac`, and `libopus` encoders

The easiest way to get a suitable FFmpeg is Homebrew:

```sh
brew install ffmpeg
```

Verify the install before using the plugin:

```sh
ffmpeg -version
ffmpeg -protocols 2>/dev/null | grep icecast
```

Both commands must succeed. The plugin looks for `ffmpeg` on `PATH`, then at
`/opt/homebrew/bin/ffmpeg`, `/usr/local/bin/ffmpeg`, and `/usr/bin/ffmpeg`.

## Install the plugin

Download the latest release: <https://radiostreamer.app/download/macos.zip>

The bundle is signed and notarized, so macOS will not block it.

1. Quit OBS Studio.
2. Unzip the download. You get a `radiostreamer.plugin` bundle.
3. Move it into your OBS plugin folder:

   ```sh
   mkdir -p ~/Library/Application\ Support/obs-studio/plugins
   mv radiostreamer.plugin ~/Library/Application\ Support/obs-studio/plugins/
   ```

4. Start OBS Studio. A **Radio Streamer** dock appears; if it is hidden, enable it
   under **Docks → Radio Streamer**.

To install for all users instead, place the bundle in
`/Library/Application Support/obs-studio/plugins/` (requires admin rights).

## First stream

1. Enter your Icecast URL in the form
   `icecast://source:password@server:8000/mountpoint`.
2. Pick a codec (MP3, AAC, or Opus), bitrate, and the OBS audio track to stream.
3. Press **Start**. The status line shows the connection state, listener count from
   the Icecast server, and bytes sent.

Leave **Reconnect** checked to retry automatically with backoff if the connection
drops.

## Troubleshooting

**"Failed to start FFmpeg"** — FFmpeg is not installed or not found in any of the
paths listed above. Install it with `brew install ffmpeg`.

**"FFmpeg exited" or "Timed out trying to write audio to FFmpeg"** — FFmpeg is
present but broken, which can happen when a Homebrew library upgrade leaves
`ffmpeg` linked against a removed dylib. Run `ffmpeg -version` in Terminal: if it
prints a `dyld: Library not loaded` error, reinstall with `brew reinstall ffmpeg`
(or `brew install ffmpeg` from homebrew-core if yours came from a third-party tap).

**"Invalid Icecast mount or credentials"** — check the username, password, and
mount path in the URL. The source password is the one from your Icecast server's
`<source-password>` setting.

**No listeners / "server unavailable" in the status line** — the plugin polls
`http://your-server:port/status-json.xsl` for health. If that endpoint is blocked
or disabled, streaming still works but listener counts stay empty.

## Uninstall

Quit OBS and delete the bundle:

```sh
rm -rf ~/Library/Application\ Support/obs-studio/plugins/radiostreamer.plugin
```
