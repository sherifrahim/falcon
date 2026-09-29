/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "chrome/browser/download/bubble/download_bubble_prefs.h"

namespace download {
bool ShouldShowDownloadBubble_ChromiumImpl(Profile* profile);
}  // namespace download

#define ShouldShowDownloadBubble ShouldShowDownloadBubble_ChromiumImpl
#include <chrome/browser/download/bubble/download_bubble_prefs.cc>
#undef ShouldShowDownloadBubble

#include "brave/browser/falcon/download/pref_names.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"

namespace download {

// Falcon: while Falcon's download engine is on, its toolbar button and
// falcon://downloader are the one download UI - including the downloads
// Chromium still handles itself (small files, private windows, blob: links),
// which the downloader page lists. Chromium's toolbar button and bubble stay
// hidden, the same way a download-manager extension hides them.
bool ShouldShowDownloadBubble(Profile* profile) {
  if (profile && profile->GetOriginalProfile()->GetPrefs()->GetBoolean(
                     falcon::prefs::kDownloadEngineEnabled)) {
    return false;
  }
  return ShouldShowDownloadBubble_ChromiumImpl(profile);
}

}  // namespace download
