// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_COLLECTIONS_COLLECTIONS_SERVICE_H_
#define BRAVE_BROWSER_FALCON_COLLECTIONS_COLLECTIONS_SERVICE_H_

#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/values.h"
#include "components/keyed_service/core/keyed_service.h"

class PrefService;

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace falcon {

namespace prefs {
// List of collections: {id, name, created, items: [{id, kind, title, url,
// text, added}]}. `kind` is "page", "link", "image" or "text".
inline constexpr char kCollections[] = "falcon.collections";
// Collection that "Add to collection" targets (last used).
inline constexpr char kCollectionsLast[] = "falcon.collections.last";
void RegisterCollectionsPrefs(user_prefs::PrefRegistrySyncable* registry);
}  // namespace prefs

// Edge-style Collections: named boards of pages, links, images and text
// snippets gathered while browsing. Stored in profile prefs (small, atomic
// writes for free); the side panel and falcon://collections render it.
class CollectionsService : public KeyedService {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnCollectionsChanged() {}
  };

  explicit CollectionsService(PrefService* prefs);
  CollectionsService(const CollectionsService&) = delete;
  CollectionsService& operator=(const CollectionsService&) = delete;
  ~CollectionsService() override;

  // Snapshot of every collection, for the UI.
  base::ListValue Collections() const;
  std::optional<base::DictValue> Find(const std::string& id) const;

  std::string Create(const std::string& name);
  bool Rename(const std::string& id, const std::string& name);
  bool Delete(const std::string& id);
  // Reorders collections; `ids` lists every collection id in the new order.
  bool Reorder(const base::ListValue& ids);

  // `item` carries kind/title/url/text; id and added are filled in here.
  // Pages, links and images with the same url are not added twice.
  // Returns the item id, empty on failure.
  std::string AddItem(const std::string& collection_id, base::DictValue item);
  bool RemoveItem(const std::string& collection_id,
                  const std::string& item_id);
  // Moves an item inside its collection to `index` (clamped), or to another
  // collection's end when `to_collection_id` differs.
  bool MoveItem(const std::string& collection_id,
                const std::string& item_id,
                const std::string& to_collection_id,
                int index);

  // The target of "Add to collection": the last used one, created on demand.
  std::string LastCollectionId();
  void SetLastCollectionId(const std::string& id);

  // Markdown export of one collection.
  std::string ToMarkdown(const std::string& id) const;

  static base::DictValue MakeItem(const std::string& kind,
                                  const std::string& title,
                                  const std::string& url,
                                  const std::string& text);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

 private:
  // Runs `mutate` on the live list and persists it; notifies when it returns
  // true.
  template <typename F>
  bool Mutate(F mutate);

  raw_ptr<PrefService> prefs_;
  base::ObserverList<Observer> observers_;
};

}  // namespace falcon

#endif  // BRAVE_BROWSER_FALCON_COLLECTIONS_COLLECTIONS_SERVICE_H_
