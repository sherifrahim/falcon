// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

// Command chains (Vivaldi-style macros): sequences of steps that run from
// Quick commands (Ctrl+Space › "Run chain: …") or the app menu.

import * as React from 'react'
import styled from 'styled-components'
import { sendWithPromise } from 'chrome://resources/js/cr.js'

export type StepType = 'command' | 'url' | 'goto' | 'deck' | 'wait'

export interface Step {
  type: StepType
  value: string
}

export interface Chain {
  id: string
  name: string
  steps: Step[]
}

interface Known {
  key: string
  title: string
}

const STEP_TYPES: Array<[StepType, string, string]> = [
  ['command', 'Browser command', 'Pick one of the built-in commands'],
  ['url', 'Open URL in a new tab', 'https://…'],
  ['goto', 'Navigate current tab', 'https://…'],
  ['deck', 'Quick command', 'Runs the top Quick-commands result for this text, e.g. "Restore session Work"'],
  ['wait', 'Wait', 'milliseconds (≤ 60000)'],
]

const List = styled.div`
  display: grid; gap: 10px; padding: 10px 0 14px;
`

const ChainBox = styled.div<{ $open: boolean }>`
  border: 1px solid var(--f-border, #334155); border-radius: 12px; overflow: hidden;
  background: var(--f-bg-2, #0f172a);
  .head { display: flex; align-items: center; gap: 10px; padding: 10px 12px; cursor: pointer; }
  .head b { flex: 1; font-size: 13.5px; }
  .head .n { font-size: 12px; opacity: 0.6; font-variant-numeric: tabular-nums; }
  .body { display: ${(p) => (p.$open ? 'grid' : 'none')}; gap: 8px; padding: 0 12px 12px; border-top: 1px solid var(--f-border, #334155); padding-top: 10px; }
  .step { display: grid; grid-template-columns: 24px 190px minmax(0, 1fr) auto auto auto; gap: 6px; align-items: center; }
  .step .i { font-size: 11px; opacity: 0.5; text-align: right; font-variant-numeric: tabular-nums; }
  select, input[type='text'] {
    padding: 6px 9px; border-radius: 9px; border: 1px solid var(--f-border, #334155);
    background: var(--f-bg-1, #0f172a); color: inherit; font: inherit; font-size: 12.5px; min-width: 0; width: 100%; box-sizing: border-box;
  }
  .mini { padding: 4px 8px; border-radius: 8px; border: 1px solid var(--f-border, #334155); background: transparent; color: inherit; cursor: pointer; font-size: 12px; }
  .mini:hover { background: var(--f-bg-3, #1e293b); }
  .mini:disabled { opacity: 0.35; cursor: default; }
  .foot { display: flex; gap: 8px; align-items: center; flex-wrap: wrap; padding-top: 4px; }
  .hint { font-size: 11.5px; opacity: 0.55; }
`

const Btn = styled.button<{ $primary?: boolean }>`
  padding: 8px 14px; border-radius: 10px; border: 1px solid transparent;
  background: ${(p) => (p.$primary ? '#0ea5e9' : 'var(--f-bg-1, #0f172a)')};
  color: ${(p) => (p.$primary ? '#fff' : 'inherit')};
  font-size: 13px; font-weight: 600; cursor: pointer;
  &:hover { filter: brightness(1.15); }
  &:disabled { opacity: 0.5; cursor: default; }
`

const PRESETS: Chain[] = [
  { id: '', name: 'Morning: mail + calendar + news', steps: [
    { type: 'url', value: 'https://mail.google.com' },
    { type: 'url', value: 'https://calendar.google.com' },
    { type: 'url', value: 'https://news.ycombinator.com' },
  ] },
  { id: '', name: 'Focus: cockpit + reader', steps: [
    { type: 'command', value: 'cockpit' },
    { type: 'command', value: 'reader' },
  ] },
  { id: '', name: 'Compare: split with duplicate', steps: [
    { type: 'command', value: 'duplicate-tab' },
    { type: 'wait', value: '300' },
    { type: 'command', value: 'split-view' },
  ] },
]

let nextLocalId = 1

export function Chains(props: { onGoTo?: () => void }) {
  const [chains, setChains] = React.useState<Chain[] | null>(null)
  const [known, setKnown] = React.useState<Known[]>([])
  const [open, setOpen] = React.useState<string | null>(null)
  const [dirty, setDirty] = React.useState(false)

  React.useEffect(() => {
    sendWithPromise('falcon_control.getChains').then((r: { chains: Chain[]; known: Known[] }) => {
      setChains(r.chains)
      setKnown(r.known)
    }).catch(() => setChains([]))
  }, [])

  React.useEffect(() => {
    if (window.location.hash === '#chains') props.onGoTo?.()
  }, [])

  if (!chains) return null

  const update = (next: Chain[]) => { setChains(next); setDirty(true) }
  const save = async () => {
    const saved: Chain[] = await sendWithPromise('falcon_control.setChains', chains)
    setChains(saved)
    setDirty(false)
  }
  const add = (preset?: Chain) => {
    const c: Chain = preset
      ? { ...preset, id: `local-${nextLocalId++}`, steps: preset.steps.map((s) => ({ ...s })) }
      : { id: `local-${nextLocalId++}`, name: 'New chain', steps: [{ type: 'command', value: 'new-tab' }] }
    update([...chains, c])
    setOpen(c.id)
  }
  const patch = (id: string, fn: (c: Chain) => Chain) => update(chains.map((c) => (c.id === id ? fn(c) : c)))
  const run = (c: Chain) => chrome.send('falcon_control.runChain', [c])

  return (
    <List>
      {chains.map((c) => (
        <ChainBox key={c.id} $open={open === c.id}>
          <div className='head' onClick={() => setOpen(open === c.id ? null : c.id)}>
            <b>{c.name}</b>
            <span className='n'>{c.steps.length} step{c.steps.length === 1 ? '' : 's'}</span>
            <button className='mini' onClick={(e) => { e.stopPropagation(); run(c) }} title='Run now'>▶ Run</button>
            <button className='mini' onClick={(e) => { e.stopPropagation(); update(chains.filter((x) => x.id !== c.id)) }} title='Delete'>✕</button>
          </div>
          <div className='body'>
            <input type='text' value={c.name} placeholder='Chain name' onChange={(e) => patch(c.id, (x) => ({ ...x, name: e.target.value }))} />
            {c.steps.map((st, i) => {
              const meta = STEP_TYPES.find((t) => t[0] === st.type) ?? STEP_TYPES[0]
              return (
                <div className='step' key={i}>
                  <span className='i'>{i + 1}</span>
                  <select value={st.type} onChange={(e) => {
                    const type = e.target.value as StepType
                    patch(c.id, (x) => ({ ...x, steps: x.steps.map((s, j) => (j === i ? { type, value: type === 'command' ? 'new-tab' : type === 'wait' ? '500' : '' } : s)) }))
                  }}>
                    {STEP_TYPES.map(([t, label]) => <option key={t} value={t}>{label}</option>)}
                  </select>
                  {st.type === 'command' ? (
                    <select value={st.value} onChange={(e) => patch(c.id, (x) => ({ ...x, steps: x.steps.map((s, j) => (j === i ? { ...s, value: e.target.value } : s)) }))}>
                      {known.map((k) => <option key={k.key} value={k.key}>{k.title}</option>)}
                    </select>
                  ) : (
                    <input type='text' value={st.value} placeholder={meta[2]} onChange={(e) => patch(c.id, (x) => ({ ...x, steps: x.steps.map((s, j) => (j === i ? { ...s, value: e.target.value } : s)) }))} />
                  )}
                  <button className='mini' disabled={i === 0} title='Move up' onClick={() => patch(c.id, (x) => { const s = [...x.steps]; [s[i - 1], s[i]] = [s[i], s[i - 1]]; return { ...x, steps: s } })}>↑</button>
                  <button className='mini' disabled={i === c.steps.length - 1} title='Move down' onClick={() => patch(c.id, (x) => { const s = [...x.steps]; [s[i + 1], s[i]] = [s[i], s[i + 1]]; return { ...x, steps: s } })}>↓</button>
                  <button className='mini' title='Remove step' onClick={() => patch(c.id, (x) => ({ ...x, steps: x.steps.filter((_, j) => j !== i) }))}>✕</button>
                </div>
              )
            })}
            <div className='foot'>
              <button className='mini' disabled={c.steps.length >= 40} onClick={() => patch(c.id, (x) => ({ ...x, steps: [...x.steps, { type: 'command', value: 'new-tab' }] }))}>+ Add step</button>
              <span className='hint'>Steps run in order; "Wait" gives pages time to load.</span>
            </div>
          </div>
        </ChainBox>
      ))}
      <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap', alignItems: 'center' }}>
        <Btn onClick={() => add()}>+ New chain</Btn>
        {PRESETS.map((p) => <Btn key={p.name} onClick={() => add(p)} title='Add this example'>+ {p.name.split(':')[0]} example</Btn>)}
        <span style={{ flex: 1 }} />
        <Btn $primary disabled={!dirty} onClick={save}>{dirty ? 'Save chains' : 'Saved'}</Btn>
      </div>
    </List>
  )
}
