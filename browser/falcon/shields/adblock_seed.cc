// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/shields/adblock_seed.h"

#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/barrier_closure.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "brave/components/brave_shields/content/browser/ad_block_service.h"
#include "brave/components/brave_shields/content/browser/ad_block_subscription_service_manager.h"
#include "brave/components/brave_shields/core/browser/ad_block_default_resource_provider.h"
#include "brave/browser/brave_browser_process.h"
#include "brave/components/debounce/core/browser/debounce_component_installer.h"
#include "brave/components/url_sanitizer/core/browser/url_sanitizer_component_installer.h"
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
    // The list behind Brave's "cookie notices" toggle. That toggle keys off the
    // filter-list catalogue component, which Falcon does not fetch, so without
    // this the feature is inert.
    "https://secure.fanboy.co.nz/fanboy-cookiemonster_ubo.txt",
};

// Brave ships these as the "Local Data Files" component; same files, from the
// repository they are built from. <dir>/1/<file> is the layout the installers
// expect (OnComponentReady appends the config version).
constexpr char kRulesDir[] = "Falcon Rules";
constexpr char kRulesVersionDir[] = "1";
constexpr char kRulesFetchedPref[] = "falcon.rules.fetched";
constexpr base::TimeDelta kRulesMaxAge = base::Days(1);
constexpr size_t kRulesMaxBytes = 4 * 1024 * 1024;
constexpr const char* kRuleFiles[][2] = {
    {"debounce.json",
     "https://raw.githubusercontent.com/brave/adblock-lists/master/brave-lists/"
     "debounce.json"},
    {"clean-urls.json",
     "https://raw.githubusercontent.com/brave/adblock-lists/master/brave-lists/"
     "clean-urls.json"},
    {"clean-urls-permissions.json",
     "https://raw.githubusercontent.com/brave/adblock-lists/master/brave-lists/"
     "clean-urls-permissions.json"},
};

base::FilePath RulesDir() {
  base::FilePath user_data;
  base::PathService::Get(chrome::DIR_USER_DATA, &user_data);
  return user_data.AppendASCII(kRulesDir);
}

// Hand the freshly written directory to the installers that would normally be
// fed by the Local Data Files component.
void NotifyRuleInstallers() {
  const base::FilePath dir = RulesDir();
  if (auto* debounce = g_brave_browser_process->debounce_component_installer()) {
    debounce->OnComponentReady(std::string(), dir, std::string());
  }
  if (auto* sanitizer =
          g_brave_browser_process->URLSanitizerComponentInstaller()) {
    sanitizer->OnComponentReady(std::string(), dir, std::string());
  }
}

bool WriteRuleFile(const base::FilePath& dir,
                   const std::string& name,
                   const std::string& body) {
  return base::CreateDirectory(dir) &&
         base::WriteFile(dir.AppendASCII(name), body);
}

void OnRuleFileFetched(std::unique_ptr<network::SimpleURLLoader> loader,
                       std::string name,
                       base::RepeatingClosure done,
                       std::optional<std::string> body) {
  if (!body || body->empty() || body->front() != '{') {
    VLOG(1) << "falcon: rules fetch failed for " << name;
    done.Run();
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(&WriteRuleFile,
                     RulesDir().AppendASCII(kRulesVersionDir), std::move(name),
                     *body),
      base::BindOnce([](base::RepeatingClosure done, bool ok) { done.Run(); },
                     std::move(done)));
}

void OnAllRulesFetched() {
  g_browser_process->local_state()->SetTime(kRulesFetchedPref,
                                            base::Time::Now());
  NotifyRuleInstallers();
}

// Debounce and the URL sanitizer are compiled in but their rules ship in a
// Brave component, so without this they are dead code in Falcon.
void FetchRules() {
  base::RepeatingClosure done = base::BarrierClosure(
      std::size(kRuleFiles), base::BindOnce(&OnAllRulesFetched));
  for (const auto& entry : kRuleFiles) {
    auto request = std::make_unique<network::ResourceRequest>();
    request->url = GURL(entry[1]);
    request->credentials_mode = network::mojom::CredentialsMode::kOmit;
    auto loader = network::SimpleURLLoader::Create(
        std::move(request),
        net::DefineNetworkTrafficAnnotation("falcon_shields_rules", R"(
          semantics {
            sender: "Falcon Shields"
            description: "Fetches the debounce and URL-sanitizer rules Brave "
              "ships as a component, from the public brave/adblock-lists repo."
            trigger: "Startup, at most once a day."
            data: "None."
            destination: OTHER
          }
          policy {
            cookies_allowed: NO
            setting: "Shields off disables these."
            policy_exception_justification: "Personal fork; no policy."
          })"));
    network::SimpleURLLoader* raw = loader.get();
    raw->DownloadToString(
        g_browser_process->shared_url_loader_factory().get(),
        base::BindOnce(&OnRuleFileFetched, std::move(loader), entry[0], done),
        kRulesMaxBytes);
  }
}

void OnRulesPresent(bool present) {
  if (present) {
    NotifyRuleInstallers();
  }
}

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
                        std::optional<std::string> body) {
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
  registry->RegisterTimePref(kRulesFetchedPref, base::Time());
}

void SeedAdblockSubscriptions(PrefService* local_state,
                              brave_shields::AdBlockService* service) {
  if (!local_state || !service) {
    return;
  }
  constexpr int kSeedVersion = 2;
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

  // Debounce / URL sanitizer rules: same deal, use what is cached and refresh.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&base::PathExists, RulesDir()
                                            .AppendASCII(kRulesVersionDir)
                                            .AppendASCII(kRuleFiles[0][0])),
      base::BindOnce(&OnRulesPresent));
  if (base::Time::Now() - local_state->GetTime(kRulesFetchedPref) >
      kRulesMaxAge) {
    FetchRules();
  }
}

}  // namespace falcon
