// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/download/download_security.h"

#include <array>
#include <atomic>
#include <utility>

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/net/system_network_context_manager.h"
#include "chrome/browser/notifications/notification_display_service.h"
#include "chrome/browser/notifications/notification_display_service_factory.h"
#include "chrome/browser/notifications/notification_handler.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/services/quarantine/quarantine.h"
#include "crypto/hash.h"
#include "net/base/load_flags.h"
#include "net/http/http_request_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "ui/base/models/image_model.h"
#include "ui/message_center/public/cpp/notification.h"
#include "ui/message_center/public/cpp/notifier_id.h"
#include "url/gurl.h"

namespace falcon {

namespace prefs {
void RegisterSecurityLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(kSecurityVtApiKey, std::string());
  registry->RegisterBooleanPref(kSecurityVtEnabled, true);
  registry->RegisterBooleanPref(kSecurityQuarantineFlagged, true);
  registry->RegisterBooleanPref(kSecuritySandboxNetworking, false);
}
}  // namespace prefs

namespace {

constexpr size_t kMaxFilesPerDownload = 40;
constexpr size_t kMaxResults = 500;
constexpr size_t kMaxVtResponse = 512 * 1024;

constexpr net::NetworkTrafficAnnotationTag kVtAnnotation =
    net::DefineNetworkTrafficAnnotation("falcon_virustotal_lookup", R"(
      semantics {
        sender: "Falcon download security"
        description:
          "Looks up the SHA-256 hash of a completed download on VirusTotal "
          "using the user's own API key. Only the hash is sent."
        trigger: "A download completes and a VirusTotal API key is set."
        data: "SHA-256 of the downloaded file."
        destination: OTHER
      }
      policy {
        cookies_allowed: NO
        setting: "Falcon downloader settings > VirusTotal API key."
        policy_exception_justification: "Opt-in via API key."
      })");

PrefService* LocalState() {
  return g_browser_process ? g_browser_process->local_state() : nullptr;
}

std::string HexSha256(const base::FilePath& path) {
  base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!file.IsValid()) {
    return std::string();
  }
  crypto::hash::Hasher hasher(crypto::hash::HashKind::kSha256);
  std::vector<uint8_t> buf(1 << 20);
  while (true) {
    std::optional<size_t> n = file.ReadAtCurrentPos(buf);
    if (!n.has_value()) {
      return std::string();
    }
    if (*n == 0) {
      break;
    }
    hasher.Update(base::span(buf).first(*n));
  }
  std::array<uint8_t, crypto::hash::kSha256Size> digest;
  hasher.Finish(digest);
  return base::ToLowerASCII(base::HexEncode(digest));
}

}  // namespace

struct DownloadSecurity::LocalScan {
  std::string av = "failed";
  std::string sha256;
};

namespace {

// Blocking sequence: MOTW + AV via the quarantine service, then hash.
std::unique_ptr<DownloadSecurity::LocalScan> RunLocalScan(
    base::FilePath path,
    GURL source) {
  auto out = std::make_unique<DownloadSecurity::LocalScan>();
  if (!base::PathExists(path) || base::DirectoryExists(path)) {
    out->av = "skipped";
    return out;
  }
  quarantine::QuarantineFileResult result =
      quarantine::QuarantineFileResult::OK;
  quarantine::QuarantineFile(
      path, source, GURL(), std::nullopt,
      chrome::kApplicationClientIDStringForAVScanning,
      base::BindOnce(
          [](quarantine::QuarantineFileResult* out,
             quarantine::QuarantineFileResult r) { *out = r; },
          &result));
  switch (result) {
    case quarantine::QuarantineFileResult::OK:
    case quarantine::QuarantineFileResult::ANNOTATION_FAILED:
      out->av = "clean";
      break;
    case quarantine::QuarantineFileResult::VIRUS_INFECTED:
    case quarantine::QuarantineFileResult::SECURITY_CHECK_FAILED:
    case quarantine::QuarantineFileResult::BLOCKED_BY_POLICY:
      out->av = "infected";
      return out;
    default:
      out->av = "failed";
      break;
  }
  out->sha256 = HexSha256(path);
  return out;
}

base::FilePath MoveToQuarantine(base::FilePath path) {
  const base::FilePath dir = path.DirName().AppendASCII("Quarantine");
  base::CreateDirectory(dir);
  base::FilePath target = dir.Append(path.BaseName());
  target = base::GetUniquePath(target);
  if (target.empty() || !base::Move(path, target)) {
    return base::FilePath();
  }
  // Keep MOTW on the moved file too.
  return target;
}

}  // namespace

DownloadSecurity::DownloadSecurity(DownloadTracker* tracker) {
  observation_.Observe(tracker);
  ProbeSandboxAvailability();
}

DownloadSecurity::~DownloadSecurity() = default;

void DownloadSecurity::OnDownloadFinished(
    const DownloadTracker::Finished& finished) {
  if (!finished.success || finished.paths.empty()) {
    return;
  }
  StartScan(finished);
}

void DownloadSecurity::Rescan(const std::string& gid) {
  auto it = pending_.find(gid);
  if (it != pending_.end()) {
    StartScan(it->second);
  }
}

void DownloadSecurity::StartScan(const DownloadTracker::Finished& finished) {
  pending_[finished.gid] = finished;
  Result& r = results_[finished.gid];
  r.gid = finished.gid;
  r.name = finished.name;
  r.done = false;
  r.files.clear();
  if (results_.size() > kMaxResults) {
    results_.erase(results_.begin());
  }
  const size_t n = std::min(finished.paths.size(), kMaxFilesPerDownload);
  for (size_t i = 0; i < n; ++i) {
    FileResult f;
    f.path = finished.paths[i];
    f.av = "pending";
    f.vt = "pending";
    r.files.push_back(std::move(f));
  }
  const GURL source(finished.is_torrent ? std::string() : finished.source_url);
  for (size_t i = 0; i < n; ++i) {
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE,
        {base::MayBlock(), base::WithBaseSyncPrimitives(),
         base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&RunLocalScan,
                       base::FilePath::FromUTF8Unsafe(finished.paths[i]),
                       source),
        base::BindOnce(&DownloadSecurity::OnLocalScan,
                       weak_factory_.GetWeakPtr(), finished.gid, i));
  }
}

void DownloadSecurity::OnLocalScan(const std::string& gid,
                                   size_t index,
                                   std::unique_ptr<LocalScan> scan) {
  auto it = results_.find(gid);
  if (it == results_.end() || index >= it->second.files.size()) {
    return;
  }
  FileResult& f = it->second.files[index];
  f.av = scan->av;
  f.sha256 = scan->sha256;
  if (f.av == "infected") {
    f.vt = "skipped";
    Notify("Download blocked by antivirus",
           it->second.name + " was removed by Windows security.");
    MaybeFinish(gid);
    return;
  }
  if (f.sha256.empty()) {
    f.vt = "skipped";
    MaybeFinish(gid);
    return;
  }
  LookupVirusTotal(gid, index);
}

void DownloadSecurity::LookupVirusTotal(const std::string& gid, size_t index) {
  PrefService* ls = LocalState();
  auto it = results_.find(gid);
  if (it == results_.end() || index >= it->second.files.size()) {
    return;
  }
  FileResult& f = it->second.files[index];
  const std::string key = ls ? ls->GetString(prefs::kSecurityVtApiKey) : "";
  if (!ls || !ls->GetBoolean(prefs::kSecurityVtEnabled) || key.empty()) {
    f.vt = "nokey";
    MaybeFinish(gid);
    return;
  }
  if (!g_browser_process->system_network_context_manager()) {
    f.vt = "error";
    MaybeFinish(gid);
    return;
  }
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL("https://www.virustotal.com/api/v3/files/" + f.sha256);
  request->method = "GET";
  request->load_flags = net::LOAD_DISABLE_CACHE;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->headers.SetHeader("x-apikey", key);
  request->headers.SetHeader(net::HttpRequestHeaders::kAccept,
                             "application/json");
  auto loader =
      network::SimpleURLLoader::Create(std::move(request), kVtAnnotation);
  loader->SetAllowHttpErrorResults(true);
  loader->SetTimeoutDuration(base::Seconds(20));
  auto* raw = loader.get();
  loaders_.push_back(std::move(loader));
  raw->DownloadToString(
      g_browser_process->system_network_context_manager()
          ->GetURLLoaderFactory(),
      base::BindOnce(&DownloadSecurity::OnVirusTotal,
                     weak_factory_.GetWeakPtr(), gid, index, raw),
      kMaxVtResponse);
}

void DownloadSecurity::OnVirusTotal(const std::string& gid,
                                    size_t index,
                                    network::SimpleURLLoader* loader,
                                    std::optional<std::string> body) {
  int code = 0;
  if (loader->ResponseInfo() && loader->ResponseInfo()->headers) {
    code = loader->ResponseInfo()->headers->response_code();
  }
  std::erase_if(loaders_, [loader](const auto& l) { return l.get() == loader; });

  auto it = results_.find(gid);
  if (it == results_.end() || index >= it->second.files.size()) {
    return;
  }
  FileResult& f = it->second.files[index];
  if (code == 404) {
    f.vt = "unknown";  // never seen by VT
  } else if (code == 200 && body) {
    auto parsed = base::JSONReader::Read(*body, base::JSON_PARSE_RFC);
    const base::DictValue* stats =
        parsed && parsed->is_dict()
            ? parsed->GetDict().FindDictByDottedPath(
                  "data.attributes.last_analysis_stats")
            : nullptr;
    if (stats) {
      f.vt_malicious = stats->FindInt("malicious").value_or(0);
      f.vt_suspicious = stats->FindInt("suspicious").value_or(0);
      f.vt_total = f.vt_malicious + f.vt_suspicious +
                   stats->FindInt("harmless").value_or(0) +
                   stats->FindInt("undetected").value_or(0);
      f.vt = f.vt_malicious > 0 ? "flagged" : "clean";
    } else {
      f.vt = "error";
    }
  } else {
    f.vt = "error";
    LOG(WARNING) << "Falcon: VirusTotal lookup failed, HTTP " << code;
  }

  if (f.vt == "flagged") {
    Notify("Download flagged by VirusTotal",
           base::NumberToString(f.vt_malicious) + " of " +
               base::NumberToString(f.vt_total) + " engines flag " +
               it->second.name);
    PrefService* ls = LocalState();
    if (ls && ls->GetBoolean(prefs::kSecurityQuarantineFlagged)) {
      QuarantineFlagged(gid, index);
      return;
    }
  }
  MaybeFinish(gid);
}

void DownloadSecurity::QuarantineFlagged(const std::string& gid, size_t index) {
  auto it = results_.find(gid);
  if (it == results_.end() || index >= it->second.files.size()) {
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&MoveToQuarantine, base::FilePath::FromUTF8Unsafe(
                                            it->second.files[index].path)),
      base::BindOnce(&DownloadSecurity::OnQuarantined,
                     weak_factory_.GetWeakPtr(), gid, index));
}

void DownloadSecurity::OnQuarantined(const std::string& gid,
                                     size_t index,
                                     base::FilePath moved_to) {
  auto it = results_.find(gid);
  if (it != results_.end() && index < it->second.files.size()) {
    it->second.files[index].quarantined_to = moved_to.AsUTF8Unsafe();
  }
  MaybeFinish(gid);
}

void DownloadSecurity::MaybeFinish(const std::string& gid) {
  auto it = results_.find(gid);
  if (it == results_.end()) {
    return;
  }
  for (const FileResult& f : it->second.files) {
    if (f.av == "pending" || f.vt == "pending") {
      return;
    }
  }
  it->second.done = true;
  MaybeOpen(it->second);
}

void DownloadSecurity::SetOpenWhenDone(const std::string& gid, bool open) {
  if (open) {
    open_when_done_.insert(gid);
    // Already finished and scanned: open right away.
    auto it = results_.find(gid);
    if (it != results_.end() && it->second.done) {
      MaybeOpen(it->second);
    }
  } else {
    open_when_done_.erase(gid);
  }
}

void DownloadSecurity::MaybeOpen(const Result& result) {
  if (!open_when_done_.erase(result.gid)) {
    return;
  }
  Profile* profile = ProfileManager::GetLastUsedProfileIfLoaded();
  if (!profile) {
    return;
  }
  for (const FileResult& f : result.files) {
    if (f.av == "infected" || f.vt == "flagged" || !f.quarantined_to.empty() ||
        f.path.empty()) {
      continue;
    }
    platform_util::OpenItem(profile, base::FilePath::FromUTF8Unsafe(f.path),
                            platform_util::OPEN_FILE,
                            platform_util::OpenOperationCallback());
    // One launch per download is plenty (torrents may hold many files).
    break;
  }
}

base::DictValue DownloadSecurity::ResultsAsDict() const {
  base::DictValue out;
  for (const auto& [gid, r] : results_) {
    base::DictValue d;
    d.Set("name", r.name);
    d.Set("done", r.done);
    base::ListValue files;
    for (const FileResult& f : r.files) {
      base::DictValue fd;
      fd.Set("path", f.path);
      fd.Set("sha256", f.sha256);
      fd.Set("av", f.av);
      fd.Set("vt", f.vt);
      fd.Set("vtMalicious", f.vt_malicious);
      fd.Set("vtSuspicious", f.vt_suspicious);
      fd.Set("vtTotal", f.vt_total);
      fd.Set("quarantinedTo", f.quarantined_to);
      files.Append(std::move(fd));
    }
    d.Set("files", std::move(files));
    out.Set(gid, std::move(d));
  }
  return out;
}

void DownloadSecurity::Notify(const std::string& title,
                              const std::string& message) {
  Profile* profile = ProfileManager::GetLastUsedProfileIfLoaded();
  if (!profile) {
    return;
  }
  auto* service = NotificationDisplayServiceFactory::GetForProfile(profile);
  if (!service) {
    return;
  }
  message_center::RichNotificationData data;
  data.context_message = u" ";
  message_center::Notification notification(
      message_center::NOTIFICATION_TYPE_SIMPLE,
      "falcon-security-" + base::NumberToString(
                               base::Time::Now().ToDeltaSinceWindowsEpoch()
                                   .InMicroseconds()),
      base::UTF8ToUTF16(title), base::UTF8ToUTF16(message), ui::ImageModel(),
      u"Falcon", GURL(),
      message_center::NotifierId(message_center::NotifierType::SYSTEM_COMPONENT,
                                 "falcon.security"),
      data, nullptr);
  service->Display(NotificationHandler::Type::TRANSIENT, notification,
                   nullptr);
}

namespace {

// Probed once on the thread pool (a stat is blocking; the UI thread may not
// block); false until the probe lands.
std::atomic<bool> g_sandbox_available{false};

bool ProbeSandbox() {
#if BUILDFLAG(IS_WIN)
  base::FilePath system;
  return base::PathService::Get(base::DIR_SYSTEM, &system) &&
         base::PathExists(system.AppendASCII("WindowsSandbox.exe"));
#else
  return false;
#endif
}

}  // namespace

// static
bool DownloadSecurity::IsSandboxAvailable() {
  return g_sandbox_available.load(std::memory_order_relaxed);
}

// static
void DownloadSecurity::ProbeSandboxAvailability() {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(&ProbeSandbox), base::BindOnce([](bool available) {
        g_sandbox_available.store(available, std::memory_order_relaxed);
      }));
}

// static
void DownloadSecurity::OpenInSandbox(const base::FilePath& file,
                                     bool networking) {
#if BUILDFLAG(IS_WIN)
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(
          [](base::FilePath file, bool networking) {
            base::FilePath user_data;
            base::PathService::Get(chrome::DIR_USER_DATA, &user_data);
            const base::FilePath dir =
                user_data.AppendASCII("falcon").AppendASCII("sandbox");
            base::CreateDirectory(dir);
            const std::string host_dir = file.DirName().AsUTF8Unsafe();
            const std::string name = file.BaseName().AsUTF8Unsafe();
            std::string wsb =
                "<Configuration>\n"
                "  <Networking>" + std::string(networking ? "Enable" : "Disable") + "</Networking>\n"
                "  <MappedFolders>\n"
                "    <MappedFolder>\n"
                "      <HostFolder>" + host_dir + "</HostFolder>\n"
                "      <SandboxFolder>C:\\Falcon</SandboxFolder>\n"
                "      <ReadOnly>true</ReadOnly>\n"
                "    </MappedFolder>\n"
                "  </MappedFolders>\n"
                "  <LogonCommand>\n"
                "    <Command>explorer.exe /select,\"C:\\Falcon\\" + name + "\"</Command>\n"
                "  </LogonCommand>\n"
                "</Configuration>\n";
            const base::FilePath wsb_path = dir.AppendASCII("falcon-sandbox.wsb");
            base::WriteFile(wsb_path, wsb);
            base::FilePath system;
            base::PathService::Get(base::DIR_SYSTEM, &system);
            base::CommandLine cmd(system.AppendASCII("WindowsSandbox.exe"));
            cmd.AppendArgPath(wsb_path);
            base::LaunchOptions options;
            base::LaunchProcess(cmd, options);
          },
          file, networking));
#endif
}

}  // namespace falcon
