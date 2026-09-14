/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/webui/brave_web_ui_controller_factory.h"

// Needed since we define CHROME_BROWSER_WEB_APPLICATIONS_WEB_APP_PROVIDER_H_
// below
#include "chrome/browser/web_applications/web_app_provider.h"
#include "brave/components/constants/falcon_url_constants.h"
#include "brave/components/constants/webui_url_constants.h"
#include "chrome/common/webui_url_constants.h"

// CHROMIUM_SRC_NOLINT
#define CHROME_BROWSER_WEB_APPLICATIONS_SYSTEM_WEB_APPS_SYSTEM_WEB_APP_MANAGER_H_
// CHROMIUM_SRC_NOLINT
#define CHROME_BROWSER_WEB_APPLICATIONS_WEB_APP_PROVIDER_H_

// `ChromeWebUIControllerFactory::GetFaviconResourceBytes` loads favicon data
// for WebUIs based upon the WebUI URL. Brave uses `kChromeUINewTabHost` instead
// of `kChromeUINewTabPageHost` for the New Tab Page. Ensure that the NTP
// favicon is loaded for Brave's NTP by swapping the host name constant.
#define kChromeUINewTabPageHost kChromeUINewTabHost

#define BRAVE_CHROME_WEBUI_CONTROLLER_FACTORY \
  return BraveWebUIControllerFactory::GetInstance();

// Falcon: falcon://downloader drives the local aria2 engine over a loopback
// WebSocket. Chromium DCHECKs that WebUI renderers only make network requests
// from an allowlist (IsWebUIAllowedToMakeNetworkRequests); this appends our
// host to that expression. kChromeUIDrivePickerHostHost is its last term and
// is used exactly once in the upstream file.
// falcon://newtab loads wallpapers (Bing daily / user image URLs) from the
// web, so it is allowlisted too. Note kChromeUINewTabPageHost is #defined to
// kChromeUINewTabHost above, so either spelling means "newtab" here.
#define kChromeUIDrivePickerHostHost                                        \
  kChromeUIDrivePickerHostHost || origin.host() == kFalconDownloaderHost || \
      origin.host() == falcon::kFalconDownloaderPanelHost ||               \
      origin.host() == chrome::kChromeUINewTabHost

#include <chrome/browser/ui/webui/chrome_web_ui_controller_factory.cc>

#undef kChromeUINewTabPageHost
#undef kChromeUIDrivePickerHostHost
#undef BRAVE_CHROME_WEBUI_CONTROLLER_FACTORY
#undef CHROME_BROWSER_WEB_APPLICATIONS_WEB_APP_PROVIDER_H_
#undef CHROME_BROWSER_WEB_APPLICATIONS_SYSTEM_WEB_APPS_SYSTEM_WEB_APP_MANAGER_H_
