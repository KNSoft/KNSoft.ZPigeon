import { apiUrl } from "./client-context.mjs";

const clipboardFormatNames = {
  1: "ANSI 文本",
  2: "位图",
  3: "图元文件",
  4: "SYLK",
  5: "DIF",
  6: "TIFF",
  7: "OEM 文本",
  8: "DIB",
  9: "调色板",
  10: "笔数据",
  11: "RIFF",
  12: "WAVE",
  13: "Unicode 文本",
  14: "增强型图元文件",
  15: "文件列表",
  16: "区域设置",
  17: "DIB V5",
};

export class ClipboardManager {
  constructor(root, { call, notify }) {
    this.root = root;
    this.call = call;
    this.notify = notify;
    this.connected = false;
    this.listenToken = 0;
    root.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <strong>剪贴板</strong><span data-role="summary" class="status"></span><span class="spacer"></span
        ><button data-action="refresh">刷新</button><button data-action="clear" class="danger">清空</button
        ><button data-action="save">保存文本</button>
      </div>
      <div class="clipboard-body">
        <section class="card">
          <h2>Unicode 文本</h2>
          <textarea data-role="text" spellcheck="false" placeholder="剪贴板中没有 Unicode 文本"></textarea>
          <h2>图像预览</h2>
          <div class="clipboard-image">
            <img data-role="image" alt="剪贴板图像" hidden /><span data-role="image-empty" class="muted"
              >剪贴板中没有图像</span
            >
          </div>
        </section>
        <section class="card">
          <h2>现有格式</h2>
          <div data-role="formats" class="clipboard-formats"></div>
        </section>
      </div>
      <div class="manager-empty">Client 未连接</div>`;
    this.text = root.querySelector("[data-role=text]");
    this.image = root.querySelector("[data-role=image]");
    this.imageEmpty = root.querySelector("[data-role=image-empty]");
    this.formats = root.querySelector("[data-role=formats]");
    this.summary = root.querySelector("[data-role=summary]");
    this.empty = root.querySelector(".manager-empty");
    this.refresh = root.querySelector("[data-action=refresh]");
    this.clear = root.querySelector("[data-action=clear]");
    this.save = root.querySelector("[data-action=save]");
    this.refresh.onclick = () => this.load();
    this.clear.onclick = () => this.clearClipboard();
    this.save.onclick = () => this.saveText();
  }
  activate(connected) {
    this.connected = connected;
    this.sync();
    if (connected && !this.loaded) this.load().then(() => this.listen());
    else if (connected) this.listen();
  }
  disconnect() {
    this.connected = false;
    this.listenToken = (this.listenToken || 0) + 1;
    this.loaded = false;
    this.text.value = "";
    if (this.imageUrl) URL.revokeObjectURL(this.imageUrl);
    this.imageUrl = null;
    this.image.removeAttribute("src");
    this.image.hidden = true;
    this.imageEmpty.hidden = false;
    this.formats.replaceChildren();
    this.summary.textContent = "";
    this.empty.hidden = false;
    this.empty.textContent = "Client 未连接";
    this.sync();
  }
  sync() {
    this.refresh.disabled = this.clear.disabled = this.save.disabled = this.text.disabled = !this.connected;
  }
  async load() {
    if (!this.connected || this.loading) return;
    this.loading = true;
    this.empty.hidden = false;
    this.empty.textContent = "正在读取用户剪贴板…";
    try {
      const records = await this.call("/api/clipboard"),
        formats = records.filter((record) => record.kind === 24),
        text = formats.find((record) => record.state === 13),
        state = records.find((record) => record.kind === 25);
      this.sequence = state?.value || "0";
      this.loaded = true;
      this.text.value = text?.detail || "";
      await this.loadImage(formats.some((record) => record.state === 2));
      this.formats.replaceChildren(
        ...formats.map((record) => {
          const row = document.createElement("div"),
            name = document.createElement("strong"),
            detail = document.createElement("span");
          name.textContent = record.name || clipboardFormatNames[record.state] || "未知格式";
          detail.textContent = `格式 ${record.state}${record.flags & 1 ? ` · ${record.value} 字节 · 可编辑` : ""}`;
          row.append(name, detail);
          return row;
        }),
      );
      this.summary.textContent = `${formats.length} 种格式 · 实时监听`;
      this.empty.hidden = true;
    } catch (error) {
      this.empty.textContent = error.message;
      this.notify(error);
    } finally {
      this.loading = false;
    }
  }
  async loadImage(available) {
    if (this.imageUrl) URL.revokeObjectURL(this.imageUrl);
    this.imageUrl = null;
    this.image.removeAttribute("src");
    this.image.hidden = true;
    this.imageEmpty.hidden = false;
    this.imageEmpty.textContent = "剪贴板中没有图像";
    if (!available) return;
    try {
      const response = await fetch(apiUrl("/api/clipboard/image"), { method: "POST" });
      if (!response.ok) throw new Error((await response.text()) || `HTTP ${response.status}`);
      this.imageUrl = URL.createObjectURL(await response.blob());
      this.image.src = this.imageUrl;
      this.image.hidden = false;
      this.imageEmpty.hidden = true;
    } catch (error) {
      this.imageEmpty.textContent = `图像预览不可用：${error.message}`;
      this.notify(error);
    }
  }
  async listen() {
    if (!this.connected) return;
    const token = ++this.listenToken;
    try {
      while (this.connected && token === this.listenToken && !this.root.closest("main").hidden) {
        const records = await this.call("/api/clipboard/wait", { identity: this.sequence || "0" });
        if (!this.connected || token !== this.listenToken) return;
        const sequence = records.find((record) => record.kind === 25)?.value || this.sequence;
        if (sequence !== this.sequence) await this.load();
      }
    } catch (error) {
      if (this.connected && token === this.listenToken) this.notify(error);
    }
  }
  async saveText() {
    try {
      await this.call("/api/clipboard/control", { action: 23, identity: "unicode", argument: this.text.value });
      this.notify("剪贴板文本已保存");
      await this.load();
    } catch (error) {
      this.notify(error);
    }
  }
  async clearClipboard() {
    if (!confirm("确定清空用户剪贴板的全部格式？")) return;
    try {
      await this.call("/api/clipboard/control", { action: 2, identity: "clipboard" });
      this.notify("剪贴板已清空");
      await this.load();
    } catch (error) {
      this.notify(error);
    }
  }
}
