// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

// Falcon first run: choose the window style (falcon.shell.mode), drawn with
// the same live preview as falcon://falcon › Look & Feel.

import * as React from 'react'
import styled from 'styled-components'
import { loadTimeData } from '$web-common/loadTimeData'
import { ShellPreview } from '../../../falcon_control_ui/components/preview'

type Mode = 0 | 1 | 2

const STYLES: Array<{ mode: Mode; name: string; desc: string; beta?: boolean }> = [
  { mode: 1, name: 'Mac-style', desc: 'Slim title bar; the toolbar slides in when you point at the top.' },
  { mode: 0, name: 'Classic', desc: 'Tabs and toolbar always showing, like most Windows browsers.' },
  { mode: 2, name: 'Cockpit', desc: 'No window chrome; a floating capsule appears on hover.', beta: true },
]

const Box = styled.section`
  width: 100%;
  margin: 28px 0 32px;
  h3 {
    margin: 0 0 12px;
    font-size: 12px;
    font-weight: 600;
    letter-spacing: 0.12em;
    text-transform: uppercase;
    color: var(--dim);
  }
  .grid {
    display: grid;
    grid-template-columns: repeat(3, minmax(0, 1fr));
    gap: 14px;
    @media (max-width: 820px) { grid-template-columns: 1fr; }
  }
`

const Choice = styled.button<{ $on: boolean }>`
  display: flex;
  flex-direction: column;
  gap: 8px;
  padding: 10px 10px 12px;
  border-radius: 14px;
  border: 1px solid ${(p) => (p.$on ? 'rgba(142, 155, 255, 0.7)' : 'var(--hair)')};
  background: ${(p) => (p.$on ? 'rgba(142, 155, 255, 0.08)' : 'rgba(255, 255, 255, 0.04)')};
  box-shadow: ${(p) => (p.$on ? '0 0 0 1px rgba(142, 155, 255, 0.35)' : 'none')};
  color: var(--ink);
  font: inherit;
  text-align: start;
  cursor: pointer;
  transition: border-color 150ms ease, background 150ms ease;
  &:hover { border-color: rgba(142, 155, 255, 0.45); }
  &:focus-visible { outline: 2px solid var(--accent); outline-offset: 3px; }
  .name { display: flex; align-items: center; gap: 8px; font-size: 14px; font-weight: 600; }
  .beta {
    font-size: 10px; font-weight: 700; letter-spacing: 0.08em; text-transform: uppercase;
    padding: 1px 7px; border-radius: 999px; color: #fbbf24; border: 1px solid rgba(251, 191, 36, 0.5);
  }
  .desc { font-size: 12.5px; line-height: 1.4; color: var(--dim); }
`

export function WindowStylePicker () {
  const [mode, setMode] = React.useState<Mode>(() => {
    const v = loadTimeData.getInteger('falconShellMode')
    return (v === 0 || v === 2 ? v : 1) as Mode
  })
  const choose = (m: Mode) => {
    setMode(m)
    chrome.send('setFalconWindowStyle', [m])
  }
  return (
    <Box>
      <h3 id="style-label">Window style</h3>
      <div className="grid" role="radiogroup" aria-labelledby="style-label">
        {STYLES.map((s) => (
          <Choice key={s.mode} $on={mode === s.mode} role="radio" aria-checked={mode === s.mode} onClick={() => choose(s.mode)}>
            <ShellPreview shellMode={s.mode} verticalTabs={false} collapsed={false} sidebar={3} black={false} light={false} rounded={s.mode !== 0} />
            <span className="name">{s.name}{s.beta && <span className="beta">Beta</span>}</span>
            <span className="desc">{s.desc}</span>
          </Choice>
        ))}
      </div>
    </Box>
  )
}
