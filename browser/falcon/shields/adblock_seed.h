// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_SHIELDS_ADBLOCK_SEED_H_
#define BRAVE_BROWSER_FALCON_SHIELDS_ADBLOCK_SEED_H_

class PrefRegistrySimple;
class PrefService;

namespace brave_shields {
class AdBlockService;
}

namespace falcon {

// Brave's default filter lists arrive through Brave's component updater, which
// only answers builds carrying Brave's private services key. Falcon instead
// subscribes Shields to the public sources of the same lists once per install
// (Brave's subscription service refreshes them on its own schedule).
void RegisterAdblockSeedPrefs(PrefRegistrySimple* registry);
void SeedAdblockSubscriptions(PrefService* local_state,
                              brave_shields::AdBlockService* service);

}  // namespace falcon

#endif  // BRAVE_BROWSER_FALCON_SHIELDS_ADBLOCK_SEED_H_
