// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/download/download_scheduler.h"

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/time/time.h"
#include "base/values.h"
#include "brave/browser/falcon/download/aria2_service.h"
#include "chrome/browser/browser_process.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace falcon {

namespace {

PrefService* LocalState() {
  return g_browser_process ? g_browser_process->local_state() : nullptr;
}

}  // namespace

namespace prefs {

void RegisterSchedulerLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kScheduleEnabled, false);
  registry->RegisterStringPref(kScheduleStart, "01:00");
  registry->RegisterStringPref(kScheduleStop, "07:00");
}

}  // namespace prefs

// static
std::optional<int> DownloadScheduler::ParseTime(const std::string& hhmm) {
  const std::vector<std::string> parts = base::SplitString(
      hhmm, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  int h = 0, m = 0;
  if (parts.size() != 2 || !base::StringToInt(parts[0], &h) ||
      !base::StringToInt(parts[1], &m) || h < 0 || h > 23 || m < 0 ||
      m > 59) {
    return std::nullopt;
  }
  return h * 60 + m;
}

DownloadScheduler::DownloadScheduler(Aria2Service* service)
    : service_(service) {
  if (PrefService* ls = LocalState()) {
    pref_change_registrar_.Init(ls);
    auto cb = base::BindRepeating(&DownloadScheduler::OnPrefsChanged,
                                  base::Unretained(this));
    for (const char* pref : {prefs::kScheduleEnabled, prefs::kScheduleStart,
                             prefs::kScheduleStop}) {
      pref_change_registrar_.Add(pref, cb);
    }
  }
  OnPrefsChanged();
}

DownloadScheduler::~DownloadScheduler() = default;

bool DownloadScheduler::enabled() const {
  PrefService* ls = LocalState();
  return ls && ls->GetBoolean(prefs::kScheduleEnabled);
}

bool DownloadScheduler::InWindow() const {
  PrefService* ls = LocalState();
  if (!ls || !ls->GetBoolean(prefs::kScheduleEnabled)) {
    return true;
  }
  const std::optional<int> start = ParseTime(ls->GetString(prefs::kScheduleStart));
  const std::optional<int> stop = ParseTime(ls->GetString(prefs::kScheduleStop));
  if (!start || !stop || *start == *stop) {
    return true;
  }
  base::Time::Exploded now;
  base::Time::Now().LocalExplode(&now);
  const int minute = now.hour * 60 + now.minute;
  return *start < *stop ? (minute >= *start && minute < *stop)
                        : (minute >= *start || minute < *stop);
}

void DownloadScheduler::OnPrefsChanged() {
  last_in_window_.reset();
  if (enabled()) {
    timer_.Start(FROM_HERE, base::Seconds(30), this,
                 &DownloadScheduler::Tick);
    Tick();
  } else {
    timer_.Stop();
    // Turning the scheduler off releases anything it paused.
    if (service_->IsReady()) {
      service_->Call("aria2.unpauseAll", base::ListValue(), base::DoNothing());
    }
  }
}

void DownloadScheduler::Tick() {
  const bool in_window = InWindow();
  if (last_in_window_.has_value() && *last_in_window_ == in_window) {
    return;
  }
  last_in_window_ = in_window;
  if (!service_->IsReady()) {
    last_in_window_.reset();  // try again next tick once aria2 is up
    return;
  }
  VLOG(1) << "Falcon: schedule window " << (in_window ? "opened" : "closed");
  service_->Call(in_window ? "aria2.unpauseAll" : "aria2.pauseAll",
                 base::ListValue(), base::DoNothing());
}

}  // namespace falcon
