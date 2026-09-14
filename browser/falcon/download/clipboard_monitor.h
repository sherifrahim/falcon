// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_DOWNLOAD_CLIPBOARD_MONITOR_H_
#define BRAVE_BROWSER_FALCON_DOWNLOAD_CLIPBOARD_MONITOR_H_

#include <optional>
#include <string>

#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "ui/base/clipboard/clipboard_monitor.h"
#include "ui/base/clipboard/clipboard_observer.h"
#include "ui/base/data_transfer_policy/data_transfer_endpoint.h"

namespace falcon {

// IDM-style clipboard watcher: when a magnet link or a URL that looks like a
// file (known category extension) is copied anywhere on the system, offers to
// download it via a notification. Off-switch: falcon.download.clipboard_monitor.
// Rides on ui::ClipboardMonitor, which listens for system-wide changes
// (WM_CLIPBOARDUPDATE on Windows) while it has observers.
class ClipboardMonitor : public ui::ClipboardObserver {
 public:
  ClipboardMonitor();
  ClipboardMonitor(const ClipboardMonitor&) = delete;
  ClipboardMonitor& operator=(const ClipboardMonitor&) = delete;
  ~ClipboardMonitor() override;

  // ui::ClipboardObserver:
  void OnClipboardDataChanged() override;

  // Exposed for the "paste" path of the downloader page and tests.
  static bool LooksLikeDownload(const std::string& text);

 private:
  void ReadClipboard();
  void OnSource(std::optional<ui::DataTransferEndpoint> source);
  void OnText(std::u16string text);
  void Offer(const std::string& url);

  std::string last_text_;
  base::OneShotTimer debounce_;
  base::ScopedObservation<ui::ClipboardMonitor, ui::ClipboardObserver>
      observation_{this};
  base::WeakPtrFactory<ClipboardMonitor> weak_factory_{this};
};

}  // namespace falcon

#endif  // BRAVE_BROWSER_FALCON_DOWNLOAD_CLIPBOARD_MONITOR_H_
