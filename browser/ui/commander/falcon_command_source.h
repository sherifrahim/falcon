// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_COMMANDER_FALCON_COMMAND_SOURCE_H_
#define BRAVE_BROWSER_UI_COMMANDER_FALCON_COMMAND_SOURCE_H_

#include "brave/browser/ui/commander/command_source.h"

namespace commander {

// Falcon entries for Quick commands (Ctrl+Space): Falcon pages, toggles that
// live outside the IDC command table, and named sessions (save / restore /
// delete via composite prompts).
class FalconCommandSource : public CommandSource {
 public:
  FalconCommandSource();
  ~FalconCommandSource() override;
  FalconCommandSource(const FalconCommandSource&) = delete;
  FalconCommandSource& operator=(const FalconCommandSource&) = delete;

  // CommandSource:
  CommandSource::CommandResults GetCommands(
      const std::u16string& input,
      BrowserWindowInterface* browser) const override;
};

}  // namespace commander

#endif  // BRAVE_BROWSER_UI_COMMANDER_FALCON_COMMAND_SOURCE_H_
