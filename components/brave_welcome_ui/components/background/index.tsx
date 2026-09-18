// Copyright (c) 2022 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

// Falcon: the welcome backdrop is the cockpit surface (deep navy, one soft
// sky glow, a faint star field) instead of Brave's animated hills. The
// interface is unchanged so the import steps keep working: `onLoad` fires
// once we are mounted and `scenes` are no-ops.

import * as React from 'react'
import * as S from './style'
import WebAnimationPlayer from '../../api/web_animation_player'
import DataContext from '../../state/context'

interface BackgroundProps {
  children?: JSX.Element
  static: boolean
  onLoad?: () => void
}

// Deterministic star positions so the field does not shimmer between renders.
const STARS = Array.from({ length: 70 }, (_, i) => {
  const a = Math.sin(i * 12.9898) * 43758.5453
  const b = Math.sin(i * 78.233) * 43758.5453
  const x = a - Math.floor(a)
  const y = b - Math.floor(b)
  return { x: x * 100, y: y * 62, s: 1 + ((i * 7) % 3) * 0.5, o: 0.25 + ((i * 13) % 5) * 0.12 }
})

function Background (props: BackgroundProps) {
  const { setScenes } = React.useContext(DataContext)

  React.useEffect(() => {
    // The import steps call scenes?.s1.play() / s2.play(); give them players
    // with nothing scheduled so those calls stay harmless.
    setScenes({ s1: new WebAnimationPlayer(), s2: new WebAnimationPlayer() })
    props.onLoad?.()
  }, [])

  return (
    <S.Box>
      <div className="glow" />
      <div className="stars" aria-hidden="true">
        {STARS.map((st, i) => (
          <i key={i} style={{ left: `${st.x}%`, top: `${st.y}%`, width: st.s, height: st.s, opacity: st.o }} />
        ))}
      </div>
      <div className="content-box">
        {props.children}
      </div>
    </S.Box>
  )
}

export default Background
