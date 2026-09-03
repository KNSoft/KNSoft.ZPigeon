const headerLength = 21,
  maximumImageLength = 0x1000000,
  maximumDimension = 7680,
  minimumVideoDimension = 128;
const h264Codec = "avc1.4D0033",
  h265Codec = "hev1.1.6.L153.B0",
  h264Flag = 1,
  h265Flag = 2;

export class CaptureFrameDecoder {
  constructor(canvas, status, active, acknowledge, reportVideoCodecs) {
    this.canvas = canvas;
    this.status = status;
    this.active = active;
    this.acknowledge = acknowledge;
    this.reportVideoCodecs = reportVideoCodecs;
    this.reset();
  }

  reset() {
    this.resetVideo();
    this.supportedWidth = this.supportedHeight = 0;
    this.supportedCodecs = 0;
    this.reportedWidth = this.reportedHeight = 0;
    this.reportedCodecs = 0;
    this.chunks = [];
    this.available = 0;
    this.pending = null;
    this.sequence = 0;
    this.decode = Promise.resolve();
  }

  resetVideo() {
    this.videoFrames?.clear();
    this.videoFrames = new Map();
    if (this.videoDecoder && this.videoDecoder.state !== "closed") this.videoDecoder.close();
    this.videoDecoder = null;
    this.videoCodec = null;
  }

  receive(data, socket) {
    if (!this.active(socket) || !data.length) return;
    this.chunks.push({ data, offset: 0 });
    this.available += data.length;
    if (this.available > maximumImageLength + headerLength) {
      socket.close(1009, "画面数据过大");
      return;
    }
    while (true) {
      if (!this.pending) {
        if (this.available < headerLength) return;
        const bytes = this.read(headerLength),
          view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
        this.pending = {
          type: view.getUint8(0),
          sequence: view.getUint32(1, true),
          canvasWidth: view.getUint16(5, true),
          canvasHeight: view.getUint16(7, true),
          left: view.getUint16(9, true),
          top: view.getUint16(11, true),
          width: view.getUint16(13, true),
          height: view.getUint16(15, true),
          length: view.getUint32(17, true),
        };
        if (this.pending.length > maximumImageLength) {
          socket.close(1009, "画面数据过大");
          return;
        }
      }
      if (!this.pending.length) {
        const frame = this.pending;
        this.pending = null;
        this.recover(frame, socket);
        continue;
      }
      if (this.available < this.pending.length) return;
      const frame = this.pending;
      frame.image = this.read(frame.length);
      this.pending = null;
      if (!valid(frame)) {
        this.recover(frame, socket);
        continue;
      }
      this.decode = this.decode
        .then(() => this.draw(frame, socket))
        .catch((error) => {
          this.recover(frame, socket, error);
        });
    }
  }

  recover(frame, socket, error) {
    if (!this.active(socket)) return;
    if (error) console.warn(error);
    if (!frame.sequence) {
      socket.close(1002, "无效的画面序号");
      return;
    }
    this.resetVideo();
    this.status.textContent = "正在重新同步远程画面…";
    this.acknowledge(frame.sequence, true, socket);
  }

  read(length) {
    const first = this.chunks[0],
      remaining = first.data.length - first.offset;
    if (remaining >= length) {
      const result = first.data.subarray(first.offset, first.offset + length);
      first.offset += length;
      if (first.offset === first.data.length) this.chunks.shift();
      this.available -= length;
      return result;
    }
    const result = new Uint8Array(length);
    for (let written = 0; written < length; ) {
      const chunk = this.chunks[0],
        count = Math.min(length - written, chunk.data.length - chunk.offset);
      result.set(chunk.data.subarray(chunk.offset, chunk.offset + count), written);
      chunk.offset += count;
      written += count;
      if (chunk.offset === chunk.data.length) this.chunks.shift();
    }
    this.available -= length;
    return result;
  }

  async draw(frame, socket) {
    if (!this.active(socket)) return;
    if (frame.type <= 2) return this.drawImage(frame, socket);
    return this.drawVideo(frame, socket);
  }

  async drawImage(frame, socket) {
    if (
      frame.type === 2 &&
      (!this.sequence ||
        frame.sequence !== nextSequence(this.sequence) ||
        this.canvas.width !== frame.canvasWidth ||
        this.canvas.height !== frame.canvasHeight)
    ) {
      this.status.textContent = "正在重新同步远程画面…";
      this.acknowledge(frame.sequence, true, socket);
      return;
    }
    await this.ensureVideoCodecs(videoDimension(frame.canvasWidth), videoDimension(frame.canvasHeight), socket);
    if (!this.active(socket)) return;
    this.resetVideo();
    const bitmap = await createImageBitmap(
      new Blob([frame.image], {
        type: frame.type === 1 ? "image/jpeg" : "image/png",
      }),
    );
    if (!this.active(socket)) {
      bitmap.close();
      return;
    }
    if (frame.type === 1) {
      this.canvas.width = frame.canvasWidth;
      this.canvas.height = frame.canvasHeight;
    }
    const context = this.canvas.getContext("2d");
    context.globalCompositeOperation = frame.type === 1 ? "copy" : "source-over";
    context.drawImage(bitmap, frame.left, frame.top, frame.width, frame.height);
    context.globalCompositeOperation = "source-over";
    bitmap.close();
    this.sequence = frame.sequence;
    this.canvas.hidden = false;
    this.status.textContent = `${frame.canvasWidth} × ${frame.canvasHeight} · 实时`;
    this.acknowledge(frame.sequence, false, socket);
  }

  async drawVideo(frame, socket) {
    const key = frame.type === 3 || frame.type === 5,
      codec = frame.type <= 4 ? h264Codec : h265Codec,
      codecFlag = frame.type <= 4 ? h264Flag : h265Flag,
      supportedCodecs = await this.ensureVideoCodecs(
        videoDimension(frame.canvasWidth),
        videoDimension(frame.canvasHeight),
        socket,
      );
    if (!this.active(socket)) return;
    if (!(supportedCodecs & codecFlag)) {
      this.resetVideo();
      this.sequence = 0;
      this.status.textContent = "正在重新同步远程画面…";
      this.acknowledge(frame.sequence, false, socket);
      return;
    }
    if (
      !key &&
      (!this.sequence ||
        frame.sequence !== nextSequence(this.sequence) ||
        this.videoCodec !== codec ||
        this.canvas.width !== frame.canvasWidth ||
        this.canvas.height !== frame.canvasHeight)
    ) {
      this.status.textContent = "正在重新同步远程画面…";
      this.acknowledge(frame.sequence, true, socket);
      return;
    }
    if (
      !this.videoDecoder ||
      this.videoCodec !== codec ||
      this.canvas.width !== frame.canvasWidth ||
      this.canvas.height !== frame.canvasHeight
    ) {
      if (!key) {
        this.acknowledge(frame.sequence, true, socket);
        return;
      }
      this.resetVideo();
      this.canvas.width = frame.canvasWidth;
      this.canvas.height = frame.canvasHeight;
      this.videoCodec = codec;
      const decoder = new VideoDecoder({
        output: (video) => this.videoOutput(video, decoder),
        error: (error) => this.videoError(error, socket, decoder, codec),
      });
      this.videoDecoder = decoder;
      try {
        decoder.configure(videoConfig(codec, frame.canvasWidth, frame.canvasHeight));
      } catch (error) {
        this.videoError(error, socket, decoder, codec);
        this.acknowledge(frame.sequence, false, socket);
        return;
      }
    }
    const timestamp = frame.sequence * 1000;
    this.videoFrames.set(timestamp, { frame, socket });
    try {
      this.videoDecoder.decode(
        new EncodedVideoChunk({
          type: key ? "key" : "delta",
          timestamp,
          data: frame.image,
        }),
      );
    } catch (error) {
      this.videoFrames.delete(timestamp);
      this.videoError(error, socket, this.videoDecoder, codec);
      this.acknowledge(frame.sequence, false, socket);
      return;
    }
    this.sequence = frame.sequence;
    this.acknowledge(frame.sequence, false, socket);
  }

  videoOutput(video, decoder) {
    const pending = this.videoFrames.get(video.timestamp);
    if (this.videoDecoder !== decoder || !pending) {
      video.close();
      return;
    }
    this.videoFrames.delete(video.timestamp);
    const { frame, socket } = pending;
    if (this.active(socket)) {
      const context = this.canvas.getContext("2d");
      context.globalCompositeOperation = "copy";
      context.drawImage(video, 0, 0, this.canvas.width, this.canvas.height);
      context.globalCompositeOperation = "source-over";
      this.canvas.hidden = false;
      this.status.textContent = `${frame.canvasWidth} × ${frame.canvasHeight} · ${frame.type <= 4 ? "H.264" : "H.265"}`;
    }
    video.close();
  }

  videoError(error, socket, decoder, codec) {
    if (this.videoDecoder !== decoder || !this.active(socket)) return;
    console.warn(error);
    this.resetVideo();
    this.sequence = 0;
    this.status.textContent = "正在重新同步远程画面…";
    this.supportedCodecs &= ~(codec === h264Codec ? h264Flag : h265Flag);
    this.sendVideoCodecs(socket);
  }

  async ensureVideoCodecs(width, height, socket) {
    if (this.supportedWidth !== width || this.supportedHeight !== height) {
      const codecs = await supportedVideoCodecs(width, height);
      if (!this.active(socket)) return 0;
      this.supportedWidth = width;
      this.supportedHeight = height;
      this.supportedCodecs = codecs;
    }
    this.sendVideoCodecs(socket);
    return this.supportedCodecs;
  }

  sendVideoCodecs(socket) {
    if (
      this.reportedWidth === this.supportedWidth &&
      this.reportedHeight === this.supportedHeight &&
      this.reportedCodecs === this.supportedCodecs
    )
      return;
    this.reportVideoCodecs(this.supportedCodecs, this.supportedWidth, this.supportedHeight, socket);
    this.reportedWidth = this.supportedWidth;
    this.reportedHeight = this.supportedHeight;
    this.reportedCodecs = this.supportedCodecs;
  }
}

function valid(frame) {
  return (
    frame.type >= 1 &&
    frame.type <= 6 &&
    frame.sequence &&
    frame.canvasWidth &&
    frame.canvasWidth <= maximumDimension &&
    frame.canvasHeight &&
    frame.canvasHeight <= maximumDimension &&
    frame.width &&
    frame.height &&
    frame.left < frame.canvasWidth &&
    frame.top < frame.canvasHeight &&
    frame.width <= frame.canvasWidth - frame.left &&
    frame.height <= frame.canvasHeight - frame.top &&
    frame.length &&
    frame.length <= maximumImageLength &&
    (frame.type === 2 ||
      (!frame.left && !frame.top && frame.width === frame.canvasWidth && frame.height === frame.canvasHeight))
  );
}

function nextSequence(sequence) {
  return sequence === 0xffffffff ? 1 : sequence + 1;
}

export async function configureCaptureEncoding(select) {
  const codecs = await supportedVideoCodecs(1280, 720),
    h264 = !!(codecs & h264Flag),
    h265 = !!(codecs & h265Flag);
  select.querySelector("[value=h264]").disabled = !h264;
  select.querySelector("[value=h265]").disabled = !h265;
  select.querySelector("[value=auto]").disabled = !(h264 || h265);
  select.dataset.autoCodec = h265 ? "1" : "0";
  if (!(h264 || h265)) select.value = "image";
}

function videoConfig(codec, width, height) {
  return { codec, codedWidth: width, codedHeight: height, optimizeForLatency: true };
}

function videoDimension(value) {
  return Math.max(value & ~1, minimumVideoDimension);
}

async function supportedVideoCodecs(width, height) {
  if (typeof VideoDecoder === "undefined") return 0;
  const supported = async (codec) => {
    try {
      return (await VideoDecoder.isConfigSupported(videoConfig(codec, width, height))).supported;
    } catch {
      return false;
    }
  };
  const [h264, h265] = await Promise.all([supported(h264Codec), supported(h265Codec)]);
  return (h264 ? h264Flag : 0) | (h265 ? h265Flag : 0);
}

export function captureEncodingOptions(select) {
  return select.value === "image"
    ? { captureMode: 0, videoCodec: 0 }
    : select.value === "auto"
      ? { captureMode: 1, videoCodec: Number(select.dataset.autoCodec || 0) }
      : { captureMode: 2, videoCodec: select.value === "h265" ? 1 : 0 };
}
