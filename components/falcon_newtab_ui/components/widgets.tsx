// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

// New tab widgets: weather (Open-Meteo, no API key) and a focus/pomodoro
// timer. Both are self-contained; state that must survive reloads lives in
// the NtpState passed down from the app.

import * as React from 'react'
import styled from 'styled-components'

// ------------------------------------------------------------------ weather

export interface WeatherSettings {
  enabled: boolean
  city: string
  latitude: number
  longitude: number
  unit: 'c' | 'f'
}

interface WeatherData {
  temp: number
  code: number
  high: number
  low: number
  fetched: number
}

const WMO: Record<number, [string, string]> = {
  0: ['Clear', '☀️'], 1: ['Mostly clear', '🌤️'], 2: ['Partly cloudy', '⛅'], 3: ['Overcast', '☁️'],
  45: ['Fog', '🌫️'], 48: ['Icy fog', '🌫️'], 51: ['Light drizzle', '🌦️'], 53: ['Drizzle', '🌦️'], 55: ['Heavy drizzle', '🌧️'],
  56: ['Freezing drizzle', '🌧️'], 57: ['Freezing drizzle', '🌧️'], 61: ['Light rain', '🌧️'], 63: ['Rain', '🌧️'], 65: ['Heavy rain', '🌧️'],
  66: ['Freezing rain', '🌧️'], 67: ['Freezing rain', '🌧️'], 71: ['Light snow', '🌨️'], 73: ['Snow', '🌨️'], 75: ['Heavy snow', '❄️'],
  77: ['Snow grains', '🌨️'], 80: ['Showers', '🌦️'], 81: ['Showers', '🌧️'], 82: ['Violent showers', '⛈️'],
  85: ['Snow showers', '🌨️'], 86: ['Snow showers', '❄️'], 95: ['Thunderstorm', '⛈️'], 96: ['Thunderstorm, hail', '⛈️'], 99: ['Thunderstorm, hail', '⛈️'],
}

const Chip = styled.div`
  position: fixed;
  top: 16px;
  right: 18px;
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 8px 14px 8px 10px;
  border-radius: 999px;
  background: rgba(15, 23, 42, 0.5);
  border: 1px solid rgba(255, 255, 255, 0.1);
  backdrop-filter: blur(14px);
  font-size: 13px;
  cursor: default;
  .big { font-size: 22px; line-height: 1; }
  .t { font-size: 18px; font-weight: 600; }
  .s { opacity: 0.75; font-size: 12px; }
`

export async function geocode(city: string): Promise<{ name: string; latitude: number; longitude: number } | null> {
  const r = await fetch(`https://geocoding-api.open-meteo.com/v1/search?name=${encodeURIComponent(city)}&count=1&language=en&format=json`)
  const j = await r.json()
  const hit = j?.results?.[0]
  if (!hit) return null
  const name = [hit.name, hit.admin1, hit.country_code].filter(Boolean).join(', ')
  return { name, latitude: hit.latitude, longitude: hit.longitude }
}

export function Weather({ settings }: { settings: WeatherSettings }) {
  const [data, setData] = React.useState<WeatherData | null>(null)
  const [error, setError] = React.useState(false)
  const key = `${settings.latitude},${settings.longitude},${settings.unit}`

  React.useEffect(() => {
    if (!settings.enabled || !isFinite(settings.latitude) || !isFinite(settings.longitude) || (settings.latitude === 0 && settings.longitude === 0)) return
    let cancelled = false
    const load = async () => {
      try {
        const cached = localStorage.getItem(`falcon.weather.${key}`)
        if (cached) {
          const c: WeatherData = JSON.parse(cached)
          if (Date.now() - c.fetched < 20 * 60 * 1000) { setData(c); return }
        }
      } catch { /* ignore */ }
      try {
        const unit = settings.unit === 'f' ? '&temperature_unit=fahrenheit' : ''
        const r = await fetch(`https://api.open-meteo.com/v1/forecast?latitude=${settings.latitude}&longitude=${settings.longitude}&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min&forecast_days=1&timezone=auto${unit}`)
        const j = await r.json()
        const d: WeatherData = {
          temp: Math.round(j.current.temperature_2m),
          code: j.current.weather_code,
          high: Math.round(j.daily.temperature_2m_max[0]),
          low: Math.round(j.daily.temperature_2m_min[0]),
          fetched: Date.now(),
        }
        if (cancelled) return
        setData(d)
        setError(false)
        try { localStorage.setItem(`falcon.weather.${key}`, JSON.stringify(d)) } catch { /* ignore */ }
      } catch {
        if (!cancelled) setError(true)
      }
    }
    load()
    const id = window.setInterval(load, 20 * 60 * 1000)
    return () => { cancelled = true; window.clearInterval(id) }
  }, [settings.enabled, key, settings.latitude, settings.longitude, settings.unit])

  if (!settings.enabled) return null
  if (!data) {
    return <Chip title="Weather"><span className="s">{error ? 'Weather unavailable' : settings.city ? 'Loading weather…' : 'Set a city in settings for weather'}</span></Chip>
  }
  const [label, icon] = WMO[data.code] ?? ['', '🌡️']
  const u = settings.unit === 'f' ? '°F' : '°C'
  return (
    <Chip title={`${label} · ${settings.city}`}>
      <span className="big">{icon}</span>
      <div>
        <div className="t">{data.temp}{u} <span className="s">{label}</span></div>
        <div className="s">{settings.city} · H {data.high}° L {data.low}°</div>
      </div>
    </Chip>
  )
}

// -------------------------------------------------------------------- focus

const Focus = styled.div`
  position: fixed;
  left: 18px;
  bottom: 16px;
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 8px 12px;
  border-radius: 999px;
  background: rgba(15, 23, 42, 0.5);
  border: 1px solid rgba(255, 255, 255, 0.1);
  backdrop-filter: blur(14px);
  font-size: 13px;
  font-variant-numeric: tabular-nums;
  .time { font-size: 18px; font-weight: 600; min-width: 58px; text-align: center; }
  button {
    border: none; border-radius: 999px; padding: 6px 12px; cursor: pointer; font-size: 12px; font-weight: 600;
    background: rgba(56, 189, 248, 0.2); color: #e0f2fe;
  }
  button:hover { background: rgba(56, 189, 248, 0.35); }
  button.ghost { background: transparent; color: inherit; opacity: 0.8; }
  select { background: transparent; color: inherit; border: none; font-size: 12px; cursor: pointer; }
  select option { color: #000; }
`

// Persisted in localStorage so a reload/new tab keeps counting.
interface FocusPersist {
  endsAt: number // 0 = idle
  minutes: number
  mode: 'focus' | 'break'
}

function loadFocus(): FocusPersist {
  try {
    const raw = localStorage.getItem('falcon.focus')
    if (raw) return JSON.parse(raw)
  } catch { /* ignore */ }
  return { endsAt: 0, minutes: 25, mode: 'focus' }
}

export function FocusTimer() {
  const [st, setSt] = React.useState<FocusPersist>(loadFocus)
  const [now, setNow] = React.useState(Date.now())
  const [done, setDone] = React.useState(false)

  React.useEffect(() => {
    const id = window.setInterval(() => setNow(Date.now()), 1000)
    return () => window.clearInterval(id)
  }, [])

  const save = (next: FocusPersist) => {
    setSt(next)
    try { localStorage.setItem('falcon.focus', JSON.stringify(next)) } catch { /* ignore */ }
  }

  const remaining = st.endsAt ? Math.max(0, st.endsAt - now) : st.minutes * 60 * 1000
  React.useEffect(() => {
    if (st.endsAt && remaining === 0 && !done) {
      setDone(true)
      document.title = st.mode === 'focus' ? 'Focus session done — take a break' : 'Break over — back to it'
      // Auto-flip to the other mode, idle.
      save({ endsAt: 0, minutes: st.mode === 'focus' ? 5 : 25, mode: st.mode === 'focus' ? 'break' : 'focus' })
    }
  }, [remaining, st, done])

  const m = Math.floor(remaining / 60000)
  const s = Math.floor((remaining % 60000) / 1000)
  const running = st.endsAt > 0 && remaining > 0

  return (
    <Focus title="Focus timer (pomodoro)">
      <span className="time">{String(m).padStart(2, '0')}:{String(s).padStart(2, '0')}</span>
      {running ? (
        <button className="ghost" onClick={() => save({ ...st, endsAt: 0 })}>Stop</button>
      ) : (
        <>
          <select value={st.minutes} onChange={(e) => save({ ...st, minutes: +e.target.value })}>
            {[5, 10, 15, 25, 45, 60].map((v) => <option key={v} value={v}>{v} min</option>)}
          </select>
          <button onClick={() => { setDone(false); document.title = 'New Tab'; save({ ...st, endsAt: Date.now() + st.minutes * 60000 }) }}>
            {st.mode === 'focus' ? 'Focus' : 'Break'}
          </button>
        </>
      )}
    </Focus>
  )
}

// ----------------------------------------------------------- search engines

export interface SearchShortcut {
  key: string
  name: string
  url: string // %s = query
}

export const SEARCH_SHORTCUTS: SearchShortcut[] = [
  { key: 'g', name: 'Google', url: 'https://www.google.com/search?q=%s' },
  { key: 'd', name: 'DuckDuckGo', url: 'https://duckduckgo.com/?q=%s' },
  { key: 'y', name: 'YouTube', url: 'https://www.youtube.com/results?search_query=%s' },
  { key: 'w', name: 'Wikipedia', url: 'https://en.wikipedia.org/w/index.php?search=%s' },
  { key: 'gh', name: 'GitHub', url: 'https://github.com/search?q=%s' },
  { key: 'r', name: 'Reddit', url: 'https://www.reddit.com/search/?q=%s' },
  { key: 'a', name: 'Amazon', url: 'https://www.amazon.com/s?k=%s' },
  { key: 'm', name: 'Maps', url: 'https://www.google.com/maps/search/%s' },
  { key: 't', name: 'Translate', url: 'https://translate.google.com/?text=%s' },
]

// "!y lofi" or "y: lofi" -> YouTube search URL; null when no shortcut used.
export function resolveShortcut(query: string): string | null {
  const m = query.match(/^(?:!([a-z]{1,3})\s+(.+)|([a-z]{1,3}):\s*(.+))$/i)
  if (!m) return null
  const key = (m[1] || m[3]).toLowerCase()
  const q = (m[2] || m[4]).trim()
  const s = SEARCH_SHORTCUTS.find((x) => x.key === key)
  return s && q ? s.url.replace('%s', encodeURIComponent(q)) : null
}

// ------------------------------------------------------------ focus sounds

// Listener-supported internet radio (SomaFM) - ambient/lo-fi channels that
// work as background focus music. Streams are plain MP3 over https.
export const STATIONS: Array<{ id: string; name: string; url: string; note: string }> = [
  { id: 'groove', name: 'Groove Salad', url: 'https://ice1.somafm.com/groovesalad-128-mp3', note: 'ambient downtempo' },
  { id: 'drone', name: 'Drone Zone', url: 'https://ice1.somafm.com/dronezone-128-mp3', note: 'deep ambient' },
  { id: 'lush', name: 'Lush', url: 'https://ice1.somafm.com/lush-128-mp3', note: 'mellow vocals' },
  { id: 'deep', name: 'Deep Space One', url: 'https://ice1.somafm.com/deepspaceone-128-mp3', note: 'space ambient' },
  { id: 'fluid', name: 'Fluid', url: 'https://ice1.somafm.com/fluid-128-mp3', note: 'instrumental hip-hop' },
  { id: 'beat', name: 'Beat Blender', url: 'https://ice1.somafm.com/beatblender-128-mp3', note: 'deep house' },
]

const Sounds = styled.div`
  position: fixed;
  left: 18px;
  top: 16px;
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 12px;
  border-radius: 999px;
  background: rgba(15, 23, 42, 0.5);
  border: 1px solid rgba(255, 255, 255, 0.1);
  backdrop-filter: blur(14px);
  font-size: 13px;
  button {
    border: none; border-radius: 50%; width: 32px; height: 32px; cursor: pointer; font-size: 14px;
    background: rgba(56, 189, 248, 0.2); color: #e0f2fe;
  }
  button:hover { background: rgba(56, 189, 248, 0.35); }
  select { background: transparent; color: inherit; border: none; font-size: 13px; cursor: pointer; max-width: 150px; }
  select option { color: #000; }
  input[type='range'] { width: 70px; accent-color: #0ea5e9; }
  .s { font-size: 11px; opacity: 0.6; }
`

interface SoundsPersist {
  station: string
  volume: number
}

export function FocusSounds() {
  const [st, setSt] = React.useState<SoundsPersist>(() => {
    try { const raw = localStorage.getItem('falcon.sounds'); if (raw) return JSON.parse(raw) } catch { /* ignore */ }
    return { station: 'groove', volume: 0.5 }
  })
  const [playing, setPlaying] = React.useState(false)
  const [status, setStatus] = React.useState('')
  const audio = React.useRef<HTMLAudioElement | null>(null)

  const save = (next: SoundsPersist) => {
    setSt(next)
    try { localStorage.setItem('falcon.sounds', JSON.stringify(next)) } catch { /* ignore */ }
  }

  React.useEffect(() => {
    const a = new Audio()
    a.preload = 'none'
    a.onplaying = () => setStatus('')
    a.onwaiting = () => setStatus('buffering…')
    a.onerror = () => { setStatus('stream unavailable'); setPlaying(false) }
    audio.current = a
    return () => { a.pause(); a.src = '' }
  }, [])

  React.useEffect(() => {
    if (audio.current) audio.current.volume = st.volume
  }, [st.volume])

  const station = STATIONS.find((s) => s.id === st.station) ?? STATIONS[0]

  const toggle = () => {
    const a = audio.current
    if (!a) return
    if (playing) {
      a.pause(); a.src = ''; setPlaying(false); setStatus('')
      return
    }
    a.src = station.url
    a.volume = st.volume
    setStatus('connecting…')
    a.play().then(() => setPlaying(true)).catch(() => { setStatus('blocked — click again'); setPlaying(false) })
  }

  const change = (id: string) => {
    save({ ...st, station: id })
    if (playing && audio.current) {
      const s = STATIONS.find((x) => x.id === id)!
      audio.current.src = s.url
      audio.current.play().catch(() => setPlaying(false))
    }
  }

  return (
    <Sounds title="Focus sounds (SomaFM)">
      <button onClick={toggle} title={playing ? 'Pause' : 'Play'}>{playing ? '❚❚' : '▶'}</button>
      <div>
        <select value={st.station} onChange={(e) => change(e.target.value)}>
          {STATIONS.map((s) => <option key={s.id} value={s.id}>{s.name}</option>)}
        </select>
        <div className="s">{status || station.note}</div>
      </div>
      <input type="range" min={0} max={1} step={0.05} value={st.volume} onChange={(e) => save({ ...st, volume: +e.target.value })} title="Volume" />
    </Sounds>
  )
}
