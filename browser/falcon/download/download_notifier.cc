// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/download/download_notifier.h"

#include <memory>
#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/utf_string_conversions.h"
#include "brave/browser/falcon/download/pref_names.h"
#include "brave/components/constants/webui_url_constants.h"
#include "chrome/browser/notifications/notification_display_service.h"
#include "chrome/browser/notifications/notification_display_service_factory.h"
#include "chrome/browser/notifications/notification_handler.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/singleton_tabs.h"
#include "components/prefs/pref_service.h"
#include "ui/base/models/image_model.h"
#include "ui/message_center/public/cpp/notification.h"
#include "ui/message_center/public/cpp/notification_delegate.h"
#include "ui/message_center/public/cpp/notifier_id.h"
#include "url/gurl.h"

namespace falcon {

namespace {

constexpr char kNotifierId[] = "falcon.downloads";

void OnNotificationClick(base::WeakPtr<Profile> profile,
                         base::FilePath path,
                         std::optional<int> button_index) {
  if (!profile) {
    return;
  }
  if (!path.empty()) {
    platform_util::OpenItem(profile.get(), path, platform_util::OPEN_FILE,
                            platform_util::OpenOperationCallback());
    return;
  }
  ShowSingletonTab(profile.get(),
                   GURL(std::string("chrome://") + kFalconDownloaderHost));
}

}  // namespace

DownloadNotifier::DownloadNotifier(DownloadTracker* tracker) {
  observation_.Observe(tracker);
}

DownloadNotifier::~DownloadNotifier() = default;

void DownloadNotifier::OnDownloadFinished(
    const DownloadTracker::Finished& finished) {
  Profile* profile = ProfileManager::GetLastUsedProfileIfLoaded();
  if (!profile ||
      !profile->GetPrefs()->GetBoolean(prefs::kDownloadNotificationsEnabled)) {
    return;
  }
  auto* service = NotificationDisplayServiceFactory::GetForProfile(profile);
  if (!service) {
    return;
  }

  const std::u16string title =
      finished.success ? u"Download complete" : u"Download failed";
  std::u16string message = base::UTF8ToUTF16(finished.name);
  if (!finished.success && !finished.error_message.empty()) {
    message += u"\n" + base::UTF8ToUTF16(finished.error_message);
  }

  message_center::RichNotificationData data;
  data.remove_on_click = true;
  data.context_message = u" ";  // hide origin line

  const base::FilePath path =
      finished.success ? base::FilePath::FromUTF8Unsafe(finished.path)
                       : base::FilePath();
  auto delegate =
      base::MakeRefCounted<message_center::HandleNotificationClickDelegate>(
          base::BindRepeating(&OnNotificationClick, profile->GetWeakPtr(),
                              path));

  message_center::Notification notification(
      message_center::NOTIFICATION_TYPE_SIMPLE,
      "falcon-download-" + finished.gid, title, message, ui::ImageModel(),
      u"Falcon", GURL(),
      message_center::NotifierId(message_center::NotifierType::SYSTEM_COMPONENT,
                                 kNotifierId),
      data, std::move(delegate));

  service->Display(NotificationHandler::Type::TRANSIENT, notification,
                   /*metadata=*/nullptr);
}

}  // namespace falcon
