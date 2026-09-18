// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_VAULT_COMPOSITE_PASSWORD_STORE_BACKEND_H_
#define BRAVE_BROWSER_FALCON_VAULT_COMPOSITE_PASSWORD_STORE_BACKEND_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "components/password_manager/core/browser/password_store/password_store_backend.h"

namespace falcon {

// Falcon Passwords = the built-in store (local logins, Brave/Google imports)
// plus connected vaults. This backend wraps the built-in one and merges
// Bitwarden logins into the fill paths, so autofill and the save bubble see
// one list. Vault entries are marked (keychain_identifier "bitwarden:<id>")
// and are read-only here; new logins can optionally be mirrored to the vault.
class CompositePasswordStoreBackend
    : public password_manager::PasswordStoreBackend {
 public:
  explicit CompositePasswordStoreBackend(
      std::unique_ptr<password_manager::PasswordStoreBackend> inner);
  ~CompositePasswordStoreBackend() override;

  static bool IsVaultCredential(
      const password_manager::StoredCredential& cred);

  // password_manager::PasswordStoreBackend:
  void InitBackend(RemoteChangesReceived remote_form_changes_received,
                   base::RepeatingClosure sync_enabled_or_disabled_cb,
                   base::OnceCallback<void(bool)> completion) override;
  void Shutdown(base::OnceClosure shutdown_completed) override;
  password_manager::ActionableError GetError() override;
  void GetAllLoginsAsync(
      password_manager::BackendLoginsOrErrorReply callback) override;
  void GetAllLoginsWithAffiliationAndBrandingAsync(
      password_manager::BackendLoginsOrErrorReply callback) override;
  void GetAutofillableLoginsAsync(
      password_manager::BackendLoginsOrErrorReply callback) override;
  void FillMatchingLoginsAsync(
      password_manager::BackendLoginsOrErrorReply callback,
      bool include_psl,
      const std::vector<password_manager::PasswordFormDigest>& forms) override;
  void GetGroupedMatchingLoginsAsync(
      const password_manager::PasswordFormDigest& form_digest,
      password_manager::BackendLoginsOrErrorReply callback) override;
  void AddLoginAsync(
      password_manager::StoredCredential cred,
      password_manager::PasswordChangesOrErrorReply callback) override;
  void UpdateLoginAsync(
      password_manager::StoredCredential cred,
      password_manager::PasswordChangesOrErrorReply callback) override;
  void RemoveLoginAsync(
      const base::Location& location,
      password_manager::StoredCredential cred,
      password_manager::PasswordChangesOrErrorReply callback) override;
  void RemoveLoginsCreatedBetweenAsync(
      const base::Location& location,
      base::Time delete_begin,
      base::Time delete_end,
      password_manager::PasswordChangesOrErrorReply callback) override;
  void DisableAutoSignInForOriginsAsync(
      const base::RepeatingCallback<bool(const GURL&)>& origin_filter,
      base::OnceClosure completion) override;
  password_manager::SmartBubbleStatsStore* GetSmartBubbleStatsStore() override;
  std::unique_ptr<syncer::DataTypeControllerDelegate>
  CreateSyncControllerDelegate() override;
  void OnSyncServiceInitialized(syncer::SyncService* sync_service) override;
  base::WeakPtr<PasswordStoreBackend> AsWeakPtr() override;

 private:
  // Appends vault logins for |url| to the inner result, then replies.
  void MergeVault(const GURL& url,
                  password_manager::BackendLoginsOrErrorReply callback,
                  password_manager::LoginsResultOrError inner_result);

  std::unique_ptr<password_manager::PasswordStoreBackend> inner_;
  base::WeakPtrFactory<CompositePasswordStoreBackend> weak_factory_{this};
};

}  // namespace falcon

#endif  // BRAVE_BROWSER_FALCON_VAULT_COMPOSITE_PASSWORD_STORE_BACKEND_H_
