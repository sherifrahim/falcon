// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/collections/collections_service_factory.h"

#include "brave/browser/falcon/collections/collections_service.h"
#include "chrome/browser/profiles/profile.h"

namespace falcon {

// static
CollectionsService* CollectionsServiceFactory::GetForProfile(Profile* profile) {
  return static_cast<CollectionsService*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
CollectionsServiceFactory* CollectionsServiceFactory::GetInstance() {
  static base::NoDestructor<CollectionsServiceFactory> instance;
  return instance.get();
}

// Private windows share the regular profile's collections.
CollectionsServiceFactory::CollectionsServiceFactory()
    : ProfileKeyedServiceFactory(
          "FalconCollectionsService",
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kRedirectedToOriginal)
              .WithGuest(ProfileSelection::kNone)
              .WithSystem(ProfileSelection::kNone)
              .Build()) {}

CollectionsServiceFactory::~CollectionsServiceFactory() = default;

std::unique_ptr<KeyedService>
CollectionsServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<CollectionsService>(
      Profile::FromBrowserContext(context)->GetPrefs());
}

}  // namespace falcon
