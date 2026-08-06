import { apiUrl } from "./client-context.mjs";

const FOLDER = 1;
const STORAGE = 2;
const CAN_DELETE = 4;
const ROOT = "DEVICE";

export class PortableDeviceManager {
  constructor(host, { call, notify }) {
    this.host = host;
    this.call = call;
    this.notify = notify;
    this.request = 0;
    this.devices = [];
    this.stack = [];
    this.records = [];
    host.innerHTML = /* HTML */ ` <div class="manager-toolbar">
        <button data-action="back" disabled>向上</button>
        <span data-role="path" class="status">选择便携设备</span>
        <span class="spacer"></span>
        <button data-action="folder" disabled>新建文件夹</button>
        <button data-action="upload" disabled>上传</button>
        <input data-role="upload" type="file" hidden />
        <button data-action="refresh">刷新设备</button>
      </div>
      <div class="portable-body">
        <aside class="administration-tree"><ul data-role="devices"></ul></aside>
        <div class="manager-table">
          <table>
            <thead>
              <tr>
                <th>名称</th>
                <th>类型</th>
                <th>大小</th>
                <th>修改时间</th>
                <th>可用空间</th>
              </tr>
            </thead>
            <tbody></tbody>
          </table>
          <button data-action="more" hidden>加载下一页</button>
          <div class="manager-empty">进入页面后读取远端便携设备</div>
        </div>
      </div>
      <div class="context-menu" data-role="menu" hidden>
        <button data-action="open">打开</button>
        <button data-action="download">下载</button>
        <hr />
        <button data-action="rename">重命名</button>
        <button data-action="delete" class="danger">删除</button>
        <hr />
        <button data-action="properties">属性</button>
      </div>`;
    this.devicesHost = host.querySelector("[data-role=devices]");
    this.body = host.querySelector("tbody");
    this.empty = host.querySelector(".manager-empty");
    this.menu = host.querySelector("[data-role=menu]");
    this.uploadInput = host.querySelector("[data-role=upload]");
    this.path = host.querySelector("[data-role=path]");
    this.more = this.action("more");
    this.action("refresh").onclick = () => this.loadDevices();
    this.action("back").onclick = () => this.back();
    this.action("folder").onclick = () => this.createFolder();
    this.action("upload").onclick = () => this.uploadInput.click();
    this.uploadInput.onchange = () => this.upload();
    this.more.onclick = () => this.loadObjects(this.nextOffset, true);
    for (const button of this.menu.querySelectorAll("[data-action]")) {
      button.onclick = () => this.invoke(button.dataset.action);
    }
    addEventListener("pointerdown", (event) => {
      if (!this.menu.contains(event.target)) this.menu.hidden = true;
    });
  }

  action(name) {
    return this.host.querySelector(`[data-action="${name}"]`);
  }

  activate(connected) {
    if (connected && !this.loaded) this.loadDevices();
  }

  disconnect() {
    this.request++;
    this.loaded = false;
    this.devices = [];
    this.records = [];
    this.stack = [];
    this.device = null;
    this.selected = null;
    this.devicesHost.replaceChildren();
    this.body.replaceChildren();
    this.empty.hidden = false;
    this.empty.textContent = "Client 未连接";
    this.path.textContent = "选择便携设备";
    this.updateActions();
  }

  async loadDevices() {
    const request = ++this.request;
    this.empty.hidden = false;
    this.empty.textContent = "正在读取便携设备…";
    this.body.replaceChildren();
    try {
      const devices = await this.call("/api/portable/devices");
      if (request !== this.request) return;
      this.devices = devices;
      this.loaded = true;
      this.renderDevices();
      this.empty.textContent = devices.length ? "请选择左侧设备" : "没有便携设备";
    } catch (error) {
      if (request !== this.request) return;
      this.empty.textContent = error.message;
      this.notify(error);
    }
  }

  renderDevices() {
    this.devicesHost.replaceChildren(
      ...this.devices.map((device) => {
        const li = document.createElement("li");
        const row = document.createElement("div");
        const arrow = document.createElement("button");
        const label = document.createElement("button");
        row.className = "administration-node-row";
        arrow.className = "administration-arrow";
        arrow.textContent = "";
        arrow.disabled = true;
        label.className = "administration-node-label";
        label.textContent = device.name || device.model || device.id;
        label.title = [device.manufacturer, device.model, device.id].filter(Boolean).join("\n");
        label.onclick = () => this.selectDevice(device, row);
        row.append(arrow, label);
        li.append(row);
        return li;
      }),
    );
  }

  selectDevice(device, row) {
    this.devicesHost.querySelector(".selected")?.classList.remove("selected");
    row.classList.add("selected");
    this.device = device;
    this.stack = [];
    this.loadObjects(0);
  }

  async loadObjects(offset = 0, append = false) {
    if (!this.device) return;
    const request = ++this.request;
    const parent = this.stack.at(-1)?.id ?? null;
    if (!append) {
      this.records = [];
      this.body.replaceChildren();
      this.empty.hidden = false;
      this.empty.textContent = "正在读取…";
    }
    this.more.hidden = true;
    try {
      const page = await this.call("/api/portable/objects", {
        deviceId: this.device.id,
        parentId: parent,
        offset,
      });
      if (request !== this.request) return;
      this.records.push(...page.objects);
      this.nextOffset = page.nextOffset;
      this.render();
      this.more.hidden = !page.nextOffset;
    } catch (error) {
      if (request !== this.request) return;
      this.empty.hidden = false;
      this.empty.textContent = error.message;
      this.notify(error);
    }
    this.updateActions();
  }

  render() {
    this.body.replaceChildren(
      ...this.records.map((item) => {
        const row = document.createElement("tr");
        const values = [
          item.name || "(无名称)",
          item.flags & STORAGE ? "存储" : item.flags & FOLDER ? "文件夹" : "文件",
          item.flags & FOLDER ? "" : bytes(item.size),
          item.modifiedTime ? date(item.modifiedTime) : "",
          item.flags & STORAGE ? bytes(item.freeSpace) : "",
        ];
        for (const value of values) {
          const cell = document.createElement("td");
          cell.textContent = value;
          row.append(cell);
        }
        row.onclick = () => this.select(row, item);
        row.ondblclick = () => {
          if (item.flags & FOLDER) this.open(item);
        };
        row.oncontextmenu = (event) => this.context(event, row, item);
        return row;
      }),
    );
    this.empty.hidden = this.records.length !== 0;
    this.empty.textContent = "此位置为空";
    this.path.textContent = [
      this.device.name || this.device.model || "便携设备",
      ...this.stack.map((item) => item.name),
    ].join(" › ");
  }

  select(row, item) {
    this.body.querySelector(".selected")?.classList.remove("selected");
    row.classList.add("selected");
    this.selected = item;
  }

  context(event, row, item) {
    event.preventDefault();
    this.select(row, item);
    this.action("open").hidden = !(item.flags & FOLDER);
    this.action("download").hidden = !!(item.flags & FOLDER);
    this.action("rename").disabled = false;
    this.action("delete").disabled = !(item.flags & CAN_DELETE);
    this.menu.hidden = false;
    const rect = this.menu.getBoundingClientRect();
    this.menu.style.left = `${Math.max(6, Math.min(event.clientX, innerWidth - rect.width - 6))}px`;
    this.menu.style.top = `${Math.max(6, Math.min(event.clientY, innerHeight - rect.height - 6))}px`;
  }

  invoke(action) {
    this.menu.hidden = true;
    const item = this.selected;
    if (!item) return;
    if (action === "open") this.open(item);
    else if (action === "download") this.download(item);
    else if (action === "rename") this.rename(item);
    else if (action === "delete") this.remove(item);
    else if (action === "properties") this.properties(item);
  }

  open(item) {
    this.stack.push(item);
    this.loadObjects(0);
  }

  back() {
    if (!this.stack.length) return;
    this.stack.pop();
    this.loadObjects(0);
  }

  currentId() {
    return this.stack.at(-1)?.id ?? ROOT;
  }

  async createFolder() {
    const name = prompt("文件夹名称");
    if (!name) return;
    try {
      await this.call("/api/portable/folder", {
        deviceId: this.device.id,
        objectId: this.currentId(),
        name,
      });
      await this.loadObjects(0);
    } catch (error) {
      this.notify(error);
    }
  }

  async rename(item) {
    const name = prompt("新名称", item.name);
    if (!name || name === item.name) return;
    try {
      await this.call("/api/portable/rename", { deviceId: this.device.id, objectId: item.id, name });
      await this.loadObjects(0);
    } catch (error) {
      this.notify(error);
    }
  }

  async remove(item) {
    if (!confirm(`确定删除“${item.name}”吗？`)) return;
    try {
      await this.call("/api/portable/delete", { deviceId: this.device.id, objectId: item.id });
      await this.loadObjects(0);
    } catch (error) {
      this.notify(error);
    }
  }

  download(item) {
    const query = new URLSearchParams({
      deviceId: this.device.id,
      objectId: item.id,
      name: item.name || "download",
    });
    const link = document.createElement("a");
    link.href = apiUrl(`/api/portable/download?${query}`);
    link.click();
  }

  async upload() {
    const file = this.uploadInput.files[0];
    this.uploadInput.value = "";
    if (!file || !this.device) return;
    try {
      const query = new URLSearchParams({
        deviceId: this.device.id,
        parentId: this.currentId(),
        name: file.name,
      });
      const response = await fetch(apiUrl(`/api/portable/upload?${query}`), { method: "PUT", body: file });
      if (!response.ok) throw new Error((await response.text()) || `HTTP ${response.status}`);
      await this.loadObjects(0);
    } catch (error) {
      this.notify(error);
    }
  }

  properties(item) {
    const storage = item.flags & STORAGE ? `\n容量：${bytes(item.capacity)}\n可用空间：${bytes(item.freeSpace)}` : "";
    alert(
      `名称：${item.name || "(无名称)"}\n对象 ID：${item.id}\n持久 ID：${item.persistentId || "—"}\n` +
        `大小：${item.flags & FOLDER ? "—" : bytes(item.size)}${storage}`,
    );
  }

  updateActions() {
    const active = !!this.device;
    this.action("back").disabled = !this.stack.length;
    this.action("folder").disabled = !active;
    this.action("upload").disabled = !active;
  }
}

function bytes(value) {
  value = Number(value);
  if (value < 1024) return `${value} B`;
  const units = ["KB", "MB", "GB", "TB"];
  let index = -1;
  do {
    value /= 1024;
    index++;
  } while (value >= 1024 && index < units.length - 1);
  return `${value.toFixed(value < 10 ? 1 : 0)} ${units[index]}`;
}

function date(value) {
  return new Date(Number(BigInt(value) / 10000n - 11644473600000n)).toLocaleString();
}
