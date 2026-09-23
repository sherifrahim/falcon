// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_COMPONENTS_CONSTANTS_FALCON_PREF_NAMES_H_
#define BRAVE_COMPONENTS_CONSTANTS_FALCON_PREF_NAMES_H_

// Falcon pref names that both platforms need. This header is parsed by
// java_cpp_strings (see //brave/browser/android/preferences/BUILD.gn), so the
// Android side can name the same pref without a JNI bridge of its own.

namespace falcon::prefs {

// Profile pref, syncable: the whole home page configuration (clock, greeting,
// background, quick links, widgets, weather) as a JSON string. A string rather
// than a dictionary so Java can read and write it through PrefService, which
// only exposes scalars.
inline constexpr char kNtpConfig[] = "falcon.ntp.config";

}  // namespace falcon::prefs

#endif  // BRAVE_COMPONENTS_CONSTANTS_FALCON_PREF_NAMES_H_
