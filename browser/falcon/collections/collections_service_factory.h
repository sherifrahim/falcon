// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_COLLECTIONS_COLLECTIONS_SERVICE_FACTORY_H_
#define BRAVE_BROWSER_FALCON_COLLECTIONS_COLLECTIONS_SERVICE_FACTORY_H_

#include <memory>

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace falcon {

class CollectionsService;

class CollectionsServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static CollectionsService* GetForProfile(Profile* profile);
  static CollectionsServiceFactory* GetInstance();

  CollectionsServiceFactory(const CollectionsServiceFactory&) = delete;
  CollectionsServiceFactory& operator=(const CollectionsServiceFactory&) =
      delete;

 private:
  friend base::NoDestructor<CollectionsServiceFactory>;
  CollectionsServiceFactory();
  ~CollectionsServiceFactory() override;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
};

}  // namespace falcon

#endif  // BRAVE_BROWSER_FALCON_COLLECTIONS_COLLECTIONS_SERVICE_FACTORY_H_
