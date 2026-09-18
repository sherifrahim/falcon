// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_VAULT_BITWARDEN_SERVICE_H_
#define BRAVE_BROWSER_FALCON_VAULT_BITWARDEN_SERVICE_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/process/process.h"
#include "base/sequence_checker.h"
#include "base/values.h"
#include "base/win/scoped_handle.h"
#include "build/build_config.h"

class GURL;
class PrefRegistrySimple;

namespace network {
class SimpleURLLoader;
}  // namespace network

namespace os_crypt_async {
class Encryptor;
}  // namespace os_crypt_async

namespace falcon {

namespace prefs {
// Local-state prefs (one vault per Windows account, like the CLI itself).
inline constexpr char kBitwardenEmail[] = "falcon.vault.bitwarden.email";
inline constexpr char kBitwardenServer[] = "falcon.vault.bitwarden.server";
// OSCrypt-encrypted, base64: the CLI session key. Lets `bw serve` start
// unlocked so autofill works right after launch (like the built-in store,
// protected by the Windows login). Cleared by Disconnect / "forget".
inline constexpr char kBitwardenSession[] = "falcon.vault.bitwarden.session";
inline constexpr char kBitwardenSaveNew[] = "falcon.vault.bitwarden.save_new";
inline constexpr char kBitwardenRememberSession[] =
    "falcon.vault.bitwarden.remember_session";
void RegisterBitwardenLocalPrefs(PrefRegistrySimple* registry);
}  // namespace prefs

// Bitwarden as a Falcon password source. Drives the official Bitwarden CLI
// (`bw`, shipped next to brave.exe) as a sidecar: `bw login` once, then
// `bw serve` on 127.0.0.1 exposes the unlocked vault as a small REST API that
// this class reads logins from (and writes new ones to). Nothing but the CLI
// talks to Bitwarden's servers.
class BitwardenService {
 public:
  struct Status {
    bool configured = false;   // logged in (session/tokens on disk)
    bool serving = false;      // bw serve is up
    std::string state;         // "unauthenticated" | "locked" | "unlocked"
    std::string email;
    std::string server;
    std::string last_sync;
    std::string error;
  };
  struct Login {
    std::string id;
    std::string name;
    std::string username;
    std::string password;
    std::vector<std::string> uris;
  };
  using StatusCallback = base::OnceCallback<void(Status)>;
  using ResultCallback = base::OnceCallback<void(bool ok, std::string error)>;
  using LoginsCallback = base::OnceCallback<void(std::vector<Login>)>;

  static BitwardenService* Get();

  // Results of the blocking helpers (public: used by free functions).
  struct LaunchResult;
  struct CliResult;

  BitwardenService(const BitwardenService&) = delete;
  BitwardenService& operator=(const BitwardenService&) = delete;

  bool IsConfigured() const;
  // True when logins can be fetched right now (serving + unlocked).
  bool IsReady() const { return serving_ && unlocked_; }

  // `bw login` (+ optional TOTP / self-hosted server), then start serving.
  void Connect(const std::string& email,
               const std::string& master_password,
               const std::string& totp_code,
               const std::string& server_url,
               ResultCallback callback);
  // `bw logout`, forget the session, stop serving.
  void Disconnect(ResultCallback callback);

  // Starts `bw serve` if configured and not running. Safe to call often.
  void EnsureServing();
  void GetStatus(StatusCallback callback);
  void Unlock(const std::string& master_password, ResultCallback callback);
  void Lock(ResultCallback callback);
  void Sync(ResultCallback callback);

  // Logins whose URIs match |url| (Bitwarden's own URI match rules).
  void GetLoginsForUrl(const GURL& url, LoginsCallback callback);
  void CreateLogin(const GURL& url,
                   const std::string& username,
                   const std::string& password,
                   ResultCallback callback);

  void Shutdown();

 private:
  friend class base::NoDestructor<BitwardenService>;
  using JsonCallback =
      base::OnceCallback<void(std::optional<base::Value> json,
                              std::string error)>;

  BitwardenService();
  ~BitwardenService();

  void OnLoginDone(const std::string& email,
                   const std::string& server,
                   ResultCallback callback,
                   std::unique_ptr<CliResult> result);
  void OnLogoutDone(ResultCallback callback, std::unique_ptr<CliResult>);
  void Launch();
  void OnLaunched(std::unique_ptr<LaunchResult> result);
  void ProbeStatus();
  void OnProbeStatus(std::optional<base::Value> json, std::string error);
  void OnUnlockResponse(ResultCallback callback,
                        std::optional<base::Value> json,
                        std::string error);
  void StopServing();

  // REST to bw serve.
  void Request(const std::string& method,
               const std::string& path,
               std::optional<std::string> json_body,
               JsonCallback callback);
  void OnResponse(network::SimpleURLLoader* loader,
                  JsonCallback callback,
                  std::optional<std::string> body);
  std::string BaseUrl() const;

  std::string LoadSessionKey() const;
  void StoreSessionKey(const std::string& key);
  // The session key is sealed with OSCrypt (DPAPI); the encryptor arrives
  // asynchronously, so anything that needs it queues behind EnsureEncryptor.
  void EnsureEncryptor(base::OnceClosure then);
  void OnEncryptor(scoped_refptr<os_crypt_async::Encryptor> encryptor);

  int port_ = 0;
  bool serving_ = false;
  bool unlocked_ = false;
  bool launching_ = false;
  int launch_attempts_ = 0;
  int probe_retries_ = 0;
  std::string state_ = "unauthenticated";
  std::string last_sync_;
  base::Process process_;
#if BUILDFLAG(IS_WIN)
  base::win::ScopedHandle job_;
#endif
  scoped_refptr<os_crypt_async::Encryptor> encryptor_;
  std::vector<base::OnceClosure> pending_encryptor_;
  std::vector<std::unique_ptr<network::SimpleURLLoader>> loaders_;
  std::vector<base::OnceClosure> pending_when_ready_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<BitwardenService> weak_factory_{this};
};

}  // namespace falcon

#endif  // BRAVE_BROWSER_FALCON_VAULT_BITWARDEN_SERVICE_H_
