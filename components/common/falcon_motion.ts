// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

// One motion vocabulary for every Falcon surface. Arc is the reference: motion
// is there to explain where a thing came from, never to be noticed. Anything
// that would read as decoration belongs in a still frame instead.

import { css, keyframes } from 'styled-components'

// Durations. Hover and press have to feel instant — past ~120ms a pointer
// interaction reads as lag rather than feedback. Surfaces that travel a real
// distance get longer, because a fast large movement reads as a jump cut.
export const dur = {
  fast: '110ms', // hover, press, selection
  base: '180ms', // rows, chips, popovers
  slow: '260ms', // panels, sheets, overlays
}

export const ease = {
  // Entering: decelerate hard so the surface settles rather than arrives.
  out: 'cubic-bezier(0.16, 1, 0.3, 1)',
  // Leaving: accelerate — get out of the way, don't linger.
  in: 'cubic-bezier(0.4, 0, 1, 1)',
  // Staying: for state that changes in place (colour, size, position).
  move: 'cubic-bezier(0.2, 0, 0, 1)',
}

export const reduced = '@media (prefers-reduced-motion: reduce)'

const riseIn = keyframes`
  from { opacity: 0; transform: translateY(8px) scale(0.985); }
  to { opacity: 1; transform: none; }
`

const fade = keyframes`
  from { opacity: 0; }
  to { opacity: 1; }
`

// A surface arriving on screen: rises a little and settles. Under reduced
// motion it still appears, just without travel — the frame must never be
// missing, only un-animated.
export const enters = css`
  animation: ${riseIn} ${dur.slow} ${ease.out} both;
  ${reduced} {
    animation: ${fade} ${dur.fast} linear both;
  }
`

// Staggered arrival for a list of peers. Keep `n` small; past ~6 the last item
// arrives late enough to feel broken.
export const entersAt = (n: number) => css`
  ${enters}
  animation-delay: ${Math.min(n, 6) * 28}ms;
`

// State that changes in place. Name the properties — a bare `transition: all`
// animates layout as well, which is where jank comes from.
export const shifts = (props = 'background-color, border-color, color') => css`
  transition: ${props} ${dur.fast} ${ease.move};
  ${reduced} {
    transition-duration: 1ms;
  }
`

// A control the pointer can press. The lift is small on purpose: this is
// feedback, not a flourish.
export const presses = css`
  transition: transform ${dur.fast} ${ease.move},
    background-color ${dur.fast} ${ease.move},
    border-color ${dur.fast} ${ease.move},
    box-shadow ${dur.fast} ${ease.move};
  &:active {
    transform: scale(0.97);
  }
  ${reduced} {
    transition-duration: 1ms;
    &:active {
      transform: none;
    }
  }
`

// Keyboard focus has to stay visible everywhere; surfaces that restyle their
// controls must not drop it.
export const focusRing = css`
  &:focus-visible {
    outline: 2px solid #38bdf8;
    outline-offset: 2px;
  }
`
