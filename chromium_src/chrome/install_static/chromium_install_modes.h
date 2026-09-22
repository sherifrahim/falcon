/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

// Brand-specific types and constants for Google Chrome.

#ifndef BRAVE_CHROMIUM_SRC_CHROME_INSTALL_STATIC_CHROMIUM_INSTALL_MODES_H_
#define BRAVE_CHROMIUM_SRC_CHROME_INSTALL_STATIC_CHROMIUM_INSTALL_MODES_H_

#include <stdlib.h>

#include <array>

#include "brave/components/brave_origin/buildflags/buildflags.h"
#include "chrome/app/chrome_dll_resource.h"
#include "chrome/common/chrome_icon_resources_win.h"
#include "chrome/install_static/install_constants.h"

namespace install_static {

// Brand-specific constants and install modes for Brave.

// The brand-specific company name to be included as a component of the install
// and user data directory paths. May be empty if no such dir is to be used.
// Falcon: its own install / user-data identity (%LOCALAPPDATA%\Falcon\Falcon)
// so a Falcon install never touches a Brave install on the same machine.
inline constexpr wchar_t kCompanyPathName[] = L"Falcon";

// The brand-specific product name to be included as a component of the install
// and user data directory paths.
#if defined(OFFICIAL_BUILD)
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
// Brave Origin uses "Brave-Origin" instead of "Brave-Browser" to allow
// side-by-side installation with Brave Browser.
inline constexpr wchar_t kProductPathName[] = L"Brave-Origin";
#else
// Falcon: same identity as the developer build (see below), so an official
// Falcon install lands in %LOCALAPPDATA%\Falcon\Falcon and never registers
// as Brave (ProgIDs, app GUID, uninstall key).
inline constexpr wchar_t kProductPathName[] = L"Falcon";
#endif  // BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
#else
// If you change this, then you also need to change occurrences of this string
// in mini_installer_constants.cc.
inline constexpr wchar_t kProductPathName[] = L"Falcon";
#endif

// The brand-specific safe browsing client name.
inline constexpr char kSafeBrowsingName[] = "chromium";

// Note: This list of indices must be kept in sync with the brand-specific
// resource strings in chrome/installer/util/prebuild/create_string_rc.
enum InstallConstantIndex {
#if defined(OFFICIAL_BUILD)
  STABLE_INDEX,
  BETA_INDEX,
  DEV_INDEX,
  NIGHTLY_INDEX,
#else
  DEVELOPER_INDEX,
#endif
  NUM_INSTALL_MODES,
};

#if defined(OFFICIAL_BUILD)

// This is overriding the upstream value and shouldn't be undef'ed
// CHROMIUM_SRC_NOLINT
#define CHROMIUM_INDEX STABLE_INDEX

// Regarding the install switch, use the same values that are in
// chrome/installer/mini_installer/configuration.cc
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
// Brave Origin uses separate identifiers from Brave Browser to allow
// side-by-side installation and independent update infrastructure.
inline constexpr auto kInstallModes = std::to_array<InstallConstants>({
    // The primary install mode for stable Brave Origin.
    {
        .size = sizeof(InstallConstants),
        .index = STABLE_INDEX,  // The first mode is for stable/beta/dev.
        .install_switch =
            "",  // No install switch for the primary install mode.
        .install_suffix =
            L"",  // Empty install suffix - "Origin" is in kProductPathName.
        .logo_suffix = L"",  // No logo suffix for the primary install mode.
        .app_guid = L"{F1EF32DE-F987-4289-81D2-6C4780027F9B}",
        .base_app_name = L"Brave Origin",         // A distinct base_app_name.
        .base_app_id = L"BraveOrigin",            // A distinct base_app_id.
        .browser_prog_id_prefix = L"BraveOHTML",  // Browser ProgID prefix.
        .browser_prog_id_description =
            L"Brave Origin HTML Document",  // Browser ProgID description.
        .direct_launch_url_scheme = "brave-origin",
        .pdf_prog_id_prefix = L"BraveOPDF",  // PDF ProgID prefix.
        .pdf_prog_id_description =
            L"Brave Origin PDF Document",  // PDF ProgID description.
        .active_setup_guid =
            L"{F1EF32DE-F987-4289-81D2-6C4780027F9B}",  // Active Setup GUID.
        .toast_activator_clsid = {0x8a7b6c5d,
                                  0x4e3f,
                                  0x2a1b,
                                  {0x9c, 0x8d, 0x7e, 0x6f, 0x5a, 0x4b, 0x3c,
                                   0x2d}},  // Toast activator CLSID.
        .elevator_clsid = {0x1a2b3c4d,
                           0x5e6f,
                           0x7a8b,
                           {0x9c, 0x0d, 0x1e, 0x2f, 0x3a, 0x4b, 0x5c,
                            0x6d}},  // Elevator CLSID.
        .elevator_iid = {0x2b3c4d5e,
                         0x6f7a,
                         0x8b9c,
                         {0x0d, 0x1e, 0x2f, 0x3a, 0x4b, 0x5c, 0x6d, 0x7e}},
        .default_channel_name = L"",  // The empty string means "stable".
        .channel_strategy = ChannelStrategy::FLOATING,
        .supports_system_level = true,  // Supports system-level installs.
        .supports_set_as_default_browser =
            true,  // Supports in-product set as default browser UX.
        .app_icon_resource_index =
            icon_resources::kApplicationIndex,  // App icon resource index.
        .app_icon_resource_id = IDR_MAINFRAME,  // App icon resource id.
        .sandbox_sid_prefix =
            L"S-1-15-2-3251537155-1984446955-2931258699-841473695-1938553385-"
            L"934012153-",  // App container sid prefix for sandbox.
    },
    // A secondary install mode for Brave Origin Beta
    {
        .size = sizeof(InstallConstants),
        .index = BETA_INDEX,  // The mode for the side-by-side beta channel.
        .install_switch = "chrome-beta",  // Install switch.
        .install_suffix = L"-Beta",       // Install suffix.
        .logo_suffix = L"Beta",           // Logo suffix.
        .app_guid =
            L"{56DA94FD-D872-416B-BFC4-1D7011DA7473}",  // A distinct app GUID.
        .base_app_name = L"Brave Origin Beta",     // A distinct base_app_name.
        .base_app_id = L"BraveOriginBeta",         // A distinct base_app_id.
        .browser_prog_id_prefix = L"BraveOBHTML",  // Browser ProgID prefix.
        .browser_prog_id_description =
            L"Brave Origin Beta HTML Document",  // Browser ProgID description.
        .direct_launch_url_scheme = "brave-origin-beta",
        .pdf_prog_id_prefix = L"BraveOBPDF",  // PDF ProgID prefix.
        .pdf_prog_id_description =
            L"Brave Origin Beta PDF Document",  // PDF ProgID description.
        .active_setup_guid =
            L"{56DA94FD-D872-416B-BFC4-1D7011DA7473}",  // Active Setup GUID.
        .toast_activator_clsid = {0x3c4d5e6f,
                                  0x7a8b,
                                  0x9c0d,
                                  {0x1e, 0x2f, 0x3a, 0x4b, 0x5c, 0x6d, 0x7e,
                                   0x8f}},  // Toast activator CLSID.
        .elevator_clsid = {0x4d5e6f7a,
                           0x8b9c,
                           0x0d1e,
                           {0x2f, 0x3a, 0x4b, 0x5c, 0x6d, 0x7e, 0x8f,
                            0x9a}},  // Elevator CLSID.
        .elevator_iid = {0x5e6f7a8b,
                         0x9c0d,
                         0x1e2f,
                         {0x3a, 0x4b, 0x5c, 0x6d, 0x7e, 0x8f, 0x9a, 0x0b}},
        .default_channel_name = L"beta",  // Forced channel name.
        .channel_strategy = ChannelStrategy::FIXED,
        .supports_system_level = true,  // Supports system-level installs.
        .supports_set_as_default_browser =
            true,  // Supports in-product set as default browser UX.
        .app_icon_resource_index =
            icon_resources::kBetaApplicationIndex,  // App icon resource index.
        .app_icon_resource_id = IDR_X005_BETA,      // App icon resource id.
        .sandbox_sid_prefix =
            L"S-1-15-2-3251537155-1984446955-2931258699-841473695-1938553385-"
            L"934012154-",  // App container sid prefix for sandbox.
    },
    // A secondary install mode for Brave Origin Dev
    {
        .size = sizeof(InstallConstants),
        .index = DEV_INDEX,  // The mode for the side-by-side dev channel.
        .install_switch = "chrome-dev",  // Install switch.
        .install_suffix = L"-Dev",       // Install suffix.
        .logo_suffix = L"Dev",           // Logo suffix.
        .app_guid =
            L"{716D6A4A-D071-47A8-AC64-DBDE3EE3797B}",  // A distinct app GUID.
        .base_app_name = L"Brave Origin Dev",      // A distinct base_app_name.
        .base_app_id = L"BraveOriginDev",          // A distinct base_app_id.
        .browser_prog_id_prefix = L"BraveODHTML",  // Browser ProgID prefix.
        .browser_prog_id_description =
            L"Brave Origin Dev HTML Document",  // Browser ProgID description.
        .direct_launch_url_scheme = "brave-origin-dev",
        .pdf_prog_id_prefix = L"BraveODPDF",  // PDF ProgID prefix.
        .pdf_prog_id_description =
            L"Brave Origin Dev PDF Document",  // PDF ProgID description.
        .active_setup_guid =
            L"{716D6A4A-D071-47A8-AC64-DBDE3EE3797B}",  // Active Setup GUID.
        .toast_activator_clsid = {0x6f7a8b9c,
                                  0x0d1e,
                                  0x2f3a,
                                  {0x4b, 0x5c, 0x6d, 0x7e, 0x8f, 0x9a, 0x0b,
                                   0x1c}},  // Toast activator CLSID.
        .elevator_clsid = {0x7a8b9c0d,
                           0x1e2f,
                           0x3a4b,
                           {0x5c, 0x6d, 0x7e, 0x8f, 0x9a, 0x0b, 0x1c,
                            0x2d}},  // Elevator CLSID.
        .elevator_iid = {0x8b9c0d1e,
                         0x2f3a,
                         0x4b5c,
                         {0x6d, 0x7e, 0x8f, 0x9a, 0x0b, 0x1c, 0x2d, 0x3e}},
        .default_channel_name = L"dev",  // Forced channel name.
        .channel_strategy = ChannelStrategy::FIXED,
        .supports_system_level = true,  // Supports system-level installs.
        .supports_set_as_default_browser =
            true,  // Supports in-product set as default browser UX.
        .app_icon_resource_index =
            icon_resources::kDevApplicationIndex,  // App icon resource index.
        .app_icon_resource_id = IDR_X004_DEV,      // App icon resource id.
        .sandbox_sid_prefix =
            L"S-1-15-2-3251537155-1984446955-2931258699-841473695-1938553385-"
            L"934012155-",  // App container sid prefix for sandbox.
    },
    // A secondary install mode for Brave Origin SxS (nightly).
    {
        .size = sizeof(InstallConstants),
        .index =
            NIGHTLY_INDEX,  // The mode for the side-by-side nightly channel.
        .install_switch = "chrome-sxs",  // Install switch.
        .install_suffix = L"-Nightly",   // Install suffix.
        .logo_suffix = L"Canary",        // Logo suffix.
        .app_guid =
            L"{50474E96-9CD2-4BC8-B0A7-0D4B6EF2E709}",  // A distinct app GUID.
        .base_app_name = L"Brave Origin Nightly",  // A distinct base_app_name.
        .base_app_id = L"BraveOriginNightly",      // A distinct base_app_id.
        .browser_prog_id_prefix = L"BraveOSHTM",   // Browser ProgID prefix.
        .browser_prog_id_description =
            L"Brave Origin Nightly HTML Document",  // Browser ProgID
                                                    // description.
        .direct_launch_url_scheme = "brave-origin-nightly",
        .pdf_prog_id_prefix = L"BraveOSPDF",  // PDF ProgID prefix.
        .pdf_prog_id_description =
            L"Brave Origin Nightly PDF Document",  // PDF ProgID description.
        .active_setup_guid =
            L"{50474E96-9CD2-4BC8-B0A7-0D4B6EF2E709}",  // Active Setup GUID.
        .toast_activator_clsid = {0x9c0d1e2f,
                                  0x3a4b,
                                  0x5c6d,
                                  {0x7e, 0x8f, 0x9a, 0x0b, 0x1c, 0x2d, 0x3e,
                                   0x4f}},  // Toast activator CLSID.
        .elevator_clsid = {0x0d1e2f3a,
                           0x4b5c,
                           0x6d7e,
                           {0x8f, 0x9a, 0x0b, 0x1c, 0x2d, 0x3e, 0x4f,
                            0x5a}},  // Elevator CLSID.
        .elevator_iid = {0x1e2f3a4b,
                         0x5c6d,
                         0x7e8f,
                         {0x9a, 0x0b, 0x1c, 0x2d, 0x3e, 0x4f, 0x5a, 0x6b}},
        .default_channel_name = L"nightly",  // Forced channel name.
        .channel_strategy = ChannelStrategy::FIXED,
        .supports_system_level = true,  // Support system-level installs.
        .supports_set_as_default_browser =
            true,  // Support in-product set as default browser UX.
        .app_icon_resource_index =
            icon_resources::kSxSApplicationIndex,  // App icon resource index.
        .app_icon_resource_id = IDR_SXS,           // App icon resource id.
        .sandbox_sid_prefix =
            L"S-1-15-2-3251537155-1984446955-2931258699-841473695-1938553385-"
            L"934012156-",  // App container sid prefix for sandbox.
    },
});
#else   // !BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
inline constexpr auto kInstallModes = std::to_array<InstallConstants>({
    // The primary install mode for stable Brave.
    {
        .size = sizeof(InstallConstants),
        .index = STABLE_INDEX,  // The first mode is for stable/beta/dev.
        .install_switch =
            "",  // No install switch for the primary install mode.
        .install_suffix =
            L"",  // Empty install_suffix for the primary install mode.
        .logo_suffix = L"",  // No logo suffix for the primary install mode.
        .app_guid = L"{AB4F9326-6CB6-4AEA-A867-755EB432632B}",
        .base_app_name = L"Falcon",              // A distinct base_app_name.
        .base_app_id = L"Falcon",                // A distinct base_app_id.
        .browser_prog_id_prefix = L"FalconHTM",  // Browser ProgID prefix.
        .browser_prog_id_description =
            L"Falcon HTML Document",  // Browser ProgID description.
        .direct_launch_url_scheme = "falcon-browser",
        .pdf_prog_id_prefix = L"FalconPDF",  // PDF ProgID prefix.
        .pdf_prog_id_description =
            L"Falcon PDF Document",  // PDF ProgID description.
        .active_setup_guid =
            L"{94BE434E-8365-4223-B32C-2F03544AEEDC}",  // Active Setup GUID.
        .toast_activator_clsid = {0x25340684,
                                  0x37fe,
                                  0x475b,
                                  {0xb4, 0x26, 0x96, 0xc7, 0x6d, 0x2a, 0x9e,
                                   0xa1}},  // Toast activator CLSID.
        .elevator_clsid = {0x28a0ee1d,
                           0x6d98,
                           0x428a,
                           {0x9d, 0xa6, 0x20, 0x61, 0xc4, 0x7d, 0x1b,
                            0x4c}},  // Elevator CLSID.
        .elevator_iid = {0x61767c05,
                         0x6888,
                         0x4dba,
                         {0x87, 0xb6, 0xa1, 0xe7, 0x84, 0xf2, 0xac, 0xcb}},
        .default_channel_name = L"",  // The empty string means "stable".
        .channel_strategy = ChannelStrategy::FLOATING,
        .supports_system_level = true,  // Supports system-level installs.
        .supports_set_as_default_browser =
            true,  // Supports in-product set as default browser UX.
        .app_icon_resource_index =
            icon_resources::kApplicationIndex,  // App icon resource index.
        .app_icon_resource_id = IDR_MAINFRAME,  // App icon resource id.
        .sandbox_sid_prefix =
            L"S-1-15-2-3251537155-1984446955-2931258699-841473695-1938553385-"
            L"934012149-",  // App container sid prefix for sandbox.
    },
    // A secondary install mode for Brave Beta
    {
        .size = sizeof(InstallConstants),
        .index = BETA_INDEX,  // The mode for the side-by-side beta channel.
        .install_switch = "chrome-beta",  // Install switch.
        .install_suffix = L"-Beta",       // Install suffix.
        .logo_suffix = L"Beta",           // Logo suffix.
        .app_guid =
            L"{103BD053-949B-43A8-9120-2E424887DE11}",  // A distinct app GUID.
        .base_app_name = L"Brave Beta",           // A distinct base_app_name.
        .base_app_id = L"BraveBeta",              // A distinct base_app_id.
        .browser_prog_id_prefix = L"BraveBHTML",  // Browser ProgID prefix.
        .browser_prog_id_description =
            L"Brave Beta HTML Document",  // Browser ProgID description.
        .direct_launch_url_scheme = "brave-browser-beta",
        .pdf_prog_id_prefix = L"BraveBPDF",  // PDF ProgID prefix.
        .pdf_prog_id_description =
            L"Brave Beta PDF Document",  // PDF ProgID description.
        .active_setup_guid =
            L"{103BD053-949B-43A8-9120-2E424887DE11}",  // Active Setup GUID.
        .toast_activator_clsid = {0x9560028d,
                                  0xcca,
                                  0x49f0,
                                  {0x8d, 0x47, 0xef, 0x22, 0xbb, 0xc4, 0xb,
                                   0xa7}},  // Toast activator CLSID.
        .elevator_clsid = {0x2313f1cd,
                           0x41f3,
                           0x4347,
                           {0xbe, 0xc0, 0xd7, 0x22, 0xca, 0x41, 0x2c,
                            0x75}},  // Elevator CLSID.
        .elevator_iid = {0x9ebad7ac,
                         0x6e1e,
                         0x4a1c,
                         {0xaa, 0x85, 0x1a, 0x70, 0xca, 0xda, 0x8d, 0x82}},
        .default_channel_name = L"beta",  // Forced channel name.
        .channel_strategy = ChannelStrategy::FIXED,
        .supports_system_level = true,  // Supports system-level installs.
        .supports_set_as_default_browser =
            true,  // Supports in-product set as default browser UX.
        .app_icon_resource_index =
            icon_resources::kBetaApplicationIndex,  // App icon resource index.
        .app_icon_resource_id = IDR_X005_BETA,      // App icon resource id.
        .sandbox_sid_prefix =
            L"S-1-15-2-3251537155-1984446955-2931258699-841473695-1938553385-"
            L"934012150-",  // App container sid prefix for sandbox.
    },
    // A secondary install mode for Brave Dev
    {
        .size = sizeof(InstallConstants),
        .index = DEV_INDEX,  // The mode for the side-by-side dev channel.
        .install_switch = "chrome-dev",  // Install switch.
        .install_suffix = L"-Dev",       // Install suffix.
        .logo_suffix = L"Dev",           // Logo suffix.
        .app_guid =
            L"{CB2150F2-595F-4633-891A-E39720CE0531}",  // A distinct app GUID.
        .base_app_name = L"Brave Dev",            // A distinct base_app_name.
        .base_app_id = L"BraveDev",               // A distinct base_app_id.
        .browser_prog_id_prefix = L"BraveDHTML",  // Browser ProgID prefix.
        .browser_prog_id_description =
            L"Brave Dev HTML Document",  // Browser ProgID description.
        .direct_launch_url_scheme = "brave-browser-dev",
        .pdf_prog_id_prefix = L"BraveDPDF",  // PDF ProgID prefix.
        .pdf_prog_id_description =
            L"Brave Dev PDF Document",  // PDF ProgID description.
        .active_setup_guid =
            L"{CB2150F2-595F-4633-891A-E39720CE0531}",  // Active Setup GUID.
        .toast_activator_clsid = {0x20b22981,
                                  0xf63a,
                                  0x47a6,
                                  {0xa5, 0x47, 0x69, 0x1c, 0xc9, 0x4c, 0xae,
                                   0xe0}},  // Toast activator CLSID.
        .elevator_clsid = {0x9129ed6a,
                           0x11d3,
                           0x43b7,
                           {0xb7, 0x18, 0x8f, 0x82, 0x61, 0x45, 0x97,
                            0xa3}},  // Elevator CLSID.
        .elevator_iid = {0x1e43c77b,
                         0x48e6,
                         0x4a4c,
                         {0x9d, 0xb2, 0xc2, 0x97, 0x17, 0x06, 0xc2, 0x55}},
        .default_channel_name = L"dev",  // Forced channel name.
        .channel_strategy = ChannelStrategy::FIXED,
        .supports_system_level = true,  // Supports system-level installs.
        .supports_set_as_default_browser =
            true,  // Supports in-product set as default browser UX.
        .app_icon_resource_index =
            icon_resources::kDevApplicationIndex,  // App icon resource index.
        .app_icon_resource_id = IDR_X004_DEV,      // App icon resource id.
        .sandbox_sid_prefix =
            L"S-1-15-2-3251537155-1984446955-2931258699-841473695-1938553385-"
            L"934012151-",  // App container sid prefix for sandbox.
    },
    // A secondary install mode for Brave SxS (canary).
    {
        .size = sizeof(InstallConstants),
        .index =
            NIGHTLY_INDEX,  // The mode for the side-by-side nightly channel.
        .install_switch = "chrome-sxs",  // Install switch.
        .install_suffix = L"-Nightly",   // Install suffix.
        .logo_suffix = L"Canary",        // Logo suffix.
        .app_guid =
            L"{C6CB981E-DB30-4876-8639-109F8933582C}",  // A distinct app GUID.
        .base_app_name = L"Brave Nightly",        // A distinct base_app_name.
        .base_app_id = L"BraveNightly",           // A distinct base_app_id.
        .browser_prog_id_prefix = L"BraveSSHTM",  // Browser ProgID prefix.
        .browser_prog_id_description =
            L"Brave Nightly HTML Document",  // Browser ProgID description.
        .direct_launch_url_scheme = "brave-browser-nightly",
        .pdf_prog_id_prefix = L"BraveSSPDF",  // PDF ProgID prefix.
        .pdf_prog_id_description =
            L"Brave Nightly PDF Document",  // PDF ProgID description.
        .active_setup_guid =
            L"{C6CB981E-DB30-4876-8639-109F8933582C}",  // Active Setup GUID.
        .toast_activator_clsid = {0xf2edbc59,
                                  0x7217,
                                  0x4da5,
                                  {0xa2, 0x59, 0x3, 0x2, 0xda, 0x6a, 0x0,
                                   0xe1}},  // Toast activator CLSID.
        .elevator_clsid = {0x1ce2f84f,
                           0x70cb,
                           0x4389,
                           {0x87, 0xdb, 0xd0, 0x99, 0x48, 0x30, 0xbb,
                            0x17}},  // Elevator CLSID.
        .elevator_iid = {0x1db2116f,
                         0x71b7,
                         0x49f0,
                         {0x89, 0x70, 0x33, 0xb1, 0xda, 0xcf, 0xb0, 0x72}},
        .default_channel_name = L"nightly",  // Forced channel name.
        .channel_strategy = ChannelStrategy::FIXED,
        .supports_system_level = true,  // Support system-level installs.
        .supports_set_as_default_browser =
            true,  // Support in-product set as default browser UX.
        .app_icon_resource_index =
            icon_resources::kSxSApplicationIndex,  // App icon resource index.
        .app_icon_resource_id = IDR_SXS,           // App icon resource id.
        .sandbox_sid_prefix =
            L"S-1-15-2-3251537155-1984446955-2931258699-841473695-1938553385-"
            L"934012152-",  // App container sid prefix for sandbox.
    },
});
#endif  // BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
#else

// CHROMIUM_SRC_NOLINT
#define CHROMIUM_INDEX DEVELOPER_INDEX

inline constexpr auto kInstallModes = std::to_array<InstallConstants>({
    // The primary (and only) install mode for Brave developer build.
    {
        .size = sizeof(InstallConstants),
        .index = DEVELOPER_INDEX,  // The one and only mode for developer mode.
        .install_switch =
            "",  // No install switch for the primary install mode.
        .install_suffix =
            L"",  // Empty install_suffix for the primary install mode.
        .logo_suffix = L"",  // No logo suffix for the primary install mode.
        .app_guid =
            L"",  // Empty app_guid since no integraion with Brave Update.
        .base_app_name = L"Falcon",               // A distinct base_app_name.
        .base_app_id = L"Falcon",                 // A distinct base_app_id.
        .browser_prog_id_prefix = L"FalconHTM",   // Browser ProgID prefix.
        .browser_prog_id_description =
            L"Falcon HTML Document",  // Browser ProgID description.
        .direct_launch_url_scheme = "falcon-browser",
        .pdf_prog_id_prefix = L"FalconPDF",  // PDF ProgID prefix.
        .pdf_prog_id_description =
            L"Falcon PDF Document",  // PDF ProgID description.
        .active_setup_guid =
            L"{0C82D7E2-EC44-4652-B5C8-8704A309469C}",  // Active Setup GUID.
        .toast_activator_clsid = {0x0f3ac38b,
                                  0xa746,
                                  0x4f4b,
                                  {0x86, 0x48, 0x03, 0xa8, 0xca, 0xb2, 0xb0,
                                   0xce}},  // Toast activator CLSID.
        .elevator_clsid = {0xb369ce7a,
                           0x8bb5,
                           0x4281,
                           {0xb4, 0x42, 0xab, 0xdb, 0x1f, 0x56, 0x9a,
                            0x41}},  // Elevator CLSID.
        .elevator_iid = {0x420829dc,
                         0xdf5b,
                         0x4f04,
                         {0x9a, 0xf7, 0xe7, 0x72, 0x00, 0x1e, 0xb6, 0x1d}},
        .default_channel_name =
            L"",  // Empty default channel name since no update integration.
        .channel_strategy = ChannelStrategy::UNSUPPORTED,
        .supports_system_level = true,  // Supports system-level installs.
        .supports_set_as_default_browser =
            true,  // Supports in-product set as default browser UX.
        .app_icon_resource_index =
            icon_resources::kApplicationIndex,  // App icon resource index.
        .app_icon_resource_id = IDR_MAINFRAME,  // App icon resource id.
        .sandbox_sid_prefix =
            L"S-1-15-2-3251537155-1984446955-2931258699-841473695-1938553385-"
            L"934012148-",  // App container sid prefix for sandbox.
    },
});
#endif

}  // namespace install_static

#endif  // BRAVE_CHROMIUM_SRC_CHROME_INSTALL_STATIC_CHROMIUM_INSTALL_MODES_H_
