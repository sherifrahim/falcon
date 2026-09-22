// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/download/download_history.h"

#include <algorithm>
#include <utility>

#include "base/time/time.h"
#include "chrome/browser/browser_process.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"

namespace falcon {

namespace {

PrefService* LocalState() {
  return g_browser_process ? g_browser_process->local_state() : nullptr;
}

}  // namespace

namespace prefs {

void RegisterHistoryLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterListPref(kHistoryEntries);
  registry->RegisterIntegerPref(kHistoryKeepDays, 90);
  registry->RegisterDoublePref(kStatsTotalBytes, 0.0);
  registry->RegisterIntegerPref(kStatsTotalFiles, 0);
}

}  // namespace prefs

DownloadHistory::DownloadHistory(DownloadTracker* tracker) {
  observation_.Observe(tracker);
  Prune();
  prune_timer_.Start(FROM_HERE, base::Hours(6), this, &DownloadHistory::Prune);
}

DownloadHistory::~DownloadHistory() = default;

void DownloadHistory::OnDownloadFinished(
    const DownloadTracker::Finished& finished) {
  PrefService* ls = LocalState();
  if (!ls || finished.internal) {
    return;
  }
  base::DictValue entry;
  entry.Set("gid", finished.gid);
  entry.Set("name", finished.name);
  entry.Set("path", finished.path);
  entry.Set("url", finished.source_url);
  entry.Set("size", static_cast<double>(finished.total_bytes));
  entry.Set("time", base::Time::Now().InSecondsFSinceUnixEpoch());
  entry.Set("success", finished.success);
  entry.Set("torrent", finished.is_torrent);
  if (!finished.success) {
    entry.Set("error", finished.error_message);
  }
  {
    ScopedListPrefUpdate update(ls, prefs::kHistoryEntries);
    base::ListValue& list = update.Get();
    list.EraseIf([&](const base::Value& v) {
      const std::string* gid = v.is_dict() ? v.GetDict().FindString("gid") : nullptr;
      return gid && *gid == finished.gid;
    });
    list.Insert(list.begin(), base::Value(std::move(entry)));
    while (list.size() > kMaxEntries) {
      list.erase(list.end() - 1);
    }
  }
  if (finished.success) {
    ls->SetDouble(prefs::kStatsTotalBytes,
                  ls->GetDouble(prefs::kStatsTotalBytes) +
                      static_cast<double>(finished.total_bytes));
    ls->SetInteger(prefs::kStatsTotalFiles,
                   ls->GetInteger(prefs::kStatsTotalFiles) + 1);
  }
}

base::ListValue DownloadHistory::Entries() const {
  PrefService* ls = LocalState();
  return ls ? ls->GetList(prefs::kHistoryEntries).Clone() : base::ListValue();
}

base::DictValue DownloadHistory::Stats() const {
  base::DictValue d;
  if (PrefService* ls = LocalState()) {
    d.Set("totalBytes", ls->GetDouble(prefs::kStatsTotalBytes));
    d.Set("totalFiles", ls->GetInteger(prefs::kStatsTotalFiles));
    d.Set("keepDays", ls->GetInteger(prefs::kHistoryKeepDays));
  }
  return d;
}

void DownloadHistory::Remove(const std::string& gid) {
  PrefService* ls = LocalState();
  if (!ls) {
    return;
  }
  ScopedListPrefUpdate update(ls, prefs::kHistoryEntries);
  update.Get().EraseIf([&](const base::Value& v) {
    const std::string* g = v.is_dict() ? v.GetDict().FindString("gid") : nullptr;
    return g && *g == gid;
  });
}

void DownloadHistory::UpdatePath(const std::string& gid,
                                 const std::string& name,
                                 const std::string& path) {
  PrefService* ls = LocalState();
  if (!ls) {
    return;
  }
  ScopedListPrefUpdate update(ls, prefs::kHistoryEntries);
  for (base::Value& v : update.Get()) {
    base::DictValue* d = v.GetIfDict();
    const std::string* g = d ? d->FindString("gid") : nullptr;
    if (g && *g == gid) {
      d->Set("name", name);
      d->Set("path", path);
      return;
    }
  }
}

void DownloadHistory::Clear() {
  if (PrefService* ls = LocalState()) {
    ls->ClearPref(prefs::kHistoryEntries);
  }
}

void DownloadHistory::Prune() {
  PrefService* ls = LocalState();
  if (!ls) {
    return;
  }
  const int keep_days = ls->GetInteger(prefs::kHistoryKeepDays);
  if (keep_days <= 0) {
    return;
  }
  const double cutoff =
      (base::Time::Now() - base::Days(keep_days)).InSecondsFSinceUnixEpoch();
  ScopedListPrefUpdate update(ls, prefs::kHistoryEntries);
  update.Get().EraseIf([&](const base::Value& v) {
    if (!v.is_dict()) return true;
    return v.GetDict().FindDouble("time").value_or(0.0) < cutoff;
  });
}

}  // namespace falcon
