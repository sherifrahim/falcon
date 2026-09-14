// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_MEDIA_VIDEO_PILL_SCRIPT_H_
#define BRAVE_BROWSER_FALCON_MEDIA_VIDEO_PILL_SCRIPT_H_

namespace falcon {

// Injected into the page's isolated world once media is spotted on a tab.
// Draws an IDM-style "Falcon" pill over every <video> while hovered; a click
// reports back through the console (the only channel an isolated-world
// script has) as  FALCON_DL:<nonce>:<currentSrc|page>. The nonce lives in
// the closure, so page scripts cannot forge the message.
// %NONCE% is replaced before injection.
inline constexpr char kVideoPillScript[] = R"JS((() => {
  if (window.__falconPill) return;
  window.__falconPill = true;
  const NONCE = '%NONCE%';
  const pills = new Map();
  const style = document.createElement('style');
  style.textContent = `
    .falcon-pill{position:fixed;z-index:2147483646;display:flex;align-items:center;gap:6px;
      padding:6px 12px;border-radius:999px;background:rgba(15,23,42,.88);color:#e0f2fe;
      font:600 12px/1 Inter,"Segoe UI",system-ui,sans-serif;border:1px solid rgba(56,189,248,.6);
      box-shadow:0 6px 24px rgba(0,0,0,.45);cursor:pointer;opacity:0;transition:opacity .15s;
      pointer-events:none;user-select:none}
    .falcon-pill.on{opacity:1;pointer-events:auto}
    .falcon-pill:hover{background:rgba(14,165,233,.95);color:#fff}`;
  (document.head || document.documentElement).appendChild(style);
  const place = (v, p) => {
    const r = v.getBoundingClientRect();
    if (r.width < 160 || r.height < 90) { p.classList.remove('on'); return; }
    p.style.left = Math.round(r.right - p.offsetWidth - 12) + 'px';
    p.style.top = Math.round(r.top + 12) + 'px';
  };
  const ensure = (v) => {
    if (pills.has(v)) return pills.get(v);
    const p = document.createElement('div');
    p.className = 'falcon-pill';
    p.textContent = '⬇ Download with Falcon';
    p.title = 'Download this video with Falcon';
    let hide;
    const show = () => { clearTimeout(hide); place(v, p); p.classList.add('on'); };
    const later = () => { clearTimeout(hide); hide = setTimeout(() => p.classList.remove('on'), 900); };
    v.addEventListener('mouseenter', show); v.addEventListener('mousemove', show); v.addEventListener('mouseleave', later);
    p.addEventListener('mouseenter', show); p.addEventListener('mouseleave', later);
    p.addEventListener('click', (e) => {
      e.preventDefault(); e.stopPropagation();
      const src = v.currentSrc || v.src || '';
      const target = /^https?:/i.test(src) ? src : 'page';
      console.log('FALCON_DL:' + NONCE + ':' + target);
      p.textContent = '✓ Sent to Falcon';
      setTimeout(() => { p.textContent = '⬇ Download with Falcon'; }, 2500);
    });
    document.documentElement.appendChild(p);
    pills.set(v, p);
    return p;
  };
  const scan = () => {
    document.querySelectorAll('video').forEach((v) => ensure(v));
    for (const [v, p] of pills) {
      if (!v.isConnected) { p.remove(); pills.delete(v); continue; }
      if (p.classList.contains('on')) place(v, p);
    }
  };
  scan();
  setInterval(scan, 2000);
  window.addEventListener('scroll', () => { for (const [v, p] of pills) if (p.classList.contains('on')) place(v, p); }, true);
})();)JS";

}  // namespace falcon

#endif  // BRAVE_BROWSER_FALCON_MEDIA_VIDEO_PILL_SCRIPT_H_
