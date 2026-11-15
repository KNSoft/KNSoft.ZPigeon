import { apiUrl } from "./client-context.mjs";

export class RtcDirect {
  constructor(notify) {
    this.notify = notify;
    this.handlers = new Map();
    this.enabled = false;
    this.pendingOpens = 0;
    this.generation = 0;
  }

  async open(receive, status, disconnected, signal) {
    if (signal?.aborted) throw new DOMException("P2P 已取消", "AbortError");
    this.pendingOpens++;
    try {
      await cancellable(this.connect(status), signal);
      if (signal?.aborted) throw new DOMException("P2P 已取消", "AbortError");
      if (this.channel?.readyState !== "open") throw new Error("P2P 数据通道已断开");
      let id;
      do id = crypto.getRandomValues(new Uint32Array(1))[0];
      while (!id || this.handlers.has(id));
      const handler = { receive, disconnected };
      this.handlers.set(id, handler);
      let closed = false;
      return {
        id,
        close: () => {
          if (closed) return;
          closed = true;
          if (this.handlers.get(id) !== handler) return;
          this.handlers.delete(id);
          if (!this.handlers.size && !this.pendingOpens) this.close();
        },
      };
    } finally {
      this.pendingOpens--;
      if (!this.handlers.size && !this.pendingOpens) this.close();
    }
  }

  async connect(status) {
    if (this.channel?.readyState === "open") return;
    if (!this.connecting) {
      const promise = this.create(status).finally(() => {
        if (this.connecting === promise) this.connecting = null;
      });
      this.connecting = promise;
    }
    await this.connecting;
  }

  async create(status) {
    status?.("P2P 加速中…");
    const generation = this.generation,
      controller = new AbortController(),
      socket = new WebSocket(apiUrl(this.url("/api/rtc"))),
      attempt = { socket, controller },
      deadline = performance.now() + 60000;
    this.attempt = attempt;
    const timer = setTimeout(() => controller.abort(), 60000);
    let peer, channel;
    try {
      const configuration = await message(socket, remaining(deadline), controller.signal);
      peer = attempt.peer = new RTCPeerConnection({
        iceServers: (configuration.iceServers || []).map((url) => ({ urls: url })),
      });
      channel = attempt.channel = peer.createDataChannel("zpigeon", { ordered: true });
      channel.binaryType = "arraybuffer";
      channel.onmessage = (event) => generation === this.generation && this.receive(event.data);
      channel.onclose = () => generation === this.generation && this.fail();
      peer.onconnectionstatechange = () => {
        if (generation === this.generation &&
            (peer.connectionState === "failed" || peer.connectionState === "closed")) this.fail();
      };
      const offer = await cancellable(peer.createOffer(), controller.signal);
      await cancellable(peer.setLocalDescription(offer), controller.signal);
      await gathering(peer, Math.min(15000, remaining(deadline)), controller.signal);
      socket.send(JSON.stringify({ offer: peer.localDescription.sdp }));
      const answer = await message(socket, remaining(deadline), controller.signal);
      if (!answer.answer) throw new Error("Client 未返回 WebRTC Answer");
      await cancellable(peer.setRemoteDescription({ type: "answer", sdp: answer.answer }), controller.signal);
      await opened(channel, remaining(deadline), controller.signal);
      if (generation !== this.generation) throw new DOMException("P2P 已取消", "AbortError");
      this.socket = socket;
      this.peer = peer;
      this.channel = channel;
      socket.onclose = () => generation === this.generation && this.fail();
    } catch (error) {
      channel?.close();
      peer?.close();
      socket.close();
      throw error;
    } finally {
      clearTimeout(timer);
      if (this.attempt === attempt) this.attempt = null;
    }
  }

  receive(data) {
    if (!(data instanceof ArrayBuffer) || data.byteLength < 4) return;
    const id = new DataView(data).getUint32(0, true);
    this.handlers.get(id)?.receive(new Uint8Array(data, 4));
  }

  fail() {
    if (!this.channel && !this.peer && !this.socket && !this.attempt) return;
    const handlers = [...this.handlers.values()];
    this.close();
    for (const handler of handlers) handler.disconnected?.();
    if (handlers.length) this.notify?.("P2P 连接已断开");
  }

  close() {
    this.generation++;
    const channel = this.channel,
      peer = this.peer,
      socket = this.socket,
      attempt = this.attempt;
    this.channel = this.peer = this.socket = this.attempt = this.connecting = null;
    this.handlers.clear();
    attempt?.controller.abort();
    attempt?.channel?.close();
    attempt?.peer?.close();
    if (attempt?.socket?.readyState < WebSocket.CLOSING) attempt.socket.close(1000);
    channel?.close();
    peer?.close();
    if (socket?.readyState < WebSocket.CLOSING) socket.close(1000);
  }

  url(path) {
    const url = new URL(path, location.href);
    url.protocol = location.protocol === "https:" ? "wss:" : "ws:";
    return url;
  }
}

function remaining(deadline) {
  const timeout = deadline - performance.now();
  if (timeout <= 0) throw new Error("P2P 信令超时");
  return timeout;
}

function cancellable(promise, signal) {
  if (!signal) return promise;
  return new Promise((resolve, reject) => {
    const abort = () => reject(new DOMException("P2P 已取消", "AbortError"));
    promise.then(resolve, reject).finally(() => signal.removeEventListener("abort", abort));
    if (signal.aborted) abort();
    else signal.addEventListener("abort", abort, { once: true });
  });
}

function message(socket, timeout, signal) {
  return new Promise((resolve, reject) => {
    const done = (callback, value) => {
      clearTimeout(timer);
      socket.removeEventListener("message", receive);
      socket.removeEventListener("close", close);
      socket.removeEventListener("error", error);
      signal.removeEventListener("abort", abort);
      callback(value);
    };
    const timer = setTimeout(() => done(reject, new Error("P2P 信令超时")), timeout),
      receive = (event) => {
        try { done(resolve, JSON.parse(event.data)); }
        catch (exception) { done(reject, exception); }
      },
      close = (event) => done(reject, new Error(event.reason || "P2P 信令已断开")),
      error = () => done(reject, new Error("P2P 信令连接失败")),
      abort = () => done(reject, new DOMException("P2P 已取消", "AbortError"));
    socket.addEventListener("message", receive, { once: true });
    socket.addEventListener("close", close, { once: true });
    socket.addEventListener("error", error, { once: true });
    signal.addEventListener("abort", abort, { once: true });
    if (signal.aborted) abort();
  });
}

function gathering(peer, timeout, signal) {
  if (peer.iceGatheringState === "complete") return Promise.resolve();
  return new Promise((resolve, reject) => {
    const done = (callback, value) => {
      clearTimeout(timer);
      peer.removeEventListener("icegatheringstatechange", change);
      signal.removeEventListener("abort", abort);
      callback(value);
    };
    const timer = setTimeout(() => done(reject, new Error("ICE 收集超时")), timeout),
      change = () => peer.iceGatheringState === "complete" && done(resolve),
      abort = () => done(reject, new DOMException("P2P 已取消", "AbortError"));
    peer.addEventListener("icegatheringstatechange", change);
    signal.addEventListener("abort", abort, { once: true });
    if (signal.aborted) abort();
  });
}

function opened(channel, timeout, signal) {
  if (channel.readyState === "open") return Promise.resolve();
  return new Promise((resolve, reject) => {
    const done = (callback, value) => {
      clearTimeout(timer);
      channel.onopen = channel.onerror = null;
      signal.removeEventListener("abort", abort);
      callback(value);
    };
    const timer = setTimeout(() => done(reject, new Error("P2P 数据通道建立超时")), timeout),
      abort = () => done(reject, new DOMException("P2P 已取消", "AbortError"));
    channel.onopen = () => done(resolve);
    channel.onerror = () => done(reject, new Error("P2P 数据通道建立失败"));
    signal.addEventListener("abort", abort, { once: true });
    if (signal.aborted) abort();
  });
}
