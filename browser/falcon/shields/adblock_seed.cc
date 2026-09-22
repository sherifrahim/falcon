// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/shields/adblock_seed.h"

#include "brave/components/brave_shields/content/browser/ad_block_service.h"
#include "brave/components/brave_shields/content/browser/ad_block_subscription_service_manager.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "url/gurl.h"

namespace falcon {

namespace {

constexpr char kSeededPref[] = "falcon.adblock.seeded";

// The lists Brave compiles into its default component, from their public homes.
constexpr const char* kLists[] = {
    "https://easylist.to/easylist/easylist.txt",
    "https://easylist.to/easylist/easyprivacy.txt",
    "https://raw.githubusercontent.com/uBlockOrigin/uAssets/master/filters/unbreak.txt",
    "https://raw.githubusercontent.com/brave/adblock-lists/master/brave-lists/brave-specific.txt",
    "https://raw.githubusercontent.com/brave/adblock-lists/master/brave-lists/brave-unbreak.txt",
};

}  // namespace

void RegisterAdblockSeedPrefs(PrefRegistrySimple* registry) {
  registry->RegisterIntegerPref(kSeededPref, 0);
}

void SeedAdblockSubscriptions(PrefService* local_state,
                              brave_shields::AdBlockService* service) {
  constexpr int kSeedVersion = 1;
  if (!local_state || !service ||
      local_state->GetInteger(kSeededPref) >= kSeedVersion) {
    return;
  }
  local_state->SetInteger(kSeededPref, kSeedVersion);
  auto* manager = service->subscription_service_manager();
  if (!manager) {
    return;
  }
  for (const char* url : kLists) {
    manager->CreateSubscription(GURL(url));
  }
}

}  // namespace falcon
