// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

// Media (yt-dlp) side of the downloader: the quality picker for a page /
// stream URL and the rows for running media jobs.

import * as React from 'react'
import styled from 'styled-components'
import { sendWithPromise } from 'chrome://resources/js/cr.js'
import { Button, Card, Chip, ErrorText, Input, Meta, fmtBytes } from './common'

export interface MediaFormat {
  format_id: string
  ext?: string
  resolution?: string
  format_note?: string
  vcodec?: string
  acodec?: string
  protocol?: string
  filesize?: number
  filesize_approx?: number
  tbr?: number
  fps?: number
  height?: number
  width?: number
  abr?: number
}

export interface MediaProbe {
  title?: string
  thumbnail?: string
  duration?: number
  uploader?: string
  extractor?: string
  formats?: MediaFormat[]
  error?: string
}

export interface MediaJob {
  id: number
  url: string
  referer: string
  title: string
  selector: string
  status: 'starting' | 'downloading' | 'merging' | 'done' | 'error' | 'cancelled'
  percent: number
  speed: string
  eta: string
  downloaded: number
  total: number
  path: string
  error: string
  started: number
}

const PRESETS: Array<[string, string, string]> = [
  ['best', 'Best quality', 'video + audio, mp4'],
  ['1080', '1080p', 'or the best below it'],
  ['720', '720p', ''],
  ['480', '480p', ''],
  ['360', '360p', 'small'],
  ['audio', 'Audio only', 'm4a'],
]

const Panel = styled(Card)`
  margin-bottom: 12px;
  display: grid;
  gap: 10px;
`

const Head = styled.div`
  display: flex;
  gap: 14px;
  align-items: center;
  img { width: 128px; height: 72px; object-fit: cover; border-radius: 8px; background: #0f172a; }
  .t { font-weight: 600; font-size: 14px; }
`

const Presets = styled.div`
  display: flex;
  gap: 6px;
  flex-wrap: wrap;
  align-items: center;
`

const Table = styled.div`
  max-height: 260px;
  overflow: auto;
  border: 1px solid var(--leo-color-divider-subtle, #334155);
  border-radius: 10px;
  font-size: 12px;
  table { width: 100%; border-collapse: collapse; }
  th, td { text-align: left; padding: 5px 10px; white-space: nowrap; }
  th { position: sticky; top: 0; background: var(--leo-color-container-background, #1e293b); opacity: 0.8; font-weight: 600; }
  tr:hover td { background: rgba(56,189,248,0.08); }
`

export function fmtDuration(s?: number): string {
  if (!s || !isFinite(s)) return ''
  s = Math.round(s)
  const h = Math.floor(s / 3600)
  const m = Math.floor((s % 3600) / 60)
  const sec = s % 60
  return h > 0
    ? `${h}:${String(m).padStart(2, '0')}:${String(sec).padStart(2, '0')}`
    : `${m}:${String(sec).padStart(2, '0')}`
}

function describe(f: MediaFormat): string {
  const v = f.vcodec && f.vcodec !== 'none'
  const a = f.acodec && f.acodec !== 'none'
  if (v && a) return 'video + audio'
  if (v) return 'video only'
  if (a) return 'audio only'
  return ''
}

export function MediaPicker(props: {
  initialUrl: string
  referer: string
  onClose: () => void
  onStarted: () => void
}) {
  const [url, setUrl] = React.useState(props.initialUrl)
  const [probe, setProbe] = React.useState<MediaProbe | null>(null)
  const [busy, setBusy] = React.useState(false)
  const [showAll, setShowAll] = React.useState(false)
  const [playlist, setPlaylist] = React.useState(false)
  const [subs, setSubs] = React.useState(false)
  const [audioFormat, setAudioFormat] = React.useState('m4a')

  const run = React.useCallback(async (u: string) => {
    const target = u.trim()
    if (!/^https?:/i.test(target)) return
    setBusy(true)
    setProbe(null)
    try {
      const r: MediaProbe = await sendWithPromise('falcon_downloader.probeMedia', target, props.referer)
      setProbe(r)
    } catch (e: any) {
      setProbe({ error: e?.message ?? 'probe failed' })
    } finally {
      setBusy(false)
    }
  }, [props.referer])

  React.useEffect(() => {
    if (props.initialUrl) run(props.initialUrl)
  }, [props.initialUrl, run])

  const start = (selector: string) => {
    chrome.send('falcon_downloader.startMedia', [url.trim(), props.referer, selector,
      { playlist, subtitles: subs ? 'en,en.*' : '', audioFormat }])
    props.onStarted()
    props.onClose()
  }

  const formats = (probe?.formats ?? []).slice().sort((a, b) => (b.height ?? 0) - (a.height ?? 0) || (b.tbr ?? 0) - (a.tbr ?? 0))
  const combined = formats.filter((f) => f.vcodec && f.vcodec !== 'none' && f.acodec && f.acodec !== 'none')
  const listed = showAll ? formats : (combined.length ? combined : formats).slice(0, 12)

  return (
    <Panel>
      <div style={{ display: 'flex', gap: 8 }}>
        <Input style={{ flex: 1 }} value={url} placeholder="Page or stream URL (YouTube, Vimeo, m3u8, mpd…)"
          onChange={(e) => setUrl(e.target.value)} onKeyDown={(e) => e.key === 'Enter' && run(url)} spellCheck={false} />
        <Button onClick={() => run(url)} disabled={busy || !url.trim()}>{busy ? 'Reading…' : 'Read formats'}</Button>
        <Button onClick={props.onClose}>Close</Button>
      </div>
      <Presets>
        <span style={{ fontSize: 12, opacity: 0.7 }}>Quick:</span>
        {PRESETS.map(([sel, label, sub]) => (
          <Chip key={sel} $active={false} onClick={() => start(sel)} title={sub} disabled={!url.trim()}>{label}</Chip>
        ))}
      </Presets>
      <Meta style={{ gap: 18 }}>
        <label style={{ display: 'flex', gap: 6, alignItems: 'center', cursor: 'pointer' }}>
          <input type="checkbox" checked={playlist} onChange={(e) => setPlaylist(e.target.checked)} /> Whole playlist (into a folder)
        </label>
        <label style={{ display: 'flex', gap: 6, alignItems: 'center', cursor: 'pointer' }}>
          <input type="checkbox" checked={subs} onChange={(e) => setSubs(e.target.checked)} /> Embed English subtitles
        </label>
        <label style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
          Audio-only format
          <select value={audioFormat} onChange={(e) => setAudioFormat(e.target.value)} style={{ background: 'transparent', color: 'inherit', border: '1px solid #334155', borderRadius: 8, padding: '2px 6px' }}>
            <option value="m4a">m4a</option><option value="mp3">mp3</option><option value="opus">opus</option>
          </select>
        </label>
      </Meta>
      {busy && <Meta>Asking yt-dlp about this page… (a few seconds)</Meta>}
      {probe?.error && <Meta><ErrorText>{probe.error}</ErrorText></Meta>}
      {probe && !probe.error && (
        <>
          <Head>
            {probe.thumbnail && <img src={probe.thumbnail} alt="" />}
            <div>
              <div className="t">{probe.title || url}</div>
              <Meta>
                {probe.uploader && <span>{probe.uploader}</span>}
                {probe.duration ? <span>{fmtDuration(probe.duration)}</span> : null}
                {probe.extractor && <span>{probe.extractor}</span>}
                <span>{formats.length} formats</span>
              </Meta>
            </div>
          </Head>
          {formats.length > 0 && (
            <Table>
              <table>
                <thead><tr><th>Format</th><th>Resolution</th><th>Type</th><th>Codecs</th><th>Bitrate</th><th>Size</th><th></th></tr></thead>
                <tbody>
                  {listed.map((f) => (
                    <tr key={f.format_id}>
                      <td>{f.format_id} · {f.ext}</td>
                      <td>{f.resolution || (f.height ? `${f.height}p` : '—')}{f.fps ? ` ${Math.round(f.fps)}fps` : ''}</td>
                      <td>{describe(f)}{f.format_note ? ` · ${f.format_note}` : ''}</td>
                      <td style={{ opacity: 0.7 }}>{[f.vcodec, f.acodec].filter((c) => c && c !== 'none').map((c) => c!.split('.')[0]).join(' + ')}</td>
                      <td>{f.tbr ? `${Math.round(f.tbr)} kbps` : '—'}</td>
                      <td>{f.filesize || f.filesize_approx ? `${fmtBytes(f.filesize || f.filesize_approx || 0)}${!f.filesize ? ' ~' : ''}` : '—'}</td>
                      <td>
                        <Button $small $primary onClick={() => start(describe(f) === 'video only' ? `${f.format_id}+ba/b` : f.format_id)}>Get</Button>
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </Table>
          )}
          {formats.length > listed.length && (
            <Button $small onClick={() => setShowAll(true)}>Show all {formats.length} formats</Button>
          )}
        </>
      )}
    </Panel>
  )
}

const Row = styled(Card)`
  display: grid;
  grid-template-columns: 1fr auto;
  gap: 8px 14px;
`

const Bar = styled.div<{ $pct: number; $status: MediaJob['status'] }>`
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
    background: ${(p) => (p.$status === 'error' || p.$status === 'cancelled' ? '#ef4444' : p.$status === 'done' ? '#22c55e' : p.$status === 'merging' ? '#a78bfa' : '#38bdf8')};
    transition: width 0.4s;
  }
`

const STATUS: Record<MediaJob['status'], string> = {
  starting: 'Starting yt-dlp',
  downloading: 'Downloading',
  merging: 'Merging with ffmpeg',
  done: 'Done',
  error: 'Failed',
  cancelled: 'Cancelled',
}

export function MediaRow({ job, onChange }: { job: MediaJob; onChange: () => void }) {
  const live = job.status === 'starting' || job.status === 'downloading' || job.status === 'merging'
  const send = (msg: string) => { chrome.send(`falcon_downloader.${msg}`, [job.id]); onChange() }
  return (
    <Row>
      <div style={{ minWidth: 0 }}>
        <div style={{ fontSize: 14, fontWeight: 600, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }} title={job.path || job.url}>
          {job.title || job.url}
        </div>
        <Meta>
          <span>{STATUS[job.status]} · media</span>
          {job.status === 'downloading' && (
            <>
              <span>{job.percent.toFixed(0)}%{job.total > 0 ? ` of ${fmtBytes(job.total)}` : ''}</span>
              {job.speed && <span>↓ {job.speed}</span>}
              {job.eta && <span>ETA {job.eta}</span>}
            </>
          )}
          {job.status === 'done' && job.total > 0 && <span>{fmtBytes(job.total)}</span>}
          {job.error && <ErrorText>{job.error}</ErrorText>}
          <span style={{ opacity: 0.6 }}>{job.selector === 'best' ? 'best quality' : job.selector}</span>
        </Meta>
      </div>
      <div style={{ display: 'flex', gap: 6, alignItems: 'start', flexWrap: 'wrap', justifyContent: 'flex-end' }}>
        {job.status === 'done' && job.path && (
          <>
            <Button $small onClick={() => chrome.send('falcon_downloader.openFile', [job.path])}>Open</Button>
            <Button $small onClick={() => chrome.send('falcon_downloader.openFolder', [job.path])}>Folder</Button>
          </>
        )}
        {(job.status === 'error' || job.status === 'cancelled') && (
          <Button $small $primary onClick={() => { chrome.send('falcon_downloader.startMedia', [job.url, job.referer, job.selector]); send('removeMedia') }}>Retry</Button>
        )}
        {live ? (
          <Button $small $danger onClick={() => send('cancelMedia')}>Cancel</Button>
        ) : (
          <Button $small onClick={() => send('removeMedia')}>Remove</Button>
        )}
      </div>
      <Bar $pct={job.status === 'done' ? 100 : job.percent} $status={job.status} />
    </Row>
  )
}

// ------------------------------------------------------- sniffed media list

export interface SniffedCandidate {
  kind: 'file' | 'playlist' | 'page'
  url: string
  name: string
  mime: string
  size: number
}

export interface SniffedTab {
  tab: string
  url: string
  candidates: SniffedCandidate[]
}

const SniffPanel = styled(Card)`
  margin-bottom: 12px;
  display: grid;
  gap: 6px;
  .tab { font-size: 12px; opacity: 0.65; margin-top: 4px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .row { display: flex; align-items: center; gap: 10px; font-size: 13px; }
  .row .n { flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .row .k { opacity: 0.6; font-size: 12px; white-space: nowrap; }
`

export function SniffedList(props: {
  tabs: SniffedTab[]
  onFile: (url: string, referer: string) => void
  onStream: (url: string, referer: string) => void
  onQuality: (url: string, referer: string) => void
}) {
  const total = props.tabs.reduce((n, t) => n + t.candidates.length, 0)
  if (total === 0) return null
  return (
    <SniffPanel>
      <div style={{ fontSize: 13, fontWeight: 600 }}>Media spotted on open tabs · {total}</div>
      {props.tabs.map((t) => (
        <React.Fragment key={t.url}>
          <div className="tab" title={t.url}>{t.tab || t.url}</div>
          {t.candidates.map((c) => (
            <div className="row" key={c.url} title={c.url}>
              <span className="n">{c.name || c.url}</span>
              <span className="k">{c.kind === 'file' ? `${c.mime || 'file'}${c.size ? ` · ${fmtBytes(c.size)}` : ''}` : c.kind === 'playlist' ? 'stream · yt-dlp' : 'page player · yt-dlp'}</span>
              <Button $small $primary onClick={() => (c.kind === 'file' ? props.onFile(c.url, t.url) : props.onStream(c.url, t.url))}>Download</Button>
              {c.kind !== 'file' && <Button $small onClick={() => props.onQuality(c.url, t.url)}>Quality…</Button>}
            </div>
          ))}
        </React.Fragment>
      ))}
    </SniffPanel>
  )
}
