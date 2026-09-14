// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

// IDM-style "site grabber": fetch a page, list every file it links to,
// filter by type, download the selection through the engine.

import * as React from 'react'
import styled from 'styled-components'
import { sendWithPromise } from 'chrome://resources/js/cr.js'
import { Button, Card, Chip, ErrorText, Input, Meta } from './common'

export interface GrabbedLink {
  url: string
  name: string
  category: string // Video, Music, Images, Documents, Compressed, Programs, Apps, Torrents, Other
}

const Panel = styled(Card)`
  margin-bottom: 12px;
  display: grid;
  gap: 10px;
`

const List = styled.div`
  max-height: 300px;
  overflow: auto;
  border: 1px solid var(--leo-color-divider-subtle, #334155);
  border-radius: 10px;
  padding: 4px 8px;
  label { display: flex; gap: 8px; align-items: center; padding: 4px 0; font-size: 12px; }
  label span.n { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; flex: 1; }
  label span.c { opacity: 0.6; white-space: nowrap; }
`

const CATS = ['All', 'Video', 'Music', 'Images', 'Documents', 'Compressed', 'Programs', 'Apps', 'Torrents', 'Other']

export function Grabber(props: { initialUrl: string; onClose: () => void; onQueue: (urls: string[], referer: string) => void }) {
  const [url, setUrl] = React.useState(props.initialUrl)
  const [busy, setBusy] = React.useState(false)
  const [error, setError] = React.useState<string | null>(null)
  const [links, setLinks] = React.useState<GrabbedLink[]>([])
  const [cat, setCat] = React.useState('All')
  const [pattern, setPattern] = React.useState('')
  const [selected, setSelected] = React.useState<Set<string>>(new Set())

  const grab = async () => {
    const target = url.trim()
    if (!/^https?:/i.test(target)) { setError('Enter a page URL (https://…)'); return }
    setBusy(true); setError(null); setLinks([]); setSelected(new Set())
    try {
      const r: { links?: GrabbedLink[]; error?: string } = await sendWithPromise('falcon_downloader.grabPage', target)
      if (r.error) setError(r.error)
      setLinks(r.links ?? [])
    } catch (e: any) {
      setError(e?.message ?? 'Could not read the page')
    } finally {
      setBusy(false)
    }
  }

  let re: RegExp | null = null
  try { re = pattern.trim() ? new RegExp(pattern.trim(), 'i') : null } catch { re = null }
  const shown = links.filter((l) => (cat === 'All' || l.category === cat) && (!re || re.test(l.url) || re.test(l.name)))
  const counts: Record<string, number> = {}
  for (const l of links) counts[l.category] = (counts[l.category] ?? 0) + 1

  const toggle = (u: string, on: boolean) => setSelected((s) => { const n = new Set(s); on ? n.add(u) : n.delete(u); return n })
  const selectShown = (on: boolean) => setSelected((s) => { const n = new Set(s); for (const l of shown) on ? n.add(l.url) : n.delete(l.url); return n })

  return (
    <Panel>
      <div style={{ display: 'flex', gap: 8 }}>
        <Input style={{ flex: 1 }} value={url} placeholder="Page to scan for files, e.g. https://example.com/downloads/"
          onChange={(e) => setUrl(e.target.value)} onKeyDown={(e) => e.key === 'Enter' && grab()} spellCheck={false} />
        <Button $primary onClick={grab} disabled={busy}>{busy ? 'Scanning…' : 'Scan page'}</Button>
        <Button onClick={props.onClose}>Close</Button>
      </div>
      {error && <Meta><ErrorText>{error}</ErrorText></Meta>}
      {links.length > 0 && (
        <>
          <div style={{ display: 'flex', gap: 6, flexWrap: 'wrap', alignItems: 'center' }}>
            {CATS.filter((c) => c === 'All' || counts[c]).map((c) => (
              <Chip key={c} $active={cat === c} onClick={() => setCat(c)}>{c}{c === 'All' ? ` ${links.length}` : ` ${counts[c]}`}</Chip>
            ))}
            <Input style={{ padding: '5px 10px', width: 170 }} placeholder="filter (regex)" value={pattern} onChange={(e) => setPattern(e.target.value)} />
            <span style={{ flex: 1 }} />
            <Button $small onClick={() => selectShown(true)}>Select shown</Button>
            <Button $small onClick={() => selectShown(false)}>Clear</Button>
          </div>
          <List>
            {shown.map((l) => (
              <label key={l.url} title={l.url}>
                <input type="checkbox" checked={selected.has(l.url)} onChange={(e) => toggle(l.url, e.target.checked)} />
                <span className="n">{l.name || l.url}</span>
                <span className="c">{l.category}</span>
              </label>
            ))}
            {shown.length === 0 && <Meta style={{ padding: 8 }}>Nothing in this category.</Meta>}
          </List>
          <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
            <Button $primary disabled={selected.size === 0} onClick={() => { props.onQueue(Array.from(selected), url.trim()); props.onClose() }}>
              Download {selected.size || ''} selected
            </Button>
            <Meta>{links.length} files found on the page</Meta>
          </div>
        </>
      )}
    </Panel>
  )
}
