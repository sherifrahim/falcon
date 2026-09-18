// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/vault/composite_password_store_backend.h"

#include <utility>
#include <variant>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "brave/browser/falcon/vault/bitwarden_service.h"
#include "chrome/browser/browser_process.h"
#include "components/password_manager/core/browser/password_form.h"
#include "components/password_manager/core/browser/password_form_digest.h"
#include "components/password_manager/core/browser/password_store/password_store_consumer.h"
#include "components/password_manager/core/browser/password_store/stored_credential.h"
#include "components/password_manager/core/browser/password_string.h"
#include "components/prefs/pref_service.h"
#include "components/sync/model/data_type_controller_delegate.h"
#include "url/gurl.h"

namespace falcon {

namespace {

constexpr char kVaultMarker[] = "bitwarden:";

// Bitwarden already matched the item's URIs against the page; we present it
// as an exact match for the origin the form lives on.
password_manager::StoredCredential ToCredential(
    const BitwardenService::Login& login,
    const GURL& page_url) {
  password_manager::StoredCredential cred;
  cred.scheme = password_manager::PasswordForm::Scheme::kHtml;
  const GURL origin = page_url.GetWithEmptyPath();
  cred.url = origin;
  cred.signon_realm = origin.spec();
  cred.username_value = base::UTF8ToUTF16(login.username);
  cred.password_value =
      password_manager::PasswordString(base::UTF8ToUTF16(login.password));
  cred.display_name = base::UTF8ToUTF16(login.name);
  cred.date_created = base::Time::Now();
  cred.date_last_used = base::Time::Now();
  cred.in_store = password_manager::PasswordForm::Store::kProfileStore;
  cred.match_type = password_manager::PasswordForm::MatchType::kExact;
  cred.type = password_manager::PasswordForm::Type::kManuallyAdded;
  cred.keychain_identifier = kVaultMarker + login.id;
  return cred;
}

}  // namespace

CompositePasswordStoreBackend::CompositePasswordStoreBackend(
    std::unique_ptr<password_manager::PasswordStoreBackend> inner)
    : inner_(std::move(inner)) {}

CompositePasswordStoreBackend::~CompositePasswordStoreBackend() = default;

// static
bool CompositePasswordStoreBackend::IsVaultCredential(
    const password_manager::StoredCredential& cred) {
  return cred.keychain_identifier.starts_with(kVaultMarker);
}

void CompositePasswordStoreBackend::InitBackend(
    RemoteChangesReceived remote_form_changes_received,
    base::RepeatingClosure sync_enabled_or_disabled_cb,
    base::OnceCallback<void(bool)> completion) {
  BitwardenService::Get()->EnsureServing();
  inner_->InitBackend(std::move(remote_form_changes_received),
                      std::move(sync_enabled_or_disabled_cb),
                      std::move(completion));
}

void CompositePasswordStoreBackend::Shutdown(
    base::OnceClosure shutdown_completed) {
  inner_->Shutdown(std::move(shutdown_completed));
}

password_manager::ActionableError CompositePasswordStoreBackend::GetError() {
  return inner_->GetError();
}

void CompositePasswordStoreBackend::GetAllLoginsAsync(
    password_manager::BackendLoginsOrErrorReply callback) {
  inner_->GetAllLoginsAsync(std::move(callback));
}

void CompositePasswordStoreBackend::GetAllLoginsWithAffiliationAndBrandingAsync(
    password_manager::BackendLoginsOrErrorReply callback) {
  inner_->GetAllLoginsWithAffiliationAndBrandingAsync(std::move(callback));
}

void CompositePasswordStoreBackend::GetAutofillableLoginsAsync(
    password_manager::BackendLoginsOrErrorReply callback) {
  inner_->GetAutofillableLoginsAsync(std::move(callback));
}

void CompositePasswordStoreBackend::FillMatchingLoginsAsync(
    password_manager::BackendLoginsOrErrorReply callback,
    bool include_psl,
    const std::vector<password_manager::PasswordFormDigest>& forms) {
  GURL url;
  if (!forms.empty()) {
    url = forms.front().url;
  }
  inner_->FillMatchingLoginsAsync(
      base::BindOnce(&CompositePasswordStoreBackend::MergeVault,
                     weak_factory_.GetWeakPtr(), url, std::move(callback)),
      include_psl, forms);
}

void CompositePasswordStoreBackend::GetGroupedMatchingLoginsAsync(
    const password_manager::PasswordFormDigest& form_digest,
    password_manager::BackendLoginsOrErrorReply callback) {
  inner_->GetGroupedMatchingLoginsAsync(
      form_digest,
      base::BindOnce(&CompositePasswordStoreBackend::MergeVault,
                     weak_factory_.GetWeakPtr(), form_digest.url,
                     std::move(callback)));
}

void CompositePasswordStoreBackend::MergeVault(
    const GURL& url,
    password_manager::BackendLoginsOrErrorReply callback,
    password_manager::LoginsResultOrError inner_result) {
  auto* vault = BitwardenService::Get();
  if (!vault->IsReady() || !url.is_valid() || !url.SchemeIsHTTPOrHTTPS() ||
      std::holds_alternative<password_manager::PasswordStoreBackendError>(
          inner_result)) {
    std::move(callback).Run(std::move(inner_result));
    return;
  }
  vault->GetLoginsForUrl(
      url,
      base::BindOnce(
          [](GURL url, password_manager::LoginsResultOrError inner_result,
             password_manager::BackendLoginsOrErrorReply callback,
             std::vector<BitwardenService::Login> logins) {
            auto& list = std::get<password_manager::LoginsResult>(inner_result);
            for (const auto& login : logins) {
              // Skip vault entries that duplicate a local login.
              const std::u16string user = base::UTF8ToUTF16(login.username);
              bool dup = false;
              for (const auto& c : list) {
                if (c.username_value == user &&
                    c.password_value == base::UTF8ToUTF16(login.password)) {
                  dup = true;
                  break;
                }
              }
              if (!dup) {
                list.push_back(ToCredential(login, url));
              }
            }
            std::move(callback).Run(std::move(inner_result));
          },
          url, std::move(inner_result), std::move(callback)));
}

void CompositePasswordStoreBackend::AddLoginAsync(
    password_manager::StoredCredential cred,
    password_manager::PasswordChangesOrErrorReply callback) {
  auto* vault = BitwardenService::Get();
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  if (vault->IsReady() && local_state &&
      local_state->GetBoolean(prefs::kBitwardenSaveNew) &&
      cred.scheme == password_manager::PasswordForm::Scheme::kHtml &&
      !cred.blocked_by_user) {
    vault->CreateLogin(cred.url, base::UTF16ToUTF8(cred.username_value),
                       base::UTF16ToUTF8(cred.password_value.value()),
                       base::DoNothing());
  }
  inner_->AddLoginAsync(std::move(cred), std::move(callback));
}

void CompositePasswordStoreBackend::UpdateLoginAsync(
    password_manager::StoredCredential cred,
    password_manager::PasswordChangesOrErrorReply callback) {
  if (IsVaultCredential(cred)) {
    // Vault entries are read-only here (usage bookkeeping etc. is dropped).
    std::move(callback).Run(password_manager::PasswordChanges(
        password_manager::PasswordStoreChangeList()));
    return;
  }
  inner_->UpdateLoginAsync(std::move(cred), std::move(callback));
}

void CompositePasswordStoreBackend::RemoveLoginAsync(
    const base::Location& location,
    password_manager::StoredCredential cred,
    password_manager::PasswordChangesOrErrorReply callback) {
  if (IsVaultCredential(cred)) {
    std::move(callback).Run(password_manager::PasswordChanges(
        password_manager::PasswordStoreChangeList()));
    return;
  }
  inner_->RemoveLoginAsync(location, std::move(cred), std::move(callback));
}

void CompositePasswordStoreBackend::RemoveLoginsCreatedBetweenAsync(
    const base::Location& location,
    base::Time delete_begin,
    base::Time delete_end,
    password_manager::PasswordChangesOrErrorReply callback) {
  inner_->RemoveLoginsCreatedBetweenAsync(location, delete_begin, delete_end,
                                          std::move(callback));
}

void CompositePasswordStoreBackend::DisableAutoSignInForOriginsAsync(
    const base::RepeatingCallback<bool(const GURL&)>& origin_filter,
    base::OnceClosure completion) {
  inner_->DisableAutoSignInForOriginsAsync(origin_filter,
                                          std::move(completion));
}

password_manager::SmartBubbleStatsStore*
CompositePasswordStoreBackend::GetSmartBubbleStatsStore() {
  return inner_->GetSmartBubbleStatsStore();
}

std::unique_ptr<syncer::DataTypeControllerDelegate>
CompositePasswordStoreBackend::CreateSyncControllerDelegate() {
  return inner_->CreateSyncControllerDelegate();
}

void CompositePasswordStoreBackend::OnSyncServiceInitialized(
    syncer::SyncService* sync_service) {
  inner_->OnSyncServiceInitialized(sync_service);
}

base::WeakPtr<password_manager::PasswordStoreBackend>
CompositePasswordStoreBackend::AsWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

}  // namespace falcon
