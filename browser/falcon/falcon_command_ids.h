// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_FALCON_COMMAND_IDS_H_
#define BRAVE_BROWSER_FALCON_FALCON_COMMAND_IDS_H_

// Falcon command ids live here, not in brave_command_ids.h: that header is
// included by most of chrome/ and every edit there recompiles ~30k objects.
// Range 56480-56599 is inside Brave's block (IDC_BRAVE_COMMANDS_START..LAST)
// and unused by brave_command_ids.h.
#define IDC_FALCON_DOWNLOAD_LINK 56480
#define IDC_FALCON_DOWNLOAD_ALL_LINKS 56481
#define IDC_FALCON_DOWNLOAD_ALL_IMAGES 56482
#define IDC_FALCON_DOWNLOAD_MEDIA 56483
#define IDC_FALCON_SHOW_DOWNLOADS 56484
#define IDC_FALCON_SHOW_CONTROL 56485
#define IDC_FALCON_BOOST_SITE 56486
// Auto-reload submenu (radio group). Keep contiguous.
#define IDC_FALCON_RELOAD_OFF 56487
#define IDC_FALCON_RELOAD_30S 56488
#define IDC_FALCON_RELOAD_1M 56489
#define IDC_FALCON_RELOAD_5M 56490
#define IDC_FALCON_RELOAD_15M 56491
#define IDC_FALCON_RELOAD_30M 56492
#define IDC_FALCON_RELOAD_MENU 56493
// Collections (Edge-style boards of pages/links/images/text).
#define IDC_FALCON_SHOW_COLLECTIONS 56500
#define IDC_FALCON_COLLECT 56501
// Command chains: the first ten chains get a menu entry each. Keep contiguous.
#define IDC_FALCON_CHAINS_MENU 56510
#define IDC_FALCON_CHAIN_FIRST 56511
#define IDC_FALCON_CHAIN_LAST 56520

#endif  // BRAVE_BROWSER_FALCON_FALCON_COMMAND_IDS_H_
