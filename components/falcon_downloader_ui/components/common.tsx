// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

import styled from 'styled-components'
import { Aria2Download, Aria2Status } from '../aria2_client'

// ---------------------------------------------------------------- helpers

export function fmtBytes(n: number): string {
  if (!isFinite(n) || n <= 0) return '0 B'
  const units = ['B', 'KB', 'MB', 'GB', 'TB']
  let i = 0
  while (n >= 1024 && i < units.length - 1) {
    n /= 1024
    i++
  }
  return `${n < 10 && i > 0 ? n.toFixed(1) : Math.round(n)} ${units[i]}`
}

export function fmtSpeed(n: number): string {
  return n > 0 ? `${fmtBytes(n)}/s` : '—'
}

export function fmtEta(remaining: number, speed: number): string {
  if (speed <= 0 || remaining <= 0) return '—'
  let s = Math.round(remaining / speed)
  if (s < 60) return `${s}s`
  const m = Math.floor(s / 60)
  s %= 60
  if (m < 60) return `${m}m ${s}s`
  const h = Math.floor(m / 60)
  return `${h}h ${m % 60}m`
}

export function basename(p: string): string {
  return p.split(/[\\/]/).pop() || p
}

export function nameOf(d: Aria2Download): string {
  const bt = d.bittorrent?.info?.name
  if (bt) return bt
  const p = d.files?.[0]?.path
  if (p) {
    const base = basename(p)
    if (base && !base.startsWith('[METADATA]')) return base
  }
  const uri = d.files?.[0]?.uris?.[0]?.uri
  if (uri) {
    if (uri.startsWith('magnet:')) {
      const dn = new URLSearchParams(uri.slice(uri.indexOf('?') + 1)).get('dn')
      if (dn) return dn
      return 'Magnet (fetching metadata…)'
    }
    try {
      const u = new URL(uri)
      return decodeURIComponent(u.pathname.split('/').pop() || u.host)
    } catch {
      return uri
    }
  }
  return d.gid
}

export function firstUri(d: Aria2Download): string {
  return d.files?.[0]?.uris?.[0]?.uri ?? ''
}

export function isTorrent(d: Aria2Download): boolean {
  return !!d.bittorrent || !!d.infoHash
}

export const STATUS_LABEL: Record<Aria2Status, string> = {
  active: 'Downloading',
  waiting: 'Queued',
  paused: 'Paused',
  error: 'Failed',
  complete: 'Done',
  removed: 'Removed',
}

export const STATUS_ORDER: Record<Aria2Status, number> = {
  active: 0, waiting: 1, paused: 2, error: 3, complete: 4, removed: 5,
}

export type Filter = 'all' | 'active' | 'done' | 'failed' | 'torrents'

export function matchesFilter(d: Aria2Download, f: Filter): boolean {
  switch (f) {
    case 'active':
      return d.status === 'active' || d.status === 'waiting' || d.status === 'paused'
    case 'done':
      return d.status === 'complete'
    case 'failed':
      return d.status === 'error' || d.status === 'removed'
    case 'torrents':
      return isTorrent(d)
    default:
      return true
  }
}

export type SortKey = 'added' | 'name' | 'size' | 'progress' | 'speed'

// ----------------------------------------------------------------- styles

export const Button = styled.button<{ $primary?: boolean; $danger?: boolean; $small?: boolean }>`
  padding: ${(p) => (p.$small ? '5px 10px' : '9px 14px')};
  border-radius: 10px;
  border: 1px solid transparent;
  background: ${(p) => (p.$primary ? '#0ea5e9' : p.$danger ? '#7f1d1d' : 'var(--leo-color-container-background, var(--f-bg-3, #1e293b))')};
  color: ${(p) => (p.$primary ? '#fff' : 'inherit')};
  font-size: ${(p) => (p.$small ? '12px' : '13px')};
  font-weight: 600;
  cursor: pointer;
  white-space: nowrap;
  &:hover { filter: brightness(1.15); }
  &:disabled { opacity: 0.4; cursor: default; filter: none; }
`

export const Input = styled.input`
  padding: 10px 12px;
  border-radius: 10px;
  border: 1px solid var(--leo-color-divider-subtle, var(--f-border, #334155));
  background: var(--leo-color-container-background, var(--f-bg-3, #1e293b));
  color: inherit;
  font-size: 14px;
  outline: none;
  &:focus { border-color: #38bdf8; }
`

export const Select = styled.select`
  padding: 6px 10px;
  border-radius: 10px;
  border: 1px solid var(--leo-color-divider-subtle, var(--f-border, #334155));
  background: var(--leo-color-container-background, var(--f-bg-3, #1e293b));
  color: inherit;
  font-size: 12px;
`

export const Chip = styled.button<{ $active: boolean }>`
  padding: 6px 12px;
  border-radius: 999px;
  border: 1px solid ${(p) => (p.$active ? '#38bdf8' : 'var(--leo-color-divider-subtle, var(--f-border, #334155))')};
  background: ${(p) => (p.$active ? 'rgba(56,189,248,0.15)' : 'transparent')};
  color: inherit;
  font-size: 12px;
  cursor: pointer;
`

export const Stat = styled.div`
  font-size: 13px;
  opacity: 0.8;
  b { font-weight: 600; opacity: 1; }
`

export const Meta = styled.div`
  font-size: 12px;
  opacity: 0.75;
  display: flex;
  gap: 14px;
  flex-wrap: wrap;
  .mono { font-family: "Cascadia Mono", Consolas, "JetBrains Mono", monospace; font-size: 11.5px; letter-spacing: 0.01em; }
`

export const ErrorText = styled.span`
  color: #f87171;
`

export const Card = styled.div`
  background: var(--leo-color-container-background, var(--f-bg-3, #1e293b));
  border: 1px solid var(--leo-color-divider-subtle, var(--f-border, #334155));
  border-radius: 14px;
  padding: 12px 14px;
`

export const Toggle = styled.label`
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  padding: 10px 0;
  border-bottom: 1px solid var(--leo-color-divider-subtle, var(--f-border, #334155));
  font-size: 13px;
  cursor: pointer;
  .sub { display: block; font-size: 12px; opacity: 0.65; margin-top: 2px; }
  input[type='checkbox'] { width: 18px; height: 18px; accent-color: #0ea5e9; }
  input[type='number'] { width: 90px; }
`
