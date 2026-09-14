/* Copyright (c) 2026 Falcon. Personal fork of Brave.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/falcon/peek_window.h"

// Arc-style Peek: a Shift+click (NEW_WINDOW disposition from a link) opens a
// floating preview instead of a new window. The hook line is added to the
// upstream function by
// patches/chrome-browser-ui-browser_web_contents_delegate-browser_web_contents_delegate.cc.patch.
#define FALCON_BROWSER_WEB_CONTENTS_DELEGATE_OPEN_URL_FROM_TAB \
  if (falcon::MaybePeek(&*browser_, source, params)) {         \
    return nullptr;                                            \
  }

#include <chrome/browser/ui/browser_web_contents_delegate/browser_web_contents_delegate.cc>

#undef FALCON_BROWSER_WEB_CONTENTS_DELEGATE_OPEN_URL_FROM_TAB
