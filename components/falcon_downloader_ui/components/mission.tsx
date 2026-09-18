// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

// "Mission control" chrome for falcon://downloader (mock v2, screen 8): the
// left rail of views, the right status column and the telemetry footer. The
// list/add/row logic stays in app.tsx; this file is layout + status only.

import * as React from 'react'
import styled, { keyframes } from 'styled-components'
import { sendWithPromise } from 'chrome://resources/js/cr.js'
import { loadTimeData } from '$web-common/loadTimeData'
import { fmtBytes, fmtSpeed } from './common'

export type View = 'all' | 'active' | 'done' | 'failed' | 'torrents' | 'video' | 'grabber' | 'scheduler' | 'history' | 'settings'

// The details column (engine / security / scheduler / session) is an
// inspector: hidden by default, toggled from the header, remembered.
export const Layout = styled.div<{ $side?: boolean }>`
  display: grid;
  grid-template-columns: 200px minmax(0, 1fr)${(p) => (p.$side ? ' 260px' : '')};
  gap: 22px;
  align-items: start;
  @media (max-width: 1180px) { grid-template-columns: 200px minmax(0, 1fr); .side { display: none; } }
  @media (max-width: 860px) { grid-template-columns: 1fr; .rail { display: none; } }
`

const SIDE_KEY = 'falcon.downloader.inspector'
export function useInspector(): [boolean, () => void] {
  const [open, setOpen] = React.useState<boolean>(() => {
    try { return localStorage.getItem(SIDE_KEY) === '1' } catch { return false }
  })
  const toggle = React.useCallback(() => setOpen((o) => {
    try { localStorage.setItem(SIDE_KEY, o ? '0' : '1') } catch {}
    return !o
  }), [])
  return [open, toggle]
}

export const InspectorButton = styled.button<{ $on: boolean }>`
  display: inline-flex;
  align-items: center;
  gap: 8px;
  height: 36px;
  padding: 0 12px;
  border-radius: 10px;
  border: 1px solid ${(p) => (p.$on ? 'rgba(56, 189, 248, 0.45)' : 'var(--f-border, #334155)')};
  background: ${(p) => (p.$on ? 'rgba(56, 189, 248, 0.12)' : 'transparent')};
  color: ${(p) => (p.$on ? '#7dd3fc' : 'inherit')};
  font: inherit;
  font-size: 12px;
  cursor: pointer;
  transition: background 150ms ease, border-color 150ms ease, color 150ms ease;
  &:hover { background: rgba(255, 255, 255, 0.06); }
  svg { width: 16px; height: 16px; stroke: currentColor; fill: none; stroke-width: 1.5; stroke-linecap: round; stroke-linejoin: round; }
`

export function Inspector({ on, onToggle }: { on: boolean; onToggle: () => void }) {
  return (
    <InspectorButton $on={on} onClick={onToggle} title={on ? 'Hide details (engine, security, scheduler, session)' : 'Show details (engine, security, scheduler, session)'} aria-pressed={on}>
      <svg viewBox="0 0 24 24" aria-hidden="true"><rect x="3" y="4" width="18" height="16" rx="3" /><path d="M15 4v16M17.5 8.5h1M17.5 11.5h1" /></svg>
      Details
    </InspectorButton>
  )
}

const RailBox = styled.nav`
  position: sticky;
  top: 20px;
  display: flex;
  flex-direction: column;
  gap: 2px;
  .brand { display: flex; align-items: center; gap: 10px; margin: 2px 0 16px 4px; font-size: 18px; font-weight: 700; letter-spacing: -0.01em; }
  .brand svg { width: 24px; height: 24px; }
  .group { font-size: 10px; letter-spacing: 0.12em; text-transform: uppercase; opacity: 0.45; margin: 14px 12px 4px; }
  button {
    display: flex; align-items: center; gap: 10px; padding: 8px 12px; border-radius: 10px; border: 1px solid transparent;
    background: transparent; color: inherit; font-size: 13px; text-align: left; cursor: pointer; opacity: 0.75; width: 100%;
  }
  button:hover { opacity: 1; background: var(--f-bg-3, #1e293b); }
  button.on { opacity: 1; background: rgba(56, 189, 248, 0.12); border-color: rgba(56, 189, 248, 0.3); color: #e0f2fe; }
  button i { width: 6px; height: 6px; border-radius: 50%; background: currentColor; opacity: 0.5; flex: none; }
  button.on i { background: #38bdf8; opacity: 1; box-shadow: 0 0 8px #38bdf8; }
  button b { margin-left: auto; font-size: 11px; font-weight: 600; opacity: 0.7; font-family: "Cascadia Mono", Consolas, monospace; }
`

const FALCON_PATH = 'M19.04 15.17 L18.36 15.80 L17.80 15.47 L17.46 14.39 Q17.13 12.66 15.27 12.29 Q12.20 11.57 8.03 11.28 L7.42 12.25 Q11.30 14.18 14.67 13.17 Q16.51 12.62 17.15 11.61 Z M13.68 11.56 Q10.44 9.05 6.02 4.11 L5.63 6.18 L3.94 5.34 L4.42 7.34 L2.57 7.11 Q6.68 9.49 10.60 11.35 Z M9.65 13.71 Q5.18 15.14 0.51 15.17 L1.35 13.83 L2.29 14.35 Q5.72 13.63 8.62 12.75 Z M7.18 11.99 L2.14 9.28 L2.53 10.86 L1.14 11.85 L2.53 12.32 L7.42 12.25 Z'

export function Rail({ view, counts, onView }: { view: View; counts: Record<string, number>; onView: (v: View) => void }) {
  const item = (v: View, label: string, count?: number) => (
    <button key={v} className={view === v ? 'on' : ''} onClick={() => onView(v)}><i />{label}{count ? <b>{count}</b> : null}</button>
  )
  return (
    <RailBox className="rail">
      <div className="brand"><svg viewBox="0 0 24 24" fill="#38bdf8"><path d={FALCON_PATH} /></svg>Downloads</div>
      {item('all', 'All', counts.all)}
      {item('active', 'Active', counts.active)}
      {item('done', 'Completed', counts.done)}
      {item('failed', 'Failed', counts.failed)}
      {item('torrents', 'Torrents', counts.torrents)}
      <div className="group">Tools</div>
      {item('video', 'Video grabber')}
      {item('grabber', 'Site grabber')}
      {item('scheduler', 'Scheduler')}
      {item('history', 'History')}
      {item('settings', 'Settings')}
    </RailBox>
  )
}

const slideIn = keyframes`
  from { opacity: 0; transform: translateX(12px); }
  to { opacity: 1; transform: translateX(0); }
`

const SideBox = styled.aside`
  position: sticky;
  top: 20px;
  display: flex;
  flex-direction: column;
  gap: 12px;
  animation: ${slideIn} 200ms ease-out both;
`

const Panel = styled.div`
  border-radius: 12px;
  border: 1px solid var(--f-border, #334155);
  background: var(--leo-color-container-background, var(--f-bg-3, #1e293b));
  padding: 12px 14px;
  h4 { margin: 0 0 8px; font-size: 11px; letter-spacing: 0.1em; text-transform: uppercase; opacity: 0.55; font-weight: 600; }
  .kv { display: flex; align-items: center; justify-content: space-between; gap: 10px; font-size: 12px; padding: 5px 0; border-top: 1px solid rgba(255,255,255,0.06); }
  .kv:first-of-type { border-top: none; }
  .kv span:last-child { font-family: "Cascadia Mono", Consolas, "JetBrains Mono", monospace; font-size: 11px; opacity: 0.9; }
  .ok { color: #22c55e; } .warn { color: #f59e0b; } .off { opacity: 0.5; }
  .dot { display: inline-block; width: 7px; height: 7px; border-radius: 50%; margin-right: 6px; vertical-align: middle; }
  a { color: #38bdf8; text-decoration: none; font-size: 12px; }
`

interface Settings {
  vtEnabled?: boolean
  vtApiKey?: string
  quarantineFlagged?: boolean
  scheduleEnabled?: boolean
  scheduleStart?: string
  scheduleStop?: string
  maxConnections?: number
  maxConcurrent?: number
  speedLimitKbps?: number
  proxy?: string
}

export function Side({ connected, down, up, active, queued, sessionBytes, totalFiles, totalBytes, onOpen }: {
  connected: boolean; down: number; up: number; active: number; queued: number; sessionBytes: number; totalFiles: number; totalBytes: number; onOpen: (v: View) => void
}) {
  const [st, setSt] = React.useState<Settings>({})
  React.useEffect(() => {
    const load = () => sendWithPromise('falcon_downloader.getSettings').then((s: Settings) => setSt(s || {})).catch(() => {})
    load()
    const t = window.setInterval(load, 15000)
    return () => window.clearInterval(t)
  }, [])
  const media = loadTimeData.getBoolean('mediaAvailable')
  const sandbox = loadTimeData.getBoolean('sandboxAvailable')
  return (
    <SideBox className="side">
      <Panel>
        <h4>Engine</h4>
        <div className="kv"><span><i className={'dot ' + (connected ? 'ok' : 'warn')} style={{ background: connected ? '#22c55e' : '#f59e0b' }} />aria2</span><span>{connected ? 'running' : 'starting'}</span></div>
        <div className="kv"><span><i className="dot" style={{ background: media ? '#22c55e' : '#64748b' }} />yt-dlp + ffmpeg</span><span>{media ? 'ready' : 'missing'}</span></div>
        <div className="kv"><span>Transfer</span><span>↓ {fmtSpeed(down)} · ↑ {fmtSpeed(up)}</span></div>
        <div className="kv"><span>Queue</span><span>{active} active · {queued} waiting</span></div>
        <div className="kv"><span>Connections</span><span>{st.maxConnections ?? '—'} per file · {st.maxConcurrent ?? '—'} at once</span></div>
        {st.speedLimitKbps ? <div className="kv"><span>Limit</span><span>{st.speedLimitKbps} KB/s</span></div> : null}
        {st.proxy ? <div className="kv"><span>Proxy</span><span title={st.proxy}>{st.proxy.slice(0, 22)}</span></div> : null}
      </Panel>
      <Panel>
        <h4>Security</h4>
        <div className="kv"><span>Mark-of-the-Web + Defender</span><span className="ok">on</span></div>
        <div className="kv"><span>SHA-256</span><span className="ok">every file</span></div>
        <div className="kv"><span>VirusTotal</span><span className={st.vtEnabled && st.vtApiKey ? 'ok' : 'off'}>{st.vtEnabled && st.vtApiKey ? 'hash lookup' : st.vtEnabled ? 'no API key' : 'off'}</span></div>
        <div className="kv"><span>Quarantine flagged</span><span className={st.quarantineFlagged ? 'ok' : 'off'}>{st.quarantineFlagged ? 'yes' : 'no'}</span></div>
        <div className="kv"><span>Windows Sandbox</span><span className={sandbox ? 'ok' : 'off'}>{sandbox ? 'available' : 'not installed'}</span></div>
      </Panel>
      <Panel>
        <h4>Scheduler</h4>
        {st.scheduleEnabled
          ? <div className="kv"><span>Window</span><span>{st.scheduleStart} → {st.scheduleStop}</span></div>
          : <div className="kv"><span className="off">No active window</span><a href="#" onClick={(e) => { e.preventDefault(); onOpen('scheduler') }}>set up</a></div>}
      </Panel>
      <Panel>
        <h4>Session</h4>
        <div className="kv"><span>Received</span><span>{fmtBytes(sessionBytes)}</span></div>
        {totalFiles > 0 && <div className="kv"><span>All-time</span><span>{fmtBytes(totalBytes)} · {totalFiles} files</span></div>}
      </Panel>
    </SideBox>
  )
}

const TelemetryBox = styled.div`
  margin-top: 18px;
  display: grid;
  grid-template-columns: 1.6fr 1fr 1fr;
  gap: 12px;
  @media (max-width: 860px) { grid-template-columns: 1fr; }
  > div {
    border-radius: 12px; border: 1px solid var(--f-border, #334155);
    background: var(--leo-color-container-background, var(--f-bg-3, #1e293b));
    padding: 10px 14px; min-height: 64px; display: flex; flex-direction: column; gap: 6px;
  }
  .lbl { font-size: 10px; letter-spacing: 0.1em; text-transform: uppercase; opacity: 0.5; }
  .val { font-family: "Cascadia Mono", Consolas, "JetBrains Mono", monospace; font-size: 15px; }
  svg { width: 100%; height: 34px; }
`

export function Telemetry({ speeds, listed, dir, categories }: { speeds: number[]; listed: number; dir: string; categories: boolean }) {
  const max = Math.max(1, ...speeds)
  const w = 300, h = 34
  const pts = speeds.map((v, i) => `${(i / Math.max(1, speeds.length - 1)) * w},${h - (v / max) * (h - 4) - 2}`).join(' ')
  const now = speeds.length ? speeds[speeds.length - 1] : 0
  return (
    <TelemetryBox>
      <div>
        <div className="lbl">Network</div>
        <div className="val">{fmtSpeed(now)}</div>
        <svg viewBox={`0 0 ${w} ${h}`} preserveAspectRatio="none">
          <defs><linearGradient id="tg" x1="0" x2="0" y1="0" y2="1"><stop offset="0" stopColor="#38bdf8" stopOpacity="0.45" /><stop offset="1" stopColor="#38bdf8" stopOpacity="0" /></linearGradient></defs>
          {speeds.length > 1 && <polygon points={`0,${h} ${pts} ${w},${h}`} fill="url(#tg)" />}
          {speeds.length > 1 && <polyline points={pts} fill="none" stroke="#38bdf8" strokeWidth="1.5" />}
        </svg>
      </div>
      <div>
        <div className="lbl">Listed</div>
        <div className="val">{listed}</div>
        <div style={{ fontSize: 11, opacity: 0.6 }}>downloads in view</div>
      </div>
      <div>
        <div className="lbl">Folder</div>
        <div style={{ fontSize: 12, wordBreak: 'break-all' }}>{dir || 'default'}</div>
        <div style={{ fontSize: 11, opacity: 0.6 }}>{categories ? 'sorted by category' : 'flat'}</div>
      </div>
    </TelemetryBox>
  )
}
