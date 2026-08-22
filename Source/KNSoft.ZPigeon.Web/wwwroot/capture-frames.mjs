export class CaptureFrameDecoder {
  constructor(canvas, status, notify, active) {
    this.canvas = canvas;
    this.status = status;
    this.notify = notify;
    this.active = active;
    this.buffer = new Uint8Array();
    this.decode = Promise.resolve();
  }

  receive(data, socket) {
    if (!this.active(socket)) return;
    const joined = new Uint8Array(this.buffer.length + data.length);
    joined.set(this.buffer);
    joined.set(data, this.buffer.length);
    this.buffer = joined;
    while (this.buffer.length >= 34) {
      const view = new DataView(this.buffer.buffer, this.buffer.byteOffset), frame = {
        type: view.getUint16(0, true), sequence: view.getUint32(2, true),
        canvasWidth: view.getUint32(6, true), canvasHeight: view.getUint32(10, true),
        left: view.getUint32(14, true), top: view.getUint32(18, true),
        width: view.getUint32(22, true), height: view.getUint32(26, true),
        length: view.getUint32(30, true)
      };
      if ((frame.type !== 1 && frame.type !== 2) || !frame.sequence || !frame.canvasWidth ||
          !frame.canvasHeight || !frame.width || !frame.height ||
          frame.left + frame.width > frame.canvasWidth || frame.top + frame.height > frame.canvasHeight ||
          !frame.length || frame.length > 0x1000000) {
        socket.close(1002, '无效的画面数据');
        return;
      }
      if (this.buffer.length < 34 + frame.length) return;
      frame.image = this.buffer.slice(34, 34 + frame.length);
      this.buffer = this.buffer.slice(34 + frame.length);
      this.decode = this.decode.then(() => this.draw(frame, socket)).catch(error => {
        if (this.active(socket)) {
          this.notify(error);
          socket.close(1002, '无法解码画面');
        }
      });
    }
  }

  async draw(frame, socket) {
    const bitmap = await createImageBitmap(new Blob([frame.image], {
      type: frame.type === 1 ? 'image/jpeg' : 'image/png'
    }));
    if (!this.active(socket)) {
      bitmap.close();
      return;
    }
    if (frame.type === 1 || this.canvas.width !== frame.canvasWidth ||
        this.canvas.height !== frame.canvasHeight) {
      this.canvas.width = frame.canvasWidth;
      this.canvas.height = frame.canvasHeight;
    }
    const context = this.canvas.getContext('2d');
    context.globalCompositeOperation = frame.type === 1 ? 'copy' : 'source-over';
    context.drawImage(bitmap, frame.left, frame.top, frame.width, frame.height);
    context.globalCompositeOperation = 'source-over';
    bitmap.close();
    this.canvas.hidden = false;
    this.status.textContent = `${frame.canvasWidth} × ${frame.canvasHeight} · 实时`;
  }
}
