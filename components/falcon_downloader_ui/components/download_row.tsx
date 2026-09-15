// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

import * as React from 'react'
import { sendWithPromise } from 'chrome://resources/js/cr.js'
import styled from 'styled-components'
import { loadTimeData } from '$web-common/loadTimeData'
import { Aria2Client, Aria2Download, Aria2File, Aria2Status } from '../aria2_client'
import {
  Button, ErrorText, Input, Meta, STATUS_LABEL, basename, firstUri, fmtBytes,
  fmtEta, fmtSpeed, isTorrent, nameOf,
} from './common'

const Row = styled.div<{ $open: boolean }>`
  background: var(--leo-color-container-background, var(--f-bg-3, #1e293b));
  border: 1px solid ${(p) => (p.$open ? '#38bdf8' : 'var(--leo-color-divider-subtle, var(--f-border, #334155))')};
  border-radius: 14px;
  padding: 12px 14px;
  display: grid;
  grid-template-columns: 34px 1fr auto;
  gap: 8px 14px;
  align-items: center;
`

// Ring gauge (mock v2): progress for live rows, tick for done, cross for error.
const RingBox = styled.svg`
  width: 34px; height: 34px; grid-row: 1; align-self: start;
  .track { stroke: rgba(255, 255, 255, 0.1); }
  .arc { transition: stroke-dashoffset 0.4s ease; }
  text { font-family: "Cascadia Mono", Consolas, "JetBrains Mono", monospace; font-size: 9px; fill: currentColor; opacity: 0.9; }
`

function Ring({ pct, status }: { pct: number; status: Aria2Status }) {
  const r = 14, c = 2 * Math.PI * r
  const color = status === 'error' ? '#f87171' : status === 'complete' ? '#22c55e' : status === 'paused' ? '#94a3b8' : '#38bdf8'
  const frac = status === 'complete' ? 1 : Math.max(0, Math.min(1, pct / 100))
  const label = status === 'complete' ? '✓' : status === 'error' ? '!' : `${Math.round(pct)}`
  return (
    <RingBox viewBox="0 0 34 34" title={`${Math.round(pct)}%`}>
      <circle className="track" cx="17" cy="17" r={r} fill="none" strokeWidth="2.5" />
      <circle className="arc" cx="17" cy="17" r={r} fill="none" stroke={color} strokeWidth="2.5" strokeLinecap="round"
        strokeDasharray={c} strokeDashoffset={c * (1 - frac)} transform="rotate(-90 17 17)"
        style={{ filter: status === 'active' ? 'drop-shadow(0 0 3px rgba(56,189,248,0.6))' : 'none' }} />
      <text x="17" y="17" textAnchor="middle" dominantBaseline="central">{label}</text>
    </RingBox>
  )
}

const Name = styled.div`
  font-size: 14px;
  font-weight: 600;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  cursor: pointer;
`

const Bar = styled.div<{ $pct: number; $status: Aria2Status }>`
  grid-column: 1 / -1;
  height: 3px;
  border-radius: 999px;
  background: rgba(148, 163, 184, 0.16);
  overflow: hidden;
  &::after {
    content: '';
    display: block;
    height: 100%;
    width: ${(p) => Math.max(0, Math.min(100, p.$pct))}%;
    background: ${(p) =>
      p.$status === 'error' ? '#ef4444' : p.$status === 'complete' ? '#22c55e' : p.$status === 'paused' ? '#94a3b8' : '#38bdf8'};
    transition: width 0.4s;
  }
`

const Actions = styled.div`
  display: flex;
  gap: 6px;
  align-items: start;
  flex-wrap: wrap;
  justify-content: flex-end;
`

const Details = styled.div`
  grid-column: 1 / -1;
  border-top: 1px solid var(--leo-color-divider-subtle, var(--f-border, #334155));
  padding-top: 10px;
  display: grid;
  grid-template-columns: 110px 1fr;
  gap: 6px 12px;
  font-size: 12px;
  .k { opacity: 0.6; }
  .v { word-break: break-all; }
`

const FileList = styled.div`
  grid-column: 1 / -1;
  max-height: 260px;
  overflow: auto;
  border: 1px solid var(--leo-color-divider-subtle, var(--f-border, #334155));
  border-radius: 10px;
  padding: 6px 10px;
  label { display: flex; gap: 8px; align-items: center; padding: 3px 0; font-size: 12px; }
  label span.size { margin-inline-start: auto; opacity: 0.6; white-space: nowrap; }
`

const Segments = styled.div`
  grid-column: 1 / -1;
  display: flex;
  gap: 2px;
  height: 4px;
  span { flex: 1; border-radius: 2px; background: rgba(148,163,184,0.2); }
  span.on { background: #38bdf8; }
`

export interface ScanFile {
  path: string
  sha256: string
  av: string
  vt: string
  vtMalicious: number
  vtSuspicious: number
  vtTotal: number
  quarantinedTo: string
}

export interface ScanResult {
  name: string
  done: boolean
  files: ScanFile[]
}

const RISKY = /\.(exe|msi|msix|bat|cmd|ps1|vbs|js|jar|scr|com|dll|zip|rar|7z|iso|apk)$/i

function scanBadge(scan?: ScanResult): { text: string; color: string } | null {
  if (!scan) return null
  const f = scan.files
  if (f.some((x) => x.av === 'infected')) return { text: 'Blocked by antivirus', color: '#ef4444' }
  if (f.some((x) => x.vt === 'flagged')) {
    const worst = f.reduce((a, b) => (b.vtMalicious > a.vtMalicious ? b : a))
    return { text: `VirusTotal: ${worst.vtMalicious}/${worst.vtTotal} flagged`, color: '#ef4444' }
  }
  if (f.some((x) => x.av === 'pending' || x.vt === 'pending')) return { text: 'Scanning…', color: '#94a3b8' }
  if (f.every((x) => x.vt === 'clean')) return { text: `Clean · VirusTotal 0/${f[0]?.vtTotal ?? 0}`, color: '#22c55e' }
  if (f.some((x) => x.vt === 'unknown')) return { text: 'Defender OK · not on VirusTotal', color: '#38bdf8' }
  if (f.some((x) => x.vt === 'nokey')) return { text: 'Defender OK', color: '#38bdf8' }
  if (f.some((x) => x.av === 'clean')) return { text: 'Defender OK', color: '#38bdf8' }
  return null
}

const Badge = styled.span<{ $color: string }>`
  display: inline-block;
  padding: 1px 8px;
  border-radius: 999px;
  font-size: 11px;
  font-weight: 600;
  color: ${(p) => p.$color};
  border: 1px solid ${(p) => p.$color}55;
  background: ${(p) => p.$color}14;
`

interface Props {
  d: Aria2Download
  scan?: ScanResult
  client: Aria2Client
  open: boolean
  onToggle: () => void
  refresh: () => void
  setError: (m: string | null) => void
  // Position among queued (waiting) downloads, for reordering.
  queueIndex?: number
  queueSize?: number
  speedHistory?: number[]
}

// Tiny inline speed graph (last minute).
function Sparkline({ data }: { data: number[] }) {
  if (data.length < 2) return null
  const w = 220
  const h = 34
  const max = Math.max(...data, 1)
  const pts = data.map((v, i) => `${(i / (data.length - 1)) * w},${h - (v / max) * (h - 2) - 1}`).join(' ')
  return (
    <svg width={w} height={h} style={{ display: 'block' }}>
      <polyline points={pts} fill="none" stroke="#38bdf8" strokeWidth={1.5} />
      <polyline points={`0,${h} ${pts} ${w},${h}`} fill="rgba(56,189,248,0.15)" stroke="none" />
    </svg>
  )
}

export function DownloadRow({ d, scan, client, open, onToggle, refresh, setError, queueIndex, queueSize, speedHistory }: Props) {
  const total = +d.totalLength
  const done = +d.completedLength
  const speed = +d.downloadSpeed
  const pct = total > 0 ? (done / total) * 100 : 0
  const path = d.files?.[0]?.path ?? ''
  const uri = firstUri(d)
  const torrent = isTorrent(d)
  const canOpen = d.status === 'complete' && path && !torrent
  const live = d.status === 'active' || d.status === 'waiting' || d.status === 'paused'
  const [files, setFiles] = React.useState<Aria2File[] | null>(null)
  const [limit, setLimit] = React.useState('')
  const [referer, setReferer] = React.useState('')
  const [openWhenDone, setOpenWhenDone] = React.useState(false)
  const [refreshing, setRefreshing] = React.useState(false)
  const [bitfield, setBitfield] = React.useState<string>('')
  const [renaming, setRenaming] = React.useState(false)
  const [newName, setNewName] = React.useState('')
  const fromHistory = !!d.fromHistory
  const badge = scanBadge(scan)
  const sandboxAvailable = loadTimeData.getBoolean('sandboxAvailable')
  const quarantined = scan?.files.find((f) => f.quarantinedTo)?.quarantinedTo
  const effectivePath = quarantined || path

  const act = (p: Promise<any>) =>
    p.then(() => { refresh(); chrome.send('falcon_downloader.poke') })
     .catch((e: any) => setError(e?.message ?? 'Action failed'))

  React.useEffect(() => {
    if (!open || fromHistory) return
    if (torrent) client.getFiles(d.gid).then(setFiles).catch(() => {})
    client.getOption(d.gid)
      .then((o) => {
        setLimit(o['max-download-limit'] ? String(Math.round(+o['max-download-limit'] / 1024) || '') : '')
        setReferer(o.referer || '')
      })
      .catch(() => {})
    if (d.status === 'active') {
      client.call('aria2.tellStatus', d.gid, ['bitfield']).then((s: any) => setBitfield(s?.bitfield ?? '')).catch(() => {})
    }
  }, [open, d.gid, d.status, torrent, client, fromHistory])

  // History rows are not known to aria2 any more; drop them from Falcon's
  // own list instead.
  const removeEntry = () =>
    fromHistory
      ? act(Promise.resolve(chrome.send('falcon_downloader.removeHistory', [d.gid])))
      : act(client.removeResult(d.gid))

  const rename = () => {
    const name = newName.trim()
    setRenaming(false)
    if (!name || name === basename(path) || /[\\/:*?"<>|]/.test(name)) return
    if (d.status === 'complete' && path) {
      // Finished: move the file on disk; the entry then lives in history.
      act(Promise.resolve(chrome.send('falcon_downloader.renameFile', [d.gid, path, name])))
    } else if (!fromHistory && !torrent) {
      act(client.changeOption(d.gid, { out: name }))
    }
  }

  const move = (how: 'top' | 'up' | 'down' | 'bottom') => {
    const call =
      how === 'top' ? client.changePosition(d.gid, 0, 'POS_SET')
      : how === 'bottom' ? client.changePosition(d.gid, 0, 'POS_END')
      : client.changePosition(d.gid, how === 'up' ? -1 : 1, 'POS_CUR')
    act(call)
  }

  const retry = () => {
    if (!uri) return
    const opts: Record<string, string> = {}
    if (d.dir) opts.dir = d.dir
    if (path && !torrent) opts.out = basename(path)
    const drop = fromHistory
      ? Promise.resolve(chrome.send('falcon_downloader.removeHistory', [d.gid]))
      : client.removeResult(d.gid).catch(() => {})
    act(drop.then(() => client.addUri([uri], opts)))
  }

  // Expired link: re-open the source page and capture the fresh URL for this
  // file (see ArmUrlRefresh in download_interceptor.cc).
  const refreshUrl = async () => {
    setRefreshing(true)
    try {
      const ok: boolean = await sendWithPromise('falcon_downloader.refreshUrl', d.gid, referer, path || '')
      setError(ok ? 'Source page opened — click the download link there; the file resumes here.' : 'No source page known — click the link again yourself; the next matching download resumes this file.')
    } catch { setError('Refresh failed') }
    finally { setRefreshing(false) }
  }

  const toggleOpenWhenDone = () => {
    const next = !openWhenDone
    setOpenWhenDone(next)
    chrome.send('falcon_downloader.setOpenWhenDone', [d.gid, next])
  }

  const deleteWithFile = () => {
    const files = d.files?.map((f) => f.path).filter(Boolean) ?? []
    const drop = fromHistory
      ? Promise.resolve(chrome.send('falcon_downloader.removeHistory', [d.gid]))
      : live ? client.remove(d.gid) : client.removeResult(d.gid)
    act(drop.then(() => {
      for (const f of files) chrome.send('falcon_downloader.deleteFile', [f])
    }))
  }

  const applyLimit = () => {
    const kb = parseInt(limit, 10)
    act(client.changeOption(d.gid, { 'max-download-limit': String(isFinite(kb) && kb > 0 ? kb * 1024 : 0) }))
  }

  const toggleFile = async (f: Aria2File, on: boolean) => {
    if (!files) return
    const selected = files
      .filter((x) => (x.index === f.index ? on : x.selected === 'true'))
      .map((x) => x.index)
    if (selected.length === 0) return
    const wasActive = d.status === 'active'
    try {
      if (wasActive) await client.pause(d.gid)
      await client.changeOption(d.gid, { 'select-file': selected.join(',') })
      if (wasActive) await client.unpause(d.gid)
      setFiles(await client.getFiles(d.gid))
      refresh()
    } catch (e: any) {
      setError(e?.message ?? 'Could not change file selection')
    }
  }

  // bitfield is hex, one bit per piece; sample it into 64 buckets.
  const segments = React.useMemo(() => {
    if (!bitfield) return null
    const bits: boolean[] = []
    for (const ch of bitfield) {
      const n = parseInt(ch, 16)
      for (let b = 3; b >= 0; b--) bits.push(((n >> b) & 1) === 1)
    }
    const buckets = 64
    const out: boolean[] = []
    for (let i = 0; i < buckets; i++) {
      const a = Math.floor((i * bits.length) / buckets)
      const z = Math.max(a + 1, Math.floor(((i + 1) * bits.length) / buckets))
      out.push(bits.slice(a, z).every(Boolean))
    }
    return out
  }, [bitfield])

  return (
    <Row $open={open}>
      <Ring pct={pct} status={d.status} />
      <div style={{ minWidth: 0 }}>
        {renaming ? (
          <Input
            autoFocus
            style={{ width: '100%', padding: '4px 8px', fontSize: 13 }}
            value={newName}
            onChange={(e) => setNewName(e.target.value)}
            onBlur={rename}
            onKeyDown={(e) => { if (e.key === 'Enter') rename(); if (e.key === 'Escape') setRenaming(false) }}
          />
        ) : (
          <Name title={path || nameOf(d)} onClick={onToggle}>{nameOf(d)}</Name>
        )}
        <Meta>
          <span>
            {STATUS_LABEL[d.status]}{torrent ? ' · torrent' : ''}
            {d.finishedAt ? ` · ${new Date(d.finishedAt * 1000).toLocaleString()}` : ''}
          </span>
          <span className="mono">
            {fmtBytes(done)}
            {total > 0 ? ` / ${fmtBytes(total)}` : ''}
          </span>
          {d.status === 'active' && (
            <>
              <span className="mono">↓ {fmtSpeed(speed)}</span>
              {torrent && <span className="mono">↑ {fmtSpeed(+d.uploadSpeed)}</span>}
              <span className="mono">ETA {fmtEta(total - done, speed)}</span>
              <span>{d.connections} conn{torrent && d.numSeeders ? ` · ${d.numSeeders} seeds` : ''}</span>
            </>
          )}
          {d.status === 'error' && d.errorMessage && <ErrorText>{d.errorMessage}</ErrorText>}
          {badge && <Badge $color={badge.color}>{badge.text}</Badge>}
          {quarantined && <ErrorText>Moved to Quarantine</ErrorText>}
        </Meta>
      </div>
      <Actions>
        {d.status === 'active' || d.status === 'waiting' ? (
          <Button $small onClick={() => act(client.pause(d.gid))}>Pause</Button>
        ) : d.status === 'paused' ? (
          <Button $small $primary onClick={() => act(client.unpause(d.gid))}>Resume</Button>
        ) : null}
        {d.status === 'error' && uri && <Button $small $primary onClick={retry}>Retry</Button>}
        {d.status === 'error' && !torrent && !fromHistory && /status=(40[13]|410)|forbidden|expired/i.test(d.errorMessage || '') && (
          <Button $small onClick={refreshUrl} disabled={refreshing} title="Re-open the page this came from and pick up a fresh link">Refresh link</Button>
        )}
        {canOpen && (
          <>
            <Button $small onClick={() => chrome.send('falcon_downloader.openFile', [path])}>Open</Button>
          </>
        )}
        {path && <Button $small onClick={() => chrome.send('falcon_downloader.openFolder', [effectivePath])}>Folder</Button>}
        {d.status === 'complete' && effectivePath && sandboxAvailable && RISKY.test(effectivePath) && (
          <Button $small onClick={() => chrome.send('falcon_downloader.openInSandbox', [effectivePath])} title="Open in a disposable Windows Sandbox VM">Sandbox</Button>
        )}
        {d.status === 'waiting' && queueIndex !== undefined && (queueSize ?? 0) > 1 && (
          <>
            <Button $small disabled={queueIndex === 0} onClick={() => move('up')} title="Move up in queue">↑</Button>
            <Button $small disabled={queueIndex === (queueSize ?? 1) - 1} onClick={() => move('down')} title="Move down in queue">↓</Button>
          </>
        )}
        {live ? (
          <Button $small $danger onClick={() => act(client.remove(d.gid))}>Cancel</Button>
        ) : (
          <Button $small onClick={removeEntry}>Remove</Button>
        )}
        <Button $small onClick={onToggle}>{open ? 'Less' : 'More'}</Button>
      </Actions>
      <Bar $pct={pct} $status={d.status} />
      {open && d.status === 'active' && speedHistory && speedHistory.length > 1 && (
        <div style={{ gridColumn: '1 / -1', display: 'flex', alignItems: 'center', gap: 10, fontSize: 11, opacity: 0.8 }}>
          <Sparkline data={speedHistory} />
          <span>peak {fmtSpeed(Math.max(...speedHistory))} · avg {fmtSpeed(speedHistory.reduce((a, b) => a + b, 0) / speedHistory.length)}</span>
        </div>
      )}
      {open && segments && (
        <Segments title="Pieces downloaded">
          {segments.map((on, i) => <span key={i} className={on ? 'on' : ''} />)}
        </Segments>
      )}
      {open && (
        <Details>
          <span className="k">URL</span>
          <span className="v">
            {uri || '—'}{' '}
            {uri && <Button $small onClick={() => navigator.clipboard.writeText(uri)}>Copy</Button>}
          </span>
          <span className="k">Saved to</span>
          <span className="v">
            {path || d.dir || '—'}{' '}
            {!torrent && (d.status === 'complete' ? !!path : d.status !== 'error' && !fromHistory) && (
              <Button $small onClick={() => { setNewName(basename(path) || nameOf(d)); setRenaming(true) }}>Rename</Button>
            )}
          </span>
          {d.status === 'waiting' && queueIndex !== undefined && (
            <>
              <span className="k">Queue</span>
              <span className="v">
                #{queueIndex + 1} of {queueSize}{' '}
                <Button $small onClick={() => move('top')}>Start next</Button>{' '}
                <Button $small onClick={() => move('bottom')}>Send to back</Button>
              </span>
            </>
          )}
          {torrent && <><span className="k">Info hash</span><span className="v">{d.infoHash || '—'}</span></>}
          {scan?.files[0]?.sha256 && (
            <><span className="k">SHA-256</span><span className="v">{scan.files[0].sha256}{' '}
              <Button $small onClick={() => navigator.clipboard.writeText(scan.files[0].sha256)}>Copy</Button>{' '}
              <Button $small onClick={() => chrome.send('falcon_downloader.rescan', [d.gid])}>Rescan</Button>{' '}
              {scan.files[0].vt !== 'nokey' && (
                <a href={`https://www.virustotal.com/gui/file/${scan.files[0].sha256}`} target="_blank" rel="noopener" style={{ color: '#38bdf8' }}>VirusTotal ↗</a>
              )}
            </span></>
          )}
          {d.errorCode && d.errorCode !== '0' && <><span className="k">Error</span><span className="v">#{d.errorCode} {d.errorMessage}</span></>}
          {!fromHistory && (d.status === 'active' || d.status === 'waiting' || d.status === 'paused') && (
            <>
              <span className="k">When finished</span>
              <span className="v">
                <label style={{ display: 'inline-flex', alignItems: 'center', gap: 6, cursor: 'pointer' }}>
                  <input type="checkbox" checked={openWhenDone} onChange={toggleOpenWhenDone} />
                  Open the file (after the security scan passes)
                </label>
              </span>
            </>
          )}
          {!fromHistory && d.status === 'error' && (
            <>
              <span className="k">Link expired?</span>
              <span className="v">
                <Button $small onClick={refreshUrl} disabled={refreshing}>Refresh link</Button>{' '}
                <span style={{ opacity: 0.6 }}>{referer ? `re-opens ${referer.slice(0, 60)}` : 'no source page recorded; re-click the link yourself'}</span>
              </span>
            </>
          )}
          {!fromHistory && <span className="k">Speed limit</span>}
          {!fromHistory && <span className="v" style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
            <Input style={{ width: 100, padding: '5px 8px' }} placeholder="KB/s" value={limit}
              onChange={(e) => setLimit(e.target.value)} onBlur={applyLimit}
              onKeyDown={(e) => e.key === 'Enter' && applyLimit()} />
            <span style={{ opacity: 0.6 }}>0 = unlimited (this download only)</span>
          </span>}
          <span className="k">Danger zone</span>
          <span className="v">
            <Button $small $danger onClick={deleteWithFile}>Delete with file{files && files.length > 1 ? 's' : ''}</Button>
          </span>
        </Details>
      )}
      {open && torrent && files && files.length > 1 && (
        <FileList>
          {files.map((f) => (
            <label key={f.index}>
              <input type="checkbox" checked={f.selected === 'true'}
                onChange={(e) => toggleFile(f, e.target.checked)} />
              <span style={{ overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{basename(f.path)}</span>
              <span className="size">{fmtBytes(+f.completedLength)} / {fmtBytes(+f.length)}</span>
            </label>
          ))}
        </FileList>
      )}
    </Row>
  )
}
