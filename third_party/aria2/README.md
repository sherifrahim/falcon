# aria2

Falcon's download engine. Upstream: https://github.com/aria2/aria2

- `win/aria2c.exe` — official release build `aria2-1.37.0-win-64bit-build1`
  (https://github.com/aria2/aria2/releases/tag/release-1.37.0)
- `LICENSE` — GPL-2.0-or-later (aria2's `COPYING`)
- `LICENSE.OpenSSL` — bundled OpenSSL notice

aria2 is executed as a separate process (see `//brave/browser/falcon/download`)
and talked to over its JSON-RPC interface; Falcon does not link against it.

To upgrade: replace `win/aria2c.exe`, update the version above.
