// Copyright (c) 2022 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

import styled from 'styled-components'

export const Box = styled.div`
  position: fixed;
  inset: 0;
  overflow: hidden;
  background:
    radial-gradient(900px 520px at 50% -10%, rgba(56, 189, 248, 0.16), transparent 70%),
    linear-gradient(180deg, #0b1220 0%, #070b16 100%);
  color: #e2e8f0;

  .glow {
    position: absolute;
    left: 50%;
    top: -260px;
    width: 1100px;
    height: 620px;
    transform: translateX(-50%);
    background: radial-gradient(closest-side, rgba(56, 189, 248, 0.10), transparent);
    filter: blur(30px);
    pointer-events: none;
  }

  .stars {
    position: absolute;
    inset: 0;
    pointer-events: none;
    i {
      position: absolute;
      display: block;
      border-radius: 50%;
      background: #e2e8f0;
    }
  }

  .content-box {
    position: absolute;
    inset: 0;
    z-index: 2;
    display: flex;
    align-items: center;
    justify-content: center;
    overflow: auto;
  }
`
