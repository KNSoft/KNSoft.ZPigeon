export class RtcDirect {
  constructor(notify) {
    this.notify = notify;
    this.handlers = new Map();
  }

  async open(receive, status) {
    await this.connect(status);
    let id;
    do id = crypto.getRandomValues(new Uint32Array(1))[0];
    while (!id || this.handlers.has(id));
    this.handlers.set(id, receive);
    let closed = false;
    return {
      id,
      close: () => {
        if (closed) return;
        closed = true;
        this.handlers.delete(id);
        if (!this.handlers.size) this.close();
      }
    };
  }

  async connect(status) {
    if (this.channel?.readyState === 'open') return;
    if (!this.connecting) {
      this.connecting = this.create(status).finally(() => this.connecting = null);
    }
    await this.connecting;
  }

  async create(status) {
    status?.('P2P 加速中…');
    const socket = new WebSocket(this.url('/api/rtc'));
    let peer, channel;
    try {
      const configuration = await message(socket, 30000);
      peer = new RTCPeerConnection({
        iceServers: (configuration.iceServers || []).map(url => ({ urls: url }))
      });
      channel = peer.createDataChannel('zpigeon', { ordered: true });
      channel.binaryType = 'arraybuffer';
      channel.onmessage = event => this.receive(event.data);
      peer.onconnectionstatechange = () => {
        if (peer.connectionState === 'failed' || peer.connectionState === 'closed') this.fail();
      };
      await peer.setLocalDescription(await peer.createOffer());
      await gathering(peer, 15000);
      socket.send(JSON.stringify({ offer: peer.localDescription.sdp }));
      const answer = await message(socket, 30000);
      if (!answer.answer) throw new Error('Client 未返回 WebRTC Answer');
      await peer.setRemoteDescription({ type: 'answer', sdp: answer.answer });
      await opened(channel, 30000);
      this.socket = socket;
      this.peer = peer;
      this.channel = channel;
      socket.onclose = () => this.fail();
    } catch (error) {
      channel?.close();
      peer?.close();
      socket.close();
      throw error;
    }
  }

  receive(data) {
    if (!(data instanceof ArrayBuffer) || data.byteLength < 4) return;
    const id = new DataView(data).getUint32(0, true);
    this.handlers.get(id)?.(new Uint8Array(data, 4));
  }

  fail() {
    if (!this.channel && !this.peer && !this.socket) return;
    const handlers = [...this.handlers.values()];
    this.close();
    if (handlers.length) this.notify?.('P2P 连接已断开');
  }

  close() {
    const channel = this.channel, peer = this.peer, socket = this.socket;
    this.channel = this.peer = this.socket = null;
    this.handlers.clear();
    channel?.close();
    peer?.close();
    if (socket?.readyState < WebSocket.CLOSING) socket.close(1000);
  }

  url(path) {
    const url = new URL(path, location.href);
    url.protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
    return url;
  }
}

function message(socket, timeout) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => done(reject, new Error('P2P 信令超时')), timeout);
    const done = (callback, value) => {
      clearTimeout(timer);
      socket.removeEventListener('message', receive);
      socket.removeEventListener('close', close);
      socket.removeEventListener('error', error);
      callback(value);
    };
    const receive = event => {
      try { done(resolve, JSON.parse(event.data)); }
      catch (exception) { done(reject, exception); }
    };
    const close = event => done(reject, new Error(event.reason || 'P2P 信令已断开'));
    const error = () => done(reject, new Error('P2P 信令连接失败'));
    socket.addEventListener('message', receive, { once: true });
    socket.addEventListener('close', close, { once: true });
    socket.addEventListener('error', error, { once: true });
  });
}

function gathering(peer, timeout) {
  if (peer.iceGatheringState === 'complete') return Promise.resolve();
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => done(reject, new Error('ICE 收集超时')), timeout);
    const change = () => peer.iceGatheringState === 'complete' && done(resolve);
    const done = (callback, value) => {
      clearTimeout(timer);
      peer.removeEventListener('icegatheringstatechange', change);
      callback(value);
    };
    peer.addEventListener('icegatheringstatechange', change);
  });
}

function opened(channel, timeout) {
  if (channel.readyState === 'open') return Promise.resolve();
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => done(reject, new Error('P2P 数据通道建立超时')), timeout);
    const done = (callback, value) => {
      clearTimeout(timer);
      channel.onopen = channel.onerror = null;
      callback(value);
    };
    channel.onopen = () => done(resolve);
    channel.onerror = () => done(reject, new Error('P2P 数据通道建立失败'));
  });
}
