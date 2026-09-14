// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/download/download_tracker.h"

#include <algorithm>
#include <iterator>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "brave/browser/falcon/download/aria2_service.h"

namespace falcon {

namespace {

constexpr int kActivePollMs = 1000;
constexpr int kIdlePollMs = 10000;

// aria2 error codes (see `aria2c --help` "EXIT STATUS").
constexpr int kAria2ErrorInvalidRange = 8;

base::ListValue StatusKeys() {
  base::ListValue keys;
  for (const char* k :
       {"gid", "status", "totalLength", "completedLength", "downloadSpeed",
        "files", "bittorrent", "errorCode", "errorMessage", "dir"}) {
    keys.Append(k);
  }
  return keys;
}

int64_t ToInt64(const base::DictValue& d, std::string_view key) {
  int64_t v = 0;
  if (const std::string* s = d.FindString(key)) {
    base::StringToInt64(*s, &v);
  }
  return v;
}

std::string Basename(const std::string& path) {
  const size_t pos = path.find_last_of("/\\");
  return pos == std::string::npos ? path : path.substr(pos + 1);
}

}  // namespace

// static
DownloadTracker::Status DownloadTracker::ParseStatus(
    const base::DictValue& d) {
  Status s;
  if (const std::string* v = d.FindString("gid")) s.gid = *v;
  if (const std::string* v = d.FindString("status")) s.status = *v;
  if (const std::string* v = d.FindString("dir")) s.dir = *v;
  s.total = ToInt64(d, "totalLength");
  s.completed = ToInt64(d, "completedLength");
  s.speed = ToInt64(d, "downloadSpeed");
  if (const std::string* v = d.FindString("errorCode")) {
    base::StringToInt(*v, &s.error_code);
  }
  if (const std::string* v = d.FindString("errorMessage")) {
    s.error_message = *v;
  }
  if (const base::DictValue* bt = d.FindDict("bittorrent")) {
    s.is_torrent = true;
    if (const base::DictValue* info = bt->FindDict("info")) {
      if (const std::string* n = info->FindString("name")) s.name = *n;
    }
  }
  if (const base::ListValue* files = d.FindList("files")) {
    for (const base::Value& fv : *files) {
      if (!fv.is_dict()) continue;
      const base::DictValue& fd = fv.GetDict();
      const std::string* sel = fd.FindString("selected");
      const std::string* p = fd.FindString("path");
      if (p && !p->empty() && (!sel || *sel == "true") &&
          ToInt64(fd, "completedLength") == ToInt64(fd, "length")) {
        s.paths.push_back(*p);
      }
    }
  }
  if (const base::ListValue* files = d.FindList("files");
      files && !files->empty() && (*files)[0].is_dict()) {
    const base::DictValue& f = (*files)[0].GetDict();
    if (const std::string* p = f.FindString("path")) {
      s.path = *p;
      if (s.name.empty()) s.name = Basename(*p);
    }
    if (const base::ListValue* uris = f.FindList("uris");
        uris && !uris->empty() && (*uris)[0].is_dict()) {
      if (const std::string* u = (*uris)[0].GetDict().FindString("uri")) {
        s.first_uri = *u;
        if (s.name.empty()) s.name = Basename(*u);
      }
    }
  }
  if (s.name.empty()) s.name = s.gid;
  return s;
}

DownloadTracker::DownloadTracker(Aria2Service* service) : service_(service) {
  net::NetworkChangeNotifier::AddNetworkChangeObserver(this);
}

DownloadTracker::~DownloadTracker() {
  net::NetworkChangeNotifier::RemoveNetworkChangeObserver(this);
}

void DownloadTracker::OnNetworkChanged(
    net::NetworkChangeNotifier::ConnectionType type) {
  const bool offline = type == net::NetworkChangeNotifier::CONNECTION_NONE;
  if (offline) {
    was_offline_ = true;
    return;
  }
  if (was_offline_ && service_->IsLaunched()) {
    was_offline_ = false;
    RetryNetworkFailures();
  }
}

// After the network comes back, re-add downloads that died with network-ish
// aria2 errors (1 unknown, 2 timeout, 5 too slow, 6 network, 19 dns).
void DownloadTracker::RetryNetworkFailures() {
  base::ListValue params;
  params.Append(0);
  params.Append(200);
  params.Append(StatusKeys());
  service_->Call("aria2.tellStopped", std::move(params),
                 base::BindOnce(&DownloadTracker::OnStoppedForRetry,
                                weak_factory_.GetWeakPtr()));
}

void DownloadTracker::OnStoppedForRetry(std::optional<base::Value> result) {
  if (!result || !result->is_list()) {
    return;
  }
  static constexpr int kNetworkErrors[] = {1, 2, 5, 6, 19};
  for (const base::Value& item : result->GetList()) {
    if (!item.is_dict()) continue;
    Status s = ParseStatus(item.GetDict());
    if (s.status != "error" || s.first_uri.empty() || s.is_torrent) continue;
    if (std::find(std::begin(kNetworkErrors), std::end(kNetworkErrors),
                  s.error_code) == std::end(kNetworkErrors)) {
      continue;
    }
    VLOG(1) << "Falcon: network back, retrying " << s.name;
    base::DictValue options;
    if (!s.dir.empty()) options.Set("dir", s.dir);
    if (!s.path.empty()) options.Set("out", Basename(s.path));
    base::ListValue rm;
    rm.Append(s.gid);
    service_->Call("aria2.removeDownloadResult", std::move(rm),
                   base::DoNothing());
    service_->AddUri(GURL(s.first_uri), std::move(options));
  }
}

void DownloadTracker::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void DownloadTracker::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void DownloadTracker::Poke() {
  ScheduleNext(200);
}

void DownloadTracker::NotifyExternalFinished(const Finished& finished) {
  for (Observer& o : observers_) {
    o.OnDownloadFinished(finished);
  }
}

void DownloadTracker::Stop() {
  timer_.Stop();
  polling_ = false;
}

void DownloadTracker::ScheduleNext(int delay_ms) {
  polling_ = true;
  timer_.Start(FROM_HERE, base::Milliseconds(delay_ms),
               base::BindOnce(&DownloadTracker::Poll,
                              weak_factory_.GetWeakPtr()));
}

void DownloadTracker::Poll() {
  if (!service_->IsLaunched()) {
    polling_ = false;
    return;
  }
  base::ListValue params;
  params.Append(StatusKeys());
  service_->Call("aria2.tellActive", std::move(params),
                 base::BindOnce(&DownloadTracker::OnActive,
                                weak_factory_.GetWeakPtr()));
}

void DownloadTracker::OnActive(std::optional<base::Value> result) {
  std::set<std::string> now_active;
  Snapshot snap;
  if (result && result->is_list()) {
    for (const base::Value& item : result->GetList()) {
      if (!item.is_dict()) continue;
      Status s = ParseStatus(item.GetDict());
      now_active.insert(s.gid);
      last_status_[s.gid] = s;
      snap.active++;
      if (s.total > 0) {
        snap.total_bytes += s.total;
        snap.completed_bytes += s.completed;
      }
      snap.download_speed += s.speed;
    }
  }

  // Anything that was active and is not any more finished (or was removed).
  for (const std::string& gid : known_active_) {
    if (!now_active.contains(gid)) {
      base::ListValue params;
      params.Append(gid);
      params.Append(StatusKeys());
      service_->Call("aria2.tellStatus", std::move(params),
                     base::BindOnce(&DownloadTracker::OnFinishedStatus,
                                    weak_factory_.GetWeakPtr(), gid));
    }
  }
  known_active_ = std::move(now_active);
  snapshot_ = snap;

  service_->Call("aria2.getGlobalStat", base::ListValue(),
                 base::BindOnce(&DownloadTracker::OnGlobalStat,
                                weak_factory_.GetWeakPtr()));
}

void DownloadTracker::OnGlobalStat(std::optional<base::Value> result) {
  if (result && result->is_dict()) {
    snapshot_.waiting =
        static_cast<int>(ToInt64(result->GetDict(), "numWaiting"));
    // Prefer aria2's own aggregate speed.
    snapshot_.download_speed = ToInt64(result->GetDict(), "downloadSpeed");
  }
  for (Observer& o : observers_) {
    o.OnDownloadsChanged(snapshot_);
  }
  const bool busy = snapshot_.active > 0 || snapshot_.waiting > 0;
  ScheduleNext(busy ? kActivePollMs : kIdlePollMs);
}

void DownloadTracker::OnFinishedStatus(const std::string& gid,
                                       std::optional<base::Value> result) {
  last_status_.erase(gid);
  if (!result || !result->is_dict()) {
    return;  // removed via UI; nothing to report
  }
  Status s = ParseStatus(result->GetDict());
  if (s.status == "paused" || s.status == "waiting" ||
      s.status == "active") {
    return;  // paused/queued, not finished
  }
  if (s.status == "error" && s.error_code == kAria2ErrorInvalidRange &&
      !retried_.contains(gid)) {
    MaybeRetrySingleConnection(s);
    return;
  }
  Finished f;
  f.gid = gid;
  f.name = s.name;
  f.path = s.path;
  f.paths = s.paths;
  f.source_url = s.first_uri;
  f.is_torrent = s.is_torrent;
  f.success = s.status == "complete";
  f.error_code = s.error_code;
  f.error_message = s.error_message;
  f.total_bytes = s.total > 0 ? s.total : s.completed;
  if (s.status == "removed") {
    return;
  }
  for (Observer& o : observers_) {
    o.OnDownloadFinished(f);
  }
}

void DownloadTracker::MaybeRetrySingleConnection(const Status& s) {
  if (s.first_uri.empty()) {
    return;
  }
  VLOG(1) << "Falcon: server ignores ranges, retrying " << s.name
          << " with a single connection";
  retried_.insert(s.gid);
  base::DictValue options;
  options.Set("split", "1");
  options.Set("max-connection-per-server", "1");
  options.Set("continue", "false");
  options.Set("allow-overwrite", "true");
  if (!s.dir.empty()) options.Set("dir", s.dir);
  if (!s.path.empty()) options.Set("out", Basename(s.path));
  // Drop the failed entry so the list shows one row for this download.
  base::ListValue rm;
  rm.Append(s.gid);
  service_->Call("aria2.removeDownloadResult", std::move(rm),
                 base::DoNothing());
  service_->AddUri(GURL(s.first_uri), std::move(options));
}

}  // namespace falcon
