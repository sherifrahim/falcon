// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_FALCON_MINI_MENU_BUBBLE_H_
#define BRAVE_BROWSER_UI_VIEWS_FALCON_MINI_MENU_BUBBLE_H_

#include <string>

#include "ui/gfx/geometry/point.h"

namespace content {
class WebContents;
}  // namespace content

namespace falcon {

// Edge-style mini menu for a text selection: a small non-activating pill
// (Copy · Search <engine>) at `screen_point`. One per WebContents; showing
// again replaces the previous one.
void ShowMiniMenu(content::WebContents* web_contents,
                  const gfx::Point& screen_point,
                  const std::u16string& text);
void CloseMiniMenu(content::WebContents* web_contents);

}  // namespace falcon

#endif  // BRAVE_BROWSER_UI_VIEWS_FALCON_MINI_MENU_BUBBLE_H_
