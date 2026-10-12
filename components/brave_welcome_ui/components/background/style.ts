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
    radial-gradient(60% 55% at 42% 62%, rgba(69, 42, 62, 0.95), transparent 70%),
    radial-gradient(70% 60% at 100% 0%, rgba(49, 36, 53, 0.85), transparent 70%),
    linear-gradient(160deg, #252434 0%, #1a1829 100%);
  color: #f1effa;

  .glow {
    position: absolute;
    left: 50%;
    top: -260px;
    width: 1100px;
    height: 620px;
    transform: translateX(-50%);
    background: radial-gradient(closest-side, rgba(142, 155, 255, 0.10), transparent);
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
      background: #f1effa;
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
