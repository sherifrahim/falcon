/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Falcon has no crash server and never reports to Brave's. Upload is already
// off (MetricsReportingEnabled is policy-disabled and the ask dialog is built
// out), but an empty URL means there is nowhere to send a dump even if consent
// were somehow granted. Upstream returns https://cr.brave.com for official
// builds, and Falcon's Release is an official build.
#define BRAVE_CRASH_REPORTER_CLIENT_GET_UPLOAD_URL return std::string();

#include <components/crash/core/app/crash_reporter_client.cc>
#undef BRAVE_CRASH_REPORTER_CLIENT_GET_UPLOAD_URL
