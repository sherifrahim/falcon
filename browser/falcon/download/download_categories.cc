// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/download/download_categories.h"

#include <array>
#include <string_view>

#include "base/containers/fixed_flat_map.h"
#include "base/strings/escape.h"
#include "base/strings/string_util.h"
#include "url/gurl.h"

namespace falcon {

namespace {

constexpr auto kExtensionToCategory =
    base::MakeFixedFlatMap<std::string_view, std::string_view>({
        // Video
        {"mp4", "Video"}, {"mkv", "Video"}, {"avi", "Video"}, {"mov", "Video"},
        {"wmv", "Video"}, {"flv", "Video"}, {"webm", "Video"}, {"m4v", "Video"},
        {"ts", "Video"}, {"mpg", "Video"}, {"mpeg", "Video"}, {"3gp", "Video"},
        // Music
        {"mp3", "Music"}, {"flac", "Music"}, {"wav", "Music"}, {"aac", "Music"},
        {"ogg", "Music"}, {"m4a", "Music"}, {"wma", "Music"}, {"opus", "Music"},
        // Images
        {"jpg", "Images"}, {"jpeg", "Images"}, {"png", "Images"}, {"gif", "Images"},
        {"webp", "Images"}, {"svg", "Images"}, {"bmp", "Images"}, {"heic", "Images"},
        {"avif", "Images"}, {"tiff", "Images"}, {"psd", "Images"},
        // Documents
        {"pdf", "Documents"}, {"doc", "Documents"}, {"docx", "Documents"},
        {"xls", "Documents"}, {"xlsx", "Documents"}, {"ppt", "Documents"},
        {"pptx", "Documents"}, {"txt", "Documents"}, {"epub", "Documents"},
        {"csv", "Documents"}, {"odt", "Documents"}, {"ods", "Documents"},
        {"rtf", "Documents"}, {"md", "Documents"}, {"mobi", "Documents"},
        // Compressed
        {"zip", "Compressed"}, {"rar", "Compressed"}, {"7z", "Compressed"},
        {"tar", "Compressed"}, {"gz", "Compressed"}, {"bz2", "Compressed"},
        {"xz", "Compressed"}, {"zst", "Compressed"}, {"tgz", "Compressed"},
        // Programs
        {"exe", "Programs"}, {"msi", "Programs"}, {"msix", "Programs"},
        {"dmg", "Programs"}, {"deb", "Programs"}, {"rpm", "Programs"},
        {"appimage", "Programs"}, {"iso", "Programs"}, {"img", "Programs"},
        {"pkg", "Programs"},
        // Apps
        {"apk", "Apps"}, {"xapk", "Apps"}, {"aab", "Apps"}, {"ipa", "Apps"},
        // Torrents
        {"torrent", "Torrents"},
    });

}  // namespace

std::string CategoryForFilename(std::string_view filename) {
  const size_t dot = filename.rfind('.');
  if (dot == std::string_view::npos || dot + 1 >= filename.size()) {
    return std::string();
  }
  const std::string ext =
      base::ToLowerASCII(std::string(filename.substr(dot + 1)));
  if (const auto it = kExtensionToCategory.find(ext);
      it != kExtensionToCategory.end()) {
    return std::string(it->second);
  }
  return std::string();
}

std::string GuessFilename(const GURL& url,
                          const std::string& content_disposition_filename) {
  if (!content_disposition_filename.empty()) {
    return content_disposition_filename;
  }
  if (!url.is_valid() || !url.has_path()) {
    return std::string();
  }
  std::string path(url.path());
  const size_t slash = path.rfind('/');
  if (slash != std::string::npos) {
    path = path.substr(slash + 1);
  }
  return base::UnescapeBinaryURLComponent(path);
}

}  // namespace falcon
