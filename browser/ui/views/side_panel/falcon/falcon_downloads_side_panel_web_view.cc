// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/side_panel/falcon/falcon_downloads_side_panel_web_view.h"

#include <string>
#include <utility>

#include "base/check.h"
#include "base/functional/callback.h"
#include "base/strings/strcat.h"
#include "brave/browser/ui/webui/falcon_downloader_ui.h"
#include "brave/components/constants/falcon_url_constants.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/common/referrer.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/page_transition_types.h"

FalconDownloadsContentsWrapper::FalconDownloadsContentsWrapper(Profile* profile)
    : WebUIContentsWrapper(GURL(base::StrCat({"chrome://",
                                              falcon::kFalconDownloaderPanelHost})),
                           profile,
                           IDS_TOOLTIP_DOWNLOAD_ICON,
                           /*webui_resizes_host=*/false,
                           /*esc_closes_ui=*/false,
                           /*supports_draggable_regions=*/false,
                           std::string(FalconDownloaderPanelUI::GetWebUIName())),
      webui_url_(
          base::StrCat({"chrome://", falcon::kFalconDownloaderPanelHost})) {
  CHECK(GetWebUIController());
  GetWebUIController()->set_embedder(weak_ptr_factory_.GetWeakPtr());
}

FalconDownloadsContentsWrapper::~FalconDownloadsContentsWrapper() = default;

void FalconDownloadsContentsWrapper::ReloadWebContents() {
  web_contents()->GetController().LoadURL(webui_url_, content::Referrer(),
                                          ui::PAGE_TRANSITION_AUTO_TOPLEVEL,
                                          std::string());
  if (auto* controller = GetWebUIController()) {
    controller->set_embedder(weak_ptr_factory_.GetWeakPtr());
  }
}

base::WeakPtr<WebUIContentsWrapper>
FalconDownloadsContentsWrapper::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

FalconDownloaderPanelUI* FalconDownloadsContentsWrapper::GetWebUIController() {
  content::WebUI* const webui = web_contents()->GetWebUI();
  return webui && webui->GetController()
             ? webui->GetController()->GetAs<FalconDownloaderPanelUI>()
             : nullptr;
}

BEGIN_METADATA(FalconDownloadsSidePanelWebView)
END_METADATA

// static
std::unique_ptr<views::View> FalconDownloadsSidePanelWebView::CreateView(
    Profile* profile,
    SidePanelEntryScope& scope) {
  CHECK(profile);
  auto contents_wrapper =
      std::make_unique<FalconDownloadsContentsWrapper>(profile);
  auto web_view = std::make_unique<FalconDownloadsSidePanelWebView>(
      scope, std::move(contents_wrapper));
  web_view->ShowUI();
  return web_view;
}

FalconDownloadsSidePanelWebView::FalconDownloadsSidePanelWebView(
    SidePanelEntryScope& scope,
    std::unique_ptr<FalconDownloadsContentsWrapper> contents_wrapper)
    : SidePanelWebUIView(scope,
                         /*on_show_cb=*/base::RepeatingClosure(),
                         /*close_cb=*/base::RepeatingClosure(),
                         contents_wrapper.get()),
      contents_wrapper_(std::move(contents_wrapper)) {}

FalconDownloadsSidePanelWebView::~FalconDownloadsSidePanelWebView() = default;
