/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/falcon/sidecars/sidecar_installer.h"

#include <optional>
#include <utility>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/strings/pattern.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/thread_pool.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "brave/browser/falcon/download/aria2_service.h"
#include "components/grit/brave_components_resources.h"
#include "chrome/common/chrome_paths.h"
#include "crypto/secure_hash.h"
#include "crypto/sha2.h"
#include "third_party/zlib/google/zip.h"
#include "ui/base/resource/resource_bundle.h"
#include "url/gurl.h"

namespace falcon {

namespace {

constexpr char kRootDirName[] = "Falcon Sidecars";
constexpr char kStagingDirName[] = "staging";

// The asset file's SHA-256, lowercase hex.
std::string HashFile(const base::FilePath& path) {
  base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!file.IsValid()) {
    return std::string();
  }
  auto hash = crypto::SecureHash::Create(crypto::SecureHash::SHA256);
  std::vector<uint8_t> buffer(1 << 20);
  for (;;) {
    std::optional<size_t> read = file.ReadAtCurrentPos(buffer);
    if (!read.has_value()) {
      return std::string();
    }
    if (*read == 0) {
      break;
    }
    hash->Update(base::span(buffer).first(*read));
  }
  std::array<uint8_t, crypto::kSHA256Length> digest;
  hash->Finish(digest);
  return base::ToLowerASCII(base::HexEncode(digest));
}

bool WantsEntry(const SidecarInfo& info, const base::FilePath& entry) {
  std::string rel = entry.AsUTF8Unsafe();
  base::ReplaceChars(rel, "\\", "/", &rel);
  std::string base = entry.BaseName().AsUTF8Unsafe();
  for (const std::string& pattern : info.files) {
    if (base::MatchPattern(rel, pattern) ||
        base::MatchPattern(base, base::FilePath::FromUTF8Unsafe(pattern)
                                     .BaseName()
                                     .AsUTF8Unsafe())) {
      return true;
    }
  }
  return false;
}

// Blocking: verifies |asset|, extracts what the manifest wants into
// |install_dir| (flattened), and removes the staging files. Returns an error
// message or an empty string.
std::string Stage(SidecarInfo info,
                  base::FilePath asset,
                  base::FilePath install_dir) {
  if (!base::PathExists(asset)) {
    return "download missing";
  }
  if (HashFile(asset) != info.sha256) {
    base::DeleteFile(asset);
    return "checksum mismatch";
  }
  base::FilePath fresh = install_dir.AddExtensionASCII("new");
  base::DeletePathRecursively(fresh);
  if (!base::CreateDirectory(fresh)) {
    return "cannot create install dir";
  }
  if (!info.zip) {
    base::FilePath target = fresh.AppendASCII(info.files.empty()
                                                  ? asset.BaseName().AsUTF8Unsafe()
                                                  : info.files[0]);
    if (!base::Move(asset, target)) {
      return "cannot move download";
    }
  } else {
    base::FilePath unzipped = fresh.AddExtensionASCII("unzip");
    base::DeletePathRecursively(unzipped);
    zip::UnzipOptions options;
    options.filter = base::BindRepeating(
        [](const SidecarInfo& info, const base::FilePath& entry) {
          return WantsEntry(info, entry);
        },
        info);
    if (!zip::Unzip(asset, unzipped, std::move(options))) {
      base::DeletePathRecursively(unzipped);
      return "cannot extract";
    }
    // Flatten: every wanted file lands directly in the install dir.
    base::FileEnumerator files(unzipped, /*recursive=*/true,
                               base::FileEnumerator::FILES);
    int moved = 0;
    for (base::FilePath f = files.Next(); !f.empty(); f = files.Next()) {
      if (base::Move(f, fresh.Append(f.BaseName()))) {
        moved++;
      }
    }
    base::DeletePathRecursively(unzipped);
    base::DeleteFile(asset);
    if (moved == 0) {
      base::DeletePathRecursively(fresh);
      return "archive had none of the expected files";
    }
  }
  base::DeletePathRecursively(install_dir);
  if (!base::Move(fresh, install_dir)) {
    return "cannot finalise install";
  }
  // Older versions of this sidecar are dead weight now.
  base::FileEnumerator siblings(install_dir.DirName(), /*recursive=*/false,
                                base::FileEnumerator::DIRECTORIES);
  for (base::FilePath d = siblings.Next(); !d.empty(); d = siblings.Next()) {
    if (d != install_dir) {
      base::DeletePathRecursively(d);
    }
  }
  return std::string();
}

bool HasAllFiles(const SidecarInfo& info, const base::FilePath& dir) {
  if (dir.empty() || !base::DirectoryExists(dir)) {
    return false;
  }
  for (const std::string& pattern : info.files) {
    if (pattern.find('*') != std::string::npos) {
      continue;  // globs (the ffmpeg DLL set) are not individually required
    }
    if (!base::PathExists(dir.Append(
            base::FilePath::FromUTF8Unsafe(pattern).BaseName()))) {
      return false;
    }
  }
  return true;
}

}  // namespace

// static
SidecarInstaller* SidecarInstaller::Get() {
  static base::NoDestructor<SidecarInstaller> instance;
  return instance.get();
}

SidecarInstaller::SidecarInstaller() {
  LoadManifest();
  RefreshPresence();
}

SidecarInstaller::~SidecarInstaller() = default;

void SidecarInstaller::LoadManifest() {
  std::string json =
      ui::ResourceBundle::GetSharedInstance().LoadDataResourceString(
          IDR_FALCON_SIDECARS_JSON);
  std::optional<base::Value> root = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!root || !root->is_dict()) {
    LOG(ERROR) << "falcon: sidecars.json is not a JSON object";
    return;
  }
  const base::ListValue* list = root->GetDict().FindList("sidecars");
  if (!list) {
    return;
  }
  for (const base::Value& v : *list) {
    const base::DictValue* d = v.GetIfDict();
    if (!d) {
      continue;
    }
    SidecarInfo info;
    info.name = d->FindString("name") ? *d->FindString("name") : "";
    info.version = d->FindString("version") ? *d->FindString("version") : "";
    info.url = d->FindString("url") ? *d->FindString("url") : "";
    info.sha256 = d->FindString("sha256") ? *d->FindString("sha256") : "";
    info.size = static_cast<int64_t>(d->FindDouble("size").value_or(0));
    const std::string* kind = d->FindString("kind");
    info.zip = kind && *kind == "zip";
    if (const base::ListValue* files = d->FindList("files")) {
      for (const base::Value& f : *files) {
        if (f.is_string()) {
          info.files.push_back(f.GetString());
        }
      }
    }
    if (!info.name.empty() && !info.url.empty() && !info.sha256.empty()) {
      manifest_.push_back(std::move(info));
    }
  }
}

const SidecarInfo* SidecarInstaller::Find(const std::string& name) const {
  for (const SidecarInfo& info : manifest_) {
    if (info.name == name) {
      return &info;
    }
  }
  return nullptr;
}

base::FilePath SidecarInstaller::RootDir() const {
  base::FilePath dir;
  base::PathService::Get(chrome::DIR_USER_DATA, &dir);
  return dir.AppendASCII(kRootDirName);
}

base::FilePath SidecarInstaller::InstallDir(const SidecarInfo& info) const {
  return RootDir().AppendASCII(info.name).AppendASCII(info.version);
}

base::FilePath SidecarInstaller::BundledDir() const {
  // Sidecars ship in the version dir (base::DIR_MODULE) when bundled; ffmpeg
  // in its own subdirectory (see //brave/third_party/ffmpeg-bin).
  base::FilePath dir;
  base::PathService::Get(base::DIR_MODULE, &dir);
  return dir;
}

base::FilePath SidecarInstaller::DirFor(const std::string& name) const {
  const SidecarInfo* info = Find(name);
  base::FilePath bundled = BundledDir();
  if (name == "ffmpeg") {
    bundled = bundled.AppendASCII("ffmpeg");
  }
  if (info && HasAllFiles(*info, bundled)) {
    return bundled;
  }
  if (!info) {
    return base::FilePath();
  }
  base::FilePath installed = InstallDir(*info);
  return HasAllFiles(*info, installed) ? installed : base::FilePath();
}

base::FilePath SidecarInstaller::ExePath(const std::string& name,
                                         const std::string& exe) const {
  base::FilePath dir = DirFor(name);
  return dir.empty() ? dir : dir.AppendASCII(exe);
}

bool SidecarInstaller::IsAvailable(const std::string& name) const {
  return !DirFor(name).empty();
}

// Blocking: runs on a thread pool sequence, never on the UI thread.
std::map<std::string, SidecarInstaller::Presence>
SidecarInstaller::ComputePresence(
    std::vector<SidecarInfo> manifest,
    base::FilePath bundled_root,
    std::map<std::string, base::FilePath> install_dirs) {
  std::map<std::string, SidecarInstaller::Presence> out;
  for (const SidecarInfo& info : manifest) {
    base::FilePath bundled = bundled_root;
    if (info.name == "ffmpeg") {
      bundled = bundled.AppendASCII("ffmpeg");
    }
    SidecarInstaller::Presence p;
    p.bundled = HasAllFiles(info, bundled);
    p.installed = HasAllFiles(info, install_dirs[info.name]);
    out[info.name] = p;
  }
  return out;
}

void SidecarInstaller::RefreshPresence() {
  std::map<std::string, base::FilePath> install_dirs;
  for (const SidecarInfo& info : manifest_) {
    install_dirs[info.name] = InstallDir(info);
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&SidecarInstaller::ComputePresence, manifest_,
                     BundledDir(),
                     std::move(install_dirs)),
      base::BindOnce(&SidecarInstaller::OnPresenceRefreshed,
                     weak_factory_.GetWeakPtr()));
}

void SidecarInstaller::OnPresenceRefreshed(
    std::map<std::string, Presence> presence) {
  if (presence == presence_) {
    return;
  }
  presence_ = std::move(presence);
  NotifyChanged();
}

std::vector<SidecarInstaller::Status> SidecarInstaller::GetStatuses() const {
  std::vector<Status> out;
  for (const SidecarInfo& info : manifest_) {
    Status s;
    s.name = info.name;
    s.version = info.version;
    s.size = info.size;
    auto p = presence_.find(info.name);
    s.bundled = p != presence_.end() && p->second.bundled;
    s.installed = p != presence_.end() && p->second.installed;
    auto it = pending_.find(info.name);
    if (it != pending_.end()) {
      s.installing = true;
      s.progress = it->second.progress;
    }
    auto err = errors_.find(info.name);
    if (err != errors_.end()) {
      s.error = err->second;
    }
    out.push_back(std::move(s));
  }
  return out;
}

void SidecarInstaller::Install(const std::string& name,
                               InstallCallback callback) {
  const SidecarInfo* info = Find(name);
  if (!info) {
    std::move(callback).Run(false, "unknown sidecar");
    return;
  }
  auto it = pending_.find(name);
  if (it != pending_.end()) {
    it->second.callbacks.push_back(std::move(callback));
    return;
  }
  errors_.erase(name);
  Pending& p = pending_[name];
  p.name = name;
  p.callbacks.push_back(std::move(callback));
  base::FilePath staging = RootDir().AppendASCII(kStagingDirName);
  std::string out_name = GURL(info->url).ExtractFileName();
  if (out_name.empty()) {
    out_name = name + (info->zip ? ".zip" : ".exe");
  }
  p.asset = staging.Append(base::FilePath::FromUTF8Unsafe(out_name));

  Aria2Service* aria2 = Aria2Service::Get();
  if (!observing_) {
    aria2->tracker()->AddObserver(this);
    observing_ = true;
  }
  base::DictValue options;
  options.Set("dir", staging.AsUTF8Unsafe());
  options.Set("out", out_name);
  options.Set("checksum", "sha-256=" + info->sha256);
  options.Set("allow-overwrite", "true");
  options.Set("auto-file-renaming", "false");
  options.Set("max-connection-per-server", "8");
  options.Set("split", "8");
  base::ListValue params;
  base::ListValue uris;
  uris.Append(info->url);
  params.Append(std::move(uris));
  params.Append(std::move(options));
  aria2->CallWhenReady("aria2.addUri", std::move(params),
                       base::BindOnce(&SidecarInstaller::OnAdded,
                                      weak_factory_.GetWeakPtr(), name));
  NotifyChanged();
}

void SidecarInstaller::OnAdded(const std::string& name,
                               std::optional<base::Value> result) {
  auto it = pending_.find(name);
  if (it == pending_.end()) {
    return;
  }
  if (!result || !result->is_string()) {
    Finish(name, false, "download engine unavailable");
    return;
  }
  it->second.gid = result->GetString();
  Aria2Service::Get()->tracker()->Poke();
}

void SidecarInstaller::OnDownloadsChanged(
    const DownloadTracker::Snapshot& snapshot) {
  if (pending_.empty()) {
    return;
  }
  for (auto& [name, p] : pending_) {
    if (p.gid.empty()) {
      continue;
    }
    base::ListValue params;
    params.Append(p.gid);
    base::ListValue keys;
    keys.Append("completedLength");
    keys.Append("totalLength");
    params.Append(std::move(keys));
    Aria2Service::Get()->Call(
        "aria2.tellStatus", std::move(params),
        base::BindOnce(
            [](base::WeakPtr<SidecarInstaller> self, std::string name,
               std::optional<base::Value> result) {
              if (!self || !result || !result->is_dict()) {
                return;
              }
              auto it = self->pending_.find(name);
              if (it == self->pending_.end()) {
                return;
              }
              const base::DictValue& d = result->GetDict();
              double done = 0, total = 0;
              if (const std::string* s = d.FindString("completedLength")) {
                base::StringToDouble(*s, &done);
              }
              if (const std::string* s = d.FindString("totalLength")) {
                base::StringToDouble(*s, &total);
              }
              it->second.progress = total > 0 ? done / total : 0;
              self->NotifyChanged();
            },
            weak_factory_.GetWeakPtr(), name));
  }
}

void SidecarInstaller::OnDownloadFinished(
    const DownloadTracker::Finished& finished) {
  for (auto& [name, p] : pending_) {
    if (p.gid != finished.gid) {
      continue;
    }
    if (!finished.success) {
      Finish(name, false,
             finished.error_message.empty() ? "download failed"
                                            : finished.error_message);
      return;
    }
    const SidecarInfo* info = Find(name);
    base::FilePath asset =
        finished.path.empty() ? p.asset
                              : base::FilePath::FromUTF8Unsafe(finished.path);
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&Stage, *info, asset, InstallDir(*info)),
        base::BindOnce(&SidecarInstaller::OnStaged, weak_factory_.GetWeakPtr(),
                       name));
    return;
  }
}

void SidecarInstaller::OnStaged(const std::string& name,
                                const std::string& error) {
  Finish(name, error.empty(), error);
}

void SidecarInstaller::Finish(const std::string& name,
                              bool ok,
                              const std::string& error) {
  auto it = pending_.find(name);
  if (it == pending_.end()) {
    return;
  }
  std::vector<InstallCallback> callbacks = std::move(it->second.callbacks);
  pending_.erase(it);
  if (!ok) {
    errors_[name] = error;
    LOG(WARNING) << "falcon: sidecar " << name << " install failed: " << error;
  }
  NotifyChanged();
  for (InstallCallback& cb : callbacks) {
    std::move(cb).Run(ok, error);
  }
  // The files just landed (or did not): re-read from disk.
  RefreshPresence();
}

void SidecarInstaller::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void SidecarInstaller::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void SidecarInstaller::NotifyChanged() {
  for (Observer& o : observers_) {
    o.OnSidecarsChanged();
  }
}

}  // namespace falcon
