// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

// New tab page state: one object, round-tripped through the falcon.ntp.state
// pref. Everything has a default so older stored states keep working.

export interface QuickLink {
  title: string
  url: string
}

export type BackgroundMode = 'gradient' | 'solid' | 'image' | 'bing' | 'video'

export interface Background {
  mode: BackgroundMode
  gradient: number // index into GRADIENTS
  color: string
  imageUrl: string
  videoUrl: string // mp4/webm, looped and muted
  blur: number // px
  dim: number // 0..80 (%)
  drift: boolean // slow animated gradient
}

export interface NtpState {
  name: string
  clock24: boolean
  showSeconds: boolean
  showClock: boolean
  showDate: boolean
  showGreeting: boolean
  showSearch: boolean
  showLinks: boolean
  showQuote: boolean
  linksSeededFromHistory: boolean
  showFocus: boolean
  showSounds: boolean
  links: QuickLink[]
  bg: Background
  weather: { enabled: boolean; city: string; latitude: number; longitude: number; unit: 'c' | 'f' }
}

export const GRADIENTS: Array<{ name: string; css: string }> = [
  { name: 'Aurora', css: 'linear-gradient(135deg, var(--f-bg-0, #0b1220) 0%, #1e3a8a 45%, #0ea5e9 100%)' },
  { name: 'Dusk', css: 'linear-gradient(140deg, var(--f-bg-1, #0f172a) 0%, #7c2d12 55%, #f59e0b 100%)' },
  { name: 'Forest', css: 'linear-gradient(135deg, #052e16 0%, #14532d 50%, #0f766e 100%)' },
  { name: 'Grape', css: 'linear-gradient(135deg, #1e1b4b 0%, #4c1d95 50%, #db2777 100%)' },
  { name: 'Slate', css: 'linear-gradient(160deg, var(--f-bg-0, #0b1220) 0%, var(--f-bg-3, #1e293b) 100%)' },
  { name: 'Mesh', css: 'radial-gradient(at 20% 20%, #1d4ed8 0px, transparent 50%), radial-gradient(at 80% 10%, #7c3aed 0px, transparent 50%), radial-gradient(at 70% 80%, #0ea5e9 0px, transparent 50%), radial-gradient(at 10% 90%, #db2777 0px, transparent 50%), var(--f-bg-0, #0b1220)' },
  { name: 'Ocean', css: 'linear-gradient(135deg, #082f49 0%, #0e7490 60%, #22d3ee 100%)' },
  { name: 'Ember', css: 'linear-gradient(135deg, #1c1917 0%, #7f1d1d 55%, #f97316 100%)' },
]

export const DEFAULT_STATE: NtpState = {
  name: '',
  clock24: false,
  showSeconds: false,
  showClock: true,
  showDate: true,
  showGreeting: true,
  showSearch: true,
  showLinks: true,
  showQuote: true,
  linksSeededFromHistory: false,
  showFocus: false,
  showSounds: false,
  links: [],
  bg: { mode: 'gradient', gradient: 0, color: '#0b1220', imageUrl: '', videoUrl: '', blur: 0, dim: 20, drift: true },
  weather: { enabled: false, city: '', latitude: 0, longitude: 0, unit: 'c' },
}

export function withDefaults(stored: Partial<NtpState> | null | undefined): NtpState {
  const s = { ...DEFAULT_STATE, ...(stored ?? {}) }
  s.bg = { ...DEFAULT_STATE.bg, ...((stored && stored.bg) || {}) }
  s.weather = { ...DEFAULT_STATE.weather, ...((stored && stored.weather) || {}) }
  s.links = Array.isArray(s.links) ? s.links.filter((l) => l && typeof l.url === 'string') : []
  return s
}

export const QUOTES: Array<[string, string]> = [
  ['Simplicity is the ultimate sophistication.', 'Leonardo da Vinci'],
  ['What we think, we become.', 'Buddha'],
  ['Well begun is half done.', 'Aristotle'],
  ['The obstacle is the way.', 'Marcus Aurelius'],
  ['Make it work, make it right, make it fast.', 'Kent Beck'],
  ['Focus is saying no to a hundred good ideas.', 'Steve Jobs'],
  ['Whatever you are, be a good one.', 'Abraham Lincoln'],
  ['Slow is smooth, smooth is fast.', 'Proverb'],
  ['The best way out is always through.', 'Robert Frost'],
  ['Everything should be made as simple as possible, but not simpler.', 'Albert Einstein'],
  ['It always seems impossible until it is done.', 'Nelson Mandela'],
  ['Do the hard things first.', 'Unknown'],
  ['Quality is not an act, it is a habit.', 'Aristotle'],
  ['A year from now you will wish you had started today.', 'Karen Lamb'],
  ['Talk is cheap. Show me the code.', 'Linus Torvalds'],
  ['Amateurs sit and wait for inspiration; the rest of us get up and go to work.', 'Stephen King'],
  ['Nothing will work unless you do.', 'Maya Angelou'],
  ['Small deeds done are better than great deeds planned.', 'Peter Marshall'],
  ['The details are not the details. They make the design.', 'Charles Eames'],
  ['Have the courage to follow your heart and intuition.', 'Steve Jobs'],
]

export function quoteOfTheDay(): [string, string] {
  const d = new Date()
  const day = Math.floor(Date.UTC(d.getFullYear(), d.getMonth(), d.getDate()) / 86400000)
  return QUOTES[day % QUOTES.length]
}

export function greeting(name: string): string {
  const h = new Date().getHours()
  const part = h < 5 ? 'Good night' : h < 12 ? 'Good morning' : h < 17 ? 'Good afternoon' : h < 22 ? 'Good evening' : 'Good night'
  return name ? `${part}, ${name}` : part
}

export function looksLikeUrl(text: string): string | null {
  const t = text.trim()
  if (!t || /\s/.test(t)) return null
  if (/^(https?|chrome|falcon|file):\/\//i.test(t)) return t
  if (/^[a-z0-9-]+(\.[a-z0-9-]+)+(:\d+)?(\/.*)?$/i.test(t) || /^localhost(:\d+)?/.test(t)) return `https://${t}`
  return null
}

export function faviconUrl(url: string): string {
  return `chrome://favicon2/?size=32&scaleFactor=2x&showFallbackMonogram=&pageUrl=${encodeURIComponent(url)}`
}
