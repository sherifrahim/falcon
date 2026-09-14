// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

import * as React from 'react'
import styled from 'styled-components'
import { sendWithPromise } from 'chrome://resources/js/cr.js'
import { Button, Input, Select, Toggle } from './common'

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
  clipboardMonitor: boolean
  categoryRules: string
  skipHosts: string
  skipExtensions: string
  proxy: string
  btTrackers: string
  duplicateAction: 'rename' | 'overwrite'
  scheduleEnabled: boolean
  scheduleStart: string
  scheduleStop: string
  historyKeepDays: number
  mouseGestures: boolean
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

const Rules = styled.textarea`
  width: 100%;
  min-height: 84px;
  margin: 6px 0 2px;
  padding: 8px 10px;
  border-radius: 10px;
  border: 1px solid var(--leo-color-divider-subtle, #334155);
  background: var(--leo-color-container-background, #1e293b);
  color: inherit;
  font-family: ui-monospace, Consolas, monospace;
  font-size: 12px;
  resize: vertical;
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

  const text = (key: keyof FalconSettings, label: string, sub: string, placeholder = '', type = 'text', width = 170) => (
    <Toggle as="div">
      <span>
        {label}
        <span className="sub">{sub}</span>
      </span>
      <Input
        type={type}
        placeholder={placeholder}
        style={{ width }}
        value={s ? String(s[key]) : ''}
        onChange={(e) => s && setS({ ...s, [key]: e.target.value })}
        onBlur={(e) => update({ [key]: e.target.value } as Partial<FalconSettings>)}
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
            <Section>Browser</Section>
            {bool('mouseGestures', 'Mouse gestures',
              'Hold right button and drag: ← back · → forward · ↑ reload · ↓ new tab · ↓→ close tab · ↓← reopen closed · ↑← / ↑→ switch tabs')}

            <Section>Browser integration</Section>
            {bool('engineEnabled', 'Use Falcon for downloads',
              'Take over downloads from pages automatically')}
            {num('minInterceptKb', 'Minimum size to take over', 'KB. Smaller files use the built-in downloader', 0, 1048576)}
            <div style={{ fontSize: 13, paddingTop: 10 }}>
              Never take over from these sites
              <span style={{ display: 'block', fontSize: 12, opacity: 0.65 }}>One host per line (matches sub-domains too); those downloads use the built-in downloader</span>
              <Rules value={s.skipHosts} spellCheck={false} placeholder={'drive.google.com'} style={{ minHeight: 56 }}
                onChange={(e) => setS({ ...s, skipHosts: e.target.value })} onBlur={(e) => update({ skipHosts: e.target.value })} />
            </div>
            <div style={{ fontSize: 13, paddingTop: 6 }}>
              Never take over these file types
              <span style={{ display: 'block', fontSize: 12, opacity: 0.65 }}>Extensions, one per line</span>
              <Rules value={s.skipExtensions} spellCheck={false} placeholder={'pdf'} style={{ minHeight: 56 }}
                onChange={(e) => setS({ ...s, skipExtensions: e.target.value })} onBlur={(e) => update({ skipExtensions: e.target.value })} />
            </div>
            {bool('magnetEnabled', 'Handle magnet links', 'Open magnet: links in the engine')}
            {bool('notificationsEnabled', 'Notifications', 'Toast when a download finishes or fails')}
            {bool('showToolbarButton', 'Toolbar button', 'Show the Downloads button with progress ring')}
            {bool('clipboardMonitor', 'Watch the clipboard',
              'Offer to download file links and magnets copied anywhere on the PC')}
            {bool('categoriesEnabled', 'Sort into category folders',
              'Video, Music, Images, Documents, Compressed, Programs, Apps, Torrents')}
            <div style={{ fontSize: 13, paddingTop: 10 }}>
              Custom folder rules
              <span style={{ display: 'block', fontSize: 12, opacity: 0.65 }}>
                One per line: <code>ext = Folder</code>. Folder is a sub-folder of Downloads, or a full path.
                Example: <code>psd = Design</code>, <code>{'iso = D:\\ISOs'}</code>
              </span>
              <Rules
                value={s.categoryRules}
                spellCheck={false}
                onChange={(e) => setS({ ...s, categoryRules: e.target.value })}
                onBlur={(e) => update({ categoryRules: e.target.value })}
              />
            </div>
            <Toggle as="div">
              <span>
                When the file already exists
                <span className="sub">Rename keeps both (file.1.zip); overwrite replaces it</span>
              </span>
              <Select value={s.duplicateAction} onChange={(e) => update({ duplicateAction: e.target.value as FalconSettings['duplicateAction'] })}>
                <option value="rename">Rename new file</option>
                <option value="overwrite">Overwrite</option>
              </Select>
            </Toggle>
            {num('historyKeepDays', 'Keep history for', 'Days, 0 = forever. Finished entries stay listed after restarts', 0, 3650)}

            <Section>Engine</Section>
            {num('maxConnections', 'Connections per download', '1–32 parallel segments per server', 1, 32)}
            {num('maxConcurrent', 'Simultaneous downloads', 'Others wait in the queue', 1, 20)}
            {num('speedLimitKbps', 'Global speed limit', 'KB/s, 0 = unlimited', 0, 1 << 24)}
            {text('proxy', 'Proxy for the engine', 'http://, https:// or socks5://host:port — blank = direct. user:pass@ allowed', 'none')}

            <Section>Schedule</Section>
            {bool('scheduleEnabled', 'Only download during a time window',
              'Everything pauses outside the window and resumes inside it; new downloads queue as paused')}
            {text('scheduleStart', 'Start at', '24h clock, e.g. 01:00', '01:00', 'time', 130)}
            {text('scheduleStop', 'Stop at', 'May be past midnight', '07:00', 'time', 130)}

            <Section>Torrents</Section>
            {num('seedRatio', 'Seed until ratio', '0 = stop seeding as soon as complete', 0, 100, 0.1)}
            {num('seedTimeMinutes', 'Seed time limit', 'Minutes, 0 = no time limit', 0, 1 << 20)}
            <div style={{ fontSize: 13, paddingTop: 10 }}>
              Extra trackers
              <span style={{ display: 'block', fontSize: 12, opacity: 0.65 }}>
                Added to every torrent and magnet (one per line, udp:// or http://). Public lists: ngosang/trackerslist on GitHub.
              </span>
              <Rules
                value={s.btTrackers}
                spellCheck={false}
                placeholder={'udp://tracker.opentrackr.org:1337/announce\nudp://open.stealth.si:80/announce'}
                onChange={(e) => setS({ ...s, btTrackers: e.target.value })}
                onBlur={(e) => update({ btTrackers: e.target.value })}
              />
            </div>

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
