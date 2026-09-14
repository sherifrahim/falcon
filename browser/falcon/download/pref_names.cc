// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/download/pref_names.h"

#include "brave/browser/falcon/download/download_history.h"
#include "brave/browser/falcon/download/download_scheduler.h"
#include "brave/browser/falcon/download/download_security.h"

#include <string>

#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_registry_simple.h"

namespace falcon::prefs {

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(kDownloadEngineEnabled, true);
  registry->RegisterInt64Pref(kDownloadMinInterceptBytes, 1024 * 1024);
  registry->RegisterBooleanPref(kDownloadCategoriesEnabled, true);
  registry->RegisterBooleanPref(kDownloadMagnetEnabled, true);
  registry->RegisterBooleanPref(kDownloadNotificationsEnabled, true);
  registry->RegisterBooleanPref(kShowDownloadsToolbarButton, true);
  registry->RegisterBooleanPref(kDownloadClipboardMonitor, true);
  registry->RegisterBooleanPref(kDownloadVideoPill, true);
  registry->RegisterStringPref(kDownloadCategoryRules, std::string());
  registry->RegisterStringPref(kDownloadSkipHosts, std::string());
  registry->RegisterStringPref(kDownloadSkipExtensions, std::string());
}

void RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterIntegerPref(kEngineMaxConnections, 16);
  registry->RegisterIntegerPref(kEngineMaxConcurrent, 5);
  registry->RegisterIntegerPref(kEngineSpeedLimitKbps, 0);
  registry->RegisterDoublePref(kEngineSeedRatio, 1.0);
  registry->RegisterIntegerPref(kEngineSeedTimeMinutes, 0);
  registry->RegisterStringPref(kEngineProxy, std::string());
  registry->RegisterStringPref(kEngineBtTrackers, std::string());
  registry->RegisterStringPref(kEngineDuplicateAction, "rename");
  RegisterSecurityLocalStatePrefs(registry);
  RegisterHistoryLocalStatePrefs(registry);
  RegisterSchedulerLocalStatePrefs(registry);
}

}  // namespace falcon::prefs
