// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/shields/adblock_seed.h"

#include <memory>
#include <string>
#include <utility>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "brave/components/brave_shields/content/browser/ad_block_service.h"
#include "brave/components/brave_shields/content/browser/ad_block_subscription_service_manager.h"
#include "brave/components/brave_shields/core/browser/ad_block_default_resource_provider.h"
#include "chrome/browser/browser_process.h"
#include "chrome/common/chrome_paths.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "url/gurl.h"

namespace falcon {

namespace {

constexpr char kSeededPref[] = "falcon.adblock.seeded";
constexpr char kResourcesFetchedPref[] = "falcon.adblock.resources_fetched";
constexpr char kResourcesDir[] = "Falcon Adblock";
constexpr char kResourcesFile[] = "resources.json";
// Scriptlets / redirect resources Brave ships as the "Ad Block Resources
// Library" component; the same file, from its public repository.
constexpr char kResourcesUrl[] =
    "https://raw.githubusercontent.com/brave/adblock-resources/master/dist/"
    "resources.json";
constexpr base::TimeDelta kResourcesMaxAge = base::Days(1);
constexpr size_t kResourcesMaxBytes = 8 * 1024 * 1024;

// The lists Brave compiles into its default component, from their public homes.
constexpr const char* kLists[] = {
    "https://easylist.to/easylist/easylist.txt",
    "https://easylist.to/easylist/easyprivacy.txt",
    "https://raw.githubusercontent.com/uBlockOrigin/uAssets/master/filters/unbreak.txt",
    "https://raw.githubusercontent.com/brave/adblock-lists/master/brave-lists/brave-specific.txt",
    "https://raw.githubusercontent.com/brave/adblock-lists/master/brave-lists/brave-unbreak.txt",
};

base::FilePath ResourcesDir() {
  base::FilePath user_data;
  base::PathService::Get(chrome::DIR_USER_DATA, &user_data);
  return user_data.AppendASCII(kResourcesDir);
}

// Blocking: writes the downloaded JSON next to the previous copy.
bool WriteResources(const base::FilePath& dir, const std::string& body) {
  return base::CreateDirectory(dir) &&
         base::WriteFile(dir.AppendASCII(kResourcesFile), body);
}

void OnResourcesWritten(base::WeakPtr<brave_shields::AdBlockService> service,
                        base::FilePath dir,
                        bool ok) {
  if (!ok || !service) {
    return;
  }
  g_browser_process->local_state()->SetTime(kResourcesFetchedPref,
                                            base::Time::Now());
  service->default_resource_provider()->UseResourcesDirectory(dir);
}

void OnResourcesFetched(std::unique_ptr<network::SimpleURLLoader> loader,
                        base::WeakPtr<brave_shields::AdBlockService> service,
                        std::unique_ptr<std::string> body) {
  if (!body || body->size() < 1024 || body->front() != '[') {
    VLOG(1) << "falcon: adblock resources fetch failed";
    return;
  }
  base::FilePath dir = ResourcesDir();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(&WriteResources, dir, *body),
      base::BindOnce(&OnResourcesWritten, service, dir));
}

void FetchResources(brave_shields::AdBlockService* service) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(kResourcesUrl);
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  auto loader = network::SimpleURLLoader::Create(
      std::move(request),
      net::DefineNetworkTrafficAnnotation("falcon_adblock_resources", R"(
        semantics {
          sender: "Falcon Shields"
          description: "Fetches the ad-block scriptlet resources Brave ships "
            "as a component, from the public brave/adblock-resources repo."
          trigger: "Startup, at most once a day."
          data: "None."
          destination: OTHER
        }
        policy {
          cookies_allowed: NO
          setting: "Shields off disables ad blocking."
          policy_exception_justification: "Personal fork; no policy."
        })"));
  network::SimpleURLLoader* raw = loader.get();
  raw->DownloadToString(
      g_browser_process->shared_url_loader_factory().get(),
      base::BindOnce(&OnResourcesFetched, std::move(loader),
                     service->AsWeakPtr()),
      kResourcesMaxBytes);
}

void OnResourcesPresent(base::WeakPtr<brave_shields::AdBlockService> service,
                        base::FilePath dir,
                        bool present) {
  if (present && service) {
    service->default_resource_provider()->UseResourcesDirectory(dir);
  }
}

}  // namespace

void RegisterAdblockSeedPrefs(PrefRegistrySimple* registry) {
  registry->RegisterIntegerPref(kSeededPref, 0);
  registry->RegisterTimePref(kResourcesFetchedPref, base::Time());
}

void SeedAdblockSubscriptions(PrefService* local_state,
                              brave_shields::AdBlockService* service) {
  if (!local_state || !service) {
    return;
  }
  constexpr int kSeedVersion = 1;
  if (local_state->GetInteger(kSeededPref) < kSeedVersion) {
    local_state->SetInteger(kSeededPref, kSeedVersion);
    if (auto* manager = service->subscription_service_manager()) {
      for (const char* url : kLists) {
        manager->CreateSubscription(GURL(url));
      }
    }
  }

  // Scriptlet resources: use the cached copy right away, refresh daily.
  base::FilePath dir = ResourcesDir();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&base::PathExists, dir.AppendASCII(kResourcesFile)),
      base::BindOnce(&OnResourcesPresent, service->AsWeakPtr(), dir));
  if (base::Time::Now() - local_state->GetTime(kResourcesFetchedPref) >
      kResourcesMaxAge) {
    FetchResources(service);
  }
}

}  // namespace falcon
