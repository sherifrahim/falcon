// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

import * as React from 'react'
import styled from 'styled-components'
import { Background, DEFAULT_STATE, GRADIENTS, NtpState } from './state'

const Panel = styled.div`
  position: fixed;
  top: 0;
  right: 0;
  height: 100%;
  width: min(380px, 100%);
  padding: 20px 20px 40px;
  overflow-y: auto;
  background: rgba(15, 23, 42, 0.92);
  backdrop-filter: blur(20px);
  border-left: 1px solid rgba(255, 255, 255, 0.1);
  box-shadow: -12px 0 40px rgba(0, 0, 0, 0.45);
  z-index: 20;
  h2 { margin: 0; font-size: 17px; font-weight: 600; }
  h3 { margin: 22px 0 6px; font-size: 11px; letter-spacing: 0.1em; text-transform: uppercase; opacity: 0.6; }
`

const Backdrop = styled.div`
  position: fixed;
  inset: 0;
  z-index: 19;
`

const Row = styled.label`
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 14px;
  padding: 9px 0;
  border-bottom: 1px solid rgba(255, 255, 255, 0.07);
  font-size: 13px;
  cursor: pointer;
  input[type='checkbox'] { width: 18px; height: 18px; accent-color: #0ea5e9; }
  input[type='text'], input[type='color'] { padding: 7px 10px; border-radius: 8px; border: 1px solid #334155; background: #1e293b; color: inherit; font-size: 13px; width: 170px; outline: none; }
  input[type='text']:focus { border-color: #38bdf8; }
  input[type='range'] { width: 150px; accent-color: #0ea5e9; }
  .sub { display: block; font-size: 11px; opacity: 0.6; margin-top: 2px; }
`

const Swatches = styled.div`
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 8px;
  margin: 8px 0;
`

const Swatch = styled.button<{ $css: string; $on: boolean }>`
  height: 48px;
  border-radius: 10px;
  border: 2px solid ${(p) => (p.$on ? '#38bdf8' : 'transparent')};
  background: ${(p) => p.$css};
  background-size: 200% 200%;
  cursor: pointer;
  color: #fff;
  font-size: 10px;
  text-shadow: 0 1px 4px rgba(0, 0, 0, 0.7);
`

const Modes = styled.div`
  display: flex;
  gap: 6px;
  flex-wrap: wrap;
  margin: 6px 0;
  button {
    padding: 6px 12px; border-radius: 999px; border: 1px solid #334155; background: transparent; color: inherit; font-size: 12px; cursor: pointer;
  }
  button.on { border-color: #38bdf8; background: rgba(56,189,248,0.15); }
`

const Btn = styled.button`
  padding: 7px 12px;
  border-radius: 10px;
  border: 1px solid transparent;
  background: #1e293b;
  color: inherit;
  font-size: 12px;
  font-weight: 600;
  cursor: pointer;
  &:hover { filter: brightness(1.15); }
`

export function SettingsPanel(props: {
  state: NtpState
  update: (patch: Partial<NtpState>) => void
  onClose: () => void
}) {
  const { state, update } = props
  const setBg = (patch: Partial<Background>) => update({ bg: { ...state.bg, ...patch } })
  const bool = (key: keyof NtpState, label: string, sub?: string) => (
    <Row>
      <span>{label}{sub && <span className="sub">{sub}</span>}</span>
      <input type="checkbox" checked={!!state[key]} onChange={(e) => update({ [key]: e.target.checked } as Partial<NtpState>)} />
    </Row>
  )
  const [name, setName] = React.useState(state.name)
  const [imageUrl, setImageUrl] = React.useState(state.bg.imageUrl)

  return (
    <>
      <Backdrop onClick={props.onClose} />
      <Panel>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
          <h2>Customize new tab</h2>
          <Btn onClick={props.onClose}>Close</Btn>
        </div>

        <h3>Background</h3>
        <Modes>
          {(['gradient', 'solid', 'image', 'bing'] as const).map((m) => (
            <button key={m} className={state.bg.mode === m ? 'on' : ''} onClick={() => setBg({ mode: m })}>
              {m === 'bing' ? 'Bing daily photo' : m[0].toUpperCase() + m.slice(1)}
            </button>
          ))}
        </Modes>
        {state.bg.mode === 'gradient' && (
          <>
            <Swatches>
              {GRADIENTS.map((g, i) => (
                <Swatch key={g.name} $css={g.css} $on={state.bg.gradient === i} onClick={() => setBg({ gradient: i })} title={g.name}>{g.name}</Swatch>
              ))}
            </Swatches>
            <Row>
              <span>Slow drift<span className="sub">Gently animate the gradient</span></span>
              <input type="checkbox" checked={state.bg.drift} onChange={(e) => setBg({ drift: e.target.checked })} />
            </Row>
          </>
        )}
        {state.bg.mode === 'solid' && (
          <Row>
            <span>Color</span>
            <input type="color" value={state.bg.color} onChange={(e) => setBg({ color: e.target.value })} />
          </Row>
        )}
        {state.bg.mode === 'image' && (
          <Row as="div">
            <span>Image URL<span className="sub">Any https image; local files: drop it in a folder served over http, or use a data: URL</span></span>
            <input type="text" value={imageUrl} placeholder="https://…/wallpaper.jpg" onChange={(e) => setImageUrl(e.target.value)}
              onBlur={() => setBg({ imageUrl: imageUrl.trim() })} onKeyDown={(e) => e.key === 'Enter' && setBg({ imageUrl: imageUrl.trim() })} />
          </Row>
        )}
        <Row>
          <span>Blur<span className="sub">{state.bg.blur}px</span></span>
          <input type="range" min={0} max={30} value={state.bg.blur} onChange={(e) => setBg({ blur: +e.target.value })} />
        </Row>
        <Row>
          <span>Dim<span className="sub">{state.bg.dim}% darker for readability</span></span>
          <input type="range" min={0} max={80} value={state.bg.dim} onChange={(e) => setBg({ dim: +e.target.value })} />
        </Row>

        <h3>Widgets</h3>
        {bool('showClock', 'Clock')}
        {bool('clock24', '24-hour clock')}
        {bool('showSeconds', 'Show seconds')}
        {bool('showDate', 'Date')}
        {bool('showGreeting', 'Greeting')}
        <Row as="div">
          <span>Your name<span className="sub">For the greeting</span></span>
          <input type="text" value={name} placeholder="Sherif" onChange={(e) => setName(e.target.value)}
            onBlur={() => update({ name: name.trim() })} onKeyDown={(e) => e.key === 'Enter' && update({ name: name.trim() })} />
        </Row>
        {bool('showSearch', 'Search bar', 'Uses your default search engine; URLs open directly')}
        {bool('showLinks', 'Shortcuts', 'Right-click a tile to edit, hover for remove')}
        {bool('showQuote', 'Quote of the day')}

        <h3>Reset</h3>
        <Row as="div">
          <span>Back to defaults<span className="sub">Keeps your shortcuts</span></span>
          <Btn onClick={() => update({ ...DEFAULT_STATE, links: state.links, linksSeededFromHistory: true })}>Reset</Btn>
        </Row>
      </Panel>
    </>
  )
}
