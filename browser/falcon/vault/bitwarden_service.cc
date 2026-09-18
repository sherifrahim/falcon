// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/vault/bitwarden_service.h"

#include <utility>

#include "base/base64.h"
#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/rand_util.h"
#include "base/strings/escape.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/net/system_network_context_manager.h"
#include "chrome/common/chrome_paths.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "net/base/load_flags.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "third_party/re2/src/re2/re2.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>
#endif

namespace falcon {

namespace prefs {

void RegisterBitwardenLocalPrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(kBitwardenEmail, std::string());
  registry->RegisterStringPref(kBitwardenServer, std::string());
  registry->RegisterStringPref(kBitwardenSession, std::string());
  registry->RegisterBooleanPref(kBitwardenSaveNew, false);
  registry->RegisterBooleanPref(kBitwardenRememberSession, true);
}

}  // namespace prefs

struct BitwardenService::LaunchResult {
  base::Process process;
#if BUILDFLAG(IS_WIN)
  base::win::ScopedHandle job;
#endif
  bool exited_early = false;
};

struct BitwardenService::CliResult {
  int exit_code = -1;
  std::string output;
};

namespace {

constexpr int kMaxLaunchAttempts = 3;
constexpr size_t kMaxResponseBytes = 8 * 1024 * 1024;

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("falcon_bitwarden_serve", R"pb(
      semantics {
        sender: "Falcon Passwords (Bitwarden)"
        description:
          "Requests to the local Bitwarden CLI sidecar (`bw serve`) that "
          "holds the user's unlocked vault. Never leaves the machine "
          "(127.0.0.1); the CLI itself talks to Bitwarden's servers."
        trigger: "A login form needs credentials, or the user manages the "
                 "vault in falcon://falcon."
        data: "Site URL, and for saves the new username/password."
        destination: LOCAL
      }
      policy {
        cookies_allowed: NO
        setting: "falcon://falcon > Passwords > Bitwarden > Disconnect."
        policy_exception_justification: "Personal fork; no policy."
      })pb");

PrefService* local_state() {
  return g_browser_process ? g_browser_process->local_state() : nullptr;
}

base::FilePath CliExe() {
  base::FilePath dir;
  base::PathService::Get(base::DIR_EXE, &dir);
  return dir.AppendASCII("bw.exe");
}

base::FilePath VaultDir() {
  base::FilePath dir;
  base::PathService::Get(chrome::DIR_USER_DATA, &dir);
  return dir.AppendASCII("Falcon Vault");
}

base::EnvironmentMap CliEnvironment(const std::string& session) {
  base::EnvironmentMap env;
#if BUILDFLAG(IS_WIN)
  env[L"BITWARDENCLI_APPDATA_DIR"] = VaultDir().value();
  env[L"BW_NOINTERACTION"] = L"true";
  if (!session.empty()) {
    env[L"BW_SESSION"] = base::UTF8ToWide(session);
  }
#else
  env["BITWARDENCLI_APPDATA_DIR"] = VaultDir().value();
  env["BW_NOINTERACTION"] = "true";
  if (!session.empty()) {
    env["BW_SESSION"] = session;
  }
#endif
  return env;
}

// Runs `bw <args>` to completion on a blocking sequence, capturing stdout +
// stderr through a temp file (LaunchOptions carries the environment;
// GetAppOutput cannot). |password_file| (if any) is deleted afterwards.
std::unique_ptr<BitwardenService::CliResult> RunCli(
    std::vector<std::string> args,
    std::string session,
    base::FilePath password_file) {
  auto result = std::make_unique<BitwardenService::CliResult>();
  base::CreateDirectory(VaultDir());
  base::CommandLine cmd(CliExe());
  for (const auto& a : args) {
    cmd.AppendArg(a);
  }
  base::FilePath out_path;
  base::CreateTemporaryFileInDir(VaultDir(), &out_path);
  {
    base::File out(out_path, base::File::FLAG_OPEN_ALWAYS |
                                 base::File::FLAG_WRITE | base::File::FLAG_APPEND);
    base::LaunchOptions options;
    options.environment = CliEnvironment(session);
    options.current_directory = CliExe().DirName();
#if BUILDFLAG(IS_WIN)
    options.start_hidden = true;
    options.stdout_handle = out.GetPlatformFile();
    options.stderr_handle = out.GetPlatformFile();
    options.handles_to_inherit.push_back(out.GetPlatformFile());
#else
    options.fds_to_remap.emplace_back(out.GetPlatformFile(), STDOUT_FILENO);
    options.fds_to_remap.emplace_back(out.GetPlatformFile(), STDERR_FILENO);
#endif
    base::Process process = base::LaunchProcess(cmd, options);
    if (process.IsValid()) {
      int exit_code = -1;
      if (process.WaitForExitWithTimeout(base::Seconds(90), &exit_code)) {
        result->exit_code = exit_code;
      } else {
        process.Terminate(1, false);
        result->output = "timed out";
      }
    } else {
      result->output = "could not start bw.exe";
    }
  }
  std::string captured;
  base::ReadFileToString(out_path, &captured);
  base::DeleteFile(out_path);
  if (!password_file.empty()) {
    base::DeleteFile(password_file);
  }
  if (!captured.empty()) {
    result->output = captured;
  }
  return result;
}

std::unique_ptr<BitwardenService::LaunchResult> LaunchServe(
    base::CommandLine cmd,
    std::string session) {
  auto result = std::make_unique<BitwardenService::LaunchResult>();
  base::CreateDirectory(VaultDir());
  base::LaunchOptions options;
  options.environment = CliEnvironment(session);
  options.current_directory = cmd.GetProgram().DirName();
#if BUILDFLAG(IS_WIN)
  options.start_hidden = true;
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
    int exit_code = 0;
    result->exited_early = result->process.WaitForExitWithTimeout(
        base::Milliseconds(500), &exit_code);
  }
  return result;
}

base::FilePath WritePasswordFile(const std::string& password) {
  base::CreateDirectory(VaultDir());
  base::FilePath path;
  if (!base::CreateTemporaryFileInDir(VaultDir(), &path)) {
    return base::FilePath();
  }
  base::WriteFile(path, password);
  return path;
}

std::string ExtractSessionKey(const std::string& output) {
  // `--raw` prints only the key; without it the CLI prints
  //   $ export BW_SESSION="..." / > $env:BW_SESSION="..."
  std::string key;
  if (RE2::PartialMatch(output, "BW_SESSION=\"([^\"]+)\"", &key)) {
    return key;
  }
  std::vector<std::string> lines = base::SplitString(
      output, "\r\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
    // Session keys are long base64 strings with no spaces.
    if (it->size() > 60 && it->find(' ') == std::string::npos) {
      return *it;
    }
  }
  return std::string();
}

std::string CliError(const std::string& output) {
  std::vector<std::string> lines = base::SplitString(
      output, "\r\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (const auto& l : lines) {
    if (l.find("Could not find") != std::string::npos) {
      continue;  // CLI chatter about creating its data dir
    }
    return l;
  }
  return "Bitwarden CLI failed";
}

}  // namespace

// static
BitwardenService* BitwardenService::Get() {
  static base::NoDestructor<BitwardenService> instance;
  return instance.get();
}

BitwardenService::BitwardenService() = default;
BitwardenService::~BitwardenService() = default;

bool BitwardenService::IsConfigured() const {
  PrefService* prefs = local_state();
  return prefs && !prefs->GetString(prefs::kBitwardenEmail).empty();
}

void BitwardenService::EnsureEncryptor(base::OnceClosure then) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (encryptor_) {
    std::move(then).Run();
    return;
  }
  pending_encryptor_.push_back(std::move(then));
  if (pending_encryptor_.size() > 1) {
    return;  // request in flight
  }
  if (!g_browser_process || !g_browser_process->os_crypt_async()) {
    OnEncryptor(nullptr);
    return;
  }
  g_browser_process->os_crypt_async()->GetInstance(base::BindOnce(
      &BitwardenService::OnEncryptor, weak_factory_.GetWeakPtr()));
}

void BitwardenService::OnEncryptor(
    scoped_refptr<os_crypt_async::Encryptor> encryptor) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  encryptor_ = std::move(encryptor);
  for (auto& cb : std::exchange(pending_encryptor_, {})) {
    std::move(cb).Run();
  }
}

std::string BitwardenService::LoadSessionKey() const {
  PrefService* prefs = local_state();
  if (!prefs || !encryptor_) {
    return std::string();
  }
  const std::string b64 = prefs->GetString(prefs::kBitwardenSession);
  if (b64.empty()) {
    return std::string();
  }
  std::string cipher;
  std::string plain;
  if (!base::Base64Decode(b64, &cipher) ||
      !encryptor_->DecryptString(cipher, &plain)) {
    return std::string();
  }
  return plain;
}

void BitwardenService::StoreSessionKey(const std::string& key) {
  PrefService* prefs = local_state();
  if (!prefs) {
    return;
  }
  if (key.empty() || !prefs->GetBoolean(prefs::kBitwardenRememberSession)) {
    prefs->ClearPref(prefs::kBitwardenSession);
    return;
  }
  if (!encryptor_) {
    // Rare: sealing needs the encryptor; fetch it and retry once.
    EnsureEncryptor(base::BindOnce(&BitwardenService::StoreSessionKey,
                                   weak_factory_.GetWeakPtr(), key));
    return;
  }
  std::string cipher;
  if (encryptor_->EncryptString(key, &cipher)) {
    prefs->SetString(prefs::kBitwardenSession, base::Base64Encode(cipher));
  }
}

void BitwardenService::Connect(const std::string& email,
                               const std::string& master_password,
                               const std::string& totp_code,
                               const std::string& server_url,
                               ResultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (email.empty() || master_password.empty()) {
    std::move(callback).Run(false, "Email and master password are required.");
    return;
  }
  StopServing();
  // `bw config server` (self-hosted) then `bw login` — both blocking, chained
  // on the pool.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::WithBaseSyncPrimitives(),
       base::TaskPriority::USER_VISIBLE},
      base::BindOnce(
          [](std::string email, std::string password, std::string totp,
             std::string server) {
            // A previous account may be logged in: log out first (ignored
            // if not).
            RunCli({"logout"}, std::string(), base::FilePath());
            if (!server.empty()) {
              auto cfg = RunCli({"config", "server", server}, std::string(),
                                base::FilePath());
              if (cfg->exit_code != 0) {
                return cfg;
              }
            }
            base::FilePath pw = WritePasswordFile(password);
            std::vector<std::string> args = {"login", email, "--passwordfile",
                                             pw.AsUTF8Unsafe(), "--raw"};
            if (!totp.empty()) {
              args.insert(args.end(), {"--method", "0", "--code", totp});
            }
            return RunCli(std::move(args), std::string(), pw);
          },
          email, master_password, totp_code, server_url),
      base::BindOnce(&BitwardenService::OnLoginDone, weak_factory_.GetWeakPtr(),
                     email, server_url, std::move(callback)));
}

void BitwardenService::OnLoginDone(const std::string& email,
                                   const std::string& server,
                                   ResultCallback callback,
                                   std::unique_ptr<CliResult> result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (result->exit_code != 0) {
    std::move(callback).Run(false, CliError(result->output));
    return;
  }
  const std::string session = ExtractSessionKey(result->output);
  if (PrefService* prefs = local_state()) {
    prefs->SetString(prefs::kBitwardenEmail, email);
    prefs->SetString(prefs::kBitwardenServer, server);
  }
  StoreSessionKey(session);
  launch_attempts_ = 0;
  EnsureServing();
  std::move(callback).Run(true, std::string());
}

void BitwardenService::Disconnect(ResultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  StopServing();
  if (PrefService* prefs = local_state()) {
    prefs->ClearPref(prefs::kBitwardenEmail);
    prefs->ClearPref(prefs::kBitwardenServer);
    prefs->ClearPref(prefs::kBitwardenSession);
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::WithBaseSyncPrimitives(),
       base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&RunCli, std::vector<std::string>{"logout"},
                     std::string(), base::FilePath()),
      base::BindOnce(&BitwardenService::OnLogoutDone,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void BitwardenService::OnLogoutDone(ResultCallback callback,
                                    std::unique_ptr<CliResult>) {
  std::move(callback).Run(true, std::string());
}

void BitwardenService::EnsureServing() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsConfigured() || serving_ || launching_) {
    return;
  }
  if (launch_attempts_ >= kMaxLaunchAttempts) {
    return;
  }
  launching_ = true;
  EnsureEncryptor(
      base::BindOnce(&BitwardenService::Launch, weak_factory_.GetWeakPtr()));
}

void BitwardenService::Launch() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ++launch_attempts_;
  launching_ = true;
  if (!IsConfigured()) {
    launching_ = false;
    return;
  }
  port_ = 47000 + static_cast<int>(base::RandGenerator(2000));
  base::CommandLine cmd(CliExe());
  cmd.AppendArg("serve");
  cmd.AppendArg("--hostname");
  cmd.AppendArg("127.0.0.1");
  cmd.AppendArg("--port");
  cmd.AppendArg(base::NumberToString(port_));
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::WithBaseSyncPrimitives(),
       base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&LaunchServe, cmd, LoadSessionKey()),
      base::BindOnce(&BitwardenService::OnLaunched,
                     weak_factory_.GetWeakPtr()));
}

void BitwardenService::OnLaunched(std::unique_ptr<LaunchResult> result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  launching_ = false;
  if (!result->process.IsValid() || result->exited_early) {
    LOG(WARNING) << "Falcon: bw serve failed to start on port " << port_
                 << " (attempt " << launch_attempts_ << ")";
    if (launch_attempts_ < kMaxLaunchAttempts) {
      Launch();
    }
    return;
  }
  process_ = std::move(result->process);
#if BUILDFLAG(IS_WIN)
  job_ = std::move(result->job);
#endif
  serving_ = true;
  launch_attempts_ = 0;
  // The CLI needs a moment before it answers.
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&BitwardenService::ProbeStatus,
                     weak_factory_.GetWeakPtr()),
      base::Milliseconds(900));
}

void BitwardenService::ProbeStatus() {
  Request("GET", "/status", std::nullopt,
          base::BindOnce(&BitwardenService::OnProbeStatus,
                         weak_factory_.GetWeakPtr()));
}

void BitwardenService::OnProbeStatus(std::optional<base::Value> json,
                                     std::string error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!json) {
    // Not up yet; try a few more times.
    if (serving_ && probe_retries_++ < 8) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&BitwardenService::ProbeStatus,
                         weak_factory_.GetWeakPtr()),
          base::Seconds(1));
    }
    return;
  }
  const base::DictValue* tpl = nullptr;
  if (json->is_dict()) {
    if (const base::DictValue* data = json->GetDict().FindDict("data")) {
      tpl = data->FindDict("template");
    }
  }
  if (tpl) {
    if (const std::string* s = tpl->FindString("status")) {
      state_ = *s;
    }
    if (const std::string* s = tpl->FindString("lastSync")) {
      last_sync_ = *s;
    }
  }
  probe_retries_ = 0;
  unlocked_ = state_ == "unlocked";
  for (auto& cb : std::exchange(pending_when_ready_, {})) {
    std::move(cb).Run();
  }
}

void BitwardenService::GetStatus(StatusCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Status st;
  st.configured = IsConfigured();
  st.serving = serving_;
  st.state = st.configured ? state_ : "unauthenticated";
  st.last_sync = last_sync_;
  if (PrefService* prefs = local_state()) {
    st.email = prefs->GetString(prefs::kBitwardenEmail);
    st.server = prefs->GetString(prefs::kBitwardenServer);
  }
  if (!serving_) {
    EnsureServing();
    std::move(callback).Run(st);
    return;
  }
  Request("GET", "/status", std::nullopt,
          base::BindOnce(
              [](Status st, StatusCallback cb, base::WeakPtr<BitwardenService> self,
                 std::optional<base::Value> json, std::string error) {
                if (self) {
                  self->OnProbeStatus(json ? std::make_optional(json->Clone())
                                           : std::nullopt,
                                      error);
                  st.state = self->state_;
                  st.last_sync = self->last_sync_;
                  st.serving = self->serving_;
                }
                st.error = error;
                std::move(cb).Run(st);
              },
              st, std::move(callback), weak_factory_.GetWeakPtr()));
}

void BitwardenService::Unlock(const std::string& master_password,
                              ResultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!serving_) {
    EnsureServing();
    std::move(callback).Run(false, "Vault sidecar is starting; try again.");
    return;
  }
  base::DictValue body;
  body.Set("password", master_password);
  std::string json;
  base::JSONWriter::Write(body, &json);
  Request("POST", "/unlock", json,
          base::BindOnce(&BitwardenService::OnUnlockResponse,
                         weak_factory_.GetWeakPtr(), std::move(callback)));
}

void BitwardenService::OnUnlockResponse(ResultCallback callback,
                                        std::optional<base::Value> json,
                                        std::string error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  bool ok = false;
  std::string session;
  if (json && json->is_dict()) {
    ok = json->GetDict().FindBool("success").value_or(false);
    if (const base::DictValue* data = json->GetDict().FindDict("data")) {
      if (const std::string* raw = data->FindString("raw")) {
        session = *raw;
      }
    }
    if (!ok) {
      if (const std::string* msg = json->GetDict().FindString("message")) {
        error = *msg;
      }
    }
  }
  if (ok) {
    unlocked_ = true;
    state_ = "unlocked";
    if (!session.empty()) {
      StoreSessionKey(session);
    }
    for (auto& cb : std::exchange(pending_when_ready_, {})) {
      std::move(cb).Run();
    }
  }
  std::move(callback).Run(ok, ok ? std::string()
                                 : (error.empty() ? "Unlock failed" : error));
}

void BitwardenService::Lock(ResultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  unlocked_ = false;
  state_ = "locked";
  StoreSessionKey(std::string());
  Request("POST", "/lock", std::string("{}"),
          base::BindOnce(
              [](ResultCallback cb, std::optional<base::Value>, std::string e) {
                std::move(cb).Run(true, std::string());
              },
              std::move(callback)));
}

void BitwardenService::Sync(ResultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Request("POST", "/sync", std::string("{}"),
          base::BindOnce(
              [](ResultCallback cb, base::WeakPtr<BitwardenService> self,
                 std::optional<base::Value> json, std::string e) {
                bool ok = json && json->is_dict() &&
                          json->GetDict().FindBool("success").value_or(false);
                if (ok && self) {
                  self->ProbeStatus();
                }
                std::move(cb).Run(ok, ok ? std::string()
                                         : (e.empty() ? "Sync failed" : e));
              },
              std::move(callback), weak_factory_.GetWeakPtr()));
}

void BitwardenService::GetLoginsForUrl(const GURL& url,
                                       LoginsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady() || !url.is_valid()) {
    std::move(callback).Run({});
    return;
  }
  const std::string origin = url.GetWithEmptyPath().spec();
  Request("GET", "/list/object/items?url=" + base::EscapeQueryParamValue(origin, true),
          std::nullopt,
          base::BindOnce(
              [](LoginsCallback cb, std::optional<base::Value> json,
                 std::string error) {
                std::vector<Login> out;
                if (json && json->is_dict()) {
                  const base::DictValue* data = json->GetDict().FindDict("data");
                  const base::ListValue* items =
                      data ? data->FindList("data") : nullptr;
                  if (items) {
                    for (const base::Value& v : *items) {
                      if (!v.is_dict()) {
                        continue;
                      }
                      const base::DictValue& item = v.GetDict();
                      const base::DictValue* login = item.FindDict("login");
                      if (!login) {
                        continue;
                      }
                      Login l;
                      if (const std::string* s = item.FindString("id")) {
                        l.id = *s;
                      }
                      if (const std::string* s = item.FindString("name")) {
                        l.name = *s;
                      }
                      if (const std::string* s = login->FindString("username")) {
                        l.username = *s;
                      }
                      if (const std::string* s = login->FindString("password")) {
                        l.password = *s;
                      }
                      if (const base::ListValue* uris = login->FindList("uris")) {
                        for (const base::Value& u : *uris) {
                          if (const std::string* s =
                                  u.is_dict() ? u.GetDict().FindString("uri")
                                              : nullptr) {
                            l.uris.push_back(*s);
                          }
                        }
                      }
                      if (!l.password.empty()) {
                        out.push_back(std::move(l));
                      }
                    }
                  }
                }
                std::move(cb).Run(std::move(out));
              },
              std::move(callback)));
}

void BitwardenService::CreateLogin(const GURL& url,
                                   const std::string& username,
                                   const std::string& password,
                                   ResultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    std::move(callback).Run(false, "Vault is locked");
    return;
  }
  base::DictValue uri;
  uri.Set("uri", url.GetWithEmptyPath().spec());
  uri.Set("match", base::Value());
  base::ListValue uris;
  uris.Append(std::move(uri));
  base::DictValue login;
  login.Set("username", username);
  login.Set("password", password);
  login.Set("uris", std::move(uris));
  login.Set("totp", base::Value());
  base::DictValue item;
  item.Set("type", 1);
  item.Set("name", url.host());
  item.Set("notes", base::Value());
  item.Set("favorite", false);
  item.Set("login", std::move(login));
  item.Set("organizationId", base::Value());
  item.Set("folderId", base::Value());
  item.Set("collectionIds", base::ListValue());
  item.Set("reprompt", 0);
  std::string json;
  base::JSONWriter::Write(item, &json);
  Request("POST", "/object/item", json,
          base::BindOnce(
              [](ResultCallback cb, std::optional<base::Value> j, std::string e) {
                bool ok = j && j->is_dict() &&
                          j->GetDict().FindBool("success").value_or(false);
                std::move(cb).Run(ok, ok ? std::string()
                                         : (e.empty() ? "Save failed" : e));
              },
              std::move(callback)));
}

std::string BitwardenService::BaseUrl() const {
  return "http://127.0.0.1:" + base::NumberToString(port_);
}

void BitwardenService::Request(const std::string& method,
                               const std::string& path,
                               std::optional<std::string> json_body,
                               JsonCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!serving_ || !g_browser_process ||
      !g_browser_process->system_network_context_manager()) {
    std::move(callback).Run(std::nullopt, "vault sidecar not running");
    return;
  }
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(BaseUrl() + path);
  request->method = method;
  request->load_flags = net::LOAD_BYPASS_PROXY | net::LOAD_DISABLE_CACHE;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  auto loader =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  if (json_body) {
    loader->AttachStringForUpload(*json_body, "application/json");
  }
  loader->SetTimeoutDuration(base::Seconds(30));
  auto* raw = loader.get();
  loaders_.push_back(std::move(loader));
  raw->DownloadToString(
      g_browser_process->system_network_context_manager()
          ->GetURLLoaderFactory(),
      base::BindOnce(&BitwardenService::OnResponse, weak_factory_.GetWeakPtr(),
                     raw, std::move(callback)),
      kMaxResponseBytes);
}

void BitwardenService::OnResponse(network::SimpleURLLoader* loader,
                                  JsonCallback callback,
                                  std::optional<std::string> body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::erase_if(loaders_, [loader](const auto& l) { return l.get() == loader; });
  if (!body) {
    std::move(callback).Run(std::nullopt, "no response from vault sidecar");
    return;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(*body, base::JSON_PARSE_RFC);
  if (!parsed) {
    std::move(callback).Run(std::nullopt, "bad response from vault sidecar");
    return;
  }
  std::string error;
  if (parsed->is_dict() &&
      !parsed->GetDict().FindBool("success").value_or(true)) {
    if (const std::string* m = parsed->GetDict().FindString("message")) {
      error = *m;
    }
  }
  std::move(callback).Run(std::move(parsed), error);
}

void BitwardenService::StopServing() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  serving_ = false;
  unlocked_ = false;
  loaders_.clear();
  if (process_.IsValid()) {
    process_.Terminate(0, false);
    process_.Close();
  }
#if BUILDFLAG(IS_WIN)
  job_.Close();
#endif
}

void BitwardenService::Shutdown() {
  StopServing();
}

}  // namespace falcon
