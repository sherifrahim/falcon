// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/webui/falcon_command_deck_ui.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/check_op.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "brave/browser/ui/commander/commander_service.h"
#include "brave/browser/ui/commander/commander_service_factory.h"
#include "brave/browser/ui/webui/brave_webui_source.h"
#include "brave/components/commander/browser/commander_frontend_delegate.h"
#include "brave/components/commander/browser/commander_item_model.h"
#include "brave/components/commander/common/constants.h"
#include "brave/components/falcon_command_deck_ui/resources/grit/falcon_command_deck_generated_map.h"
#include "chrome/browser/profiles/profile.h"
#include "components/grit/brave_components_resources.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/browser/web_ui_message_handler.h"

namespace {

class FalconCommandDeckHandler
    : public content::WebUIMessageHandler,
      public commander::CommanderFrontendDelegate::Observer {
 public:
  explicit FalconCommandDeckHandler(FalconCommandDeckUI* controller)
      : controller_(controller) {}
  ~FalconCommandDeckHandler() override { Detach(); }

 private:
  commander::CommanderService* service() {
    return commander::CommanderServiceFactory::GetForBrowserContext(
        Profile::FromWebUI(web_ui()));
  }

  void RegisterMessages() override {
    web_ui()->RegisterMessageCallback(
        "deck.ready", base::BindRepeating(&FalconCommandDeckHandler::Ready,
                                          base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "deck.query", base::BindRepeating(&FalconCommandDeckHandler::Query,
                                          base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "deck.select", base::BindRepeating(&FalconCommandDeckHandler::Select,
                                           base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "deck.close", base::BindRepeating(&FalconCommandDeckHandler::Close,
                                          base::Unretained(this)));
  }

  void OnJavascriptDisallowed() override { Detach(); }

  void Detach() {
    if (attached_ && observation_.IsObserving()) {
      observation_.Reset();
      attached_->SetExternalFrontend(false);
    }
    attached_ = nullptr;
  }

  // args: [] — page mounted: take over the commander, show the bubble.
  void Ready(const base::ListValue& args) {
    AllowJavascript();
    auto* s = service();
    if (s && !observation_.IsObserving()) {
      attached_ = s;
      s->SetExternalFrontend(true);
      observation_.Observe(s);  // AddObserver fires OnCommanderUpdated once
      s->UpdateText(std::u16string(commander::kCommandPrefix) + u" ");
    }
    if (auto embedder = controller_->embedder()) {
      embedder->ShowUI();
    }
  }

  // args: [text]
  void Query(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    if (auto* s = service(); s && args[0].is_string()) {
      s->UpdateText(base::StrCat({commander::kCommandPrefix, u" ",
                                  base::UTF8ToUTF16(args[0].GetString())}));
    }
  }

  // args: [callbackId, index, resultSetId] -> {prompt} (non-empty prompt =
  // composite command: the deck stays open for the next step).
  void Select(const base::ListValue& args) {
    CHECK_EQ(3U, args.size());
    AllowJavascript();
    base::DictValue out;
    if (auto* s = service(); s && args[1].is_int() && args[2].is_int()) {
      s->SelectCommand(static_cast<uint32_t>(args[1].GetInt()),
                       static_cast<uint32_t>(args[2].GetInt()));
      out.Set("prompt", s->GetPrompt());
    }
    ResolveJavascriptCallback(args[0], out);
  }

  void Close(const base::ListValue& args) {
    Detach();
    if (auto embedder = controller_->embedder()) {
      embedder->CloseUI();
    }
  }

  // CommanderFrontendDelegate::Observer:
  void OnCommanderUpdated() override {
    if (!IsJavascriptAllowed()) {
      return;
    }
    auto* s = service();
    if (!s) {
      return;
    }
    base::ListValue items;
    for (const commander::CommandItemModel& m : s->GetItems()) {
      base::DictValue d;
      d.Set("title", m.title);
      d.Set("annotation", m.annotation);
      d.Set("entity", m.entity);
      base::ListValue ranges;
      for (const gfx::Range& r : m.matched_ranges) {
        base::ListValue pair;
        pair.Append(static_cast<int>(r.start()));
        pair.Append(static_cast<int>(r.end()));
        ranges.Append(std::move(pair));
      }
      d.Set("ranges", std::move(ranges));
      items.Append(std::move(d));
    }
    base::DictValue payload;
    payload.Set("prompt", s->GetPrompt());
    payload.Set("resultSetId", s->GetResultSetId());
    payload.Set("items", std::move(items));
    FireWebUIListener("deck-items", payload);
  }

  raw_ptr<FalconCommandDeckUI> controller_;
  raw_ptr<commander::CommanderService> attached_ = nullptr;
  base::ScopedObservation<commander::CommanderFrontendDelegate,
                          commander::CommanderFrontendDelegate::Observer>
      observation_{this};
};

}  // namespace

FalconCommandDeckUI::FalconCommandDeckUI(content::WebUI* web_ui)
    : TopChromeWebUIController(web_ui, /*enable_chrome_send=*/true) {
  CreateAndAddWebUIDataSource(web_ui, falcon::kFalconCommandDeckHost,
                              kFalconCommandDeckGenerated,
                              IDR_FALCON_COMMAND_DECK_HTML);
  web_ui->AddMessageHandler(std::make_unique<FalconCommandDeckHandler>(this));
}

FalconCommandDeckUI::~FalconCommandDeckUI() = default;

WEB_UI_CONTROLLER_TYPE_IMPL(FalconCommandDeckUI)

bool FalconCommandDeckUIConfig::ShouldAutoResizeHost() {
  return true;
}
