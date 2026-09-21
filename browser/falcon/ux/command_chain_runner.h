// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_UX_COMMAND_CHAIN_RUNNER_H_
#define BRAVE_BROWSER_FALCON_UX_COMMAND_CHAIN_RUNNER_H_

#include <string>
#include <vector>

#include "base/containers/span.h"
#include "base/values.h"

class BrowserWindowInterface;
class Profile;

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace falcon {

namespace prefs {
// List of chains: {id, name, steps: [{type, value}]}. Step types:
//   "url"     open |value| in a new foreground tab
//   "goto"    navigate the active tab to |value|
//   "command" run the browser command whose key is |value| (KnownCommands)
//   "deck"    run the first Quick-commands result for the query |value|
//   "wait"    pause |value| milliseconds (≤ 60000)
inline constexpr char kCommandChains[] = "falcon.chains";
void RegisterChainPrefs(user_prefs::PrefRegistrySyncable* registry);
}  // namespace prefs

struct CommandChainStep {
  std::string type;
  std::string value;
};

struct CommandChain {
  CommandChain();
  CommandChain(const CommandChain&);
  ~CommandChain();
  std::string id;
  std::string name;
  std::vector<CommandChainStep> steps;
};

// Vivaldi-style command chains: a saved sequence of steps run one after the
// other on a browser window. Chains live in prefs; the control panel edits
// them and the command deck / app menu run them.
class CommandChainRunner {
 public:
  struct KnownCommand {
    const char* key;
    const char16_t* title;
    int command_id;  // 0: handled by key in Execute()
  };
  static base::span<const KnownCommand> KnownCommands();

  static std::vector<CommandChain> Chains(Profile* profile);
  static void Run(BrowserWindowInterface* browser, const std::string& chain_id);
  static void RunSteps(BrowserWindowInterface* browser,
                       std::vector<CommandChainStep> steps);

  // Validates and stores the list the control panel sends.
  static void SetChains(Profile* profile, const base::ListValue& chains);
};

}  // namespace falcon

#endif  // BRAVE_BROWSER_FALCON_UX_COMMAND_CHAIN_RUNNER_H_
