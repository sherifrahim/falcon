// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

// falcon://falcon — the control panel: look, behaviour, engines, about.

import * as React from 'react'
import styled from 'styled-components'
import { sendWithPromise } from 'chrome://resources/js/cr.js'

export interface Boost {
  id: string
  host: string
  name: string
  css: string
  js: string
  enabled: boolean
}

interface Session {
  name: string
  modified: number
  windows: number
  tabs: number
}

interface State {
  boosts: Boost[]
  sessionsAvailable: boolean
  colorScheme: 0 | 1 | 2 // system, light, dark
  verticalTabs: boolean
  sidebarShow: 0 | 1 | 3 // always, on hover, never
  roundedCorners: boolean
  mouseGestures: boolean
  peek: boolean
  miniMenu: boolean
  shellMode: 0 | 1 | 2 // classic, mac, cockpit
  blackTheme: boolean
  verticalTabsCollapsed: boolean
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
  background: var(--leo-color-container-background, var(--f-bg-3, #1e293b));
  border: 1px solid var(--leo-color-divider-subtle, var(--f-border, #334155));
  border-radius: 14px;
  padding: 4px 16px;
`

const Row = styled.label`
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  padding: 12px 0;
  border-bottom: 1px solid var(--leo-color-divider-subtle, var(--f-border, #334155));
  font-size: 14px;
  cursor: pointer;
  &:last-child { border-bottom: none; }
  .sub { display: block; font-size: 12px; opacity: 0.65; margin-top: 2px; }
  input[type='checkbox'] { width: 18px; height: 18px; accent-color: #0ea5e9; }
  select { padding: 6px 10px; border-radius: 10px; border: 1px solid var(--f-border, #334155); background: var(--f-bg-1, #0f172a); color: inherit; font-size: 13px; }
`

const Seg = styled.div`
  display: inline-flex;
  border: 1px solid var(--f-border, #334155);
  border-radius: 999px;
  overflow: hidden;
  button { padding: 6px 14px; border: none; background: transparent; color: inherit; cursor: pointer; font-size: 13px; }
  button.on { background: rgba(56, 189, 248, 0.2); color: #e0f2fe; }
`

const Btn = styled.button<{ $primary?: boolean }>`
  padding: 8px 14px;
  border-radius: 10px;
  border: 1px solid transparent;
  background: ${(p) => (p.$primary ? '#0ea5e9' : 'var(--f-bg-1, #0f172a)')};
  color: ${(p) => (p.$primary ? '#fff' : 'inherit')};
  font-size: 13px;
  font-weight: 600;
  cursor: pointer;
  &:hover { filter: brightness(1.15); }
  &:disabled { opacity: 0.5; cursor: default; }
`

const Editor = styled.div`
  padding: 10px 0 14px;
  display: grid;
  gap: 8px;
  input[type='text'], textarea {
    width: 100%; box-sizing: border-box; padding: 8px 10px; border-radius: 10px;
    border: 1px solid var(--f-border, #334155); background: var(--f-bg-1, #0f172a); color: inherit; font-size: 13px;
  }
  textarea { min-height: 90px; font-family: Consolas, "Cascadia Mono", monospace; font-size: 12px; resize: vertical; }
  .row { display: flex; gap: 8px; align-items: center; }
  .row label { font-size: 12px; opacity: 0.7; display: flex; align-items: center; gap: 6px; }
  .grow { flex: 1; }
  .muted { font-size: 12px; opacity: 0.6; }
`

const Links = styled.div`
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 10px;
  a {
    display: block; padding: 14px 16px; border-radius: 14px; color: inherit; text-decoration: none;
    background: var(--leo-color-container-background, var(--f-bg-3, #1e293b)); border: 1px solid var(--leo-color-divider-subtle, var(--f-border, #334155));
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

  // Boosts ------------------------------------------------------------------
  const [openBoost, setOpenBoost] = React.useState<string | null>(null)
  const saveBoosts = (boosts: Boost[]) => {
    if (!s) return
    setS({ ...s, boosts })
    chrome.send('falcon_control.setBoosts', [boosts])
  }
  const addBoost = (host = '') => {
    if (!s) return
    const id = 'b' + Date.now().toString(36)
    const boost: Boost = { id, host, name: '', css: '', js: '', enabled: true }
    saveBoosts([...s.boosts, boost])
    setOpenBoost(id)
  }
  const patchBoost = (id: string, patch: Partial<Boost>) => {
    if (!s) return
    saveBoosts(s.boosts.map((b) => (b.id === id ? { ...b, ...patch } : b)))
  }
  const PRESETS: Boost[] = [
    { id: 'preset-yt-shorts', host: 'youtube.com', name: 'YouTube: no Shorts', enabled: true, js: '',
      css: 'ytd-rich-shelf-renderer[is-shorts], ytd-reel-shelf-renderer, ytd-rich-section-renderer:has(ytd-rich-shelf-renderer[is-shorts]), #shorts-container, ytd-guide-entry-renderer:has(a[title="Shorts"]), ytd-mini-guide-entry-renderer:has(a[title="Shorts"]) { display: none !important; }' },
    { id: 'preset-reddit-sidebar', host: 'reddit.com', name: 'Reddit: no right sidebar / promos', enabled: true, js: '',
      css: 'shreddit-sidebar-ad, aside[aria-label*="Sidebar" i], #right-sidebar-container, .promotedlink, shreddit-ad-post { display: none !important; }' },
    { id: 'preset-x-trends', host: 'x.com', name: 'X: no trends / who to follow', enabled: true, js: '',
      css: '[data-testid="sidebarColumn"] { display: none !important; } [data-testid="primaryColumn"] { max-width: 100% !important; }' },
    { id: 'preset-cookie-banners', host: '*', name: 'Everywhere: hide common cookie banners', enabled: true, js: '',
      css: '#onetrust-banner-sdk, #onetrust-consent-sdk, .cc-banner, .cc-window, #cookie-banner, #cookieBanner, #CybotCookiebotDialog, .qc-cmp2-container, [id*="cookie-consent" i], [class*="cookie-banner" i], [aria-label*="cookie" i][role="dialog"] { display: none !important; } body { overflow: auto !important; }' },
  ]
  const addPreset = (p: Boost) => {
    if (!s) return
    if (s.boosts.some((b) => b.id === p.id)) { setOpenBoost(p.id); return }
    saveBoosts([...s.boosts, { ...p }])
    setOpenBoost(p.id)
  }

  const removeBoost = (id: string) => {
    if (!s) return
    saveBoosts(s.boosts.filter((b) => b.id !== id))
  }
  // Context menu "Boost this site…" lands here with #boost=<host>.
  React.useEffect(() => {
    if (!s) return
    const m = /boost=([^&]+)/.exec(location.hash)
    if (!m) return
    const host = decodeURIComponent(m[1])
    history.replaceState(null, '', location.pathname)
    const existing = s.boosts.find((b) => b.host === host)
    if (existing) setOpenBoost(existing.id)
    else addBoost(host)
    setTimeout(() => document.getElementById('boosts')?.scrollIntoView({ behavior: 'smooth' }), 50)
  }, [!!s])

  // Sessions ----------------------------------------------------------------
  const [sessions, setSessions] = React.useState<Session[] | null>(null)
  const [sessionName, setSessionName] = React.useState('')
  React.useEffect(() => {
    if (s?.sessionsAvailable) sendWithPromise('falcon_control.getSessions').then(setSessions).catch(() => {})
  }, [s?.sessionsAvailable])
  const saveSession = async () => {
    const name = sessionName.trim()
    if (!name) return
    setSessions(await sendWithPromise('falcon_control.saveSession', name))
    setSessionName('')
  }
  const restoreSession = (name: string) => sendWithPromise('falcon_control.restoreSession', name).catch(() => {})
  const deleteSession = async (name: string) => setSessions(await sendWithPromise('falcon_control.deleteSession', name))

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
          <span>Colour scheme<span className="sub">Slate/sky palette · Black is a pitch-black (OLED) dark variant</span></span>
          <Seg>
            <button className={s.colorScheme === 2 && !s.blackTheme ? 'on' : ''} onClick={() => update({ colorScheme: 2, blackTheme: false })}>Dark</button>
            <button className={s.colorScheme === 2 && s.blackTheme ? 'on' : ''} onClick={() => update({ colorScheme: 2, blackTheme: true })}>Black</button>
            <button className={s.colorScheme === 1 ? 'on' : ''} onClick={() => update({ colorScheme: 1, blackTheme: false })}>Light</button>
            <button className={s.colorScheme === 0 ? 'on' : ''} onClick={() => update({ colorScheme: 0, blackTheme: false })}>System</button>
          </Seg>
        </Row>
        <Row as="div">
          <span>Window style<span className="sub">Cockpit: no chrome, capsule on hover · Mac-style: slim title bar, toolbar on hover · Classic: Windows default</span></span>
          <Seg>
            {([[2, 'Cockpit'], [1, 'Mac-style'], [0, 'Classic']] as Array<[0 | 1 | 2, string]>).map(([v, l]) => (
              <button key={v} className={s.shellMode === v ? 'on' : ''} onClick={() => update({ shellMode: v })}>{l}</button>
            ))}
          </Seg>
        </Row>
        {bool('verticalTabs', 'Vertical tabs', 'Tab strip on the side (Zen / Arc style)')}
        {bool('verticalTabsCollapsed', 'Dock (collapsed tabs)', 'Icon-only tab rail that expands on hover')}
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
        <Row as="div">
          <span>Quick commands<span className="sub">Ctrl+Space (or type <code>:&gt;</code> in the address bar): switch tabs, run any command, open bookmarks, save/restore sessions, Falcon pages</span></span>
        </Row>
        {bool('mouseGestures', 'Mouse gestures', 'Hold right button and drag: ← back · → forward · ↑ reload · ↓ new tab · ↓→ close · ↓← reopen · ↑←/↑→ switch tab')}
        {bool('peek', 'Peek', 'Shift+click a link to preview it in a floating window (Arc-style); Esc closes, "Open in tab" keeps it')}
        {bool('miniMenu', 'Mini menu on text selection', 'Select text with the mouse: a tiny Copy · Search pill appears by the cursor (Edge-style, no big menu)')}
        {bool('videoPill', 'Download button on videos', 'Hover any video for "Download with Falcon"')}
        {bool('clipboardMonitor', 'Watch the clipboard', 'Offer to download copied file links and magnets')}
        {bool('engineEnabled', 'Falcon download engine', 'Take over downloads from pages (off = plain Chromium downloads)')}
      </Card>

      <h2>Keys</h2>
      <Card>
        {([
          ['Ctrl + Space', 'Quick commands (tabs, commands, bookmarks, sessions, Falcon pages)'],
          ['Ctrl + L  /  top edge', 'Reveal the address capsule (cockpit / mac-style)'],
          ['Ctrl + Shift + F', 'Toggle cockpit mode (hide / show the toolbar)'],
          ['Ctrl + J', 'Falcon Downloads'],
          ['Ctrl + B', 'Show / hide the sidebar'],
          ['Shift + click a link', 'Peek: floating preview (Esc closes, "Open in tab" keeps it)'],
          ['Right-drag ← → ↑ ↓', 'Mouse gestures: back · forward · reload · new tab (↓→ close, ↓← reopen, ↑← ↑→ switch)'],
          ['Select text', 'Mini menu: Copy · Search'],
        ] as Array<[string, string]>).map(([k, d]) => (
          <Row as="div" key={k}><span>{d}</span><code style={{ fontSize: 12, opacity: 0.85, whiteSpace: 'nowrap', marginLeft: 16 }}>{k}</code></Row>
        ))}
      </Card>

      <h2 id="boosts">Boosts</h2>
      <p className="hint">Arc-style per-site tweaks: your own CSS and JavaScript, applied to every page on a host (subdomains included; <code>*</code> = all sites). Right-click a page → "Boost this site…" to jump here. JS runs in an isolated world after DOMContentLoaded.</p>
      <Card>
        {s.boosts.length === 0 && <Row as="div"><span className="muted" style={{ opacity: 0.6 }}>No boosts yet.</span></Row>}
        {s.boosts.map((b) => (
          <div key={b.id}>
            <Row as="div">
              <span style={{ cursor: 'pointer', flex: 1 }} onClick={() => setOpenBoost(openBoost === b.id ? null : b.id)}>
                <b>{b.name || b.host || 'New boost'}</b>
                <span className="sub">{b.host || 'no host yet'}{b.css && ' · CSS'}{b.js && ' · JS'}</span>
              </span>
              <input type="checkbox" checked={b.enabled} onChange={(e) => patchBoost(b.id, { enabled: e.target.checked })} title="Enabled" />
              <Btn onClick={() => setOpenBoost(openBoost === b.id ? null : b.id)}>{openBoost === b.id ? 'Close' : 'Edit'}</Btn>
              <Btn onClick={() => removeBoost(b.id)}>Delete</Btn>
            </Row>
            {openBoost === b.id && (
              <Editor>
                <div className="row">
                  <input type="text" className="grow" placeholder="host, e.g. reddit.com (or * for all sites)" value={b.host} onChange={(e) => patchBoost(b.id, { host: e.target.value })} />
                  <input type="text" className="grow" placeholder="name (optional)" value={b.name} onChange={(e) => patchBoost(b.id, { name: e.target.value })} />
                </div>
                <textarea placeholder="/* CSS — e.g. .sidebar { display: none } */" value={b.css} onChange={(e) => patchBoost(b.id, { css: e.target.value })} spellCheck={false} />
                <textarea placeholder="// JavaScript — runs after the DOM is ready, in an isolated world (DOM access, no page globals)" value={b.js} onChange={(e) => patchBoost(b.id, { js: e.target.value })} spellCheck={false} />
                <span className="muted">Changes apply on the next page load.</span>
              </Editor>
            )}
          </div>
        ))}
        <Row as="div"><span className="sub">Add a boost for a site</span><Btn $primary onClick={() => addBoost('')}>New boost</Btn></Row>
        <Row as="div" style={{ flexWrap: 'wrap', justifyContent: 'flex-start', gap: 8 }}>
          <span className="sub" style={{ width: '100%' }}>Presets — one click, editable afterwards</span>
          {PRESETS.map((p) => <Btn key={p.id} onClick={() => addPreset(p)} disabled={s.boosts.some((b) => b.id === p.id)}>{p.name}</Btn>)}
        </Row>
      </Card>

      {s.sessionsAvailable && (
        <>
          <h2>Sessions</h2>
          <p className="hint">Save every open window and tab under a name; restore it later (opens in new windows). Also in Quick commands (Ctrl+Space).</p>
          <Card>
            <Row as="div">
              <input type="text" placeholder="session name, e.g. Work" value={sessionName} onChange={(e) => setSessionName(e.target.value)} onKeyDown={(e) => e.key === 'Enter' && saveSession()}
                style={{ flex: 1, padding: '8px 10px', borderRadius: 10, border: '1px solid var(--f-border, #334155)', background: 'var(--f-bg-1, #0f172a)', color: 'inherit', fontSize: 13 }} />
              <Btn $primary onClick={saveSession} disabled={!sessionName.trim()}>Save current session</Btn>
            </Row>
            {(sessions ?? []).map((ss) => (
              <Row as="div" key={ss.name}>
                <span><b>{ss.name}</b><span className="sub">{ss.windows} window{ss.windows === 1 ? '' : 's'} · {ss.tabs} tab{ss.tabs === 1 ? '' : 's'} · {new Date(ss.modified).toLocaleString()}</span></span>
                <Btn onClick={() => restoreSession(ss.name)}>Restore</Btn>
                <Btn onClick={() => deleteSession(ss.name)}>Delete</Btn>
              </Row>
            ))}
            {sessions && sessions.length === 0 && <Row as="div"><span style={{ opacity: 0.6, fontSize: 12 }}>No saved sessions.</span></Row>}
          </Card>
        </>
      )}

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
