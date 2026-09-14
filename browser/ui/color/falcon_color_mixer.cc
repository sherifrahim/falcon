// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/color/falcon_color_mixer.h"

#include "brave/browser/ui/color/brave_color_id.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_recipe.h"
#include "ui/color/color_transform.h"

namespace {

// Tailwind slate/sky, the same tokens the Falcon WebUI pages use.
constexpr SkColor kSlate950 = SkColorSetRGB(0x0B, 0x12, 0x20);
constexpr SkColor kSlate900 = SkColorSetRGB(0x0F, 0x17, 0x2A);
constexpr SkColor kSlate850 = SkColorSetRGB(0x14, 0x1E, 0x33);
constexpr SkColor kSlate800 = SkColorSetRGB(0x1E, 0x29, 0x3B);
constexpr SkColor kSlate700 = SkColorSetRGB(0x33, 0x41, 0x55);
constexpr SkColor kSlate300 = SkColorSetRGB(0xCB, 0xD5, 0xE1);
constexpr SkColor kSlate400 = SkColorSetRGB(0x94, 0xA3, 0xB8);
constexpr SkColor kSlate100 = SkColorSetRGB(0xF1, 0xF5, 0xF9);
constexpr SkColor kSlate50 = SkColorSetRGB(0xF8, 0xFA, 0xFC);
constexpr SkColor kSlate200 = SkColorSetRGB(0xE2, 0xE8, 0xF0);
constexpr SkColor kSky400 = SkColorSetRGB(0x38, 0xBD, 0xF8);
constexpr SkColor kSky500 = SkColorSetRGB(0x0E, 0xA5, 0xE9);

void AddDark(ui::ColorMixer& mixer) {
  mixer[ui::kColorFrameActive] = {kSlate950};
  mixer[ui::kColorFrameInactive] = {kSlate950};
  mixer[kColorToolbar] = {kSlate900};
  mixer[kColorBookmarkBarBackground] = {kSlate900};
  mixer[kColorToolbarContentAreaSeparator] = {kSlate700};
  mixer[kColorToolbarSeparator] = {kSlate700};
  mixer[kColorToolbarButtonIcon] = {kSlate300};
  mixer[kColorToolbarButtonActivated] = {kSky400};

  mixer[kColorTabBackgroundActiveFrameActive] = {kSlate900};
  mixer[kColorTabBackgroundActiveFrameInactive] = {kSlate900};
  mixer[kColorTabBackgroundInactiveFrameActive] = {kSlate950};
  mixer[kColorTabBackgroundInactiveFrameInactive] = {kSlate950};
  mixer[kColorTabForegroundActiveFrameActive] = {kSlate100};
  mixer[kColorTabForegroundInactiveFrameActive] = {kSlate400};
  mixer[kColorTabStrokeFrameActive] = {kSlate900};
  mixer[kColorTabStrokeFrameInactive] = {kSlate900};
  mixer[kColorTabDividerFrameActive] = {kSlate700};
  mixer[kColorNewTabButtonBackgroundFrameActive] = {kSlate950};
  mixer[kColorNewTabButtonBackgroundFrameInactive] = {kSlate950};

  mixer[kColorLocationBarBackground] = {kSlate800};
  mixer[kColorLocationBarBackgroundHovered] = {kSlate700};
  mixer[kColorOmniboxResultsBackground] = {kSlate850};
  mixer[kColorLocationBarFocusRing] = {kSky400};

  mixer[kColorSidePanelBackground] = {kSlate900};
  mixer[kColorSidebarButtonBase] = {kSlate300};
  mixer[kColorSidebarButtonPressed] = {kSky400};
  mixer[kColorSidebarSeparator] = {kSlate700};
  mixer[kColorBraveVerticalTabActiveBackground] = {kSlate800};
  mixer[kColorBraveVerticalTabHoveredBackground] = {kSlate850};
  mixer[kColorBraveVerticalTabInactiveBackground] = {kSlate950};
  mixer[kColorBraveVerticalTabSeparator] = {kSlate700};
  mixer[kColorNewTabPageBackground] = {kSlate950};
}

void AddLight(ui::ColorMixer& mixer) {
  mixer[ui::kColorFrameActive] = {kSlate200};
  mixer[ui::kColorFrameInactive] = {kSlate200};
  mixer[kColorToolbar] = {kSlate50};
  mixer[kColorBookmarkBarBackground] = {kSlate50};
  mixer[kColorToolbarButtonActivated] = {kSky500};
  mixer[kColorTabBackgroundActiveFrameActive] = {kSlate50};
  mixer[kColorTabBackgroundActiveFrameInactive] = {kSlate50};
  mixer[kColorTabBackgroundInactiveFrameActive] = {kSlate200};
  mixer[kColorTabBackgroundInactiveFrameInactive] = {kSlate200};
  mixer[kColorTabStrokeFrameActive] = {kSlate50};
  mixer[kColorTabStrokeFrameInactive] = {kSlate50};
  mixer[kColorNewTabButtonBackgroundFrameActive] = {kSlate200};
  mixer[kColorNewTabButtonBackgroundFrameInactive] = {kSlate200};
  mixer[kColorLocationBarBackground] = {SK_ColorWHITE};
  mixer[kColorLocationBarBackgroundHovered] = {kSlate100};
  mixer[kColorLocationBarFocusRing] = {kSky500};
  mixer[kColorSidePanelBackground] = {kSlate50};
  mixer[kColorSidebarButtonPressed] = {kSky500};
  mixer[kColorBraveVerticalTabActiveBackground] = {SK_ColorWHITE};
  mixer[kColorBraveVerticalTabHoveredBackground] = {kSlate100};
  mixer[kColorBraveVerticalTabInactiveBackground] = {kSlate200};
}

}  // namespace

void AddFalconColorMixer(ui::ColorProvider* provider,
                         const ui::ColorProviderKey& key) {
  if (key.custom_theme) {
    return;
  }
  ui::ColorMixer& mixer = provider->AddMixer();
  if (key.color_mode == ui::ColorProviderKey::ColorMode::kDark) {
    AddDark(mixer);
  } else {
    AddLight(mixer);
  }
}
