// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_UX_TAB_ARCHIVER_H_
#define BRAVE_BROWSER_FALCON_UX_TAB_ARCHIVER_H_

#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "components/prefs/pref_change_registrar.h"

class PrefService;
class TabStripModel;

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace falcon {

namespace prefs {
// Hours a background tab may sit untouched before it is archived (closed;
// it stays in "recently closed" / Ctrl+Shift+T). 0 = off. Arc uses 12.
inline constexpr char kTabArchiveHours[] = "falcon.tabs.archive_hours";
void RegisterTabArchiverPrefs(user_prefs::PrefRegistrySyncable* registry);
}  // namespace prefs

// Arc-style tab auto-archive for one window: every few minutes, close
// unpinned, inactive, silent tabs whose last activation is older than the
// pref. Never closes the last tab.
class TabArchiver {
 public:
  TabArchiver(TabStripModel* model, PrefService* prefs);
  TabArchiver(const TabArchiver&) = delete;
  TabArchiver& operator=(const TabArchiver&) = delete;
  ~TabArchiver();

  // Runs one pass now (also used by the timer). Returns tabs closed.
  int Sweep();

 private:
  void OnPrefChanged();

  raw_ptr<TabStripModel> model_;
  raw_ptr<PrefService> prefs_;
  PrefChangeRegistrar registrar_;
  base::RepeatingTimer timer_;
};

}  // namespace falcon

#endif  // BRAVE_BROWSER_FALCON_UX_TAB_ARCHIVER_H_
