// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

// Live preview of the shell for falcon://falcon (mock v2, screen 9): a small
// window drawing that follows the Look & Feel settings.

import * as React from 'react'
import styled from 'styled-components'

export interface PreviewProps {
  shellMode: 0 | 1 | 2
  verticalTabs: boolean
  collapsed: boolean
  sidebar: 0 | 1 | 3
  black: boolean
  light: boolean
  rounded: boolean
}

const Frame = styled.div<{ $bg: string; $border: string }>`
  position: relative;
  width: 100%;
  aspect-ratio: 16 / 10;
  border-radius: 10px;
  overflow: hidden;
  background: ${(p) => p.$bg};
  border: 1px solid ${(p) => p.$border};
  box-shadow: 0 12px 32px rgba(0, 0, 0, 0.35);
  font-size: 8px;
  user-select: none;
`

const Bar = styled.div<{ $h: number; $bg: string; $top: number }>`
  position: absolute; left: 0; right: 0; top: ${(p) => p.$top}px; height: ${(p) => p.$h}px;
  background: ${(p) => p.$bg};
  display: flex; align-items: center; gap: 4px; padding: 0 8px; box-sizing: border-box;
`

const Dot = styled.span<{ $c: string }>`
  width: 6px; height: 6px; border-radius: 50%; background: ${(p) => p.$c}; display: inline-block;
`

const Rail = styled.div<{ $w: number; $bg: string; $top: number }>`
  position: absolute; left: 0; top: ${(p) => p.$top}px; bottom: 0; width: ${(p) => p.$w}px;
  background: ${(p) => p.$bg};
  display: flex; flex-direction: column; gap: 5px; padding: 8px 6px; box-sizing: border-box;
`

const Tab = styled.div<{ $on: boolean; $fg: string; $wide: boolean }>`
  height: 12px; border-radius: 4px;
  background: ${(p) => (p.$on ? 'rgba(56,189,248,0.35)' : 'rgba(255,255,255,0.08)')};
  display: flex; align-items: center; gap: 4px; padding: 0 4px; color: ${(p) => p.$fg};
  &::before { content: ''; width: 6px; height: 6px; border-radius: 2px; background: ${(p) => (p.$on ? '#38bdf8' : 'rgba(255,255,255,0.35)')}; flex: none; }
  span { display: ${(p) => (p.$wide ? 'block' : 'none')}; font-size: 7px; white-space: nowrap; overflow: hidden; }
`

const Content = styled.div<{ $left: number; $top: number; $right: number; $rounded: boolean; $bg: string }>`
  position: absolute; left: ${(p) => p.$left}px; right: ${(p) => p.$right}px; top: ${(p) => p.$top}px; bottom: ${(p) => (p.$rounded ? 4 : 0)}px;
  background: ${(p) => p.$bg};
  border-radius: ${(p) => (p.$rounded ? 6 : 0)}px;
  overflow: hidden;
  &::before { content: ''; position: absolute; left: 14%; right: 14%; top: 16%; height: 6px; border-radius: 3px; background: rgba(0,0,0,0.12); }
  &::after { content: ''; position: absolute; left: 14%; right: 30%; top: 30%; height: 40%; border-radius: 4px; background: rgba(0,0,0,0.07); }
`

const Capsule = styled.div`
  position: absolute; left: 50%; top: 6px; transform: translateX(-50%);
  width: 62%; height: 16px; border-radius: 999px;
  background: rgba(11, 18, 32, 0.92); border: 1px solid rgba(56,189,248,0.35);
  box-shadow: 0 6px 16px rgba(0,0,0,0.45);
  display: flex; align-items: center; gap: 4px; padding: 0 8px; box-sizing: border-box;
  .url { flex: 1; height: 6px; border-radius: 3px; background: rgba(255,255,255,0.12); }
  .ring { width: 8px; height: 8px; border-radius: 50%; border: 1.5px solid #38bdf8; border-right-color: rgba(56,189,248,0.25); }
`

const Edge = styled.div`
  position: absolute; right: 1px; top: 40%; bottom: 20%; width: 2px; border-radius: 2px;
  background: #38bdf8; box-shadow: 0 0 6px #38bdf8;
`

const SideRail = styled.div<{ $bg: string; $top: number }>`
  position: absolute; right: 0; top: ${(p) => p.$top}px; bottom: 0; width: 14px; background: ${(p) => p.$bg};
  display: flex; flex-direction: column; align-items: center; gap: 5px; padding-top: 8px;
  i { width: 6px; height: 6px; border-radius: 2px; background: rgba(255,255,255,0.35); }
`

export function ShellPreview(p: PreviewProps) {
  const dark = !p.light
  const bg0 = p.black ? '#000' : dark ? '#0b1220' : '#e2e8f0'
  const bg1 = p.black ? '#050505' : dark ? '#0f172a' : '#f8fafc'
  const border = p.black ? '#1f1f1f' : dark ? '#334155' : '#cbd5e1'
  const fg = dark ? '#e2e8f0' : '#0f172a'
  const page = dark ? (p.black ? '#0a0a0a' : '#111827') : '#ffffff'

  const classic = p.shellMode === 0
  const mac = p.shellMode === 1
  const cockpit = p.shellMode === 2
  const topH = classic ? 12 + 14 : mac ? 10 : 0
  const railW = p.verticalTabs ? (p.collapsed && !classic ? 14 : 44) : 0
  const sideW = p.sidebar === 0 ? 14 : 0

  return (
    <Frame $bg={bg0} $border={border}>
      {classic && (
        <>
          <Bar $h={12} $bg={bg0} $top={0}><span style={{ color: fg, opacity: 0.7, fontSize: 7 }}>Falcon</span><span style={{ flex: 1 }} /><Dot $c="rgba(255,255,255,0.35)" /><Dot $c="rgba(255,255,255,0.35)" /><Dot $c="rgba(255,255,255,0.35)" /></Bar>
          <Bar $h={14} $bg={bg1} $top={12}><Dot $c="rgba(255,255,255,0.35)" /><Dot $c="rgba(255,255,255,0.35)" /><div style={{ flex: 1, height: 7, borderRadius: 4, background: 'rgba(255,255,255,0.1)' }} /><Dot $c="#38bdf8" /></Bar>
        </>
      )}
      {mac && (
        <Bar $h={10} $bg={bg1} $top={0}><span style={{ flex: 1 }} /><Dot $c="#38bdf8" /><span style={{ color: fg, opacity: 0.75, fontSize: 6 }}>Falcon · falcon.app</span><span style={{ flex: 1 }} /><Dot $c="rgba(255,255,255,0.3)" /><Dot $c="rgba(255,255,255,0.3)" /></Bar>
      )}
      {p.verticalTabs && (
        <Rail $w={railW} $bg={bg0} $top={topH}>
          {[true, false, false, false].map((on, i) => <Tab key={i} $on={on} $fg={fg} $wide={railW > 20}><span>{['Falcon', 'GitHub', 'Docs', 'Music'][i]}</span></Tab>)}
        </Rail>
      )}
      <Content $left={railW} $right={sideW} $top={topH} $rounded={p.rounded} $bg={page} />
      {p.sidebar === 0 && <SideRail $bg={bg0} $top={topH}><i /><i /><i /></SideRail>}
      {cockpit && <Capsule><Dot $c="rgba(255,255,255,0.4)" /><Dot $c="rgba(255,255,255,0.25)" /><div className="url" /><div className="ring" /><Dot $c="rgba(255,255,255,0.4)" /></Capsule>}
      {cockpit && <Edge />}
    </Frame>
  )
}
