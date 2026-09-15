// Copyright (c) 2023 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_COMPONENTS_COMMANDER_BROWSER_COMMANDER_ITEM_MODEL_H_
#define BRAVE_COMPONENTS_COMMANDER_BROWSER_COMMANDER_ITEM_MODEL_H_

#include <vector>

#include "base/component_export.h"
#include "ui/gfx/range/range.h"

namespace commander {

// See brave/browser/ui/commander/commander_view_model.h for details on these
// structs. They exist to get around some deps violations.
struct COMPONENT_EXPORT(COMMANDER_BROWSER) CommandItemModel {
 public:
  CommandItemModel(const std::u16string& title,
                   const std::vector<gfx::Range>& matched_ranges,
                   const std::u16string& annotation,
                   double score);
  ~CommandItemModel();
  CommandItemModel(const CommandItemModel& other);
  CommandItemModel(CommandItemModel&& other);
  CommandItemModel& operator=(const CommandItemModel& other) = default;
  CommandItemModel& operator=(CommandItemModel&& other) = default;

  std::u16string title;
  std::vector<gfx::Range> matched_ranges;
  std::u16string annotation;
  double score;
  // Falcon: CommandItem::Entity (0 command, 1 bookmark, 2 tab, 3 window,
  // 4 group) so the command deck can group results.
  int entity = 0;
};

}  // namespace commander

#endif  // BRAVE_COMPONENTS_COMMANDER_BROWSER_COMMANDER_ITEM_MODEL_H_
