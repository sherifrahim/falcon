// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_DOWNLOAD_PREF_NAMES_H_
#define BRAVE_BROWSER_FALCON_DOWNLOAD_PREF_NAMES_H_

class PrefRegistrySimple;

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace falcon::prefs {

// ---- Profile prefs (behaviour) ----
inline constexpr char kDownloadEngineEnabled[] =
    "falcon.download.engine_enabled";
inline constexpr char kDownloadMinInterceptBytes[] =
    "falcon.download.min_intercept_bytes";
inline constexpr char kDownloadCategoriesEnabled[] =
    "falcon.download.categories_enabled";
inline constexpr char kDownloadMagnetEnabled[] =
    "falcon.download.magnet_enabled";
inline constexpr char kDownloadNotificationsEnabled[] =
    "falcon.download.notifications_enabled";
inline constexpr char kShowDownloadsToolbarButton[] =
    "falcon.download.show_toolbar_button";
inline constexpr char kDownloadClipboardMonitor[] =
    "falcon.download.clipboard_monitor";
// "ext = Folder" lines; Folder is relative to the download dir or absolute.
inline constexpr char kDownloadCategoryRules[] =
    "falcon.download.category_rules";

// ---- Local-state prefs (engine, browser-wide) ----
inline constexpr char kEngineMaxConnections[] = "falcon.engine.max_connections";
inline constexpr char kEngineMaxConcurrent[] = "falcon.engine.max_concurrent";
inline constexpr char kEngineSpeedLimitKbps[] = "falcon.engine.speed_limit_kbps";
inline constexpr char kEngineSeedRatio[] = "falcon.engine.seed_ratio";
inline constexpr char kEngineSeedTimeMinutes[] =
    "falcon.engine.seed_time_minutes";
inline constexpr char kEngineProxy[] = "falcon.engine.proxy";
// Extra BitTorrent trackers, one per line; added to every torrent/magnet.
inline constexpr char kEngineBtTrackers[] = "falcon.engine.bt_trackers";
// "rename" (file.1.ext) or "overwrite".
inline constexpr char kEngineDuplicateAction[] =
    "falcon.engine.duplicate_action";

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);
void RegisterLocalStatePrefs(PrefRegistrySimple* registry);

}  // namespace falcon::prefs

#endif  // BRAVE_BROWSER_FALCON_DOWNLOAD_PREF_NAMES_H_
