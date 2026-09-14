// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

// falcon://falcon — the control panel: look, behaviour, engines, about.

import * as React from 'react'
import styled from 'styled-components'
import { sendWithPromise } from 'chrome://resources/js/cr.js'

interface State {
  colorScheme: 0 | 1 | 2 // system, light, dark
  verticalTabs: boolean
  sidebarShow: 0 | 1 | 3 // always, on hover, never
  roundedCorners: boolean
  mouseGestures: boolean
  videoPill: boolean
  clipboardMonitor: boolean
  engineEnabled: boolean
  chromiumVersion: string
  braveVersion: string
  ytDlpVersion: string
  aria2Version: string
}

const Page = styled.div`
  max-width: 760px;
  margin: 0 auto;
  padding: 36px 24px 60px;
  h1 { font-size: 28px; font-weight: 700; margin: 0 0 4px; display: flex; align-items: center; gap: 12px; }
  h1 span.sub { font-size: 13px; font-weight: 400; opacity: 0.6; }
  h2 { font-size: 12px; letter-spacing: 0.1em; text-transform: uppercase; opacity: 0.6; margin: 28px 0 6px; }
  p.hint { font-size: 13px; opacity: 0.7; margin: 0 0 12px; }
`

const Card = styled.div`
  background: var(--leo-color-container-background, #1e293b);
  border: 1px solid var(--leo-color-divider-subtle, #334155);
  border-radius: 14px;
  padding: 4px 16px;
`

const Row = styled.label`
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  padding: 12px 0;
  border-bottom: 1px solid var(--leo-color-divider-subtle, #334155);
  font-size: 14px;
  cursor: pointer;
  &:last-child { border-bottom: none; }
  .sub { display: block; font-size: 12px; opacity: 0.65; margin-top: 2px; }
  input[type='checkbox'] { width: 18px; height: 18px; accent-color: #0ea5e9; }
  select { padding: 6px 10px; border-radius: 10px; border: 1px solid #334155; background: #0f172a; color: inherit; font-size: 13px; }
`

const Seg = styled.div`
  display: inline-flex;
  border: 1px solid #334155;
  border-radius: 999px;
  overflow: hidden;
  button { padding: 6px 14px; border: none; background: transparent; color: inherit; cursor: pointer; font-size: 13px; }
  button.on { background: rgba(56, 189, 248, 0.2); color: #e0f2fe; }
`

const Btn = styled.button<{ $primary?: boolean }>`
  padding: 8px 14px;
  border-radius: 10px;
  border: 1px solid transparent;
  background: ${(p) => (p.$primary ? '#0ea5e9' : '#0f172a')};
  color: ${(p) => (p.$primary ? '#fff' : 'inherit')};
  font-size: 13px;
  font-weight: 600;
  cursor: pointer;
  &:hover { filter: brightness(1.15); }
  &:disabled { opacity: 0.5; cursor: default; }
`

const Links = styled.div`
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 10px;
  a {
    display: block; padding: 14px 16px; border-radius: 14px; color: inherit; text-decoration: none;
    background: var(--leo-color-container-background, #1e293b); border: 1px solid var(--leo-color-divider-subtle, #334155);
  }
  a:hover { border-color: #38bdf8; }
  a b { display: block; font-size: 14px; margin-bottom: 2px; }
  a span { font-size: 12px; opacity: 0.65; }
`

export function App() {
  const [s, setS] = React.useState<State | null>(null)
  const [updateLog, setUpdateLog] = React.useState<string | null>(null)
  const [updating, setUpdating] = React.useState(false)

  React.useEffect(() => {
    sendWithPromise('falcon_control.getState').then(setS).catch(() => {})
  }, [])

  const update = (patch: Partial<State>) => {
    if (!s) return
    setS({ ...s, ...patch })
    chrome.send('falcon_control.setState', [patch])
  }

  const bool = (key: keyof State, label: string, sub?: string) => (
    <Row>
      <span>{label}{sub && <span className="sub">{sub}</span>}</span>
      <input type="checkbox" checked={!!s?.[key]} onChange={(e) => update({ [key]: e.target.checked } as Partial<State>)} />
    </Row>
  )

  const updateYtDlp = async () => {
    setUpdating(true)
    setUpdateLog('Checking for a newer yt-dlp…')
    try {
      const r: { ok: boolean; output: string; version: string } = await sendWithPromise('falcon_control.updateYtDlp')
      setUpdateLog(r.output || (r.ok ? 'Up to date.' : 'Update failed.'))
      if (s && r.version) setS({ ...s, ytDlpVersion: r.version })
    } catch (e: any) {
      setUpdateLog(e?.message ?? 'Update failed')
    } finally {
      setUpdating(false)
    }
  }

  if (!s) return <Page><h1>Falcon</h1><p className="hint">Loading…</p></Page>

  return (
    <Page>
      <h1>Falcon <span className="sub">control panel · falcon://falcon</span></h1>
      <p className="hint">Everything Falcon adds on top of the browser, in one place. Brave/Chromium settings stay at <a href="chrome://settings" style={{ color: '#38bdf8' }}>settings</a>.</p>

      <Links>
        <a href="chrome://downloader"><b>Downloads</b><span>Engine, media, torrents, security</span></a>
        <a href="chrome://downloader#settings"><b>Download settings</b><span>Connections, folders, VirusTotal, scheduler…</span></a>
        <a href="chrome://newtab"><b>New tab</b><span>Wallpapers, widgets (gear at bottom-right)</span></a>
        <a href="chrome://settings/appearance"><b>Appearance</b><span>Themes, fonts, toolbar, tabs</span></a>
      </Links>

      <h2>Look</h2>
      <Card>
        <Row as="div">
          <span>Colour scheme<span className="sub">Falcon's slate/sky palette follows this</span></span>
          <Seg>
            {([[2, 'Dark'], [1, 'Light'], [0, 'System']] as Array<[0 | 1 | 2, string]>).map(([v, l]) => (
              <button key={v} className={s.colorScheme === v ? 'on' : ''} onClick={() => update({ colorScheme: v })}>{l}</button>
            ))}
          </Seg>
        </Row>
        {bool('verticalTabs', 'Vertical tabs', 'Tab strip on the side (Zen / Arc style)')}
        <Row as="div">
          <span>Sidebar<span className="sub">Bookmarks, reading list, downloads, panels</span></span>
          <Seg>
            {([[0, 'Always'], [1, 'On hover'], [3, 'Never']] as Array<[0 | 1 | 3, string]>).map(([v, l]) => (
              <button key={v} className={s.sidebarShow === v ? 'on' : ''} onClick={() => update({ sidebarShow: v })}>{l}</button>
            ))}
          </Seg>
        </Row>
        {bool('roundedCorners', 'Rounded web content', 'Page area with rounded corners')}
      </Card>

      <h2>Behaviour</h2>
      <Card>
        {bool('mouseGestures', 'Mouse gestures', 'Hold right button and drag: ← back · → forward · ↑ reload · ↓ new tab · ↓→ close · ↓← reopen · ↑←/↑→ switch tab')}
        {bool('videoPill', 'Download button on videos', 'Hover any video for "Download with Falcon"')}
        {bool('clipboardMonitor', 'Watch the clipboard', 'Offer to download copied file links and magnets')}
        {bool('engineEnabled', 'Falcon download engine', 'Take over downloads from pages (off = plain Chromium downloads)')}
      </Card>

      <h2>Engines</h2>
      <Card>
        <Row as="div">
          <span>yt-dlp<span className="sub">Site extractors change often — update when YouTube breaks. Version {s.ytDlpVersion || '?'}</span></span>
          <Btn $primary onClick={updateYtDlp} disabled={updating}>{updating ? 'Updating…' : 'Update yt-dlp'}</Btn>
        </Row>
        {updateLog && <Row as="div"><pre style={{ margin: 0, fontSize: 12, whiteSpace: 'pre-wrap', opacity: 0.8 }}>{updateLog}</pre></Row>}
        <Row as="div"><span>aria2<span className="sub">Download engine</span></span><span style={{ opacity: 0.7 }}>{s.aria2Version}</span></Row>
      </Card>

      <h2>About</h2>
      <Card>
        <Row as="div"><span>Falcon</span><span style={{ opacity: 0.7 }}>Brave {s.braveVersion} · Chromium {s.chromiumVersion}</span></Row>
      </Card>
    </Page>
  )
}
