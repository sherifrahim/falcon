// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_FALCON_COMMAND_IDS_H_
#define BRAVE_BROWSER_FALCON_FALCON_COMMAND_IDS_H_

// Falcon command ids live here, not in brave_command_ids.h: that header is
// included by most of chrome/ and every edit there recompiles ~30k objects.
// Range 56480-56499 is inside Brave's block (IDC_BRAVE_COMMANDS_START..LAST)
// and unused by brave_command_ids.h.
#define IDC_FALCON_DOWNLOAD_LINK 56480
#define IDC_FALCON_DOWNLOAD_ALL_LINKS 56481
#define IDC_FALCON_DOWNLOAD_ALL_IMAGES 56482
#define IDC_FALCON_DOWNLOAD_MEDIA 56483

#endif  // BRAVE_BROWSER_FALCON_FALCON_COMMAND_IDS_H_
