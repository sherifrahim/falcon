// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

// falcon://collections and the side panel: boards of pages, links, images
// and text snippets. One bundle; `panel` (loadTimeData) picks the narrow
// layout.

import * as React from 'react'
import styled from 'styled-components'
import { dur, ease } from '$web-common/falcon_motion'
import { loadTimeData } from '$web-common/loadTimeData'
import { sendWithPromise, addWebUiListener } from 'chrome://resources/js/cr.js'

type Kind = 'page' | 'link' | 'image' | 'text'

interface Item {
  id: string
  kind: Kind
  title: string
  url: string
  text: string
  added: number
}

interface Collection {
  id: string
  name: string
  created: number
  items: Item[]
}

interface State {
  collections: Collection[]
  last: string
}

const isPanel = (() => { try { return loadTimeData.getBoolean('panel') } catch { return false } })()

function favicon(url: string) {
  return `chrome://favicon2/?size=32&scaleFactor=2x&showFallbackMonogram=&pageUrl=${encodeURIComponent(url)}`
}

function host(url: string) {
  try { return new URL(url).host.replace(/^www\./, '') } catch { return '' }
}

function when(ms: number) {
  const d = new Date(ms)
  const days = (Date.now() - ms) / 86400000
  if (days < 1) return 'today'
  if (days < 2) return 'yesterday'
  if (days < 7) return `${Math.floor(days)} d ago`
  return d.toLocaleDateString(undefined, { month: 'short', day: 'numeric' })
}

const Shell = styled.div<{ $panel: boolean }>`
  display: grid;
  grid-template-columns: ${(p) => (p.$panel ? 'minmax(0, 1fr)' : '240px minmax(0, 1fr)')};
  grid-template-rows: ${(p) => (p.$panel ? 'auto minmax(0, 1fr)' : 'minmax(0, 1fr)')};
  height: 100%;
  min-height: 0;
  color: var(--leo-color-text-primary, #e2e8f0);
`

const Rail = styled.nav<{ $panel: boolean }>`
  display: flex;
  flex-direction: ${(p) => (p.$panel ? 'row' : 'column')};
  gap: 4px;
  padding: ${(p) => (p.$panel ? '10px 12px 6px' : '18px 12px')};
  overflow: ${(p) => (p.$panel ? 'auto hidden' : 'hidden auto')};
  border-right: ${(p) => (p.$panel ? 'none' : '1px solid var(--f-border, #334155)')};
  border-bottom: ${(p) => (p.$panel ? '1px solid var(--f-border, #334155)' : 'none')};
  min-width: 0;
  scrollbar-width: thin;
`

const RailTitle = styled.div`
  font-size: 11px; letter-spacing: 0.08em; text-transform: uppercase; opacity: 0.55;
  padding: 0 10px 8px;
`

const RailItem = styled.button<{ $active: boolean; $panel: boolean }>`
  display: flex; align-items: center; gap: 8px;
  padding: ${(p) => (p.$panel ? '6px 10px' : '8px 10px')};
  border-radius: 10px; border: 1px solid transparent;
  background: ${(p) => (p.$active ? 'rgba(56, 189, 248, 0.14)' : 'transparent')};
  border-color: ${(p) => (p.$active ? 'rgba(56, 189, 248, 0.35)' : 'transparent')};
  color: inherit; font: inherit; font-size: 13px; text-align: left; cursor: pointer;
  white-space: nowrap; flex: ${(p) => (p.$panel ? '0 0 auto' : '0 0 auto')};
  min-width: 0;
  &:hover { background: ${(p) => (p.$active ? 'rgba(56, 189, 248, 0.18)' : 'var(--f-bg-3, #1e293b)')}; }
  span.n { opacity: 0.5; font-variant-numeric: tabular-nums; font-size: 12px; }
  span.name { overflow: hidden; text-overflow: ellipsis; }
`

const Main = styled.section`
  display: flex; flex-direction: column; min-height: 0; min-width: 0;
`

const Head = styled.header<{ $panel: boolean }>`
  display: flex; align-items: center; gap: 8px; flex-wrap: wrap;
  padding: ${(p) => (p.$panel ? '10px 12px' : '18px 24px 12px')};
  h1 {
    margin: 0; font-size: ${(p) => (p.$panel ? '15px' : '22px')}; font-weight: 600;
    flex: 1 1 auto; min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
  }
  h1 input {
    font: inherit; color: inherit; background: var(--f-bg-1, #0f172a);
    border: 1px solid var(--f-border, #334155); border-radius: 8px; padding: 2px 8px; width: 100%;
  }
`

const Btn = styled.button<{ $primary?: boolean; $danger?: boolean }>`
  height: 30px; padding: 0 12px; border-radius: 9px; border: 1px solid var(--f-border, #334155);
  background: ${(p) => (p.$primary ? '#0ea5e9' : 'var(--f-bg-1, #0f172a)')};
  color: ${(p) => (p.$primary ? '#03151f' : p.$danger ? '#fca5a5' : 'inherit')};
  font: inherit; font-size: 12.5px; cursor: pointer; white-space: nowrap;
  &:hover { filter: brightness(1.12); }
  &:disabled { opacity: 0.45; cursor: default; filter: none; }
`

const Grid = styled.div<{ $panel: boolean }>`
  display: grid;
  grid-template-columns: ${(p) => (p.$panel ? '1fr' : 'repeat(auto-fill, minmax(230px, 1fr))')};
  gap: ${(p) => (p.$panel ? '8px' : '12px')};
  padding: ${(p) => (p.$panel ? '8px 12px 16px' : '6px 24px 24px')};
  overflow: hidden auto; min-height: 0;
  scrollbar-width: thin;
`

const Card = styled.article<{ $over: boolean; $drag: boolean }>`
  position: relative;
  border-radius: 12px; border: 1px solid var(--f-border, #334155);
  background: var(--leo-color-container-background, var(--f-bg-3, #1e293b));
  overflow: hidden; cursor: pointer;
  opacity: ${(p) => (p.$drag ? 0.4 : 1)};
  outline: ${(p) => (p.$over ? '2px solid rgba(56, 189, 248, 0.7)' : 'none')};
  outline-offset: -2px;
  transition: transform ${dur.fast} ${ease.move}, border-color ${dur.fast} ${ease.move};
  &:hover { border-color: rgba(56, 189, 248, 0.45); transform: translateY(-1px); }
  &:hover .x { opacity: 1; }
  img.hero { display: block; width: 100%; height: 140px; object-fit: cover; background: var(--f-bg-1, #0f172a); }
  .body { padding: 10px 12px 11px; display: flex; flex-direction: column; gap: 5px; min-width: 0; }
  .row { display: flex; align-items: center; gap: 8px; min-width: 0; }
  .row img { width: 16px; height: 16px; border-radius: 4px; flex: none; }
  .title { font-size: 13px; font-weight: 550; line-height: 1.3; overflow: hidden; display: -webkit-box; -webkit-line-clamp: 2; -webkit-box-orient: vertical; }
  .meta { font-size: 11.5px; opacity: 0.55; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; font-variant-numeric: tabular-nums; }
  .quote {
    font-size: 13px; line-height: 1.45; opacity: 0.9; white-space: pre-wrap; overflow: hidden;
    display: -webkit-box; -webkit-line-clamp: 6; -webkit-box-orient: vertical;
    border-left: 3px solid rgba(56, 189, 248, 0.6); padding-left: 9px; margin: 2px 0;
  }
  .x {
    position: absolute; top: 6px; right: 6px; width: 24px; height: 24px; border-radius: 8px;
    border: none; background: rgba(2, 6, 23, 0.7); color: #e2e8f0; cursor: pointer; opacity: 0;
    font-size: 14px; line-height: 24px; text-align: center;
  }
  .x:hover { background: #ef4444; }
`

const Empty = styled.div`
  padding: 40px 24px; opacity: 0.6; font-size: 13.5px; line-height: 1.6; max-width: 46ch;
  b { opacity: 1; }
`

const Note = styled.form`
  display: flex; gap: 8px; padding: 0 24px 10px; align-items: flex-start;
  textarea {
    flex: 1; min-height: 34px; max-height: 120px; resize: vertical; padding: 8px 10px; border-radius: 9px;
    border: 1px solid var(--f-border, #334155); background: var(--f-bg-1, #0f172a); color: inherit; font: inherit; font-size: 13px;
  }
`

function kindLabel(k: Kind) {
  return k === 'page' ? 'Page' : k === 'link' ? 'Link' : k === 'image' ? 'Image' : 'Note'
}

function ItemCard(props: {
  item: Item
  over: boolean
  dragging: boolean
  onOpen: (bg: boolean) => void
  onRemove: () => void
  onDragStart: () => void
  onDragOver: () => void
  onDrop: () => void
  onDragEnd: () => void
}) {
  const { item } = props
  const openable = item.kind !== 'text' && !!item.url
  const sourceHost = host(item.url)
  return (
    <Card
      $over={props.over}
      $drag={props.dragging}
      draggable
      title={openable ? item.url : undefined}
      onDragStart={(e) => { e.dataTransfer.effectAllowed = 'move'; props.onDragStart() }}
      onDragOver={(e) => { e.preventDefault(); props.onDragOver() }}
      onDrop={(e) => { e.preventDefault(); props.onDrop() }}
      onDragEnd={props.onDragEnd}
      onClick={(e) => { if (openable) props.onOpen(e.ctrlKey || e.button === 1) }}
      onAuxClick={(e) => { if (openable && e.button === 1) { e.preventDefault(); props.onOpen(true) } }}
    >
      {item.kind === 'image' && <img className='hero' src={item.url} alt='' loading='lazy' />}
      <div className='body'>
        {item.kind === 'text' && <div className='quote'>{item.text}</div>}
        {item.kind !== 'text' && (
          <div className='row'>
            {sourceHost && <img src={favicon(item.url)} alt='' />}
            <div className='title'>{item.title || item.url}</div>
          </div>
        )}
        <div className='meta'>
          {kindLabel(item.kind)}{sourceHost ? ` · ${sourceHost}` : ''} · {when(item.added)}
        </div>
      </div>
      <button className='x' title='Remove' onClick={(e) => { e.stopPropagation(); props.onRemove() }}>×</button>
    </Card>
  )
}

export function App() {
  const [state, setState] = React.useState<State>({ collections: [], last: '' })
  const [active, setActive] = React.useState<string>('')
  const [editing, setEditing] = React.useState(false)
  const [nameDraft, setNameDraft] = React.useState('')
  const [drag, setDrag] = React.useState<{ id: string; over: string | null } | null>(null)
  const [note, setNote] = React.useState('')
  const [copied, setCopied] = React.useState(false)

  React.useEffect(() => {
    const apply = (s: State) => {
      setState(s)
      setActive((cur) => (s.collections.some((c) => c.id === cur) ? cur : (s.last && s.collections.some((c) => c.id === s.last) ? s.last : (s.collections[0]?.id ?? ''))))
    }
    sendWithPromise('collections.get').then(apply).catch(() => {})
    addWebUiListener('collections-changed', apply)
  }, [])

  const current = state.collections.find((c) => c.id === active)

  const select = (id: string) => {
    setActive(id)
    setEditing(false)
    chrome.send('collections.setLast', [id])
  }

  const create = async () => {
    const id: string = await sendWithPromise('collections.create', 'New collection')
    setActive(id)
    setNameDraft('New collection')
    setEditing(true)
  }

  const commitName = () => {
    if (current && nameDraft.trim() && nameDraft.trim() !== current.name) {
      chrome.send('collections.rename', [current.id, nameDraft.trim()])
    }
    setEditing(false)
  }

  const remove = () => {
    if (!current) return
    if (current.items.length > 0 && !window.confirm(`Delete "${current.name}" and its ${current.items.length} items?`)) return
    chrome.send('collections.delete', [current.id])
  }

  const copyMarkdown = () => {
    if (!current) return
    chrome.send('collections.copyMarkdown', [current.id])
    setCopied(true)
    window.setTimeout(() => setCopied(false), 1500)
  }

  const addNote = (e: React.FormEvent) => {
    e.preventDefault()
    if (!current || !note.trim()) return
    chrome.send('collections.addItem', [current.id, 'text', '', '', note.trim()])
    setNote('')
  }

  const dropOn = (targetId: string | null) => {
    if (!current || !drag) return
    const ids = current.items.map((i) => i.id)
    const from = ids.indexOf(drag.id)
    let to = targetId ? ids.indexOf(targetId) : ids.length
    if (from < 0 || to < 0) { setDrag(null); return }
    if (from < to) to -= 1
    if (from !== to) chrome.send('collections.moveItem', [current.id, drag.id, current.id, to])
    setDrag(null)
  }

  const openableCount = current ? current.items.filter((i) => i.kind !== 'text' && i.url).length : 0

  return (
    <Shell $panel={isPanel}>
      <Rail $panel={isPanel}>
        {!isPanel && <RailTitle>Collections</RailTitle>}
        {state.collections.map((c) => (
          <RailItem key={c.id} $active={c.id === active} $panel={isPanel} onClick={() => select(c.id)}>
            <span className='name'>{c.name}</span>
            <span className='n'>{c.items.length}</span>
          </RailItem>
        ))}
        <RailItem $active={false} $panel={isPanel} onClick={create} style={{ opacity: 0.75 }}>+ New</RailItem>
      </Rail>
      <Main>
        {current ? (
          <>
            <Head $panel={isPanel}>
              <h1 onDoubleClick={() => { setNameDraft(current.name); setEditing(true) }} title='Double-click to rename'>
                {editing ? (
                  <input
                    autoFocus
                    value={nameDraft}
                    onChange={(e) => setNameDraft(e.target.value)}
                    onBlur={commitName}
                    onKeyDown={(e) => { if (e.key === 'Enter') commitName(); if (e.key === 'Escape') setEditing(false) }}
                  />
                ) : current.name}
              </h1>
              {isPanel && <Btn $primary onClick={() => chrome.send('collections.addCurrentPage', [current.id])}>+ Add this page</Btn>}
              <Btn disabled={openableCount === 0} onClick={() => chrome.send('collections.openAll', [current.id])}>Open all ({openableCount})</Btn>
              <Btn disabled={current.items.length === 0} onClick={copyMarkdown}>{copied ? 'Copied' : 'Copy as Markdown'}</Btn>
              {!isPanel && <Btn onClick={() => { setNameDraft(current.name); setEditing(true) }}>Rename</Btn>}
              <Btn $danger onClick={remove}>Delete</Btn>
            </Head>
            <Note onSubmit={addNote} style={isPanel ? { padding: '0 12px 8px' } : undefined}>
              <textarea
                placeholder='Add a note…'
                value={note}
                onChange={(e) => setNote(e.target.value)}
                onKeyDown={(e) => { if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); addNote(e) } }}
              />
              <Btn type='submit' disabled={!note.trim()}>Add</Btn>
            </Note>
            {current.items.length === 0 ? (
              <Empty>
                <b>Nothing here yet.</b><br />
                Right-click a page, link, image or selected text and choose <b>Add to Collection</b>{isPanel ? <>, or press <b>Add this page</b></> : <>, or open this panel from the sidebar to add the page you are on</>}.
              </Empty>
            ) : (
              <Grid
                $panel={isPanel}
                onDragOver={(e) => { e.preventDefault(); if (drag && drag.over !== null) setDrag({ ...drag, over: null }) }}
                onDrop={(e) => { e.preventDefault(); dropOn(null) }}
              >
                {current.items.map((item) => (
                  <ItemCard
                    key={item.id}
                    item={item}
                    over={!!drag && drag.over === item.id && drag.id !== item.id}
                    dragging={!!drag && drag.id === item.id}
                    onOpen={(bg) => chrome.send('collections.open', [item.url, bg])}
                    onRemove={() => chrome.send('collections.removeItem', [current.id, item.id])}
                    onDragStart={() => setDrag({ id: item.id, over: null })}
                    onDragOver={() => { if (drag && drag.over !== item.id) setDrag({ ...drag, over: item.id }) }}
                    onDrop={() => dropOn(item.id)}
                    onDragEnd={() => setDrag(null)}
                  />
                ))}
              </Grid>
            )}
          </>
        ) : (
          <Empty>
            <b>Collections</b> keep pages, links, images and snippets together — research, shopping, a trip.<br />
            <Btn $primary style={{ marginTop: 14 }} onClick={create}>Create your first collection</Btn>
          </Empty>
        )}
      </Main>
    </Shell>
  )
}
