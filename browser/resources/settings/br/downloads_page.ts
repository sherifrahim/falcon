// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

import type { PropertyValues } from '//resources/lit/v3_0/lit.rollup.js'

import { SettingsDownloadsPageElement } from '../downloads_page/downloads_page.js'

// Falcon's download engine has its own options inside falcon://downloader.
// Add a row on the Chromium Downloads page that links to them.
const addFalconRow = (root: ShadowRoot) => {
  if (root.querySelector('#falconEngineRow')) {
    return
  }
  const section = root.querySelector('settings-section')
  if (!section) {
    console.error('[Settings] Could not find settings-section on downloads page')
    return
  }
  const row = document.createElement('a')
  row.id = 'falconEngineRow'
  row.className = 'cr-row cr-link-row'
  row.href = 'chrome://downloader#settings'
  row.style.textDecoration = 'none'
  row.style.color = 'inherit'
  row.innerHTML = `
    <div class="flex cr-padded-text">
      <div>Falcon download engine</div>
      <div class="secondary">Connections, categories, torrent seeding, speed limits and more</div>
    </div>
    <cr-icon-button class="subpage-arrow" iron-icon="cr:arrow-right"></cr-icon-button>`
  section.appendChild(row)
}

const proto = SettingsDownloadsPageElement.prototype as unknown as {
  firstUpdated?: (changedProperties: PropertyValues) => void
}
const originalFirstUpdated = proto.firstUpdated
proto.firstUpdated = function (
  this: SettingsDownloadsPageElement,
  changedProperties: PropertyValues,
) {
  originalFirstUpdated?.call(this, changedProperties)
  if (this.shadowRoot) {
    addFalconRow(this.shadowRoot)
  }
}
