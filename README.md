# Falcon

Falcon is a personal browser for **Windows and Android**, built on
[Brave](https://github.com/brave/brave-core) and Chromium. It keeps Brave's
Shields and privacy work, drops Brave's services, and adds a real download
manager, a media grabber and a quieter, Arc-inspired interface.

It is an independent hobby fork. It is not affiliated with or endorsed by Brave
Software.

**[Download the latest release](https://github.com/sherifrahim/falcon/releases/latest)**
— Windows installer, Windows portable zip and Android APK, with SHA-256 sums.

## What's different from Brave

### Everywhere

- **No Brave services.** Rewards, Wallet, VPN, Leo, News, Talk and ads are
  compiled out or switched off by policy. Shields (ad and tracker blocking)
  stays, with its filter lists fetched from their public sources.
- **Nothing reported home.** Usage and crash reporting are off, and the crash
  upload address is empty in the binary, so there is no "send a crash report"
  prompt.
- **Encrypted DNS by default.** Names resolve through Cloudflare's
  DNS-over-HTTPS; on networks where that's unreachable Falcon falls back to the
  system resolver.
- **Self-hosted sync.** Falcon syncs through a
  [go-sync](https://github.com/brave/go-sync) server you run yourself. No sync
  address is compiled in; set yours in the settings and restart.

### Windows

- **Download manager** (`falcon://downloader`) on an aria2 engine: parallel
  connections, pause and resume, a queue and scheduler, torrents, automatic
  sorting into folders by type, a history, and a scan of finished files with
  optional quarantine or opening in Windows Sandbox. It is the only download
  UI: Chromium's own download button stays hidden, and anything Chromium still
  downloads itself is listed there too.
- **Media grabber** for the video and audio a page plays, with quality choice,
  backed by yt-dlp and ffmpeg.
- **New tab page** with a clock, greeting, search, quick links, weather, a
  focus timer and wallpapers.
- **Shell:** vertical tabs and an Arc-style dock, a floating toolbar, a command
  deck on Ctrl+Space, link previews (Peek), mouse gestures, per-site style and
  script boosts, saved sessions and automatic archiving of idle tabs.
- **Passwords:** import from Brave or Google, or use Bitwarden as the store.

### Android

- **Arc-style bottom bar** with the address bar at your thumb, and a pitch-black
  option for OLED screens.
- **Download manager** with multi-connection downloads, pause and resume, a
  Wi-Fi-only option, SHA-256 checks and live speed.
- **Media grabber** (like 1DM+): ⋮ › *Media on this page* lists the files, HLS
  and DASH streams a page played, with their qualities. Streams download in
  parallel, AES-128 segments are decrypted, and the pieces are joined into one
  MP4 without re-encoding. It works in private tabs, where nothing about the
  download is written to disk.
- **Passkeys** through Android's Credential Manager, so Bitwarden or Microsoft
  Authenticator can hold them. (Passkeys stored in Google Password Manager only
  work in browsers Google allowlists.)

## Installing

- **Windows:** run `FalconSetup-<version>-x64.exe`. It installs per user to
  `%LOCALAPPDATA%\Falcon\Falcon`, next to any Brave install. The installer isn't
  code-signed, so SmartScreen asks once. The portable zip runs from
  `Chrome-bin\brave.exe`.
- **Android (arm64):** install `Falcon-<version>-android-arm64.apk`. It's signed
  with Falcon's own key and installs as `io.github.sherifrahim.falcon`, so it
  sits alongside Brave and updates in place from one release to the next.

## Building

Falcon builds the way brave-core does: follow Brave's
[build documentation](https://github.com/brave/brave-browser/wiki) to set up
the toolchain and a Chromium checkout, with this repository's `falcon` branch
mounted at `src/brave`. Falcon's own build arguments live in
`build/args/falcon.gni` (desktop) and `build/args/falcon_android.gni`.

## License

Mozilla Public License 2.0, like brave-core; see [LICENSE](LICENSE). Chromium
and the other bundled projects keep their own licenses.
