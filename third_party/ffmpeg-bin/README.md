# ffmpeg (binaries)

Used by yt-dlp to merge DASH audio+video and remux HLS. Build:
https://github.com/yt-dlp/FFmpeg-Builds (`ffmpeg-master-latest-win64-gpl-shared`).

- `win/` — `ffmpeg.exe`, `ffprobe.exe` and the av*/sw* DLLs; **not committed**
  (avcodec alone is >100 MB). Fetched by `tools/fetch-sidecars.ps1`.
- Installed to `<out>/ffmpeg/` and passed to yt-dlp via `--ffmpeg-location`.
- `LICENSE` — GPL (the build's LICENSE.txt).
