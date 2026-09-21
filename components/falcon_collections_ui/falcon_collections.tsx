// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

import * as React from 'react'
import { loadTimeData } from '$web-common/loadTimeData'
import { createRoot } from 'react-dom/client'
import StyledComponentsProvider from '$web-common/StyledComponentsProvider'

import { App } from './components/app'

function initialize() {
  // Pitch-black scheme: flip the CSS tokens (inline scripts are blocked by CSP).
  try { if (loadTimeData.getBoolean('blackTheme')) document.documentElement.dataset.black = '1' } catch (e) {}
  const container = document.getElementById('root')
  if (container) {
    createRoot(container).render(
      <StyledComponentsProvider>
        <App />
      </StyledComponentsProvider>,
    )
  }
}

document.addEventListener('DOMContentLoaded', initialize)
