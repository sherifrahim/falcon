// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_DOWNLOAD_DOWNLOAD_SCHEDULER_H_
#define BRAVE_BROWSER_FALCON_DOWNLOAD_DOWNLOAD_SCHEDULER_H_

#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "components/prefs/pref_change_registrar.h"

class PrefRegistrySimple;

namespace falcon {

class Aria2Service;

namespace prefs {
// Local state.
inline constexpr char kScheduleEnabled[] = "falcon.schedule.enabled";
inline constexpr char kScheduleStart[] = "falcon.schedule.start";  // "HH:MM"
inline constexpr char kScheduleStop[] = "falcon.schedule.stop";    // "HH:MM"
void RegisterSchedulerLocalStatePrefs(PrefRegistrySimple* registry);
}  // namespace prefs

// IDM-style download window: when enabled, everything is paused outside
// [start, stop) (which may wrap past midnight) and resumed inside it.
// Downloads added outside the window are queued as paused.
class DownloadScheduler {
 public:
  explicit DownloadScheduler(Aria2Service* service);
  DownloadScheduler(const DownloadScheduler&) = delete;
  DownloadScheduler& operator=(const DownloadScheduler&) = delete;
  ~DownloadScheduler();

  bool enabled() const;
  // True when downloads should be running right now (always true when the
  // scheduler is off).
  bool InWindow() const;

  // "HH:MM" -> minutes since midnight.
  static std::optional<int> ParseTime(const std::string& hhmm);

 private:
  void OnPrefsChanged();
  void Tick();

  const raw_ptr<Aria2Service> service_;
  std::optional<bool> last_in_window_;
  base::RepeatingTimer timer_;
  PrefChangeRegistrar pref_change_registrar_;
};

}  // namespace falcon

#endif  // BRAVE_BROWSER_FALCON_DOWNLOAD_DOWNLOAD_SCHEDULER_H_
