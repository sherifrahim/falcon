// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

import * as React from 'react'
import styled, { createGlobalStyle, keyframes } from 'styled-components'
import { sendWithPromise } from 'chrome://resources/js/cr.js'
import { loadTimeData } from '$web-common/loadTimeData'
import {
  DEFAULT_STATE, GRADIENTS, NtpState, QuickLink, faviconUrl, greeting, looksLikeUrl,
  quoteOfTheDay, withDefaults,
} from './state'
import { SettingsPanel } from './settings'
import { FocusSounds, FocusTimer, Weather, resolveShortcut } from './widgets'

const Global = createGlobalStyle`
  ::selection { background: rgba(56,189,248,0.35); }
  a { color: inherit; text-decoration: none; }
`

const drift = keyframes`
  0% { background-position: 0% 50%; }
  50% { background-position: 100% 50%; }
  100% { background-position: 0% 50%; }
`

const fadeIn = keyframes`
  from { opacity: 0; transform: translateY(6px); }
  to { opacity: 1; transform: none; }
`

const Backdrop = styled.div<{ $css: string; $blur: number; $drift: boolean; $image: boolean }>`
  position: fixed;
  inset: ${(p) => (p.$blur > 0 ? `-${p.$blur * 2}px` : 0)};
  background: ${(p) => p.$css};
  background-size: ${(p) => (p.$image ? 'cover' : '200% 200%')};
  background-position: center;
  filter: ${(p) => (p.$blur > 0 ? `blur(${p.$blur}px)` : 'none')};
  animation: ${(p) => (p.$drift && !p.$image ? drift : 'none')} 40s ease-in-out infinite;
  transition: background 0.6s ease;
`

const Video = styled.video<{ $blur: number }>`
  position: fixed;
  inset: ${(p) => (p.$blur > 0 ? `-${p.$blur * 2}px` : 0)};
  width: 100%;
  height: 100%;
  object-fit: cover;
  filter: ${(p) => (p.$blur > 0 ? `blur(${p.$blur}px)` : 'none')};
  background: var(--f-bg-0, #0b1220);
`

const Dim = styled.div<{ $dim: number }>`
  position: fixed;
  inset: 0;
  background: rgba(2, 6, 23, ${(p) => p.$dim / 100});
  pointer-events: none;
`

const Hint = styled.div`
  position: fixed; top: 14px; left: 50%; transform: translateX(-50%); z-index: 6; white-space: nowrap; max-width: 92vw; overflow: hidden;
  display: flex; align-items: center; gap: 12px; padding: 8px 12px 8px 14px;
  border-radius: 999px; font-size: 12px; color: #e2e8f0;
  background: rgba(11, 18, 32, 0.72); border: 1px solid rgba(56, 189, 248, 0.35);
  backdrop-filter: blur(10px); box-shadow: 0 8px 24px rgba(0,0,0,0.35);
  kbd { font: inherit; padding: 1px 5px; border-radius: 5px; background: rgba(255,255,255,0.1); border: 1px solid rgba(255,255,255,0.15); }
  button { border: none; background: transparent; color: inherit; cursor: pointer; font-size: 13px; opacity: 0.7; }
  button:hover { opacity: 1; }
`

const Page = styled.div`
  position: relative;
  min-height: 100%;
  display: grid;
  grid-template-rows: 1fr auto 1fr;
  justify-items: center;
  padding: 24px;
`

const Center = styled.div`
  grid-row: 2;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 22px;
  width: min(760px, 100%);
  animation: ${fadeIn} 0.5s ease both;
`

const Clock = styled.div`
  font-family: "Segoe UI Variable Display", "Segoe UI Light", Inter, system-ui, sans-serif;
  font-size: clamp(72px, 12vw, 132px);
  font-weight: 200;
  letter-spacing: -0.02em;
  line-height: 1;
  text-shadow: 0 8px 40px rgba(0, 0, 0, 0.45);
  font-variant-numeric: tabular-nums;
  span.ampm { font-size: 0.22em; font-weight: 300; margin-left: 0.25em; opacity: 0.75; letter-spacing: 0.06em; }
`

const Date_ = styled.div`
  font-size: 13px;
  font-weight: 500;
  letter-spacing: 0.14em;
  text-transform: uppercase;
  opacity: 0.8;
  margin-top: -6px;
  text-shadow: 0 2px 12px rgba(0, 0, 0, 0.35);
`

const Greeting = styled.div`
  font-family: Georgia, "Times New Roman", "Noto Serif", serif;
  font-size: 30px;
  font-weight: 400;
  font-style: italic;
  letter-spacing: 0.01em;
  text-shadow: 0 2px 14px rgba(0, 0, 0, 0.4);
`

const Search = styled.form`
  width: 100%;
  position: relative;
  input {
    width: 100%;
    padding: 13px 48px 13px 46px;
    border-radius: 999px;
    border: 1px solid rgba(255, 255, 255, 0.16);
    background: rgba(8, 12, 24, 0.42);
    backdrop-filter: blur(16px);
    color: #f8fafc;
    font-size: 15px;
    outline: none;
    transition: border-color 0.2s, box-shadow 0.2s, background 0.2s;
  }
  input::placeholder { color: rgba(226, 232, 240, 0.5); }
  input:focus {
    border-color: rgba(56, 189, 248, 0.75);
    box-shadow: 0 0 0 3px rgba(56, 189, 248, 0.14), 0 0 24px rgba(56, 189, 248, 0.18);
    background: rgba(8, 12, 24, 0.6);
  }
  &::before {
    content: ''; position: absolute; left: 18px; top: 50%; width: 14px; height: 14px; transform: translateY(-58%);
    border: 1.5px solid rgba(226, 232, 240, 0.7); border-radius: 50%; box-sizing: border-box;
    box-shadow: 5px 5px 0 -3px rgba(226, 232, 240, 0.7);
  }
  button {
    position: absolute;
    right: 6px;
    top: 50%;
    transform: translateY(-50%);
    width: 36px; height: 36px;
    border-radius: 50%;
    border: none;
    background: transparent;
    color: rgba(226, 232, 240, 0.75);
    cursor: pointer;
    font-size: 16px;
  }
  button:hover { color: #38bdf8; }
`

const Links = styled.div<{ $cols: number }>`
  display: grid;
  grid-template-columns: repeat(${(p) => p.$cols}, 64px);
  gap: 14px;
  justify-content: center;
`

const Tile = styled.a`
  position: relative;
  color: #f1f5f9;
  text-decoration: none;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 7px;
  padding: 0;
  cursor: pointer;
  img {
    width: 48px; height: 48px; padding: 12px; box-sizing: border-box; border-radius: 14px;
    background: rgba(8, 12, 24, 0.55); border: 1px solid rgba(255, 255, 255, 0.1);
    backdrop-filter: blur(14px);
    transition: transform 0.15s, border-color 0.15s, box-shadow 0.15s;
  }
  &:hover img { transform: translateY(-2px); border-color: rgba(56, 189, 248, 0.55); box-shadow: 0 0 18px rgba(56, 189, 248, 0.2); }
  span { font-size: 11px; max-width: 64px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; opacity: 0.8; letter-spacing: 0.01em; }
  .x {
    position: absolute; top: 4px; right: 4px; width: 18px; height: 18px; border-radius: 50%;
    background: rgba(2, 6, 23, 0.7); color: #fca5a5; font-size: 11px; line-height: 18px; text-align: center;
    opacity: 0; transition: opacity 0.15s;
  }
  &:hover .x { opacity: 1; }
  .x:hover { background: #7f1d1d; color: #fff; }
`

const AddTile = styled.button`
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 7px;
  padding: 0;
  background: transparent;
  border: none;
  color: rgba(226, 232, 240, 0.8);
  cursor: pointer;
  font-size: 20px;
  &::before { content: '+'; display: grid; place-items: center; width: 48px; height: 48px; border-radius: 14px; border: 1px dashed rgba(255, 255, 255, 0.28); background: rgba(8, 12, 24, 0.35); }
  &:hover::before { border-color: rgba(56, 189, 248, 0.6); }
  small { font-size: 11px; opacity: 0.8; }
`

const Quote = styled.div`
  grid-row: 3;
  align-self: end;
  text-align: center;
  font-family: Georgia, "Times New Roman", "Noto Serif", serif;
  font-style: italic;
  font-size: 15px;
  opacity: 0.8;
  text-shadow: 0 2px 10px rgba(0, 0, 0, 0.45);
  max-width: 640px;
  em { font-style: normal; font-family: Inter, "Segoe UI", system-ui, sans-serif; font-size: 12px; opacity: 0.7; letter-spacing: 0.06em; }
`

const Corner = styled.div`
  position: fixed;
  left: 18px;
  bottom: 16px;
  display: flex;
  gap: 8px;
  align-items: center;
  opacity: 0.55;
  transition: opacity 0.2s;
  &:hover { opacity: 1; }
`

const IconBtn = styled.button`
  width: 34px; height: 34px;
  border-radius: 50%;
  border: 1px solid rgba(255, 255, 255, 0.12);
  background: rgba(8, 12, 24, 0.5);
  backdrop-filter: blur(14px);
  color: #e2e8f0;
  font-size: 15px;
  cursor: pointer;
  transition: transform 0.15s;
  &:hover { transform: rotate(20deg); }
`

const Credit = styled.a`
  color: #e2e8f0;
  text-decoration: none;
  font-size: 11px;
  opacity: 0.55;
  max-width: 360px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  &:hover { opacity: 0.9; text-decoration: underline; }
`

const Dialog = styled.div`
  position: fixed;
  inset: 0;
  display: grid;
  place-items: center;
  background: rgba(2, 6, 23, 0.55);
  z-index: 30;
  form {
    width: min(420px, 92vw);
    padding: 22px;
    border-radius: 18px;
    background: rgba(15, 23, 42, 0.92);
    border: 1px solid rgba(255, 255, 255, 0.1);
    display: grid;
    gap: 10px;
  }
  h3 { margin: 0 0 4px; font-size: 16px; font-weight: 600; }
  input {
    padding: 10px 12px; border-radius: 10px; border: 1px solid var(--f-border, #334155); background: var(--f-bg-3, #1e293b); color: inherit; font-size: 14px; outline: none;
  }
  input:focus { border-color: #38bdf8; }
  .row { display: flex; gap: 8px; justify-content: flex-end; }
  button {
    padding: 8px 14px; border-radius: 10px; border: 1px solid transparent; background: var(--f-bg-3, #1e293b); color: inherit; cursor: pointer; font-weight: 600;
  }
  button.primary { background: #0ea5e9; color: #fff; }
`

function useClock(showSeconds: boolean) {
  const [now, setNow] = React.useState(() => new Date())
  React.useEffect(() => {
    const tick = () => setNow(new Date())
    const id = window.setInterval(tick, showSeconds ? 1000 : 5000)
    return () => window.clearInterval(id)
  }, [showSeconds])
  return now
}

export function App() {
  const [state, setState] = React.useState<NtpState>(DEFAULT_STATE)
  const [loaded, setLoaded] = React.useState(false)
  const [settingsOpen, setSettingsOpen] = React.useState(false)
  // Cockpit hint: the chrome is hidden, so say how to summon it (until dismissed).
  const [hint, setHint] = React.useState(() => {
    try { return loadTimeData.getInteger('shellMode') === 2 && !localStorage.getItem('falcon.hint.cockpit') } catch { return false }
  })
  const dismissHint = () => { setHint(false); try { localStorage.setItem('falcon.hint.cockpit', '1') } catch {} }
  const [bing, setBing] = React.useState<{ url: string; title?: string; copyright?: string; link?: string } | null>(null)
  const [editing, setEditing] = React.useState<{ index: number; link: QuickLink } | null>(null)
  const [dragFrom, setDragFrom] = React.useState<number | null>(null)
  const [dragOver, setDragOver] = React.useState<number | null>(null)
  const [query, setQuery] = React.useState('')
  const searchRef = React.useRef<HTMLInputElement>(null)
  const now = useClock(state.showSeconds)

  // Load + persist -----------------------------------------------------------
  React.useEffect(() => {
    sendWithPromise('falcon_newtab.getState').then((stored: Partial<NtpState>) => {
      const s = withDefaults(stored)
      setState(s)
      setLoaded(true)
      if (!s.linksSeededFromHistory && s.links.length === 0) {
        sendWithPromise('falcon_newtab.getTopSites').then((sites: QuickLink[]) => {
          const next = { ...s, links: (sites ?? []).slice(0, 8), linksSeededFromHistory: true }
          setState(next)
          chrome.send('falcon_newtab.setState', [next])
        }).catch(() => {})
      }
    }).catch(() => setLoaded(true))
  }, [])

  const update = React.useCallback((patch: Partial<NtpState>) => {
    setState((prev) => {
      const next = { ...prev, ...patch }
      chrome.send('falcon_newtab.setState', [next])
      return next
    })
  }, [])

  React.useEffect(() => {
    if (state.bg.mode !== 'bing') return
    sendWithPromise('falcon_newtab.getBingImage').then((r: any) => { if (r && r.url) setBing(r) }).catch(() => {})
  }, [state.bg.mode])

  // "/" focuses the page search (the omnibox keeps focus by default).
  React.useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key === '/' && document.activeElement !== searchRef.current && !settingsOpen && !editing) {
        e.preventDefault()
        searchRef.current?.focus()
      }
      if (e.key === 'Escape') { setSettingsOpen(false); setEditing(null) }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [settingsOpen, editing])

  // Background ---------------------------------------------------------------
  const bg = state.bg
  let css = GRADIENTS[bg.gradient % GRADIENTS.length].css
  let isImage = false
  if (bg.mode === 'solid') css = bg.color || 'var(--f-bg-0, #0b1220)'
  else if (bg.mode === 'image' && bg.imageUrl) { css = `url("${bg.imageUrl.replace(/"/g, '')}") center / cover no-repeat var(--f-bg-0, #0b1220)`; isImage = true }
  else if (bg.mode === 'bing' && bing) { css = `url("${bing.url}") center / cover no-repeat var(--f-bg-0, #0b1220)`; isImage = true }
  const isVideo = bg.mode === 'video' && !!bg.videoUrl
  if (isVideo) css = 'var(--f-bg-0, #0b1220)'

  // Clock/date ---------------------------------------------------------------
  const h = now.getHours()
  const hh = state.clock24 ? String(h).padStart(2, '0') : String(h % 12 || 12)
  const mm = String(now.getMinutes()).padStart(2, '0')
  const ss = String(now.getSeconds()).padStart(2, '0')
  const dateText = now.toLocaleDateString(undefined, { weekday: 'long', month: 'long', day: 'numeric' })

  const submit = (e: React.FormEvent) => {
    e.preventDefault()
    const q = query.trim()
    if (!q) return
    const shortcut = resolveShortcut(q)
    const url = shortcut || looksLikeUrl(q)
    if (url) chrome.send('falcon_newtab.open', [url, false])
    else chrome.send('falcon_newtab.search', [q])
  }

  const openLink = (e: React.MouseEvent, url: string) => {
    e.preventDefault()
    chrome.send('falcon_newtab.open', [url, e.ctrlKey || e.metaKey || e.button === 1])
  }

  const removeLink = (i: number) => update({ links: state.links.filter((_, j) => j !== i) })
  const moveLink = (from: number, to: number) => {
    if (from === to || from < 0 || to < 0) return
    const links = state.links.slice()
    const [item] = links.splice(from, 1)
    links.splice(to, 0, item)
    update({ links })
  }
  const saveLink = (link: QuickLink) => {
    if (!editing) return
    const url = looksLikeUrl(link.url) || link.url
    const links = state.links.slice()
    const clean = { title: link.title.trim() || new URL(url).hostname.replace(/^www\./, ''), url }
    if (editing.index < 0) links.push(clean)
    else links[editing.index] = clean
    update({ links })
    setEditing(null)
  }

  const cols = Math.min(Math.max(state.links.length + 1, 4), 8)
  const [quote, author] = quoteOfTheDay()

  return (
    <>
      <Global />
      <Backdrop $css={css} $blur={bg.blur} $drift={bg.drift} $image={isImage} />
      {isVideo && <Video $blur={bg.blur} src={bg.videoUrl} autoPlay muted loop playsInline />}
      <Dim $dim={bg.dim} />
      {hint && (
        <Hint>
          <span><b>Cockpit mode</b> · move the mouse to the top edge or press <kbd>Ctrl</kbd>+<kbd>L</kbd> for the address bar · <kbd>Ctrl</kbd>+<kbd>Space</kbd> quick commands · <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>F</kbd> shows the toolbar</span>
          <button onClick={dismissHint} title="Got it">✕</button>
        </Hint>
      )}
      {loaded && <Weather settings={state.weather} />}
      {loaded && state.showFocus && <FocusTimer />}
      {loaded && state.showSounds && <FocusSounds />}
      <Page>
        {loaded && (
          <Center>
            {state.showClock && (
              <Clock>
                {hh}:{mm}{state.showSeconds ? `:${ss}` : ''}
                {!state.clock24 && <span className="ampm">{h < 12 ? 'AM' : 'PM'}</span>}
              </Clock>
            )}
            {state.showDate && <Date_>{dateText}</Date_>}
            {state.showGreeting && <Greeting>{greeting(state.name)}</Greeting>}
            {state.showSearch && (
              <Search onSubmit={submit}>
                <input ref={searchRef} value={query} onChange={(e) => setQuery(e.target.value)}
                  placeholder="Search or type a URL  ·  / to focus  ·  !y !w !gh shortcuts" spellCheck={false} autoComplete="off" />
                <button type="submit" title="Search">→</button>
              </Search>
            )}
            {state.showLinks && (
              <Links $cols={cols}>
                {state.links.map((l, i) => (
                  <Tile key={`${l.url}-${i}`} href={l.url} onClick={(e) => openLink(e, l.url)}
                    onAuxClick={(e) => e.button === 1 && openLink(e, l.url)}
                    onContextMenu={(e) => { e.preventDefault(); setEditing({ index: i, link: l }) }}
                    draggable
                    onDragStart={(e) => { setDragFrom(i); e.dataTransfer.effectAllowed = 'move' }}
                    onDragOver={(e) => { e.preventDefault(); if (dragOver !== i) setDragOver(i) }}
                    onDragLeave={() => setDragOver(null)}
                    onDrop={(e) => { e.preventDefault(); if (dragFrom !== null) moveLink(dragFrom, i); setDragFrom(null); setDragOver(null) }}
                    onDragEnd={() => { setDragFrom(null); setDragOver(null) }}
                    style={dragOver === i && dragFrom !== null && dragFrom !== i ? { borderColor: '#38bdf8', transform: 'scale(1.06)' } : undefined}
                    title={`${l.url}\nRight-click to edit · drag to reorder`}>
                    <img src={faviconUrl(l.url)} alt="" />
                    <span>{l.title || l.url}</span>
                    <div className="x" onClick={(e) => { e.preventDefault(); e.stopPropagation(); removeLink(i) }} title="Remove">✕</div>
                  </Tile>
                ))}
                {state.links.length < 40 && (
                  <AddTile onClick={() => setEditing({ index: -1, link: { title: '', url: '' } })} title="Add a shortcut">
                    <small>add</small>
                  </AddTile>
                )}
              </Links>
            )}
          </Center>
        )}
        {loaded && state.showQuote && (
          <Quote>“{quote}” <em>— {author}</em></Quote>
        )}
      </Page>
      <Corner>
        {bg.mode === 'bing' && bing?.copyright && (
          <Credit href={bing.link || '#'} onClick={(e) => { e.preventDefault(); if (bing.link) chrome.send('falcon_newtab.open', [bing.link, true]) }} title={bing.copyright}>
            {bing.copyright}
          </Credit>
        )}
        <IconBtn onClick={() => setSettingsOpen((v) => !v)} title="Customize new tab">⚙</IconBtn>
      </Corner>
      {settingsOpen && <SettingsPanel state={state} update={update} onClose={() => setSettingsOpen(false)} />}
      {editing && (
        <Dialog onClick={() => setEditing(null)}>
          <form onClick={(e) => e.stopPropagation()} onSubmit={(e) => { e.preventDefault(); const f = e.currentTarget; saveLink({ title: (f.elements.namedItem('t') as HTMLInputElement).value, url: (f.elements.namedItem('u') as HTMLInputElement).value }) }}>
            <h3>{editing.index < 0 ? 'Add shortcut' : 'Edit shortcut'}</h3>
            <input name="t" defaultValue={editing.link.title} placeholder="Name (optional)" autoFocus={editing.index >= 0} />
            <input name="u" defaultValue={editing.link.url} placeholder="URL, e.g. github.com" autoFocus={editing.index < 0} required />
            <div className="row">
              <button type="button" onClick={() => setEditing(null)}>Cancel</button>
              <button type="submit" className="primary">Save</button>
            </div>
          </form>
        </Dialog>
      )}
    </>
  )
}
