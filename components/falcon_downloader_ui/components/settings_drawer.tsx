// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

import * as React from 'react'
import styled from 'styled-components'
import { sendWithPromise } from 'chrome://resources/js/cr.js'
import { Button, Input, Toggle } from './common'

export interface FalconSettings {
  engineEnabled: boolean
  minInterceptKb: number
  categoriesEnabled: boolean
  magnetEnabled: boolean
  notificationsEnabled: boolean
  showToolbarButton: boolean
  maxConnections: number
  maxConcurrent: number
  speedLimitKbps: number
  seedRatio: number
  seedTimeMinutes: number
  vtApiKey: string
  vtEnabled: boolean
  quarantineFlagged: boolean
  sandboxNetworking: boolean
}

const Drawer = styled.div`
  position: fixed;
  top: 0;
  right: 0;
  height: 100%;
  width: min(440px, 100%);
  background: var(--leo-color-page-background, #0f172a);
  border-left: 1px solid var(--leo-color-divider-subtle, #334155);
  box-shadow: -12px 0 40px rgba(0, 0, 0, 0.45);
  padding: 22px 22px 40px;
  overflow-y: auto;
  z-index: 20;
`

const Backdrop = styled.div`
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.35);
  z-index: 19;
`

const H = styled.h2`
  font-size: 18px;
  margin: 0 0 6px;
`

const Section = styled.h3`
  font-size: 12px;
  text-transform: uppercase;
  letter-spacing: 0.08em;
  opacity: 0.6;
  margin: 22px 0 4px;
`

export function SettingsDrawer(props: { onClose: () => void }) {
  const [s, setS] = React.useState<FalconSettings | null>(null)

  React.useEffect(() => {
    sendWithPromise('falcon_downloader.getSettings').then((v: FalconSettings) =>
      setS(v),
    )
  }, [])

  const update = (patch: Partial<FalconSettings>) => {
    if (!s) return
    const next = { ...s, ...patch }
    setS(next)
    chrome.send('falcon_downloader.setSettings', [patch])
  }

  const bool = (key: keyof FalconSettings, label: string, sub?: string) => (
    <Toggle>
      <span>
        {label}
        {sub && <span className="sub">{sub}</span>}
      </span>
      <input
        type="checkbox"
        checked={!!s?.[key]}
        onChange={(e) => update({ [key]: e.target.checked } as Partial<FalconSettings>)}
      />
    </Toggle>
  )

  const num = (
    key: keyof FalconSettings,
    label: string,
    sub: string,
    min: number,
    max: number,
    step = 1,
  ) => (
    <Toggle>
      <span>
        {label}
        <span className="sub">{sub}</span>
      </span>
      <Input
        type="number"
        min={min}
        max={max}
        step={step}
        value={s ? String(s[key]) : ''}
        onChange={(e) => {
          const v = step < 1 ? parseFloat(e.target.value) : parseInt(e.target.value, 10)
          if (isFinite(v)) update({ [key]: v } as Partial<FalconSettings>)
        }}
      />
    </Toggle>
  )

  return (
    <>
      <Backdrop onClick={props.onClose} />
      <Drawer>
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
          <H>Download engine settings</H>
          <Button $small onClick={props.onClose}>Close</Button>
        </div>
        {!s ? (
          <div style={{ opacity: 0.6 }}>Loading…</div>
        ) : (
          <>
            <Section>Browser integration</Section>
            {bool('engineEnabled', 'Use Falcon for downloads',
              'Take over downloads from pages automatically')}
            {num('minInterceptKb', 'Minimum size to take over', 'KB. Smaller files use the built-in downloader', 0, 1048576)}
            {bool('magnetEnabled', 'Handle magnet links', 'Open magnet: links in the engine')}
            {bool('notificationsEnabled', 'Notifications', 'Toast when a download finishes or fails')}
            {bool('showToolbarButton', 'Toolbar button', 'Show the Downloads button with progress ring')}
            {bool('categoriesEnabled', 'Sort into category folders',
              'Video, Music, Images, Documents, Compressed, Programs, Apps, Torrents')}

            <Section>Engine</Section>
            {num('maxConnections', 'Connections per download', '1–32 parallel segments per server', 1, 32)}
            {num('maxConcurrent', 'Simultaneous downloads', 'Others wait in the queue', 1, 20)}
            {num('speedLimitKbps', 'Global speed limit', 'KB/s, 0 = unlimited', 0, 1 << 24)}

            <Section>Torrents</Section>
            {num('seedRatio', 'Seed until ratio', '0 = stop seeding as soon as complete', 0, 100, 0.1)}
            {num('seedTimeMinutes', 'Seed time limit', 'Minutes, 0 = no time limit', 0, 1 << 20)}

            <Section>Security</Section>
            <Toggle as="div">
              <span>
                VirusTotal API key
                <span className="sub">Only the SHA-256 of finished files is sent, never the file. Free key at virustotal.com</span>
              </span>
              <Input
                type="password"
                placeholder="paste key"
                style={{ width: 170 }}
                value={s.vtApiKey}
                onChange={(e) => setS({ ...s, vtApiKey: e.target.value })}
                onBlur={(e) => update({ vtApiKey: e.target.value })}
              />
            </Toggle>
            {bool('vtEnabled', 'Check hashes on VirusTotal', 'When a key is set')}
            {bool('quarantineFlagged', 'Quarantine flagged files', 'Move files VirusTotal flags into a Quarantine folder')}
            {bool('sandboxNetworking', 'Network inside Windows Sandbox', 'Off is safer for suspicious files')}
          </>
        )}
      </Drawer>
    </>
  )
}
