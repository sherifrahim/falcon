// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

import * as React from 'react'
import { createRoot } from 'react-dom/client'
import StyledComponentsProvider from '$web-common/StyledComponentsProvider'

import { Deck } from './components/deck'

function initialize() {
  const container = document.getElementById('root')
  if (container) {
    createRoot(container).render(
      <StyledComponentsProvider>
        <Deck />
      </StyledComponentsProvider>,
    )
  }
}

document.addEventListener('DOMContentLoaded', initialize)
