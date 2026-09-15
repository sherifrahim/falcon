// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_COLOR_FALCON_COLOR_MIXER_H_
#define BRAVE_BROWSER_UI_COLOR_FALCON_COLOR_MIXER_H_

#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"

// Falcon's own palette (slate + sky, matching falcon://downloader and the new
// tab page), layered after Brave's mixers so it wins for the built-in themes.
// Custom (web store) themes are left alone.
void AddFalconColorMixer(ui::ColorProvider* provider,
                         const ui::ColorProviderKey& key);

namespace falcon {
// Pitch-black (OLED) variant of the dark scheme. Process-wide because colour
// mixers have no profile; the value is persisted in local state
// (falcon.theme.black) by the caller. `SetBlackTheme` rebuilds every colour
// provider so open windows repaint.
void SetBlackTheme(bool black);
bool IsBlackTheme();
}  // namespace falcon

#endif  // BRAVE_BROWSER_UI_COLOR_FALCON_COLOR_MIXER_H_
