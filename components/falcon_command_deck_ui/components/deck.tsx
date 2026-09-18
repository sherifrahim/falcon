// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

// The command deck (mock v2, screen 5): one input, results grouped by entity
// (tabs · commands · bookmarks · windows · groups), keyboard driven. Items
// come from Brave's CommanderService via the FalconCommandDeckHandler.

import * as React from 'react'
import styled from 'styled-components'
import { addWebUiListener, sendWithPromise } from 'chrome://resources/js/cr.js'

interface Item {
  title: string
  annotation: string
  entity: number // 0 command, 1 bookmark, 2 tab, 3 window, 4 group
  ranges: Array<[number, number]>
  index: number
}

const GROUP: Record<number, string> = { 2: 'Tabs', 0: 'Commands', 1: 'Bookmarks', 3: 'Windows', 4: 'Tab groups' }
const GROUP_ORDER = [2, 0, 1, 3, 4]

const Root = styled.div`
  width: 760px;
  box-sizing: border-box;
  border-radius: 16px;
  background: rgba(11, 18, 32, 0.96);
  border: 1px solid rgba(255, 255, 255, 0.12);
  box-shadow: 0 24px 64px rgba(0, 0, 0, 0.5), 0 0 0 1px rgba(56, 189, 248, 0.08);
  overflow: hidden;
  display: flex;
  flex-direction: column;
  max-height: 560px;
`

const InputRow = styled.div`
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 14px 18px;
  border-bottom: 1px solid rgba(255, 255, 255, 0.08);
  .glyph { width: 16px; height: 16px; border-radius: 50%; border: 1.5px solid rgba(226,232,240,0.7); box-sizing: border-box; position: relative; flex: none; }
  .glyph::after { content: ''; position: absolute; right: -6px; bottom: -5px; width: 7px; height: 1.5px; background: rgba(226,232,240,0.7); transform: rotate(45deg); }
  .prompt { font-size: 12px; letter-spacing: 0.08em; text-transform: uppercase; color: #38bdf8; white-space: nowrap; }
  input {
    flex: 1; min-width: 0; border: none; outline: none; background: transparent; color: #f8fafc;
    font: inherit; font-size: 18px; caret-color: #38bdf8;
  }
  input::placeholder { color: rgba(226, 232, 240, 0.45); }
  .close { border: none; background: transparent; color: rgba(226,232,240,0.6); cursor: pointer; font-size: 16px; }
  .close:hover { color: #fff; }
`

const Body = styled.div`
  overflow: auto;
  padding: 8px 8px 10px;
  display: grid;
  gap: 8px;
`

const Group = styled.div`
  .h { font-size: 10px; letter-spacing: 0.12em; text-transform: uppercase; opacity: 0.5; padding: 8px 12px 4px; }
`

const Row = styled.div<{ $on: boolean }>`
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 9px 12px;
  border-radius: 10px;
  cursor: pointer;
  background: ${(p) => (p.$on ? 'rgba(56, 189, 248, 0.14)' : 'transparent')};
  border: 1px solid ${(p) => (p.$on ? 'rgba(56, 189, 248, 0.35)' : 'transparent')};
  .ico { width: 26px; height: 26px; border-radius: 8px; background: rgba(255,255,255,0.06); display: grid; place-items: center; font-size: 12px; opacity: 0.85; flex: none; }
  .t { flex: 1; min-width: 0; font-size: 13.5px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .t mark { background: transparent; color: #7dd3fc; font-weight: 600; }
  .k { font-family: "Cascadia Mono", Consolas, "JetBrains Mono", monospace; font-size: 11px; opacity: 0.65; white-space: nowrap; }
  kbd { font: inherit; padding: 1px 6px; border-radius: 5px; background: rgba(255,255,255,0.08); border: 1px solid rgba(255,255,255,0.14); }
`

const Foot = styled.div`
  display: flex;
  gap: 18px;
  padding: 8px 18px 10px;
  border-top: 1px solid rgba(255, 255, 255, 0.08);
  font-size: 11px;
  opacity: 0.55;
  kbd { font: inherit; padding: 0 5px; border-radius: 4px; background: rgba(255,255,255,0.08); border: 1px solid rgba(255,255,255,0.14); }
`

const ICON: Record<number, string> = { 0: '⌘', 1: '★', 2: '▢', 3: '⧉', 4: '▤' }

function Highlight({ text, ranges }: { text: string; ranges: Array<[number, number]> }) {
  if (!ranges.length) return <>{text}</>
  const out: React.ReactNode[] = []
  let pos = 0
  for (const [a, b] of ranges) {
    if (a > pos) out.push(text.slice(pos, a))
    out.push(<mark key={a}>{text.slice(a, b)}</mark>)
    pos = b
  }
  if (pos < text.length) out.push(text.slice(pos))
  return <>{out}</>
}

export function Deck() {
  const [query, setQuery] = React.useState('')
  const [prompt, setPrompt] = React.useState('')
  const [items, setItems] = React.useState<Item[]>([])
  const [setId, setSetId] = React.useState(0)
  const [sel, setSel] = React.useState(0)
  const input = React.useRef<HTMLInputElement>(null)

  React.useEffect(() => {
    addWebUiListener('deck-items', (p: { prompt: string; resultSetId: number; items: Omit<Item, 'index'>[] }) => {
      setPrompt(p.prompt || '')
      setSetId(p.resultSetId)
      setItems((p.items || []).map((it, index) => ({ ...it, index })))
      setSel(0)
    })
    // The page is cached between opens (top-chrome WebUI): every time the
    // bubble re-shows the document becomes visible again, so (re)attach to the
    // commander and ask the embedder to show us; on hide, detach.
    const ready = () => {
      setQuery('')
      setPrompt('')
      chrome.send('deck.ready')
      setTimeout(() => input.current?.focus(), 30)
    }
    const onVis = () => {
      if (document.visibilityState === 'visible') ready()
      else chrome.send('deck.hidden')
    }
    document.addEventListener('visibilitychange', onVis)
    ready()
    return () => document.removeEventListener('visibilitychange', onVis)
  }, [])

  // Ordered flat list for keyboard navigation follows the grouped rendering.
  const groups = GROUP_ORDER.map((e) => ({ entity: e, items: items.filter((i) => i.entity === e) })).filter((g) => g.items.length)
  // Arc-style: the deck doubles as the new-tab box. A URL-ish query gets an
  // "Open" row first; anything else gets a "Search" row after the matches.
  const q = query.trim()
  const urlish = !!q && !prompt && !/\s/.test(q) && (/:\/\//.test(q) || /^localhost/.test(q) || /^[^.]+\.[^.]+/.test(q))
  const go: Item | null = q && !prompt ? { title: urlish ? `Open ${q}` : `Search for “${q}”`, annotation: urlish ? 'new tab' : 'web', entity: -1, ranges: [], index: -1 } : null
  const flat = go ? (urlish ? [go, ...groups.flatMap((g) => g.items)] : [...groups.flatMap((g) => g.items), go]) : groups.flatMap((g) => g.items)
  const close = () => chrome.send('deck.close')
  const choose = async (it: Item) => {
    if (it.entity === -1) { chrome.send('deck.navigate', [q]); return }
    try {
      const r: { prompt?: string } = await sendWithPromise('deck.select', it.index, setId)
      if (r && r.prompt) { setQuery(''); setPrompt(r.prompt); input.current?.focus() } else close()
    } catch { close() }
  }
  const onKey = (e: React.KeyboardEvent) => {
    if (e.key === 'ArrowDown') { e.preventDefault(); setSel((s) => Math.min(flat.length - 1, s + 1)) }
    else if (e.key === 'ArrowUp') { e.preventDefault(); setSel((s) => Math.max(0, s - 1)) }
    else if (e.key === 'Enter') { e.preventDefault(); if (flat[sel]) choose(flat[sel]) }
    else if (e.key === 'Escape') { e.preventDefault(); if (prompt) { setPrompt(''); setQuery(''); chrome.send('deck.query', ['']) } else close() }
  }
  const onChange = (v: string) => { setQuery(v); chrome.send('deck.query', [v]) }

  let cursor = 0
  const GoRow = ({ idx }: { idx: number }) => go ? (
    <Row $on={idx === sel} onMouseEnter={() => setSel(idx)} onClick={() => choose(go)}>
      <span className="ico">{urlish ? '↗' : '⌕'}</span>
      <span className="t">{go.title}</span>
      <span className="k">{go.annotation}</span>
    </Row>
  ) : null
  const goFirst = !!go && urlish
  const goLast = !!go && !urlish
  return (
    <Root onKeyDown={onKey}>
      <InputRow>
        <span className="glyph" />
        {prompt && <span className="prompt">{prompt}</span>}
        <input ref={input} value={query} placeholder={prompt ? 'Type to filter…' : 'Search tabs, commands, bookmarks, sessions — or type a URL'} onChange={(e) => onChange(e.target.value)} spellCheck={false} autoComplete="off" />
        <button className="close" onClick={close} title="Close (Esc)">✕</button>
      </InputRow>
      <Body>
        {goFirst && <Group><div className="h">Go</div><GoRow idx={cursor++} /></Group>}
        {groups.map((g) => (
          <Group key={g.entity}>
            <div className="h">{GROUP[g.entity]} ({g.items.length})</div>
            {g.items.map((it) => {
              const idx = cursor++
              return (
                <Row key={`${g.entity}-${it.index}`} $on={idx === sel} onMouseEnter={() => setSel(idx)} onClick={() => choose(it)}>
                  <span className="ico">{ICON[g.entity] ?? '·'}</span>
                  <span className="t"><Highlight text={it.title} ranges={it.ranges} /></span>
                  {it.annotation && <span className="k">{it.annotation}</span>}
                </Row>
              )
            })}
          </Group>
        ))}
        {goLast && <Group><div className="h">Web</div><GoRow idx={cursor++} /></Group>}
        {flat.length === 0 && <div style={{ padding: '18px 12px', fontSize: 13, opacity: 0.55 }}>{query ? 'Nothing matches.' : 'Type to search tabs, commands, bookmarks and sessions — or a URL.'}</div>}
      </Body>
      <Foot><span><kbd>↑</kbd> <kbd>↓</kbd> to navigate</span><span><kbd>Enter</kbd> to select</span><span><kbd>Esc</kbd> to close</span></Foot>
    </Root>
  )
}
