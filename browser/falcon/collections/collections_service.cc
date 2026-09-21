// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/collections/collections_service.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"

namespace falcon {

namespace prefs {

void RegisterCollectionsPrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterListPref(kCollections);
  registry->RegisterStringPref(kCollectionsLast, std::string());
}

}  // namespace prefs

namespace {

constexpr size_t kMaxTextChars = 4000;
constexpr size_t kMaxTitleChars = 300;

std::string_view Str(const base::DictValue* d, std::string_view key) {
  const std::string* s = d ? d->FindString(key) : nullptr;
  return s ? std::string_view(*s) : std::string_view();
}

std::string NewId() {
  return base::Uuid::GenerateRandomV4().AsLowercaseString();
}

double NowMs() {
  return base::Time::Now().InMillisecondsFSinceUnixEpoch();
}

base::DictValue* FindCollection(base::ListValue& list, std::string_view id) {
  for (base::Value& v : list) {
    base::DictValue* d = v.GetIfDict();
    if (Str(d, "id") == id) {
      return d;
    }
  }
  return nullptr;
}

base::ListValue* ItemsOf(base::DictValue* collection) {
  base::ListValue* items = collection->FindList("items");
  return items ? items : &collection->Set("items", base::ListValue())->GetList();
}

std::string Clip(std::string_view s, size_t max) {
  std::string out(base::TrimWhitespaceASCII(s, base::TRIM_ALL));
  if (out.size() > max) {
    out.resize(max);
    // Do not cut a UTF-8 sequence in half.
    while (!out.empty() && (static_cast<uint8_t>(out.back()) & 0xC0) == 0x80) {
      out.pop_back();
    }
    if (!out.empty()) {
      out.pop_back();
    }
    out += "\xE2\x80\xA6";  // …
  }
  return out;
}

}  // namespace

CollectionsService::CollectionsService(PrefService* prefs) : prefs_(prefs) {}

CollectionsService::~CollectionsService() = default;

template <typename F>
bool CollectionsService::Mutate(F mutate) {
  bool changed = false;
  {
    ScopedListPrefUpdate update(prefs_, prefs::kCollections);
    changed = mutate(update.Get());
  }
  if (changed) {
    for (Observer& observer : observers_) {
      observer.OnCollectionsChanged();
    }
  }
  return changed;
}

base::ListValue CollectionsService::Collections() const {
  return prefs_->GetList(prefs::kCollections).Clone();
}

std::optional<base::DictValue> CollectionsService::Find(
    const std::string& id) const {
  for (const base::Value& v : prefs_->GetList(prefs::kCollections)) {
    const base::DictValue* d = v.GetIfDict();
    if (d && Str(d, "id") == id) {
      return d->Clone();
    }
  }
  return std::nullopt;
}

std::string CollectionsService::Create(const std::string& name) {
  const std::string id = NewId();
  Mutate([&](base::ListValue& list) {
    base::DictValue c;
    c.Set("id", id);
    c.Set("name", Clip(name.empty() ? "New collection" : name, kMaxTitleChars));
    c.Set("created", NowMs());
    c.Set("items", base::ListValue());
    list.Append(std::move(c));
    return true;
  });
  prefs_->SetString(prefs::kCollectionsLast, id);
  return id;
}

bool CollectionsService::Rename(const std::string& id,
                                const std::string& name) {
  return Mutate([&](base::ListValue& list) {
    base::DictValue* c = FindCollection(list, id);
    if (!c || name.empty()) {
      return false;
    }
    c->Set("name", Clip(name, kMaxTitleChars));
    return true;
  });
}

bool CollectionsService::Delete(const std::string& id) {
  const bool removed = Mutate([&](base::ListValue& list) {
    return list.EraseIf([&](const base::Value& v) {
      return Str(v.GetIfDict(), "id") == id;
    }) > 0;
  });
  if (removed && prefs_->GetString(prefs::kCollectionsLast) == id) {
    prefs_->SetString(prefs::kCollectionsLast, std::string());
  }
  return removed;
}

bool CollectionsService::Reorder(const base::ListValue& ids) {
  return Mutate([&](base::ListValue& list) {
    if (ids.size() != list.size()) {
      return false;
    }
    base::ListValue ordered;
    for (const base::Value& idv : ids) {
      const std::string* id = idv.GetIfString();
      base::DictValue* c = id ? FindCollection(list, *id) : nullptr;
      if (!c) {
        return false;
      }
      ordered.Append(c->Clone());
    }
    list = std::move(ordered);
    return true;
  });
}

// static
base::DictValue CollectionsService::MakeItem(const std::string& kind,
                                             const std::string& title,
                                             const std::string& url,
                                             const std::string& text) {
  base::DictValue item;
  item.Set("kind", kind);
  item.Set("title", Clip(title, kMaxTitleChars));
  item.Set("url", url);
  item.Set("text", Clip(text, kMaxTextChars));
  return item;
}

std::string CollectionsService::AddItem(const std::string& collection_id,
                                        base::DictValue item) {
  const std::string id = NewId();
  const bool ok = Mutate([&](base::ListValue& list) {
    base::DictValue* c = FindCollection(list, collection_id);
    if (!c) {
      return false;
    }
    const std::string* kind = item.FindString("kind");
    const std::string* url = item.FindString("url");
    if (!kind || (*kind != "page" && *kind != "link" && *kind != "image" &&
                  *kind != "text")) {
      return false;
    }
    base::ListValue* items = ItemsOf(c);
    if (*kind != "text" && url && !url->empty()) {
      for (const base::Value& v : *items) {
        const base::DictValue* d = v.GetIfDict();
        const std::string* existing = d ? d->FindString("url") : nullptr;
        const std::string* existing_kind = d ? d->FindString("kind") : nullptr;
        if (existing && existing_kind && *existing == *url &&
            *existing_kind == *kind) {
          return false;
        }
      }
    }
    item.Set("id", id);
    item.Set("added", NowMs());
    items->Append(std::move(item));
    return true;
  });
  if (ok) {
    prefs_->SetString(prefs::kCollectionsLast, collection_id);
  }
  return ok ? id : std::string();
}

bool CollectionsService::RemoveItem(const std::string& collection_id,
                                    const std::string& item_id) {
  return Mutate([&](base::ListValue& list) {
    base::DictValue* c = FindCollection(list, collection_id);
    if (!c) {
      return false;
    }
    return ItemsOf(c)->EraseIf([&](const base::Value& v) {
      return Str(v.GetIfDict(), "id") == item_id;
    }) > 0;
  });
}

bool CollectionsService::MoveItem(const std::string& collection_id,
                                  const std::string& item_id,
                                  const std::string& to_collection_id,
                                  int index) {
  return Mutate([&](base::ListValue& list) {
    base::DictValue* from = FindCollection(list, collection_id);
    base::DictValue* to = FindCollection(list, to_collection_id);
    if (!from || !to) {
      return false;
    }
    base::ListValue* items = ItemsOf(from);
    auto it = std::ranges::find_if(*items, [&](const base::Value& v) {
      return Str(v.GetIfDict(), "id") == item_id;
    });
    if (it == items->end()) {
      return false;
    }
    base::Value moved = std::move(*it);
    items->erase(it);
    base::ListValue* dest = ItemsOf(to);
    const int size = static_cast<int>(dest->size());
    const int at = std::clamp(index < 0 ? size : index, 0, size);
    dest->Insert(dest->begin() + at, std::move(moved));
    return true;
  });
}

std::string CollectionsService::LastCollectionId() {
  std::string id = prefs_->GetString(prefs::kCollectionsLast);
  if (!id.empty() && Find(id)) {
    return id;
  }
  const base::ListValue& list = prefs_->GetList(prefs::kCollections);
  if (!list.empty()) {
    if (const base::DictValue* d = list.front().GetIfDict()) {
      id = std::string(Str(d, "id"));
      prefs_->SetString(prefs::kCollectionsLast, id);
      return id;
    }
  }
  return Create("My collection");
}

void CollectionsService::SetLastCollectionId(const std::string& id) {
  if (Find(id)) {
    prefs_->SetString(prefs::kCollectionsLast, id);
  }
}

std::string CollectionsService::ToMarkdown(const std::string& id) const {
  std::optional<base::DictValue> c = Find(id);
  if (!c) {
    return std::string();
  }
  std::string out = base::StrCat({"# ", *c->FindString("name"), "\n\n"});
  const base::ListValue* items = c->FindList("items");
  if (!items) {
    return out;
  }
  for (const base::Value& v : *items) {
    const base::DictValue* d = v.GetIfDict();
    if (!d) {
      continue;
    }
    const std::string_view kind = Str(d, "kind");
    const std::string* title = d->FindString("title");
    const std::string* url = d->FindString("url");
    const std::string* text = d->FindString("text");
    if (kind == "text") {
      out += base::StrCat({"> ", text ? *text : "", "\n"});
      if (url && !url->empty()) {
        out += base::StrCat({"> — <", *url, ">\n"});
      }
      out += "\n";
    } else if (kind == "image") {
      out += base::StrCat({"![", title ? *title : "", "](", url ? *url : "",
                           ")\n\n"});
    } else {
      out += base::StrCat({"- [", title && !title->empty() ? *title
                                                            : (url ? *url : ""),
                           "](", url ? *url : "", ")\n"});
    }
  }
  return out;
}

void CollectionsService::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void CollectionsService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

}  // namespace falcon
