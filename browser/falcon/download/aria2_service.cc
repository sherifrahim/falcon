// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/download/aria2_service.h"

#include <utility>

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/net/system_network_context_manager.h"
#include "chrome/common/chrome_paths.h"
#include "net/base/load_flags.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>
#endif

namespace falcon {

namespace {

constexpr int kMaxLaunchAttempts = 3;
constexpr int kMaxProbeAttempts = 25;  // 25 * 200ms = 5s
constexpr size_t kMaxRpcResponseBytes = 4 * 1024 * 1024;

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("falcon_aria2_rpc", R"(
      semantics {
        sender: "Falcon download engine"
        description:
          "JSON-RPC calls to the local aria2c sidecar process that performs "
          "Falcon's downloads. Never leaves the machine (127.0.0.1)."
        trigger: "A download is started, paused, resumed or queried."
        data: "Download URLs, request headers (cookies, referer, user agent) "
              "and download options for the local engine."
        destination: LOCAL
      }
      policy {
        cookies_allowed: NO
        setting: "Falcon settings > Downloads > Use Falcon download engine."
        policy_exception_justification: "Local-only IPC."
      })");

base::FilePath Aria2Dir() {
  base::FilePath user_data;
  base::PathService::Get(chrome::DIR_USER_DATA, &user_data);
  return user_data.AppendASCII("falcon");
}

}  // namespace

struct Aria2Service::LaunchResult {
  base::Process process;
#if BUILDFLAG(IS_WIN)
  base::win::ScopedHandle job;
#endif
  bool exited_early = false;
};

namespace {

// Runs on a blocking sequence.
std::unique_ptr<Aria2Service::LaunchResult> LaunchAria2(
    base::CommandLine cmd,
    base::FilePath aria2_dir) {
  auto result = std::make_unique<Aria2Service::LaunchResult>();
  base::CreateDirectory(aria2_dir);

  // aria2 refuses --input-file that does not exist; create an empty session.
  const base::FilePath session = aria2_dir.AppendASCII("aria2.session");
  if (!base::PathExists(session)) {
    base::WriteFile(session, "");
  }

  base::LaunchOptions options;
  options.current_directory = cmd.GetProgram().DirName();
#if BUILDFLAG(IS_WIN)
  options.start_hidden = true;
  // Belt and braces next to --stop-with-process: if the browser dies without
  // running shutdown, closing the job handle kills aria2c.
  HANDLE job = ::CreateJobObject(nullptr, nullptr);
  if (job) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
    info.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_BREAKAWAY_OK;
    ::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info,
                              sizeof(info));
    result->job.Set(job);
    options.job_handle = job;
  }
#elif BUILDFLAG(IS_LINUX)
  options.kill_on_parent_death = true;
#endif

  result->process = base::LaunchProcess(cmd, options);
  if (result->process.IsValid()) {
    // A port collision makes aria2 exit immediately; detect that so the
    // caller can retry with another port.
    int exit_code = 0;
    result->exited_early = result->process.WaitForExitWithTimeout(
        base::Milliseconds(400), &exit_code);
  }
  return result;
}

}  // namespace

// static
Aria2Service* Aria2Service::Get() {
  static base::NoDestructor<Aria2Service> instance;
  return instance.get();
}

Aria2Service::Aria2Service() = default;
Aria2Service::~Aria2Service() = default;

std::string Aria2Service::rpc_http_url() const {
  return base::StringPrintf("http://127.0.0.1:%d/jsonrpc", rpc_port_);
}

std::string Aria2Service::rpc_ws_url() const {
  return base::StringPrintf("ws://127.0.0.1:%d/jsonrpc", rpc_port_);
}

base::FilePath Aria2Service::SessionFile() const {
  return Aria2Dir().AppendASCII("aria2.session");
}

void Aria2Service::PickEndpoint() {
  rpc_port_ = base::RandIntInclusive(20000, 59999);
  if (rpc_secret_.empty()) {
    rpc_secret_ = base::HexEncode(base::RandBytesAsVector(16));
  }
}

void Aria2Service::EnsureRunning() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (launched_) {
    return;
  }
  launched_ = true;
  launch_attempts_ = 0;
  PickEndpoint();
  Launch();
}

void Aria2Service::Launch() {
  ++launch_attempts_;

  base::FilePath exe_dir;
  base::PathService::Get(base::DIR_EXE, &exe_dir);
  const base::FilePath exe = exe_dir.AppendASCII("aria2c.exe");
  const base::FilePath dir = Aria2Dir();

  base::CommandLine cmd(exe);
  cmd.AppendArg("--enable-rpc=true");
  cmd.AppendArg("--rpc-listen-all=false");
  cmd.AppendArg("--rpc-listen-port=" + base::NumberToString(rpc_port_));
  cmd.AppendArg("--rpc-secret=" + rpc_secret_);
  cmd.AppendArg("--rpc-allow-origin-all=true");
  cmd.AppendArg("--stop-with-process=" +
                base::NumberToString(base::Process::Current().Pid()));
  // Session persistence: resume unfinished downloads across restarts.
  cmd.AppendArg("--input-file=" + SessionFile().AsUTF8Unsafe());
  cmd.AppendArg("--save-session=" + SessionFile().AsUTF8Unsafe());
  cmd.AppendArg("--save-session-interval=30");
  // Engine defaults (IDM-class). Per-download overrides come via RPC.
  cmd.AppendArg("--continue=true");
  cmd.AppendArg("--max-connection-per-server=16");
  cmd.AppendArg("--split=16");
  cmd.AppendArg("--min-split-size=1M");
  cmd.AppendArg("--max-concurrent-downloads=5");
  cmd.AppendArg("--file-allocation=none");
  cmd.AppendArg("--auto-file-renaming=true");
  cmd.AppendArg("--remote-time=true");
  cmd.AppendArg("--content-disposition-default-utf8=true");
  cmd.AppendArg("--max-tries=5");
  cmd.AppendArg("--retry-wait=3");
  // BitTorrent / magnet.
  cmd.AppendArg("--enable-dht=true");
  cmd.AppendArg("--enable-dht6=true");
  cmd.AppendArg("--enable-peer-exchange=true");
  cmd.AppendArg("--bt-enable-lpd=true");
  cmd.AppendArg("--bt-max-peers=100");
  cmd.AppendArg("--seed-time=0");
  cmd.AppendArg("--dht-file-path=" +
                dir.AppendASCII("dht.dat").AsUTF8Unsafe());
  cmd.AppendArg("--dht-file-path6=" +
                dir.AppendASCII("dht6.dat").AsUTF8Unsafe());
  // Logging.
  cmd.AppendArg("--quiet=true");
  cmd.AppendArg("--log=" + dir.AppendASCII("aria2.log").AsUTF8Unsafe());
  cmd.AppendArg("--log-level=notice");

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&LaunchAria2, cmd, dir),
      base::BindOnce(&Aria2Service::OnLaunched, weak_factory_.GetWeakPtr()));
}

void Aria2Service::OnLaunched(std::unique_ptr<LaunchResult> result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!result->process.IsValid() || result->exited_early) {
    LOG(WARNING) << "Falcon: aria2c failed to start on port " << rpc_port_
                 << " (attempt " << launch_attempts_ << ")";
    if (launch_attempts_ < kMaxLaunchAttempts) {
      PickEndpoint();
      Launch();
    } else {
      launched_ = false;
      queued_.clear();
    }
    return;
  }
  process_ = std::move(result->process);
#if BUILDFLAG(IS_WIN)
  job_ = std::move(result->job);
#endif
  launch_attempts_ = 0;
  ProbeReady();
}

void Aria2Service::ProbeReady() {
  Call("aria2.getVersion", base::ListValue(),
       base::BindOnce(&Aria2Service::OnProbeResult,
                      weak_factory_.GetWeakPtr()));
}

void Aria2Service::OnProbeResult(std::optional<base::Value> result) {
  if (result.has_value()) {
    ready_ = true;
    VLOG(1) << "Falcon: aria2c ready on port " << rpc_port_;
    FlushQueue();
    return;
  }
  if (++launch_attempts_ > kMaxProbeAttempts) {
    LOG(ERROR) << "Falcon: aria2c RPC never became ready";
    launch_attempts_ = 0;
    return;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&Aria2Service::ProbeReady, weak_factory_.GetWeakPtr()),
      base::Milliseconds(200));
}

void Aria2Service::FlushQueue() {
  std::vector<base::OnceClosure> queued = std::move(queued_);
  queued_.clear();
  for (auto& closure : queued) {
    std::move(closure).Run();
  }
}

void Aria2Service::AddUri(const GURL& url,
                          const std::string& referer,
                          const std::string& user_agent,
                          const std::string& cookie_header,
                          const std::string& out_filename,
                          const base::FilePath& download_dir) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  EnsureRunning();
  if (!ready_) {
    queued_.push_back(base::BindOnce(&Aria2Service::AddUri,
                                     weak_factory_.GetWeakPtr(), url, referer,
                                     user_agent, cookie_header, out_filename,
                                     download_dir));
    return;
  }

  base::DictValue options;
  if (!download_dir.empty()) {
    options.Set("dir", download_dir.AsUTF8Unsafe());
  }
  if (!out_filename.empty()) {
    options.Set("out", out_filename);
  }
  if (!referer.empty()) {
    options.Set("referer", referer);
  }
  if (!user_agent.empty()) {
    options.Set("user-agent", user_agent);
  }
  base::ListValue headers;
  if (!cookie_header.empty()) {
    headers.Append("Cookie: " + cookie_header);
  }
  if (!headers.empty()) {
    options.Set("header", std::move(headers));
  }

  base::ListValue params;
  base::ListValue uris;
  uris.Append(url.spec());
  params.Append(std::move(uris));
  params.Append(std::move(options));
  Call("aria2.addUri", std::move(params), base::DoNothing());
}

void Aria2Service::Call(const std::string& method,
                        base::ListValue params,
                        RpcCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::ListValue full_params;
  full_params.Append("token:" + rpc_secret_);
  for (auto& p : params) {
    full_params.Append(std::move(p));
  }
  base::DictValue body;
  body.Set("jsonrpc", "2.0");
  body.Set("id", base::NumberToString(base::RandUint64()));
  body.Set("method", method);
  body.Set("params", std::move(full_params));
  std::string json;
  base::JSONWriter::Write(body, &json);

  if (!g_browser_process || !g_browser_process->system_network_context_manager()) {
    std::move(callback).Run(std::nullopt);
    return;
  }

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(rpc_http_url());
  request->method = "POST";
  request->load_flags = net::LOAD_BYPASS_PROXY | net::LOAD_DISABLE_CACHE;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;

  auto loader =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  loader->AttachStringForUpload(json, "application/json");
  loader->SetTimeoutDuration(base::Seconds(10));
  auto* raw = loader.get();
  loaders_.push_back(std::move(loader));
  raw->DownloadToString(
      g_browser_process->system_network_context_manager()
          ->GetURLLoaderFactory(),
      base::BindOnce(&Aria2Service::OnRpcResponse, weak_factory_.GetWeakPtr(),
                     raw, std::move(callback)),
      kMaxRpcResponseBytes);
}

void Aria2Service::OnRpcResponse(network::SimpleURLLoader* loader,
                                 RpcCallback callback,
                                 std::optional<std::string> body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::erase_if(loaders_, [loader](const auto& l) { return l.get() == loader; });

  std::optional<base::Value> result;
  if (body) {
    if (auto parsed = base::JSONReader::Read(*body, base::JSON_PARSE_RFC);
        parsed && parsed->is_dict()) {
      if (base::Value* r = parsed->GetDict().Find("result")) {
        result = std::move(*r);
      } else if (const base::DictValue* err =
                     parsed->GetDict().FindDict("error")) {
        LOG(WARNING) << "Falcon: aria2 RPC error: " << *err;
      }
    }
  }
  std::move(callback).Run(std::move(result));
}

void Aria2Service::Shutdown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  loaders_.clear();
  queued_.clear();
  if (process_.IsValid()) {
    // Ask nicely so the session file is flushed; the job object / the
    // --stop-with-process watchdog finish the job if this races shutdown.
    Call("aria2.shutdown", base::ListValue(), base::DoNothing());
    process_.Close();
  }
  ready_ = false;
  launched_ = false;
}

}  // namespace falcon
