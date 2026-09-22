// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_COMPONENTS_CONSTANTS_FALCON_VERSION_H_
#define BRAVE_COMPONENTS_CONSTANTS_FALCON_VERSION_H_

namespace falcon {

// Falcon's own release version (semver). Bump before tagging `v<version>`;
// .github/workflows/falcon-release.yml reads it to name the installer and the
// About page compares it with the latest GitHub release.
inline constexpr char kFalconVersion[] = "1.0.0";

// Where releases live (GitHub Releases of the fork).
inline constexpr char kFalconRepo[] = "sherifrahim/falcon";

}  // namespace falcon

#endif  // BRAVE_COMPONENTS_CONSTANTS_FALCON_VERSION_H_
