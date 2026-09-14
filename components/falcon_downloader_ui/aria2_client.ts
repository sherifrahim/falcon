// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

// Minimal aria2 JSON-RPC over WebSocket client with auto-reconnect.

export type Aria2Status =
  | 'active'
  | 'waiting'
  | 'paused'
  | 'error'
  | 'complete'
  | 'removed'

export interface Aria2File {
  index: string
  path: string
  length: string
  completedLength: string
  selected: string
  uris: Array<{ uri: string; status: string }>
}

export interface Aria2Download {
  gid: string
  status: Aria2Status
  totalLength: string
  completedLength: string
  uploadLength: string
  downloadSpeed: string
  uploadSpeed: string
  connections: string
  numSeeders?: string
  errorCode?: string
  errorMessage?: string
  dir: string
  files: Aria2File[]
  bittorrent?: { info?: { name?: string } }
  infoHash?: string
}

export interface Aria2GlobalStat {
  downloadSpeed: string
  uploadSpeed: string
  numActive: string
  numWaiting: string
  numStopped: string
}

type Pending = {
  resolve: (v: any) => void
  reject: (e: Error) => void
}

export class Aria2Client {
  private ws: WebSocket | null = null
  private nextId = 1
  private pending = new Map<string, Pending>()
  private reconnectTimer: number | null = null
  private closed = false

  onConnectionChange: (connected: boolean) => void = () => {}
  onNotification: (method: string, gid: string) => void = () => {}

  constructor(
    private readonly url: string,
    private readonly secret: string,
  ) {}

  connect() {
    this.closed = false
    this.open()
  }

  close() {
    this.closed = true
    if (this.reconnectTimer !== null) {
      window.clearTimeout(this.reconnectTimer)
      this.reconnectTimer = null
    }
    this.ws?.close()
    this.ws = null
  }

  get connected() {
    return this.ws !== null && this.ws.readyState === WebSocket.OPEN
  }

  private open() {
    let ws: WebSocket
    try {
      ws = new WebSocket(this.url)
    } catch (e) {
      this.scheduleReconnect()
      return
    }
    this.ws = ws
    ws.onopen = () => this.onConnectionChange(true)
    ws.onclose = () => {
      this.onConnectionChange(false)
      this.failAll(new Error('disconnected'))
      if (this.ws === ws) this.ws = null
      this.scheduleReconnect()
    }
    ws.onerror = () => {
      /* onclose follows */
    }
    ws.onmessage = (ev) => this.handleMessage(ev.data)
  }

  private scheduleReconnect() {
    if (this.closed || this.reconnectTimer !== null) return
    this.reconnectTimer = window.setTimeout(() => {
      this.reconnectTimer = null
      this.open()
    }, 1000)
  }

  private failAll(err: Error) {
    for (const p of this.pending.values()) p.reject(err)
    this.pending.clear()
  }

  private handleMessage(raw: string) {
    let msg: any
    try {
      msg = JSON.parse(raw)
    } catch {
      return
    }
    const messages = Array.isArray(msg) ? msg : [msg]
    for (const m of messages) {
      if (m.id !== undefined && m.id !== null) {
        const p = this.pending.get(String(m.id))
        if (!p) continue
        this.pending.delete(String(m.id))
        if (m.error) p.reject(new Error(m.error.message ?? 'rpc error'))
        else p.resolve(m.result)
      } else if (typeof m.method === 'string') {
        const gid = m.params?.[0]?.gid ?? ''
        this.onNotification(m.method, gid)
      }
    }
  }

  call<T = any>(method: string, ...params: any[]): Promise<T> {
    if (!this.connected) return Promise.reject(new Error('not connected'))
    const id = String(this.nextId++)
    const payload = {
      jsonrpc: '2.0',
      id,
      method,
      params: [`token:${this.secret}`, ...params],
    }
    return new Promise<T>((resolve, reject) => {
      this.pending.set(id, { resolve, reject })
      this.ws!.send(JSON.stringify(payload))
    })
  }

  // Convenience wrappers ---------------------------------------------------

  addUri(uris: string[], options: Record<string, string> = {}) {
    return this.call<string>('aria2.addUri', uris, options)
  }

  addTorrent(base64: string, options: Record<string, string> = {}) {
    return this.call<string>('aria2.addTorrent', base64, [], options)
  }

  pause(gid: string) {
    return this.call('aria2.pause', gid)
  }

  unpause(gid: string) {
    return this.call('aria2.unpause', gid)
  }

  remove(gid: string) {
    return this.call('aria2.forceRemove', gid)
  }

  removeResult(gid: string) {
    return this.call('aria2.removeDownloadResult', gid)
  }

  purge() {
    return this.call('aria2.purgeDownloadResult')
  }

  setGlobalSpeedLimit(bytesPerSec: number) {
    return this.call('aria2.changeGlobalOption', {
      'max-overall-download-limit': String(bytesPerSec),
    })
  }

  async snapshot(): Promise<{
    downloads: Aria2Download[]
    stat: Aria2GlobalStat
  }> {
    const keys = [
      'gid', 'status', 'totalLength', 'completedLength', 'uploadLength',
      'downloadSpeed', 'uploadSpeed', 'connections', 'numSeeders',
      'errorCode', 'errorMessage', 'dir', 'files', 'bittorrent', 'infoHash',
    ]
    const token = `token:${this.secret}`
    const results = await this.call<any[]>('system.multicall', [
      { methodName: 'aria2.tellActive', params: [token, keys] },
      { methodName: 'aria2.tellWaiting', params: [token, 0, 500, keys] },
      { methodName: 'aria2.tellStopped', params: [token, 0, 500, keys] },
      { methodName: 'aria2.getGlobalStat', params: [token] },
    ])
    const unwrap = (r: any) => (Array.isArray(r) ? r[0] : r)
    const downloads = [
      ...unwrap(results[0]),
      ...unwrap(results[1]),
      ...unwrap(results[2]),
    ] as Aria2Download[]
    return { downloads, stat: unwrap(results[3]) as Aria2GlobalStat }
  }
}
