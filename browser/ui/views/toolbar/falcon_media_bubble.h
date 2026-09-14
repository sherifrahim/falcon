// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_TOOLBAR_FALCON_MEDIA_BUBBLE_H_
#define BRAVE_BROWSER_UI_VIEWS_TOOLBAR_FALCON_MEDIA_BUBBLE_H_

#include <memory>

class BrowserWindowInterface;

namespace content {
class WebContents;
}  // namespace content

namespace views {
class View;
class BubbleDialogModelHost;
}  // namespace views

namespace falcon {

// IDM-style "media found on this page" panel anchored to the downloads
// toolbar button: one row per sniffed video/audio/playlist with Download
// (best quality, straight away) and Quality… (opens the picker in the
// downloader page).
std::unique_ptr<views::BubbleDialogModelHost> CreateFalconMediaBubble(
    BrowserWindowInterface* browser,
    content::WebContents* web_contents,
    views::View* anchor);

}  // namespace falcon

#endif  // BRAVE_BROWSER_UI_VIEWS_TOOLBAR_FALCON_MEDIA_BUBBLE_H_
