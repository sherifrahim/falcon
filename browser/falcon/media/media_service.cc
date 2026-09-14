// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/media/media_service.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "brave/browser/falcon/download/aria2_service.h"
#include "brave/browser/falcon/download/download_interceptor.h"
#include "brave/browser/falcon/download/download_tracker.h"
#include "chrome/browser/profiles/profile.h"
#include "components/embedder_support/user_agent_utils.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_options.h"
#include "net/cookies/cookie_partition_key_collection.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>
#endif

namespace falcon {

namespace {

constexpr base::TimeDelta kProbeTimeout = base::Seconds(90);
constexpr size_t kMaxJobs = 200;

// Sentinel prefixes emitted through yt-dlp's --print / --progress-template so
// stdout parsing does not depend on its human-readable output.
constexpr char kProgressPrefix[] = "FALCONP|";
constexpr char kTitlePrefix[] = "FALCONT|";
constexpr char kFilePrefix[] = "FALCONF|";
constexpr char kPostPrefix[] = "FALCONM|";

base::FilePath ExeDir() {
  base::FilePath dir;
  base::PathService::Get(base::DIR_EXE, &dir);
  return dir;
}

std::string PresetSelector(const std::string& preset) {
  if (preset == "best" || preset.empty()) return "bv*+ba/b";
  if (preset == "audio") return "ba/b";
  int height = 0;
  if (base::StringToInt(preset, &height) && height > 0) {
    return base::StringPrintf("bv*[height<=%d]+ba/b[height<=%d]", height,
                              height);
  }
  return preset;  // raw yt-dlp selector
}

// Netscape cookies.txt for the cookies the profile would send to |url|.
std::string CookiesToNetscape(const net::CookieAccessResultList& cookies) {
  std::string out = "# Netscape HTTP Cookie File\n";
  const int64_t fallback_expiry =
      (base::Time::Now() + base::Days(365)).ToTimeT();
  for (const auto& cwr : cookies) {
    const net::CanonicalCookie& c = cwr.cookie;
    const int64_t expiry =
        c.IsPersistent() ? c.ExpiryDate().ToTimeT() : fallback_expiry;
    out += base::StringPrintf(
        "%s\t%s\t%s\t%s\t%lld\t%s\t%s\n", c.Domain().c_str(),
        c.IsDomainCookie() ? "TRUE" : "FALSE", c.Path().c_str(),
        c.SecureAttribute() ? "TRUE" : "FALSE",
        static_cast<long long>(expiry), c.Name().c_str(), c.Value().c_str());
  }
  return out;
}

int64_t ParseInt64(std::string_view s) {
  int64_t v = 0;
  base::StringToInt64(base::TrimWhitespaceASCII(s, base::TRIM_ALL), &v);
  return v;
}

}  // namespace

// Everything a blocking-sequence run needs; built on the UI thread.
struct MediaService::Launch {
  base::CommandLine cmd{base::CommandLine::NO_PROGRAM};
  base::FilePath cookie_file;
  base::FilePath work_dir;
#if BUILDFLAG(IS_WIN)
  HANDLE job_object = nullptr;  // not owned
#endif
};

base::FilePath YtDlpPath() {
  return ExeDir().AppendASCII("yt-dlp.exe");
}

base::FilePath FfmpegDirectory() {
  return ExeDir().AppendASCII("ffmpeg");
}

base::FilePath MediaDownloadDirectory(Profile* profile) {
  return TargetDirectory(profile, "media.mp4");
}

// static
MediaService* MediaService::Get() {
  static base::NoDestructor<MediaService> instance;
  return instance.get();
}

MediaService::MediaService() = default;
MediaService::~MediaService() = default;

// static
bool MediaService::IsAvailable() {
  // yt-dlp.exe and ffmpeg/ are build outputs next to the browser
  // (//brave/third_party/yt-dlp, //brave/third_party/ffmpeg-bin); a missing
  // binary surfaces as a launch failure on the job instead.
  return true;
}

namespace {

// Common yt-dlp arguments.
void AddCommonArgs(base::CommandLine& cmd,
                   const GURL& referer,
                   const base::FilePath& cookie_file) {
  cmd.AppendArg("--no-playlist");
  cmd.AppendArg("--no-warnings");
  cmd.AppendArg("--no-colors");
  cmd.AppendArg("--ignore-config");
  cmd.AppendArg("--user-agent");
  cmd.AppendArg(embedder_support::GetUserAgent());
  if (referer.SchemeIsHTTPOrHTTPS()) {
    cmd.AppendArg("--referer");
    cmd.AppendArg(referer.spec());
  }
  if (!cookie_file.empty()) {
    cmd.AppendArg("--cookies");
    cmd.AppendArgPath(cookie_file);
  }
  cmd.AppendArg("--ffmpeg-location");
  cmd.AppendArgPath(FfmpegDirectory());
}

base::FilePath TempDir() {
  base::FilePath dir;
  base::PathService::Get(base::DIR_TEMP, &dir);
  return dir.AppendASCII("falcon-media");
}

base::DictValue FormatToDict(const base::DictValue& f) {
  base::DictValue d;
  auto copy_str = [&](const char* key) {
    if (const std::string* v = f.FindString(key)) d.Set(key, *v);
  };
  auto copy_num = [&](const char* key) {
    if (std::optional<double> v = f.FindDouble(key)) d.Set(key, *v);
  };
  for (const char* k : {"format_id", "ext", "resolution", "format_note",
                        "vcodec", "acodec", "protocol", "container"}) {
    copy_str(k);
  }
  for (const char* k : {"filesize", "filesize_approx", "tbr", "fps", "height",
                        "width", "abr", "vbr"}) {
    copy_num(k);
  }
  return d;
}

// Blocking: run `yt-dlp -J` and reduce its JSON to what the picker shows.
base::DictValue RunProbe(std::unique_ptr<MediaService::Launch> launch,
                         std::string cookies) {
  base::DictValue result;
  base::CreateDirectory(launch->work_dir);
  if (!cookies.empty()) {
    base::WriteFile(launch->cookie_file, cookies);
  }
  std::string output;
  int exit_code = -1;
  base::LaunchOptions options;
#if BUILDFLAG(IS_WIN)
  options.start_hidden = true;
#endif
  const bool ran = base::GetAppOutputWithExitCodeAndTimeout(
      launch->cmd.GetCommandLineString(), /*include_stderr=*/false, &output,
      &exit_code, kProbeTimeout, options);
  if (!launch->cookie_file.empty()) {
    base::DeleteFile(launch->cookie_file);
  }
  if (!ran) {
    result.Set("error", "yt-dlp did not finish (timeout or launch failure)");
    return result;
  }
  // yt-dlp may print notices before the JSON; find the object.
  const size_t brace = output.find('{');
  std::optional<base::Value> json =
      brace == std::string::npos
          ? std::nullopt
          : base::JSONReader::Read(output.substr(brace), base::JSON_PARSE_RFC);
  if (!json || !json->is_dict()) {
    result.Set("error", exit_code == 0
                            ? "Could not read media info"
                            : "yt-dlp could not extract media from this URL");
    return result;
  }
  const base::DictValue& info = json->GetDict();
  for (const char* k : {"title", "thumbnail", "webpage_url", "extractor",
                        "uploader", "ext"}) {
    if (const std::string* v = info.FindString(k)) result.Set(k, *v);
  }
  if (std::optional<double> d = info.FindDouble("duration")) {
    result.Set("duration", *d);
  }
  base::ListValue formats;
  if (const base::ListValue* list = info.FindList("formats")) {
    for (const base::Value& f : *list) {
      if (!f.is_dict()) continue;
      const base::DictValue& fd = f.GetDict();
      // Skip storyboards / images.
      const std::string* vcodec = fd.FindString("vcodec");
      const std::string* acodec = fd.FindString("acodec");
      const std::string* ext = fd.FindString("ext");
      if (ext && *ext == "mhtml") continue;
      if (vcodec && *vcodec == "none" && acodec && *acodec == "none") continue;
      formats.Append(FormatToDict(fd));
    }
  }
  result.Set("formats", std::move(formats));
  return result;
}

}  // namespace

void MediaService::Probe(Profile* profile,
                         const GURL& url,
                         const GURL& referer,
                         ProbeCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::DictValue err;
  if (!IsAvailable()) {
    err.Set("error", "yt-dlp is not bundled with this build");
    std::move(callback).Run(std::move(err));
    return;
  }
  if (!url.SchemeIsHTTPOrHTTPS()) {
    err.Set("error", "Only http(s) URLs can be probed");
    std::move(callback).Run(std::move(err));
    return;
  }
  auto launch = std::make_unique<Launch>();
  launch->work_dir = TempDir();
  launch->cookie_file = launch->work_dir.AppendASCII(
      base::StringPrintf("probe-%d.txt", next_id_++));
  launch->cmd = base::CommandLine(YtDlpPath());
  launch->cmd.AppendArg("-J");
  AddCommonArgs(launch->cmd, referer, launch->cookie_file);
  launch->cmd.AppendArg(url.spec());

  auto* cookie_manager = profile ? profile->GetDefaultStoragePartition()
                                       ->GetCookieManagerForBrowserProcess()
                                 : nullptr;
  if (!cookie_manager) {
    OnCookiesForProbe(profile, std::move(launch), std::move(callback), "");
    return;
  }
  cookie_manager->GetCookieList(
      url, net::CookieOptions::MakeAllInclusive(),
      net::CookiePartitionKeyCollection::ContainsAll(),
      base::BindOnce(
          [](base::WeakPtr<MediaService> self, Profile* profile,
             std::unique_ptr<Launch> launch, ProbeCallback callback,
             const net::CookieAccessResultList& cookies,
             const net::CookieAccessResultList& excluded) {
            if (!self) return;
            self->OnCookiesForProbe(profile, std::move(launch),
                                    std::move(callback),
                                    CookiesToNetscape(cookies));
          },
          weak_factory_.GetWeakPtr(), profile, std::move(launch),
          std::move(callback)));
}

void MediaService::OnCookiesForProbe(Profile* profile,
                                     std::unique_ptr<Launch> launch,
                                     ProbeCallback callback,
                                     std::string cookies) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::WithBaseSyncPrimitives(),
       base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&RunProbe, std::move(launch), std::move(cookies)),
      base::BindOnce(&MediaService::OnProbeDone, weak_factory_.GetWeakPtr(),
                     std::move(callback)));
}

void MediaService::OnProbeDone(ProbeCallback callback, base::DictValue result) {
  std::move(callback).Run(std::move(result));
}

namespace {

// Blocking: run the download, streaming stdout lines to |on_line| (UI thread)
// and the exit status to |on_exit|.
void RunDownload(std::unique_ptr<MediaService::Launch> launch,
                 std::string cookies,
                 base::RepeatingCallback<void(std::string)> on_line,
                 base::OnceCallback<void(bool, int)> on_exit) {
  base::CreateDirectory(launch->work_dir);
  if (!cookies.empty()) {
    base::WriteFile(launch->cookie_file, cookies);
  }
  base::LaunchOptions options;
#if BUILDFLAG(IS_WIN)
  options.start_hidden = true;
  options.job_handle = launch->job_object;
#endif
  std::string pending;
  int exit_code = -1;
  const bool ran = base::GetAppOutputWithExitCodeAndTimeout(
      launch->cmd.GetCommandLineString(), /*include_stderr=*/true,
      /*output=*/nullptr, &exit_code, base::TimeDelta::Max(), options,
      [&](const base::Process& process, std::string_view partial) {
        pending.append(partial);
        size_t nl;
        while ((nl = pending.find('\n')) != std::string::npos) {
          std::string line = pending.substr(0, nl);
          pending.erase(0, nl + 1);
          if (!line.empty() && line.back() == '\r') line.pop_back();
          if (!line.empty()) on_line.Run(std::move(line));
        }
      });
  if (!pending.empty()) {
    on_line.Run(std::move(pending));
  }
  if (!launch->cookie_file.empty()) {
    base::DeleteFile(launch->cookie_file);
  }
  std::move(on_exit).Run(ran, exit_code);
}

}  // namespace

int MediaService::Start(Profile* profile,
                        const GURL& url,
                        const GURL& referer,
                        const std::string& selector) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsAvailable() || !url.SchemeIsHTTPOrHTTPS() || !profile) {
    return 0;
  }
  const int id = next_id_++;
  Job& job = jobs_[id];
  job.id = id;
  job.url = url;
  job.referer = referer;
  job.selector = selector;
  job.title = url.host();

  auto launch = std::make_unique<Launch>();
  launch->work_dir = TempDir();
  launch->cookie_file =
      launch->work_dir.AppendASCII(base::StringPrintf("job-%d.txt", id));
  base::CommandLine& cmd = launch->cmd;
  cmd = base::CommandLine(YtDlpPath());
  AddCommonArgs(cmd, referer, launch->cookie_file);
  cmd.AppendArg("--newline");
  cmd.AppendArg("--no-simulate");
  cmd.AppendArg("--concurrent-fragments");
  cmd.AppendArg("4");
  cmd.AppendArg("--retries");
  cmd.AppendArg("10");
  cmd.AppendArg("--fragment-retries");
  cmd.AppendArg("10");
  cmd.AppendArg("--continue");
  cmd.AppendArg("--windows-filenames");
  cmd.AppendArg("-f");
  cmd.AppendArg(PresetSelector(selector));
  if (selector == "audio") {
    cmd.AppendArg("--extract-audio");
    cmd.AppendArg("--audio-format");
    cmd.AppendArg("m4a");
  } else {
    cmd.AppendArg("--merge-output-format");
    cmd.AppendArg("mp4");
    cmd.AppendArg("--remux-video");
    cmd.AppendArg("mp4");
  }
  cmd.AppendArg("--progress-template");
  cmd.AppendArg(std::string("download:") + kProgressPrefix +
                "%(progress._percent_str)s|%(progress._speed_str)s|"
                "%(progress._eta_str)s|%(progress.downloaded_bytes)s|"
                "%(progress.total_bytes)s|%(progress.total_bytes_estimate)s");
  cmd.AppendArg("--progress-template");
  cmd.AppendArg(std::string("postprocess:") + kPostPrefix +
                "%(progress.status)s");
  cmd.AppendArg("--print");
  cmd.AppendArg(std::string("before_dl:") + kTitlePrefix + "%(title)s");
  cmd.AppendArg("--print");
  cmd.AppendArg(std::string("after_move:") + kFilePrefix + "%(filepath)s");
  cmd.AppendArg("-o");
  cmd.AppendArg("%(title).150B [%(id)s].%(ext)s");
  cmd.AppendArg("-P");
  cmd.AppendArgPath(MediaDownloadDirectory(profile));
  cmd.AppendArg(url.spec());

#if BUILDFLAG(IS_WIN)
  HANDLE job_object = ::CreateJobObject(nullptr, nullptr);
  if (job_object) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
    info.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_BREAKAWAY_OK;
    ::SetInformationJobObject(job_object, JobObjectExtendedLimitInformation,
                              &info, sizeof(info));
    job.job_object.Set(job_object);
    launch->job_object = job_object;
  }
#endif

  while (jobs_.size() > kMaxJobs) {
    // Drop the oldest finished job.
    auto it = std::find_if(jobs_.begin(), jobs_.end(), [](const auto& kv) {
      return kv.second.status == "done" || kv.second.status == "error" ||
             kv.second.status == "cancelled";
    });
    if (it == jobs_.end()) break;
    jobs_.erase(it);
  }

  auto* cookie_manager =
      profile->GetDefaultStoragePartition()->GetCookieManagerForBrowserProcess();
  if (!cookie_manager) {
    OnCookiesForStart(id, std::move(launch), "");
    return id;
  }
  cookie_manager->GetCookieList(
      url, net::CookieOptions::MakeAllInclusive(),
      net::CookiePartitionKeyCollection::ContainsAll(),
      base::BindOnce(
          [](base::WeakPtr<MediaService> self, int id,
             std::unique_ptr<Launch> launch,
             const net::CookieAccessResultList& cookies,
             const net::CookieAccessResultList& excluded) {
            if (!self) return;
            self->OnCookiesForStart(id, std::move(launch),
                                    CookiesToNetscape(cookies));
          },
          weak_factory_.GetWeakPtr(), id, std::move(launch)));
  return id;
}

void MediaService::OnCookiesForStart(int id,
                                     std::unique_ptr<Launch> launch,
                                     std::string cookies) {
  if (!jobs_.contains(id)) {
    return;  // cancelled before it started
  }
  base::ThreadPool::PostTask(
      FROM_HERE,
      {base::MayBlock(), base::WithBaseSyncPrimitives(),
       base::TaskPriority::USER_VISIBLE},
      base::BindOnce(
          &RunDownload, std::move(launch), std::move(cookies),
          base::BindPostTaskToCurrentDefault(base::BindRepeating(
              &MediaService::OnOutputLine, weak_factory_.GetWeakPtr(), id)),
          base::BindPostTaskToCurrentDefault(base::BindOnce(
              &MediaService::OnExited, weak_factory_.GetWeakPtr(), id))));
}

void MediaService::OnOutputLine(int id, std::string line) {
  auto it = jobs_.find(id);
  if (it == jobs_.end()) {
    return;
  }
  Job& job = it->second;
  if (base::StartsWith(line, kProgressPrefix)) {
    const std::vector<std::string_view> parts = base::SplitStringPiece(
        std::string_view(line).substr(sizeof(kProgressPrefix) - 1), "|",
        base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
    if (parts.size() >= 6) {
      double pct = 0;
      std::string p(parts[0]);
      if (!p.empty() && p.back() == '%') p.pop_back();
      base::StringToDouble(p, &pct);
      job.percent = std::clamp(pct, 0.0, 100.0);
      job.speed = parts[1] == "Unknown B/s" ? "" : std::string(parts[1]);
      job.eta = parts[2] == "Unknown" ? "" : std::string(parts[2]);
      job.downloaded = ParseInt64(parts[3]);
      job.total = ParseInt64(parts[4]);
      if (job.total <= 0) job.total = ParseInt64(parts[5]);
      if (job.status == "starting" || job.status == "merging") {
        job.status = "downloading";
      }
    }
  } else if (base::StartsWith(line, kTitlePrefix)) {
    job.title = line.substr(sizeof(kTitlePrefix) - 1);
  } else if (base::StartsWith(line, kFilePrefix)) {
    job.path = base::FilePath::FromUTF8Unsafe(line.substr(sizeof(kFilePrefix) - 1));
  } else if (base::StartsWith(line, kPostPrefix)) {
    job.status = "merging";
    job.speed.clear();
    job.eta.clear();
  } else if (base::StartsWith(line, "ERROR:")) {
    job.error = std::string(base::TrimWhitespaceASCII(
        std::string_view(line).substr(6), base::TRIM_ALL));
  }
}

void MediaService::OnExited(int id, bool launched, int exit_code) {
  auto it = jobs_.find(id);
  if (it == jobs_.end()) {
    return;
  }
  Job& job = it->second;
  if (job.status == "cancelled") {
    return;
  }
  const bool ok = launched && exit_code == 0 && !job.path.empty();
  job.status = ok ? "done" : "error";
  if (ok) {
    job.percent = 100;
  } else if (job.error.empty()) {
    job.error = launched ? base::StringPrintf("yt-dlp exited with code %d",
                                              exit_code)
                         : "Could not start yt-dlp";
  }
  job.speed.clear();
  job.eta.clear();
#if BUILDFLAG(IS_WIN)
  job.job_object.Close();
#endif

  // Feed the same pipeline as engine downloads: toast, history, security.
  DownloadTracker::Finished f;
  f.gid = base::StringPrintf("media-%d", id);
  f.name = job.path.empty() ? job.title : job.path.BaseName().AsUTF8Unsafe();
  f.path = job.path.AsUTF8Unsafe();
  if (!f.path.empty()) f.paths.push_back(f.path);
  f.source_url = job.url.spec();
  f.success = ok;
  f.error_message = job.error;
  f.total_bytes = job.total > 0 ? job.total : job.downloaded;
  Aria2Service::Get()->tracker()->NotifyExternalFinished(f);
}

void MediaService::Cancel(int id) {
  auto it = jobs_.find(id);
  if (it == jobs_.end()) {
    return;
  }
  Job& job = it->second;
  if (job.status == "done" || job.status == "error") {
    return;
  }
  job.status = "cancelled";
  job.speed.clear();
  job.eta.clear();
#if BUILDFLAG(IS_WIN)
  if (job.job_object.is_valid()) {
    ::TerminateJobObject(job.job_object.Get(), 1);
    job.job_object.Close();
  }
#endif
}

void MediaService::Remove(int id) {
  Cancel(id);
  jobs_.erase(id);
}

const MediaService::Job* MediaService::GetJob(int id) const {
  auto it = jobs_.find(id);
  return it == jobs_.end() ? nullptr : &it->second;
}

base::ListValue MediaService::JobsAsList() const {
  base::ListValue list;
  for (const auto& [id, job] : jobs_) {
    base::DictValue d;
    d.Set("id", id);
    d.Set("url", job.url.spec());
    d.Set("referer", job.referer.spec());
    d.Set("title", job.title);
    d.Set("selector", job.selector);
    d.Set("status", job.status);
    d.Set("percent", job.percent);
    d.Set("speed", job.speed);
    d.Set("eta", job.eta);
    d.Set("downloaded", static_cast<double>(job.downloaded));
    d.Set("total", static_cast<double>(job.total));
    d.Set("path", job.path.AsUTF8Unsafe());
    d.Set("error", job.error);
    d.Set("started", job.started.InSecondsFSinceUnixEpoch());
    list.Append(std::move(d));
  }
  return list;
}

}  // namespace falcon
