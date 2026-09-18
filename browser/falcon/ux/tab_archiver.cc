// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/ux/tab_archiver.h"

#include <vector>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"

namespace falcon {

namespace prefs {

void RegisterTabArchiverPrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterIntegerPref(kTabArchiveHours, 0);
}

}  // namespace prefs

namespace {
constexpr base::TimeDelta kSweepInterval = base::Minutes(5);
}  // namespace

TabArchiver::TabArchiver(TabStripModel* model, PrefService* prefs)
    : model_(model), prefs_(prefs) {
  registrar_.Init(prefs_);
  registrar_.Add(prefs::kTabArchiveHours,
                 base::BindRepeating(&TabArchiver::OnPrefChanged,
                                     base::Unretained(this)));
  OnPrefChanged();
}

TabArchiver::~TabArchiver() = default;

void TabArchiver::OnPrefChanged() {
  if (prefs_->GetInteger(prefs::kTabArchiveHours) > 0) {
    if (!timer_.IsRunning()) {
      timer_.Start(FROM_HERE, kSweepInterval,
                   base::BindRepeating(base::IgnoreResult(&TabArchiver::Sweep),
                                       base::Unretained(this)));
    }
  } else {
    timer_.Stop();
  }
}

int TabArchiver::Sweep() {
  const int hours = prefs_->GetInteger(prefs::kTabArchiveHours);
  if (hours <= 0 || !model_ || model_->empty()) {
    return 0;
  }
  const base::TimeTicks cutoff = base::TimeTicks::Now() - base::Hours(hours);

  // Collect first: closing shifts indices.
  std::vector<content::WebContents*> stale;
  for (int i = 0; i < model_->count(); ++i) {
    if (i == model_->active_index() || model_->IsTabPinned(i)) {
      continue;
    }
    content::WebContents* contents = model_->GetWebContentsAt(i);
    if (!contents || contents->IsCurrentlyAudible() ||
        contents->GetLastActiveTimeTicks() > cutoff) {
      continue;
    }
    stale.push_back(contents);
  }

  int closed = 0;
  for (content::WebContents* contents : stale) {
    if (model_->count() <= 1) {
      break;
    }
    const int index = model_->GetIndexOfWebContents(contents);
    if (index == TabStripModel::kNoTab) {
      continue;
    }
    model_->CloseWebContentsAt(index, CLOSE_CREATE_HISTORICAL_TAB);
    ++closed;
  }
  return closed;
}

}  // namespace falcon
