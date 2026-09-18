// Copyright (c) 2022 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

// Falcon first run (mock v2, screen 8): one screen that explains the cockpit
// in three moves — Spaces, Navigate, Make it yours — then Get started. The
// import flow (Brave's select-browser → progress → done) is one click away
// and returns here through HelpImprove, which auto-finishes.

import * as React from 'react'
import * as S from './style'
import {
  WelcomeBrowserProxyImpl,
  DefaultBrowserBrowserProxyImpl,
  P3APhase,
} from '../../api/welcome_browser_proxy'
import DataContext from '../../state/context'
import { ViewType } from '../../state/component_types'

const BIRD =
  'M19.04 15.17 L18.36 15.80 L17.80 15.47 L17.46 14.39 Q17.13 12.66 15.27 12.29 Q12.20 11.57 8.03 11.28 L7.42 12.25 Q11.38 14.45 14.75 15.26 Q16.57 15.78 17.69 15.28 Z M14.47 12.23 Q12.73 8.61 12.41 2.29 L10.87 3.66 L9.96 2.25 L9.25 4.17 L7.75 3.09 Q10.09 7.53 12.55 11.51 Z M10.86 13.98 Q6.60 15.70 1.68 16.26 L2.42 14.83 L3.24 15.40 Q6.68 14.25 9.82 13.02 Z M8.03 11.28 L3.51 8.75 L3.88 10.33 L2.47 11.24 L3.83 11.63 L7.42 12.25 Z'

const Glyph = ({ d }: { d: React.ReactNode }) => (
  <svg viewBox="0 0 24 24" width="22" height="22" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
    {d}
  </svg>
)

const MOVES = [
  {
    key: 'spaces',
    kicker: 'Spaces',
    title: 'Tabs live in the dock',
    body: 'The dock on the left stays folded until you hover. Group tabs into spaces, and save the whole set as a session to bring back later.',
    keys: ['Ctrl', 'Shift', 'S'],
    hint: 'save a session',
    glyph: (
      <>
        <rect x="3" y="4" width="18" height="16" rx="3" />
        <path d="M8 4v16M12 9h6M12 13h4" />
      </>
    ),
  },
  {
    key: 'navigate',
    kicker: 'Navigate',
    title: 'One key, everything',
    body: 'The command deck searches tabs, commands, bookmarks and sessions at once. Shift‑click any link to peek at it without leaving the page.',
    keys: ['Ctrl', 'Space'],
    hint: 'open the deck',
    glyph: (
      <>
        <circle cx="11" cy="11" r="6.5" />
        <path d="M16 16l4.5 4.5" />
      </>
    ),
  },
  {
    key: 'yours',
    kicker: 'Make it yours',
    title: 'Shape the cockpit',
    body: 'Pick a window style — cockpit, Mac‑style or classic — switch to pitch black, and boost sites with your own CSS. Downloads run through Falcon’s own engine.',
    keys: ['Ctrl', 'J'],
    hint: 'downloads · falcon://falcon',
    glyph: (
      <>
        <path d="M4 7h10M18 7h2M4 12h3M11 12h9M4 17h12M20 17h0" />
        <circle cx="16" cy="7" r="2" />
        <circle cx="9" cy="12" r="2" />
        <circle cx="18" cy="17" r="2" />
      </>
    ),
  },
]

function Welcome () {
  const { setViewType, browserProfiles } = React.useContext(DataContext)
  const [busy, setBusy] = React.useState(false)
  const canImport = !!browserProfiles && browserProfiles.length > 0

  const finish = React.useCallback(async () => {
    if (busy) return
    setBusy(true)
    const proxy = WelcomeBrowserProxyImpl.getInstance()
    proxy.recordP3A(P3APhase.Finished)
    let url = ''
    try { url = await proxy.getWelcomeCompleteURL() } catch {}
    window.open(url || 'chrome://newtab', '_self', 'noopener')
  }, [busy])

  const setDefault = () => {
    DefaultBrowserBrowserProxyImpl.getInstance().setAsDefaultBrowser()
    finish()
  }

  const startImport = () => {
    WelcomeBrowserProxyImpl.getInstance().recordP3A(P3APhase.Import)
    setViewType(ViewType.ImportSelectBrowser)
  }

  React.useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Enter' && !(e.target instanceof HTMLButtonElement)) finish()
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [finish])

  return (
    <S.Box>
      <header className="hero">
        <svg className="bird" viewBox="0 0 24 24" aria-hidden="true"><path d={BIRD} /></svg>
        <h1>Welcome to Falcon</h1>
        <p className="lede">The cockpit, in three moves.</p>
      </header>

      <section className="moves">
        {MOVES.map((m, i) => (
          <article key={m.key} className="move" style={{ animationDelay: `${120 + i * 90}ms` }}>
            <div className="kicker"><Glyph d={m.glyph} /><span>{m.kicker}</span></div>
            <h2>{m.title}</h2>
            <p>{m.body}</p>
            <div className="keys">
              {m.keys.map((k, j) => (
                <React.Fragment key={k}>
                  {j > 0 && <span className="plus">+</span>}
                  <kbd>{k}</kbd>
                </React.Fragment>
              ))}
              <span className="hint">{m.hint}</span>
            </div>
          </article>
        ))}
      </section>

      <footer className="actions">
        <button className="primary" onClick={finish} disabled={busy} autoFocus>Get started</button>
        <button className="secondary" onClick={setDefault} disabled={busy}>Set as default browser</button>
        {canImport && (
          <button className="link" onClick={startImport} disabled={busy}>Import from another browser →</button>
        )}
      </footer>
      <p className="fine">Everything you saw here is in the control panel later: <span className="mono">falcon://falcon</span></p>
    </S.Box>
  )
}

export default Welcome
