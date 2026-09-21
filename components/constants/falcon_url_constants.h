// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_COMPONENTS_CONSTANTS_FALCON_URL_CONSTANTS_H_
#define BRAVE_COMPONENTS_CONSTANTS_FALCON_URL_CONSTANTS_H_

// falcon:// is an alias for chrome:// (exactly like brave://). Navigation
// accepts brave://, falcon:// and chrome://; the UI displays falcon://.
// Kept out of url_constants.h on purpose: that header fans out to the whole
// tree and would turn a 10-file change into a full rebuild.
namespace falcon {
inline constexpr char kFalconUIScheme[] = "falcon";
inline constexpr char16_t kFalconUIScheme16[] = u"falcon";
// The downloader page in side-panel layout (top-chrome WebUI hosts must end
// with .top-chrome).
inline constexpr char kFalconDownloaderPanelHost[] = "downloader.top-chrome";
// falcon://falcon — the control panel.
inline constexpr char kFalconControlHost[] = "falcon";
inline constexpr char kFalconCommandDeckHost[] = "command-deck.top-chrome";
// falcon://collections (tab page) and the same UI in the side panel.
inline constexpr char kFalconCollectionsHost[] = "collections";
inline constexpr char kFalconCollectionsPanelHost[] = "collections.top-chrome";
}  // namespace falcon

#endif  // BRAVE_COMPONENTS_CONSTANTS_FALCON_URL_CONSTANTS_H_
