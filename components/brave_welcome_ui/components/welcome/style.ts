// Copyright (c) 2022 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

import styled, { keyframes } from 'styled-components'

const rise = keyframes`
  from { opacity: 0; transform: translateY(14px); }
  to { opacity: 1; transform: translateY(0); }
`

export const Box = styled.div`
  --ink: #e2e8f0;
  --dim: rgba(226, 232, 240, 0.62);
  --hair: rgba(255, 255, 255, 0.12);
  --accent: #38bdf8;
  width: min(1040px, calc(100vw - 64px));
  padding: 40px 0 32px;
  color: var(--ink);
  font-family: Inter, "Segoe UI Variable Text", "Segoe UI", system-ui, sans-serif;

  .hero {
    text-align: center;
    animation: ${rise} 320ms ease-out both;
    .bird {
      width: 52px;
      height: 52px;
      fill: var(--accent);
      filter: drop-shadow(0 0 22px rgba(56, 189, 248, 0.45));
    }
    h1 {
      margin: 14px 0 6px;
      font-size: 40px;
      font-weight: 600;
      letter-spacing: -0.02em;
    }
    .lede {
      margin: 0;
      font-family: Georgia, "Times New Roman", serif;
      font-style: italic;
      font-size: 20px;
      color: var(--dim);
    }
  }

  .moves {
    display: grid;
    grid-template-columns: repeat(3, minmax(0, 1fr));
    gap: 16px;
    margin: 44px 0 36px;
    @media (max-width: 820px) { grid-template-columns: 1fr; }
  }

  .move {
    position: relative;
    padding: 22px 22px 18px;
    border-radius: 14px;
    background: rgba(255, 255, 255, 0.04);
    border: 1px solid var(--hair);
    box-shadow: 0 20px 50px rgba(0, 0, 0, 0.35);
    animation: ${rise} 360ms ease-out both;
    transition: border-color 150ms ease, background 150ms ease, transform 200ms ease;
    &:hover {
      border-color: rgba(56, 189, 248, 0.45);
      background: rgba(56, 189, 248, 0.06);
      transform: translateY(-2px);
    }
    .kicker {
      display: flex;
      align-items: center;
      gap: 10px;
      color: var(--accent);
      font-size: 11px;
      letter-spacing: 0.14em;
      text-transform: uppercase;
      /* reset.css paints svg fills; these are 1.5 px stroke glyphs */
      svg { width: 22px; height: 22px; fill: none; stroke: currentColor; }
    }
    h2 {
      margin: 14px 0 8px;
      font-size: 19px;
      font-weight: 600;
      letter-spacing: -0.01em;
    }
    p {
      margin: 0 0 18px;
      font-size: 13.5px;
      line-height: 1.55;
      color: var(--dim);
    }
    .keys {
      display: flex;
      align-items: center;
      gap: 6px;
      flex-wrap: wrap;
      font-family: "Cascadia Mono", Consolas, "JetBrains Mono", monospace;
      font-size: 11px;
      .plus { color: var(--dim); }
      kbd {
        font: inherit;
        padding: 3px 8px;
        border-radius: 6px;
        background: rgba(255, 255, 255, 0.08);
        border: 1px solid rgba(255, 255, 255, 0.16);
        box-shadow: inset 0 -1px 0 rgba(0, 0, 0, 0.4);
      }
      .hint { margin-left: 6px; color: var(--dim); }
    }
  }

  .actions {
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 12px;
    flex-wrap: wrap;
    animation: ${rise} 360ms 420ms ease-out both;
    button {
      font: inherit;
      font-size: 14px;
      font-weight: 600;
      padding: 11px 22px;
      border-radius: 999px;
      border: 1px solid transparent;
      cursor: pointer;
      transition: background 150ms ease, border-color 150ms ease, box-shadow 150ms ease;
      &:disabled { opacity: 0.6; cursor: default; }
      &:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
    }
    .primary {
      background: var(--accent);
      color: #06131f;
      box-shadow: 0 0 0 1px rgba(56, 189, 248, 0.3), 0 10px 30px rgba(56, 189, 248, 0.28);
      &:hover:not(:disabled) { background: #7dd3fc; }
    }
    .secondary {
      background: rgba(255, 255, 255, 0.06);
      color: var(--ink);
      border-color: var(--hair);
      &:hover:not(:disabled) { background: rgba(255, 255, 255, 0.1); }
    }
    .link {
      background: transparent;
      color: var(--dim);
      font-weight: 500;
      padding: 11px 10px;
      &:hover:not(:disabled) { color: var(--ink); }
    }
  }

  .fine {
    margin: 22px 0 0;
    text-align: center;
    font-size: 12px;
    color: rgba(226, 232, 240, 0.42);
    animation: ${rise} 360ms 520ms ease-out both;
    .mono {
      font-family: "Cascadia Mono", Consolas, "JetBrains Mono", monospace;
      color: rgba(125, 211, 252, 0.8);
    }
  }
`
