// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/download/clipboard_monitor.h"

#include <optional>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "brave/browser/falcon/download/download_categories.h"
#include "brave/browser/falcon/download/download_interceptor.h"
#include "brave/browser/falcon/download/pref_names.h"
#include "brave/components/constants/url_constants.h"
#include "chrome/browser/notifications/notification_display_service.h"
#include "chrome/browser/notifications/notification_display_service_factory.h"
#include "chrome/browser/notifications/notification_handler.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "components/prefs/pref_service.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/clipboard_monitor.h"
#include "ui/base/data_transfer_policy/data_transfer_endpoint.h"
#include "ui/base/models/image_model.h"
#include "ui/message_center/public/cpp/notification.h"
#include "ui/message_center/public/cpp/notification_delegate.h"
#include "ui/message_center/public/cpp/notifier_id.h"
#include "url/gurl.h"

namespace falcon {

namespace {

constexpr char kNotifierId[] = "falcon.clipboard";
constexpr char kNotificationId[] = "falcon-clipboard-offer";

void OnOfferClick(base::WeakPtr<Profile> profile,
                  GURL url,
                  std::optional<int> button_index) {
  // Only the explicit "Download" button starts anything; a body click just
  // dismisses the toast.
  if (!profile || !button_index.has_value() || *button_index != 0) {
    return;
  }
  StartEngineDownload(profile.get(), url, GURL());
}

}  // namespace

// static
bool ClipboardMonitor::LooksLikeDownload(const std::string& text) {
  if (text.size() > 4096 || text.find('\n') != std::string::npos) {
    return false;
  }
  const GURL url(base::TrimWhitespaceASCII(text, base::TRIM_ALL));
  if (!url.is_valid()) {
    return false;
  }
  if (url.SchemeIs(kMagnetScheme)) {
    return true;
  }
  if (!url.SchemeIsHTTPOrHTTPS() && !url.SchemeIs("ftp")) {
    return false;
  }
  const std::string filename = GuessFilename(url, std::string());
  return filename.find('.') != std::string::npos &&
         !CategoryForFilename(filename).empty();
}

ClipboardMonitor::ClipboardMonitor() {
  observation_.Observe(ui::ClipboardMonitor::GetInstance());
}

ClipboardMonitor::~ClipboardMonitor() = default;

void ClipboardMonitor::OnClipboardDataChanged() {
  // Apps often write several formats in a burst; read once it settles.
  debounce_.Start(FROM_HERE, base::Milliseconds(150),
                  base::BindOnce(&ClipboardMonitor::ReadClipboard,
                                 weak_factory_.GetWeakPtr()));
}

void ClipboardMonitor::ReadClipboard() {
  Profile* profile = ProfileManager::GetLastUsedProfileIfLoaded();
  if (!profile ||
      !profile->GetPrefs()->GetBoolean(prefs::kDownloadEngineEnabled) ||
      !profile->GetPrefs()->GetBoolean(prefs::kDownloadClipboardMonitor)) {
    return;
  }
  ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
  if (!clipboard) {
    return;
  }
  clipboard->GetSource(ui::ClipboardBuffer::kCopyPaste,
                       base::BindOnce(&ClipboardMonitor::OnSource,
                                      weak_factory_.GetWeakPtr()));
}

void ClipboardMonitor::OnSource(std::optional<ui::DataTransferEndpoint> source) {
  // Copies made from Falcon's own pages (e.g. the downloader's "Copy" button)
  // should not bounce back as an offer.
  if (source && source->IsUrlType() && source->GetURL() &&
      (source->GetURL()->SchemeIs("chrome") ||
       source->GetURL()->SchemeIs("falcon"))) {
    return;
  }
  ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
  if (!clipboard) {
    return;
  }
  clipboard->ReadText(ui::ClipboardBuffer::kCopyPaste, std::nullopt,
                      base::BindOnce(&ClipboardMonitor::OnText,
                                     weak_factory_.GetWeakPtr()));
}

void ClipboardMonitor::OnText(std::u16string text16) {
  const std::string text = base::UTF16ToUTF8(text16);
  if (text.empty() || text == last_text_) {
    return;
  }
  last_text_ = text;
  if (!LooksLikeDownload(text)) {
    return;
  }
  Offer(std::string(base::TrimWhitespaceASCII(text, base::TRIM_ALL)));
}

void ClipboardMonitor::Offer(const std::string& spec) {
  Profile* profile = ProfileManager::GetLastUsedProfileIfLoaded();
  if (!profile) {
    return;
  }
  auto* service = NotificationDisplayServiceFactory::GetForProfile(profile);
  if (!service) {
    return;
  }
  const GURL url(spec);
  std::string name = url.SchemeIs(kMagnetScheme)
                         ? "Magnet link"
                         : GuessFilename(url, std::string());
  if (name.empty()) {
    name = url.host();
  }

  message_center::RichNotificationData data;
  data.remove_on_click = true;
  data.context_message = u" ";
  data.buttons.emplace_back(u"Download");

  auto delegate =
      base::MakeRefCounted<message_center::HandleNotificationClickDelegate>(
          base::BindRepeating(&OnOfferClick, profile->GetWeakPtr(), url));

  message_center::Notification notification(
      message_center::NOTIFICATION_TYPE_SIMPLE, kNotificationId,
      u"Download with Falcon?", base::UTF8ToUTF16(name), ui::ImageModel(),
      u"Falcon", GURL(),
      message_center::NotifierId(message_center::NotifierType::SYSTEM_COMPONENT,
                                 kNotifierId),
      data, std::move(delegate));

  // Replace any earlier offer so only the latest copy is shown.
  service->Close(NotificationHandler::Type::TRANSIENT, kNotificationId);
  service->Display(NotificationHandler::Type::TRANSIENT, notification,
                   /*metadata=*/nullptr);
}

}  // namespace falcon
