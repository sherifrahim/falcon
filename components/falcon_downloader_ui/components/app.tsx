// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

import * as React from 'react'
import styled from 'styled-components'
import { loadTimeData } from '$web-common/loadTimeData'
import { sendWithPromise } from 'chrome://resources/js/cr.js'
import {
  Aria2Client, Aria2Download, Aria2GlobalStat, HistoryEntry, expandBatch,
  historyToDownload, isDownloadable,
} from '../aria2_client'
import {
  Button, Chip, ErrorText, Filter, Input, Meta, STATUS_ORDER, Select, SortKey,
  Stat, fmtBytes, fmtSpeed, matchesFilter, nameOf,
} from './common'
import { DownloadRow, ScanResult } from './download_row'
import { Grabber } from './grabber'
import { MediaJob, MediaPicker, MediaRow, SniffedList, SniffedTab } from './media_panel'
import { SettingsDrawer } from './settings_drawer'

const MEDIA_AVAILABLE = loadTimeData.getBoolean('mediaAvailable')
// Side-panel layout: narrower paddings, no page title, wrapped toolbar.
const PANEL = 'panel' in (loadTimeData.data_ || {}) && loadTimeData.getBoolean('panel')

// #media=<url>&referer=<url> opens the quality picker (from the toolbar bubble).
function pickerFromHash(): { url: string; referer: string } | null {
  const m = location.hash.match(/^#media=([^&]*)(?:&referer=([^&]*))?/)
  if (!m) return null
  try {
    return { url: decodeURIComponent(m[1]), referer: m[2] ? decodeURIComponent(m[2]) : '' }
  } catch {
    return null
  }
}

const Page = styled.div<{ $drag: boolean }>`
  max-width: 1040px;
  margin: 0 auto;
  padding: ${PANEL ? '12px 10px 24px' : '28px 24px 48px'};
  min-height: 100%;
  outline: ${(p) => (p.$drag ? '2px dashed #38bdf8' : 'none')};
  outline-offset: -12px;
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
  margin-bottom: 10px;
  flex-wrap: ${PANEL ? 'wrap' : 'nowrap'};
`

const Batch = styled.textarea`
  width: 100%;
  min-height: 120px;
  margin-bottom: 8px;
  padding: 10px 12px;
  border-radius: 10px;
  border: 1px solid var(--leo-color-divider-subtle, #334155);
  background: var(--leo-color-container-background, #1e293b);
  color: inherit;
  font-family: ui-monospace, Consolas, monospace;
  font-size: 12px;
  resize: vertical;
`

const Options = styled.div`
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
  gap: 8px 12px;
  margin-bottom: 10px;
  padding: 12px 14px;
  border-radius: 12px;
  border: 1px solid var(--leo-color-divider-subtle, #334155);
  label { display: flex; flex-direction: column; gap: 4px; font-size: 12px; opacity: 0.85; }
  label.row { flex-direction: row; align-items: center; gap: 8px; }
  input[type='text'], input[type='password'], input[type='number'], select { padding: 6px 10px; font-size: 13px; }
`

// Per-download aria2 options from the "Options" panel.
interface AddOptions {
  out: string
  dir: string
  checksumAlgo: string
  checksum: string
  user: string
  pass: string
  proxy: string
  split: string
  pause: boolean
}

const EMPTY_OPTIONS: AddOptions = {
  out: '', dir: '', checksumAlgo: 'sha-256', checksum: '', user: '', pass: '',
  proxy: '', split: '', pause: false,
}

function toAria2Options(o: AddOptions, single: boolean): Record<string, string> {
  const r: Record<string, string> = {}
  if (single && o.out.trim()) r.out = o.out.trim()
  if (o.dir.trim()) r.dir = o.dir.trim()
  const sum = o.checksum.trim().toLowerCase()
  if (single && sum && /^[0-9a-f]+$/.test(sum)) r.checksum = `${o.checksumAlgo}=${sum}`
  if (o.user) { r['http-user'] = o.user; r['ftp-user'] = o.user }
  if (o.pass) { r['http-passwd'] = o.pass; r['ftp-passwd'] = o.pass }
  if (o.proxy.trim()) r['all-proxy'] = o.proxy.trim()
  const n = parseInt(o.split, 10)
  if (isFinite(n) && n > 0) { r.split = String(n); r['max-connection-per-server'] = String(Math.min(n, 16)) }
  if (o.pause) r.pause = 'true'
  return r
}

const Toolbar = styled.div`
  display: flex;
  gap: 6px;
  align-items: center;
  margin: 8px 0 12px;
  flex-wrap: wrap;
`

const Spacer = styled.div`
  flex: 1;
`

const List = styled.div`
  display: flex;
  flex-direction: column;
  gap: 10px;
`

const Empty = styled.div`
  text-align: center;
  padding: 64px 0;
  opacity: 0.6;
  font-size: 14px;
`

const Footer = styled.div`
  margin-top: 18px;
  font-size: 12px;
  opacity: 0.55;
  display: flex;
  gap: 16px;
  flex-wrap: wrap;
`

export function App() {
  const client = React.useMemo(
    () => new Aria2Client(loadTimeData.getString('rpcUrl'), loadTimeData.getString('rpcSecret')),
    [],
  )
  const [connected, setConnected] = React.useState(false)
  const [downloads, setDownloads] = React.useState<Aria2Download[]>([])
  const [stat, setStat] = React.useState<Aria2GlobalStat | null>(null)
  const [filter, setFilter] = React.useState<Filter>('all')
  const [sort, setSort] = React.useState<SortKey>('added')
  const [query, setQuery] = React.useState('')
  const [url, setUrl] = React.useState('')
  const [batchOpen, setBatchOpen] = React.useState(false)
  const [batchText, setBatchText] = React.useState('')
  const [error, setError] = React.useState<string | null>(null)
  const [openGid, setOpenGid] = React.useState<string | null>(null)
  const [settingsOpen, setSettingsOpen] = React.useState(
    () => location.hash === '#settings',
  )
  const [drag, setDrag] = React.useState(false)
  const [scans, setScans] = React.useState<Record<string, ScanResult>>({})
  const [history, setHistory] = React.useState<HistoryEntry[]>([])
  const [stats, setStats] = React.useState<{ totalBytes: number; totalFiles: number } | null>(null)
  const [optionsOpen, setOptionsOpen] = React.useState(false)
  const [opts, setOpts] = React.useState<AddOptions>(EMPTY_OPTIONS)
  const [mediaJobs, setMediaJobs] = React.useState<MediaJob[]>([])
  const [sniffed, setSniffed] = React.useState<SniffedTab[]>([])
  const [picker, setPicker] = React.useState<{ url: string; referer: string } | null>(() => pickerFromHash())
  const [grabber, setGrabber] = React.useState<string | null>(null)
  const fileInput = React.useRef<HTMLInputElement>(null)
  const listInput = React.useRef<HTMLInputElement>(null)
  const order = React.useRef(new Map<string, number>())
  const sessionBytes = React.useRef(0)
  const lastDone = React.useRef(new Map<string, number>())
  // Per-download speed samples (last ~60 s) for the sparkline in the row.
  const speeds = React.useRef(new Map<string, number[]>())

  const refresh = React.useCallback(async () => {
    if (!client.connected) return
    try {
      const snap = await client.snapshot()
      for (const d of snap.downloads) {
        if (!order.current.has(d.gid)) order.current.set(d.gid, order.current.size)
        const prev = lastDone.current.get(d.gid) ?? +d.completedLength
        const now = +d.completedLength
        if (now > prev) sessionBytes.current += now - prev
        lastDone.current.set(d.gid, now)
        if (d.status === 'active') {
          const arr = speeds.current.get(d.gid) ?? []
          arr.push(+d.downloadSpeed)
          if (arr.length > 60) arr.shift()
          speeds.current.set(d.gid, arr)
        }
      }
      setDownloads(snap.downloads)
      setStat(snap.stat)
    } catch {
      /* transient */
    }
  }, [client])

  const loadMedia = React.useCallback(() => {
    if (!MEDIA_AVAILABLE) return
    sendWithPromise('falcon_downloader.getMediaJobs').then((jobs: MediaJob[]) => setMediaJobs(jobs ?? [])).catch(() => {})
    sendWithPromise('falcon_downloader.getSniffedMedia').then((tabs: SniffedTab[]) => setSniffed(tabs ?? [])).catch(() => {})
  }, [])

  const loadHistory = React.useCallback(() => {
    sendWithPromise('falcon_downloader.getHistory')
      .then((r: { entries: HistoryEntry[]; stats: { totalBytes: number; totalFiles: number } }) => {
        setHistory(r.entries ?? [])
        setStats(r.stats ?? null)
      })
      .catch(() => {})
  }, [])

  React.useEffect(() => {
    client.onConnectionChange = (c) => {
      setConnected(c)
      if (c) refresh()
    }
    client.onNotification = () => { refresh(); window.setTimeout(loadHistory, 800) }
    client.connect()
    loadHistory()
    loadMedia()
    const timer = window.setInterval(() => { refresh(); loadMedia() }, 1000)
    const scanTimer = window.setInterval(() => {
      sendWithPromise('falcon_downloader.getScanResults').then(setScans).catch(() => {})
      loadHistory()
    }, 2000)
    const onHash = () => {
      setSettingsOpen(location.hash === '#settings')
      const p = pickerFromHash()
      if (p) setPicker(p)
    }
    window.addEventListener('hashchange', onHash)
    return () => {
      window.clearInterval(timer)
      window.clearInterval(scanTimer)
      window.removeEventListener('hashchange', onHash)
      client.close()
    }
  }, [client, refresh, loadHistory, loadMedia])

  const poke = () => chrome.send('falcon_downloader.poke')

  const addUrls = async (urls: string[]) => {
    const valid = urls.filter(isDownloadable)
    if (valid.length === 0) {
      setError('Nothing downloadable found (http, https, ftp, sftp or magnet links)')
      return
    }
    // Skip links that are already queued or running.
    const inList = new Set(
      downloads
        .filter((d) => d.status === 'active' || d.status === 'waiting' || d.status === 'paused')
        .map((d) => d.files?.[0]?.uris?.[0]?.uri)
        .filter(Boolean),
    )
    const fresh = valid.filter((u) => !inList.has(u))
    setError(fresh.length < valid.length ? `${valid.length - fresh.length} already in the list, skipped` : null)
    try {
      const options = toAria2Options(opts, fresh.length === 1)
      for (const u of fresh) await client.addUri([u], options)
      if (fresh.length > 0) setOpts((o) => ({ ...o, out: '', checksum: '' }))
      refresh()
      poke()
    } catch (err: any) {
      setError(err?.message ?? 'Could not add download')
    }
  }

  const importList = async (f: File) => {
    const text = await f.text()
    setBatchText((t) => (t ? t + '\n' : '') + text)
    setBatchOpen(true)
  }

  const add = (e: React.FormEvent) => {
    e.preventDefault()
    const v = url.trim()
    if (!v) return
    addUrls(expandBatch(v)).then(() => setUrl(''))
  }

  const addTorrentFile = async (f: File) => {
    const buf = await f.arrayBuffer()
    let bin = ''
    const bytes = new Uint8Array(buf)
    for (let i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i])
    try {
      await client.addTorrent(btoa(bin))
      refresh()
      poke()
    } catch (err: any) {
      setError(err?.message ?? 'Could not add torrent')
    }
  }

  const onDrop = (e: React.DragEvent) => {
    e.preventDefault()
    setDrag(false)
    const files = Array.from(e.dataTransfer.files || [])
    for (const f of files) if (f.name.endsWith('.torrent')) addTorrentFile(f)
    const text = e.dataTransfer.getData('text/uri-list') || e.dataTransfer.getData('text/plain')
    if (text) addUrls(expandBatch(text))
  }

  const onPaste = (e: React.ClipboardEvent) => {
    const text = e.clipboardData.getData('text')
    if (text.includes('\n') && text.split('\n').filter(isDownloadable).length > 1) {
      e.preventDefault()
      setBatchText(text)
      setBatchOpen(true)
    }
  }

  const q = query.trim().toLowerCase()
  const known = new Set(downloads.map((d) => d.gid))
  const merged: Aria2Download[] = [
    ...downloads,
    ...history.filter((h) => !known.has(h.gid)).map(historyToDownload),
  ]
  const queue = downloads.filter((d) => d.status === 'waiting').map((d) => d.gid)
  const visible = merged
    .filter((d) => matchesFilter(d, filter))
    .filter((d) => !q || nameOf(d).toLowerCase().includes(q))
  const cmp: Record<SortKey, (a: Aria2Download, b: Aria2Download) => number> = {
    added: (a, b) =>
      a.fromHistory && b.fromHistory
        ? (b.finishedAt ?? 0) - (a.finishedAt ?? 0)
        : (order.current.get(b.gid) ?? -1) - (order.current.get(a.gid) ?? -1),
    name: (a, b) => nameOf(a).localeCompare(nameOf(b)),
    size: (a, b) => +b.totalLength - +a.totalLength,
    progress: (a, b) => (+b.completedLength / (+b.totalLength || 1)) - (+a.completedLength / (+a.totalLength || 1)),
    speed: (a, b) => +b.downloadSpeed - +a.downloadSpeed,
  }
  visible.sort((a, b) => STATUS_ORDER[a.status] - STATUS_ORDER[b.status] || cmp[sort](a, b))

  const mediaLive = (j: MediaJob) => j.status === 'starting' || j.status === 'downloading' || j.status === 'merging'
  const visibleMedia = mediaJobs
    .filter((j) => filter === 'all' || (filter === 'active' && mediaLive(j)) || (filter === 'done' && j.status === 'done') || (filter === 'failed' && (j.status === 'error' || j.status === 'cancelled')))
    .filter((j) => !q || (j.title || j.url).toLowerCase().includes(q))
    .sort((a, b) => b.started - a.started)
  const counts = {
    active: downloads.filter((d) => matchesFilter(d, 'active')).length + mediaJobs.filter(mediaLive).length,
    done: merged.filter((d) => d.status === 'complete').length + mediaJobs.filter((j) => j.status === 'done').length,
    failed: merged.filter((d) => d.status === 'error').length + mediaJobs.filter((j) => j.status === 'error').length,
  }
  const anyActive = downloads.some((d) => d.status === 'active' || d.status === 'waiting')
  const anyPaused = downloads.some((d) => d.status === 'paused')

  return (
    <Page
      $drag={drag}
      onDragOver={(e) => { e.preventDefault(); setDrag(true) }}
      onDragLeave={() => setDrag(false)}
      onDrop={onDrop}
    >
      {settingsOpen && (
        <SettingsDrawer onClose={() => { setSettingsOpen(false); if (location.hash) window.history.replaceState(null, '', ' ') }} />
      )}
      <Header>
        {!PANEL && <Title>Downloads</Title>}
        <Stat>
          <Dot $on={connected} />
          {connected ? 'Engine connected' : 'Connecting to engine…'}
        </Stat>
        {stat && (
          <Stat>
            ↓ <b>{fmtSpeed(+stat.downloadSpeed)}</b> &nbsp; ↑ <b>{fmtSpeed(+stat.uploadSpeed)}</b>
            &nbsp; · {stat.numActive} active{+stat.numWaiting > 0 ? ` · ${stat.numWaiting} queued` : ''}
          </Stat>
        )}
        <Button $small={PANEL} onClick={() => setSettingsOpen(true)} title="Engine settings">⚙ Settings</Button>
        {PANEL && <Button $small onClick={() => window.open('chrome://downloader', '_blank')} title="Open as a full page">⤢</Button>}
      </Header>

      <AddRow onSubmit={add}>
        <Input
          style={{ flex: 1 }}
          placeholder="Paste a URL, magnet link, or a pattern like file[01-20].zip — or drop links / .torrent files anywhere"
          value={url}
          onChange={(e) => setUrl(e.target.value)}
          onPaste={onPaste}
          spellCheck={false}
        />
        <Button $primary type="submit" disabled={!connected || !url.trim()}>Download</Button>
        {MEDIA_AVAILABLE && (
          <Button type="button" onClick={() => setPicker({ url: url.trim(), referer: '' })}
            title="Grab video/audio from a page or stream (YouTube, Vimeo, HLS, DASH…) at a chosen quality">Video</Button>
        )}
        <Button type="button" onClick={() => setGrabber(url.trim())} disabled={!connected}
          title="Scan a page for files (site grabber)">Grab</Button>
        <Button type="button" onClick={() => setOptionsOpen((v) => !v)} disabled={!connected}
          title="Filename, folder, checksum, login, proxy, connections">{optionsOpen ? 'Options ▴' : 'Options ▾'}</Button>
        <Button type="button" onClick={() => setBatchOpen((v) => !v)} disabled={!connected}>Batch</Button>
        <Button type="button" onClick={() => fileInput.current?.click()} disabled={!connected}>.torrent</Button>
        <input
          ref={fileInput} type="file" accept=".torrent,.metalink,.meta4" multiple style={{ display: 'none' }}
          onChange={(e) => {
            for (const f of Array.from(e.target.files ?? [])) addTorrentFile(f)
            e.target.value = ''
          }}
        />
      </AddRow>
      {grabber !== null && (
        <Grabber
          initialUrl={grabber}
          onClose={() => setGrabber(null)}
          onQueue={(urls, referer) => {
            const options = { ...toAria2Options(opts, false), referer }
            Promise.all(urls.map((u) => client.addUri([u], options).catch(() => null))).then(() => { refresh(); poke(); setUrl('') })
          }}
        />
      )}
      {!picker && (
        <SniffedList
          tabs={sniffed}
          onFile={(u, ref) => { client.addUri([u], { ...toAria2Options(opts, false), referer: ref }).then(() => { refresh(); poke() }).catch((e: any) => setError(e?.message ?? 'Could not add')) }}
          onStream={(u, ref) => { chrome.send('falcon_downloader.startMedia', [u, ref, 'best']); window.setTimeout(loadMedia, 300) }}
          onQuality={(u, ref) => setPicker({ url: u, referer: ref })}
        />
      )}
      {picker && (
        <MediaPicker
          initialUrl={picker.url}
          referer={picker.referer}
          onClose={() => { setPicker(null); if (location.hash.startsWith('#media=')) window.history.replaceState(null, '', ' ') }}
          onStarted={() => { setUrl(''); window.setTimeout(loadMedia, 300) }}
        />
      )}
      {optionsOpen && (
        <Options>
          <label>Save as (single link)
            <Input type="text" value={opts.out} placeholder="keep server name" onChange={(e) => setOpts({ ...opts, out: e.target.value })} /></label>
          <label>Save to folder
            <Input type="text" value={opts.dir} placeholder={loadTimeData.getString('downloadDir') || 'default'} onChange={(e) => setOpts({ ...opts, dir: e.target.value })} /></label>
          <label>Verify checksum (single link)
            <span style={{ display: 'flex', gap: 6 }}>
              <Select value={opts.checksumAlgo} onChange={(e) => setOpts({ ...opts, checksumAlgo: e.target.value })}>
                <option value="sha-256">SHA-256</option><option value="sha-1">SHA-1</option><option value="md5">MD5</option>
                <option value="sha-512">SHA-512</option>
              </Select>
              <Input type="text" style={{ flex: 1 }} value={opts.checksum} placeholder="hex digest" spellCheck={false}
                onChange={(e) => setOpts({ ...opts, checksum: e.target.value })} />
            </span></label>
          <label>Site login (HTTP/FTP basic auth)
            <span style={{ display: 'flex', gap: 6 }}>
              <Input type="text" style={{ flex: 1 }} value={opts.user} placeholder="user" autoComplete="off" onChange={(e) => setOpts({ ...opts, user: e.target.value })} />
              <Input type="password" style={{ flex: 1 }} value={opts.pass} placeholder="password" autoComplete="new-password" onChange={(e) => setOpts({ ...opts, pass: e.target.value })} />
            </span></label>
          <label>Proxy for this download
            <Input type="text" value={opts.proxy} placeholder="http://host:port or socks5://…" onChange={(e) => setOpts({ ...opts, proxy: e.target.value })} /></label>
          <label>Connections
            <Input type="number" min={1} max={64} value={opts.split} placeholder="engine default" onChange={(e) => setOpts({ ...opts, split: e.target.value })} /></label>
          <label className="row">
            <input type="checkbox" checked={opts.pause} onChange={(e) => setOpts({ ...opts, pause: e.target.checked })} />
            Add paused (start later)
          </label>
          <label className="row" style={{ justifyContent: 'flex-end' }}>
            <Button $small type="button" onClick={() => setOpts(EMPTY_OPTIONS)}>Reset</Button>
          </label>
        </Options>
      )}
      {batchOpen && (
        <div>
          <Batch
            placeholder={'One link per line. Patterns expand: photo[001-120].jpg, disc[a-d].iso'}
            value={batchText}
            onChange={(e) => setBatchText(e.target.value)}
          />
          <div style={{ display: 'flex', gap: 8, marginBottom: 10 }}>
            <Button $primary onClick={() => addUrls(expandBatch(batchText)).then(() => { setBatchText(''); setBatchOpen(false) })}>
              Add {expandBatch(batchText).filter(isDownloadable).length || ''} downloads
            </Button>
            <Button onClick={() => listInput.current?.click()}>Import .txt list…</Button>
            <input ref={listInput} type="file" accept=".txt,.csv,.lst,text/plain" style={{ display: 'none' }}
              onChange={(e) => { for (const f of Array.from(e.target.files ?? [])) importList(f); e.target.value = '' }} />
            <Button onClick={() => setBatchOpen(false)}>Cancel</Button>
          </div>
        </div>
      )}
      {error && <Meta style={{ marginBottom: 8 }}><ErrorText>{error}</ErrorText></Meta>}

      <Toolbar>
        {([['all', 'All'], ['active', `Active${counts.active ? ` ${counts.active}` : ''}`],
           ['done', `Completed${counts.done ? ` ${counts.done}` : ''}`],
           ['failed', `Failed${counts.failed ? ` ${counts.failed}` : ''}`],
           ['torrents', 'Torrents']] as Array<[Filter, string]>).map(([f, label]) => (
          <Chip key={f} $active={filter === f} onClick={() => setFilter(f)}>{label}</Chip>
        ))}
        <Input style={{ padding: '6px 10px', width: 180 }} placeholder="Search" value={query}
          onChange={(e) => setQuery(e.target.value)} />
        <Select value={sort} onChange={(e) => setSort(e.target.value as SortKey)}>
          <option value="added">Newest first</option>
          <option value="name">Name</option>
          <option value="size">Size</option>
          <option value="progress">Progress</option>
          <option value="speed">Speed</option>
        </Select>
        <Spacer />
        {anyActive && <Button $small onClick={() => client.pauseAll().then(refresh)}>Pause all</Button>}
        {anyPaused && <Button $small $primary onClick={() => client.unpauseAll().then(refresh)}>Resume all</Button>}
        <Button $small onClick={() => client.purge().then(refresh)} disabled={!connected}>Clear finished</Button>
        {filter === 'done' && history.length > 0 && (
          <Button $small onClick={() => { chrome.send('falcon_downloader.clearHistory'); setHistory([]) }}>Clear history</Button>
        )}
      </Toolbar>

      {visible.length === 0 && visibleMedia.length === 0 ? (
        <Empty>
          {!connected
            ? 'Starting the download engine…'
            : downloads.length === 0
              ? 'No downloads yet. Paste a link above, or just download something — Falcon takes over automatically.'
              : 'Nothing matches this filter.'}
        </Empty>
      ) : (
        <List>
          {visibleMedia.map((j) => (
            <MediaRow key={`m${j.id}`} job={j} onChange={() => window.setTimeout(loadMedia, 200)} />
          ))}
          {visible.map((d) => (
            <DownloadRow
              key={d.gid} d={d} client={client}
              open={openGid === d.gid}
              onToggle={() => setOpenGid(openGid === d.gid ? null : d.gid)}
              refresh={() => { refresh(); loadHistory() }} setError={setError}
              scan={scans[d.gid]}
              queueIndex={d.status === 'waiting' ? queue.indexOf(d.gid) : undefined}
              queueSize={queue.length}
              speedHistory={speeds.current.get(d.gid)}
            />
          ))}
        </List>
      )}

      <Footer>
        <span>{merged.length + mediaJobs.length} downloads listed</span>
        <span>{fmtBytes(sessionBytes.current)} received this session</span>
        {stats && stats.totalFiles > 0 && (
          <span>{fmtBytes(stats.totalBytes)} · {stats.totalFiles} files all-time</span>
        )}
        <span>Saving to {loadTimeData.getString('downloadDir') || 'default folder'}{loadTimeData.getBoolean('categoriesEnabled') ? ' (sorted by category)' : ''}</span>
      </Footer>
    </Page>
  )
}
