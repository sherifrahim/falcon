# yt-dlp

Falcon's media extractor (HLS/DASH playlists, YouTube-class sites).
Upstream: https://github.com/yt-dlp/yt-dlp (Unlicense).

- `win/yt-dlp.exe` — official release binary; **not committed** (fetched by
  `tools/fetch-sidecars.ps1`, see `win/VERSION`).
- Run as a sidecar by `//brave/browser/falcon/media` (`-J` to list formats,
  `--progress-template` for progress); needs ffmpeg next to it for merging.

To upgrade: re-run the fetch script (it takes the latest release).
