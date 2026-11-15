import { apiUrl } from "./client-context.mjs";

export class CaptureStream {
  constructor({ rtc, url, decoder, route, closed, releaseInput, status }) {
    this.rtc = rtc;
    this.url = new URL(url, location.href);
    this.url.protocol = location.protocol === "https:" ? "wss:" : "ws:";
    this.decoder = decoder;
    this.route = route;
    this.closed = closed;
    this.releaseInput = releaseInput;
    this.status = status;
    this.controller = new AbortController();
    this.open(null);
  }

  open(direct) {
    if (this.stopped) { direct?.close(); return; }
    const previous = this.socket,
      previousDirect = this.direct,
      url = new URL(this.url);
    url.searchParams.set("directStreamId", direct?.id || 0);
    const socket = new WebSocket(apiUrl(url));
    this.releaseInput?.(previous);
    this.socket = socket;
    this.direct = direct;
    this.earlyChunks = [];
    this.earlyBytes = 0;
    this.route(socket, direct);
    this.decoder.reset();
    socket.binaryType = "arraybuffer";
    if (previous?.readyState < WebSocket.CLOSING) previous.close(1000);
    previousDirect?.close();
    socket.onopen = () => {
      if (this.socket !== socket || this.stopped) return;
      this.status?.(direct ? "P2P 实时画面已连接" : "Server 中转实时画面已连接");
      for (const data of this.earlyChunks) this.decoder.receive(data, socket);
      this.earlyChunks = [];
      this.earlyBytes = 0;
      if (!direct && this.rtc.enabled && !this.attempted) {
        this.attempted = true;
        this.upgrade();
      }
    };
    socket.onmessage = (event) => {
      if (!direct && this.socket === socket) this.decoder.receive(new Uint8Array(event.data), socket);
    };
    socket.onclose = (event) => {
      if (this.socket !== socket || this.stopped) return;
      if (direct) { this.fallback(); return; }
      this.stop();
      this.closed(event);
    };
    socket.onerror = () => {
      if (this.socket === socket && !this.stopped) this.status?.("画面连接失败");
    };
  }

  async upgrade() {
    let direct;
    try {
      direct = await this.rtc.open(
        (data) => {
          if (this.direct !== direct || this.stopped) return;
          if (this.socket.readyState === WebSocket.OPEN) this.decoder.receive(data, this.socket);
          else {
            this.earlyBytes += data.length;
            if (this.earlyBytes > 0x2000000) { this.fallback(); return; }
            this.earlyChunks.push(data);
          }
        },
        undefined,
        () => this.fallback(),
        this.controller.signal,
      );
      if (this.stopped) { direct.close(); return; }
      this.open(direct);
    } catch (error) {
      direct?.close();
      if (!this.stopped && error.name !== "AbortError") console.warn(error);
    }
  }

  fallback() {
    if (this.stopped || !this.direct) return;
    this.status?.("P2P 已断开，正在恢复 Server 中转…");
    this.open(null);
  }

  stop() {
    if (this.stopped) return;
    this.stopped = true;
    this.controller.abort();
    this.releaseInput?.(this.socket);
    const socket = this.socket, direct = this.direct;
    this.socket = this.direct = null;
    this.earlyChunks = [];
    this.earlyBytes = 0;
    this.route(null, null);
    direct?.close();
    if (socket?.readyState < WebSocket.CLOSING) socket.close(1000);
    this.decoder.reset();
  }
}
