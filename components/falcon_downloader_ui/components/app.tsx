// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

import * as React from 'react'
import styled from 'styled-components'
import { loadTimeData } from '$web-common/loadTimeData'
import {
  Aria2Client,
  Aria2Download,
  Aria2GlobalStat,
  Aria2Status,
} from '../aria2_client'

// ---------------------------------------------------------------- helpers

function fmtBytes(n: number): string {
  if (!isFinite(n) || n <= 0) return '0 B'
  const units = ['B', 'KB', 'MB', 'GB', 'TB']
  let i = 0
  while (n >= 1024 && i < units.length - 1) {
    n /= 1024
    i++
  }
  return `${n < 10 && i > 0 ? n.toFixed(1) : Math.round(n)} ${units[i]}`
}

function fmtSpeed(n: number): string {
  return n > 0 ? `${fmtBytes(n)}/s` : '—'
}

function fmtEta(remaining: number, speed: number): string {
  if (speed <= 0 || remaining <= 0) return '—'
  let s = Math.round(remaining / speed)
  if (s < 60) return `${s}s`
  const m = Math.floor(s / 60)
  s %= 60
  if (m < 60) return `${m}m ${s}s`
  const h = Math.floor(m / 60)
  return `${h}h ${m % 60}m`
}

function nameOf(d: Aria2Download): string {
  const bt = d.bittorrent?.info?.name
  if (bt) return bt
  const p = d.files?.[0]?.path
  if (p) {
    const base = p.split(/[\\/]/).pop()
    if (base) return base
  }
  const uri = d.files?.[0]?.uris?.[0]?.uri
  if (uri) {
    try {
      const u = new URL(uri)
      return decodeURIComponent(u.pathname.split('/').pop() || u.host)
    } catch {
      return uri
    }
  }
  return d.gid
}

type Filter = 'all' | 'active' | 'done' | 'failed'

function matchesFilter(d: Aria2Download, f: Filter): boolean {
  switch (f) {
    case 'active':
      return d.status === 'active' || d.status === 'waiting' || d.status === 'paused'
    case 'done':
      return d.status === 'complete'
    case 'failed':
      return d.status === 'error' || d.status === 'removed'
    default:
      return true
  }
}

const STATUS_LABEL: Record<Aria2Status, string> = {
  active: 'Downloading',
  waiting: 'Queued',
  paused: 'Paused',
  error: 'Failed',
  complete: 'Done',
  removed: 'Removed',
}

// ----------------------------------------------------------------- styles

const Page = styled.div`
  max-width: 1040px;
  margin: 0 auto;
  padding: 28px 24px 48px;
`

const Header = styled.div`
  display: flex;
  align-items: center;
  gap: 16px;
  flex-wrap: wrap;
  margin-bottom: 18px;
`

const Title = styled.h1`
  font-size: 26px;
  font-weight: 700;
  margin: 0;
  flex: 1;
`

const Stat = styled.div`
  font-size: 13px;
  opacity: 0.8;
  b { font-weight: 600; opacity: 1; }
`

const Dot = styled.span<{ $on: boolean }>`
  display: inline-block;
  width: 8px;
  height: 8px;
  border-radius: 50%;
  margin-inline-end: 6px;
  background: ${(p) => (p.$on ? '#22c55e' : '#f59e0b')};
`

const AddRow = styled.form`
  display: flex;
  gap: 8px;
  margin-bottom: 14px;
`

const Input = styled.input`
  flex: 1;
  padding: 10px 12px;
  border-radius: 10px;
  border: 1px solid var(--leo-color-divider-subtle, #334155);
  background: var(--leo-color-container-background, #1e293b);
  color: inherit;
  font-size: 14px;
  outline: none;
  &:focus { border-color: #38bdf8; }
`

const Button = styled.button<{ $primary?: boolean; $danger?: boolean }>`
  padding: 9px 14px;
  border-radius: 10px;
  border: 1px solid transparent;
  background: ${(p) => (p.$primary ? '#0ea5e9' : p.$danger ? '#7f1d1d' : 'var(--leo-color-container-background, #1e293b)')};
  color: ${(p) => (p.$primary ? '#fff' : 'inherit')};
  font-size: 13px;
  font-weight: 600;
  cursor: pointer;
  white-space: nowrap;
  &:hover { filter: brightness(1.15); }
  &:disabled { opacity: 0.4; cursor: default; filter: none; }
`

const Toolbar = styled.div`
  display: flex;
  gap: 6px;
  align-items: center;
  margin-bottom: 12px;
  flex-wrap: wrap;
`

const Chip = styled.button<{ $active: boolean }>`
  padding: 6px 12px;
  border-radius: 999px;
  border: 1px solid ${(p) => (p.$active ? '#38bdf8' : 'var(--leo-color-divider-subtle, #334155)')};
  background: ${(p) => (p.$active ? 'rgba(56,189,248,0.15)' : 'transparent')};
  color: inherit;
  font-size: 12px;
  cursor: pointer;
`

const Spacer = styled.div`
  flex: 1;
`

const List = styled.div`
  display: flex;
  flex-direction: column;
  gap: 10px;
`

const Row = styled.div`
  background: var(--leo-color-container-background, #1e293b);
  border: 1px solid var(--leo-color-divider-subtle, #334155);
  border-radius: 14px;
  padding: 12px 14px;
  display: grid;
  grid-template-columns: 1fr auto;
  gap: 8px 14px;
`

const Name = styled.div`
  font-size: 14px;
  font-weight: 600;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
`

const Meta = styled.div`
  font-size: 12px;
  opacity: 0.75;
  display: flex;
  gap: 14px;
  flex-wrap: wrap;
`

const Bar = styled.div<{ $pct: number; $status: Aria2Status }>`
  grid-column: 1 / -1;
  height: 6px;
  border-radius: 999px;
  background: rgba(148, 163, 184, 0.2);
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
`

const Empty = styled.div`
  text-align: center;
  padding: 64px 0;
  opacity: 0.6;
  font-size: 14px;
`

const ErrorText = styled.span`
  color: #f87171;
`

// -------------------------------------------------------------------- app

export function App() {
  const client = React.useMemo(
    () =>
      new Aria2Client(
        loadTimeData.getString('rpcUrl'),
        loadTimeData.getString('rpcSecret'),
      ),
    [],
  )
  const [connected, setConnected] = React.useState(false)
  const [downloads, setDownloads] = React.useState<Aria2Download[]>([])
  const [stat, setStat] = React.useState<Aria2GlobalStat | null>(null)
  const [filter, setFilter] = React.useState<Filter>('all')
  const [url, setUrl] = React.useState('')
  const [limit, setLimit] = React.useState('')
  const [error, setError] = React.useState<string | null>(null)
  const fileInput = React.useRef<HTMLInputElement>(null)

  const refresh = React.useCallback(async () => {
    if (!client.connected) return
    try {
      const snap = await client.snapshot()
      setDownloads(snap.downloads)
      setStat(snap.stat)
    } catch {
      /* transient */
    }
  }, [client])

  React.useEffect(() => {
    client.onConnectionChange = (c) => {
      setConnected(c)
      if (c) refresh()
    }
    client.onNotification = () => refresh()
    client.connect()
    const timer = window.setInterval(refresh, 1000)
    return () => {
      window.clearInterval(timer)
      client.close()
    }
  }, [client, refresh])

  const add = async (e: React.FormEvent) => {
    e.preventDefault()
    const v = url.trim()
    if (!v) return
    setError(null)
    try {
      await client.addUri([v])
      setUrl('')
      refresh()
    } catch (err: any) {
      setError(err?.message ?? 'Could not add download')
    }
  }

  const addTorrentFile = async (f: File) => {
    const buf = await f.arrayBuffer()
    let bin = ''
    const bytes = new Uint8Array(buf)
    for (let i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i])
    try {
      await client.addTorrent(btoa(bin))
      refresh()
    } catch (err: any) {
      setError(err?.message ?? 'Could not add torrent')
    }
  }

  const applyLimit = async () => {
    const kb = parseInt(limit, 10)
    await client.setGlobalSpeedLimit(isFinite(kb) && kb > 0 ? kb * 1024 : 0)
  }

  const visible = downloads.filter((d) => matchesFilter(d, filter))
  const order: Record<Aria2Status, number> = {
    active: 0, waiting: 1, paused: 2, error: 3, complete: 4, removed: 5,
  }
  visible.sort((a, b) => order[a.status] - order[b.status])

  return (
    <Page>
      <Header>
        <Title>Downloads</Title>
        <Stat>
          <Dot $on={connected} />
          {connected ? 'Engine connected' : 'Connecting to engine…'}
        </Stat>
        {stat && (
          <Stat>
            ↓ <b>{fmtSpeed(+stat.downloadSpeed)}</b> &nbsp; ↑ <b>{fmtSpeed(+stat.uploadSpeed)}</b>
            &nbsp; · {stat.numActive} active
          </Stat>
        )}
      </Header>

      <AddRow onSubmit={add}>
        <Input
          placeholder="Paste a URL, magnet link, or drop a .torrent file"
          value={url}
          onChange={(e) => setUrl(e.target.value)}
          spellCheck={false}
        />
        <Button $primary type="submit" disabled={!connected || !url.trim()}>
          Download
        </Button>
        <Button type="button" onClick={() => fileInput.current?.click()} disabled={!connected}>
          .torrent
        </Button>
        <input
          ref={fileInput}
          type="file"
          accept=".torrent"
          style={{ display: 'none' }}
          onChange={(e) => {
            const f = e.target.files?.[0]
            if (f) addTorrentFile(f)
            e.target.value = ''
          }}
        />
      </AddRow>
      {error && <Meta><ErrorText>{error}</ErrorText></Meta>}

      <Toolbar>
        {(['all', 'active', 'done', 'failed'] as Filter[]).map((f) => (
          <Chip key={f} $active={filter === f} onClick={() => setFilter(f)}>
            {f === 'all' ? 'All' : f === 'active' ? 'Active' : f === 'done' ? 'Completed' : 'Failed'}
          </Chip>
        ))}
        <Spacer />
        <Stat>Speed limit</Stat>
        <Input
          style={{ flex: '0 0 90px', padding: '6px 10px' }}
          placeholder="KB/s"
          value={limit}
          onChange={(e) => setLimit(e.target.value)}
          onBlur={applyLimit}
          onKeyDown={(e) => e.key === 'Enter' && applyLimit()}
        />
        <Button type="button" onClick={() => client.purge().then(refresh)} disabled={!connected}>
          Clear finished
        </Button>
      </Toolbar>

      {visible.length === 0 ? (
        <Empty>
          {connected
            ? 'No downloads yet. Paste a link above, or just download something — Falcon takes over automatically.'
            : 'Starting the download engine…'}
        </Empty>
      ) : (
        <List>
          {visible.map((d) => {
            const total = +d.totalLength
            const done = +d.completedLength
            const speed = +d.downloadSpeed
            const pct = total > 0 ? (done / total) * 100 : 0
            const path = d.files?.[0]?.path ?? ''
            const canOpen = d.status === 'complete' && path
            return (
              <Row key={d.gid}>
                <div style={{ minWidth: 0 }}>
                  <Name title={path || nameOf(d)}>{nameOf(d)}</Name>
                  <Meta>
                    <span>{STATUS_LABEL[d.status]}</span>
                    <span>
                      {fmtBytes(done)}
                      {total > 0 ? ` / ${fmtBytes(total)}` : ''}
                      {total > 0 ? ` (${pct.toFixed(0)}%)` : ''}
                    </span>
                    {d.status === 'active' && (
                      <>
                        <span>{fmtSpeed(speed)}</span>
                        <span>ETA {fmtEta(total - done, speed)}</span>
                        <span>{d.connections} conn</span>
                      </>
                    )}
                    {d.status === 'error' && d.errorMessage && (
                      <ErrorText>{d.errorMessage}</ErrorText>
                    )}
                  </Meta>
                </div>
                <Actions>
                  {d.status === 'active' || d.status === 'waiting' ? (
                    <Button onClick={() => client.pause(d.gid).then(refresh)}>Pause</Button>
                  ) : d.status === 'paused' ? (
                    <Button $primary onClick={() => client.unpause(d.gid).then(refresh)}>Resume</Button>
                  ) : null}
                  {canOpen && (
                    <>
                      <Button onClick={() => chrome.send('falcon_downloader.openFile', [path])}>Open</Button>
                      <Button onClick={() => chrome.send('falcon_downloader.openFolder', [path])}>Folder</Button>
                    </>
                  )}
                  {d.status === 'active' || d.status === 'waiting' || d.status === 'paused' ? (
                    <Button $danger onClick={() => client.remove(d.gid).then(refresh)}>Cancel</Button>
                  ) : (
                    <Button onClick={() => client.removeResult(d.gid).then(refresh)}>Remove</Button>
                  )}
                </Actions>
                <Bar $pct={pct} $status={d.status} />
              </Row>
            )
          })}
        </List>
      )}
    </Page>
  )
}
