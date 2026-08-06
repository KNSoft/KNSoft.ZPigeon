import { apiUrl } from "./client-context.mjs";
import { t } from "./i18n.mjs";

const fileTime = (value) =>
  !value || value === "0"
    ? "—"
    : new Date(Number((BigInt(value) - 116444736000000000n) / 10000n)).toLocaleString();

const placeMenu = (menu, event) => {
  menu.hidden = false;
  const box = menu.getBoundingClientRect();
  menu.style.left = `${Math.max(6, Math.min(event.clientX, innerWidth - box.width - 6))}px`;
  menu.style.top = `${Math.max(6, Math.min(event.clientY, innerHeight - box.height - 6))}px`;
};

class SnapshotManager {
  constructor(root, { call, notify, path, loading }) {
    this.root = root;
    this.call = call;
    this.notify = notify;
    this.path = path;
    this.loadingText = loading;
    this.connected = false;
    this.records = [];
  }
  activate(connected) {
    this.connected = connected;
    if (connected && !this.loaded) this.load();
    else if (!connected) this.empty.textContent = "Client 未连接";
  }
  disconnect() {
    this.connected = false;
    this.loaded = this.loading = false;
    this.request = (this.request || 0) + 1;
    this.records = [];
    this.render();
    this.empty.hidden = false;
    this.empty.textContent = "Client 未连接";
  }
  async load(force = false) {
    if (!this.connected || this.loading || (this.loaded && !force)) return;
    const request = (this.request || 0) + 1;
    this.request = request;
    this.loading = true;
    this.records = [];
    this.render();
    this.empty.hidden = false;
    this.empty.textContent = this.loadingText;
    try {
      const records = await this.call(this.path);
      if (request !== this.request) return;
      this.records = records;
      this.loaded = true;
      this.render();
    } catch (error) {
      if (request !== this.request) return;
      this.records = [];
      this.render();
      this.empty.hidden = false;
      this.empty.textContent = error.message;
      this.notify(error);
    } finally {
      if (request === this.request) this.loading = false;
    }
  }
}

export class PageFileManager extends SnapshotManager {
  constructor(root, { call, notify }) {
    super(root, { call, notify, path: "/api/page-files", loading: "正在读取页面文件配置…" });
    root.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <input data-role="filter" placeholder="筛选页面文件" /><span class="spacer"></span
        ><button data-action="new">新建</button><button data-action="refresh">刷新</button>
      </div>
      <div class="manager-table">
        <table>
          <thead>
            <tr>
              <th>路径</th>
              <th>管理方式</th>
              <th>初始大小 (MB)</th>
              <th>最大大小 (MB)</th>
            </tr>
          </thead>
          <tbody></tbody>
        </table>
        <div class="manager-empty">Client 未连接</div>
      </div>
      <div class="context-menu administration-menu" hidden>
        <button data-action="edit">修改</button><button data-action="delete" class="danger">删除</button>
      </div>
      <dialog data-role="editor">
        <form>
          <h2>页面文件</h2>
          <label>路径<input data-field="path" required maxlength="32767" placeholder="C:\\pagefile.sys" /></label
          ><label><input data-field="managed" type="checkbox" />由系统管理大小</label
          ><label>初始大小 (MB)<input data-field="initial" type="number" min="1" max="4294967295" required /></label
          ><label>最大大小 (MB)<input data-field="maximum" type="number" min="1" max="4294967295" required /></label>
          <p class="property-note">修改在重新启动后生效。</p>
          <div class="dialog-actions">
            <button type="submit">保存</button><button type="button" data-action="cancel">取消</button>
          </div>
        </form>
      </dialog>`;
    this.filter = root.querySelector("[data-role=filter]");
    this.body = root.querySelector("tbody");
    this.empty = root.querySelector(".manager-empty");
    this.menu = root.querySelector(".context-menu");
    this.dialog = root.querySelector("dialog");
    this.filter.oninput = () => this.render();
    root.querySelector("[data-action=refresh]").onclick = () => this.load(true);
    root.querySelector("[data-action=new]").onclick = () => this.edit();
    this.dialog.querySelector("[data-action=cancel]").onclick = () => this.dialog.close();
    this.dialog.querySelector("[data-field=managed]").onchange = () => this.syncManaged();
    this.dialog.querySelector("form").onsubmit = (event) => {
      event.preventDefault();
      this.save();
    };
    this.body.oncontextmenu = (event) => {
      const row = event.target.closest("tr");
      if (!row) return;
      event.preventDefault();
      this.selected = row.record;
      placeMenu(this.menu, event);
    };
    this.menu.querySelector("[data-action=edit]").onclick = () => {
      this.menu.hidden = true;
      this.edit(this.selected);
    };
    this.menu.querySelector("[data-action=delete]").onclick = () => {
      this.menu.hidden = true;
      this.remove(this.selected);
    };
    addEventListener("pointerdown", (event) => {
      if (!this.menu.contains(event.target)) this.menu.hidden = true;
    });
  }
  render() {
    const query = this.filter?.value.toLocaleLowerCase() || "",
      records = this.records.filter((record) => !query || record.identity.toLocaleLowerCase().includes(query));
    this.body?.replaceChildren(
      ...records.map((record) => {
        const row = document.createElement("tr");
        row.record = record;
        for (const value of [
          record.name,
          record.state === 0 && record.value === "0" ? "系统管理" : "自定义大小",
          record.state || "—",
          record.value === "0" ? "—" : record.value,
        ]) {
          const cell = row.insertCell();
          cell.textContent = value;
          cell.title = value;
        }
        return row;
      }),
    );
    if (this.empty) {
      this.empty.hidden = records.length !== 0;
      if (this.connected && this.loaded && !records.length) this.empty.textContent = "没有页面文件配置";
    }
  }
  edit(record = null) {
    this.editing = record;
    const field = (name) => this.dialog.querySelector(`[data-field=${name}]`);
    field("path").value = record?.identity || "";
    field("path").readOnly = !!record;
    field("managed").checked = !!record && record.state === 0 && record.value === "0";
    field("initial").value = record?.state || 1024;
    field("maximum").value = record && record.value !== "0" ? record.value : 4096;
    this.syncManaged();
    this.dialog.showModal();
    field(record ? "managed" : "path").focus();
  }
  syncManaged() {
    const managed = this.dialog.querySelector("[data-field=managed]").checked;
    for (const name of ["initial", "maximum"]) this.dialog.querySelector(`[data-field=${name}]`).disabled = managed;
  }
  async save() {
    const field = (name) => this.dialog.querySelector(`[data-field=${name}]`),
      managed = field("managed").checked,
      initial = managed ? 0 : Number(field("initial").value),
      maximum = managed ? 0 : Number(field("maximum").value),
      path = field("path").value.trim();
    if (!path || (!managed && (initial < 1 || maximum < initial))) {
      this.notify("页面文件大小无效");
      return;
    }
    try {
      await this.call("/api/page-files/control", { action: 23, identity: path, argument: `${initial}|${maximum}` });
      this.dialog.close();
      this.notify("页面文件配置已保存，重新启动后生效");
      await this.load(true);
    } catch (error) {
      this.notify(error);
    }
  }
  async remove(record) {
    if (!confirm(`确定删除页面文件配置“${record.identity}”？\n修改在重新启动后生效。`)) return;
    try {
      await this.call("/api/page-files/control", { action: 2, identity: record.identity });
      this.notify("页面文件配置已删除，重新启动后生效");
      await this.load(true);
    } catch (error) {
      this.notify(error);
    }
  }
}

export class BluetoothManager extends SnapshotManager {
  constructor(root, { call, notify }) {
    super(root, { call, notify, path: "/api/bluetooth", loading: "正在读取蓝牙设备…" });
    root.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <input data-role="filter" placeholder="筛选蓝牙设备" /><span data-role="summary" class="status"></span
        ><span class="spacer"></span><button data-action="refresh">刷新</button>
      </div>
      <div class="manager-table">
        <table>
          <thead>
            <tr>
              <th>类型</th>
              <th>名称</th>
              <th>地址</th>
              <th>状态</th>
              <th>详细信息</th>
            </tr>
          </thead>
          <tbody></tbody>
        </table>
        <div class="manager-empty">Client 未连接</div>
      </div>
      <div class="context-menu administration-menu" hidden></div>`;
    this.filter = root.querySelector("[data-role=filter]");
    this.summary = root.querySelector("[data-role=summary]");
    this.body = root.querySelector("tbody");
    this.empty = root.querySelector(".manager-empty");
    this.menu = root.querySelector(".context-menu");
    this.filter.oninput = () => this.render();
    root.querySelector("[data-action=refresh]").onclick = () => this.load(true);
    this.body.oncontextmenu = (event) => {
      const row = event.target.closest("tr");
      if (!row) return;
      event.preventDefault();
      this.openMenu(event, row.record);
    };
    addEventListener("pointerdown", (event) => {
      if (!this.menu.contains(event.target)) this.menu.hidden = true;
    });
  }
  render() {
    const query = this.filter?.value.toLocaleLowerCase() || "",
      records = this.records.filter(
        (record) =>
          !query ||
          `${record.name} ${record.description} ${record.detail} ${JSON.stringify(record.data)}`
            .toLocaleLowerCase()
            .includes(query),
      );
    this.body?.replaceChildren(
      ...records.map((record) => {
        const radio = record.kind === 43,
          row = document.createElement("tr"),
          flags = record.state,
          state = radio
            ? flags & 8
              ? "已由系统禁用"
              : flags & 4
                ? [flags & 2 ? "可连接" : "禁止传入", flags & 1 ? "可发现" : "不可发现"].join("，")
                : "已关闭"
            : [flags & 1 ? "已连接" : "未连接", flags & 2 ? "已配对" : flags & 4 ? "已记住" : "未配对"].join("，"),
          detail =
            radio && record.data
              ? `制造商: 0x${record.data.manufacturer.toString(16).toUpperCase().padStart(4, "0")}\nLMP 子版本: 0x${record.data.lmpSubversion.toString(16).toUpperCase().padStart(4, "0")}`
              : `${t("bluetooth.radio")}: ${record.detail}\n${t("bluetooth.lastSeen")}: ${fileTime(record.value)}`;
        row.record = record;
        for (const value of [radio ? "无线电" : "设备", record.name || "—", record.description, state, detail]) {
          const cell = row.insertCell();
          cell.textContent = value || "—";
          cell.title = cell.textContent;
        }
        return row;
      }),
    );
    if (this.summary) {
      const radios = records.filter((record) => record.kind === 43).length;
      const devices = records.filter((record) => record.kind === 44).length;
      this.summary.textContent = this.loaded ? `${radios} 个无线电，${devices} 个设备` : "";
    }
    if (this.empty) {
      this.empty.hidden = records.length !== 0;
      if (this.connected && this.loaded && !records.length) this.empty.textContent = "未发现蓝牙无线电";
    }
  }
  openMenu(event, record) {
    const radio = record.kind === 43,
      powered = !!(record.state & 4),
      actions = radio
        ? [
            [powered ? "关闭蓝牙" : "开启蓝牙", `power:${powered ? 0 : 1}`, false, !!(record.state & 8)],
            ...(powered
              ? [
                  [record.state & 1 ? "关闭发现" : "启用发现", `discovery:${record.state & 1 ? 0 : 1}`],
                  [record.state & 2 ? "禁止传入连接" : "允许传入连接", `incoming:${record.state & 2 ? 0 : 1}`],
                ]
              : []),
          ]
        : [["删除配对", null, true]];
    this.menu.replaceChildren(
      ...actions.map(([title, argument, danger, disabled]) => {
        const button = document.createElement("button");
        button.textContent = title;
        button.disabled = disabled === true;
        button.classList.toggle("danger", danger === true);
        button.onclick = async () => {
          this.menu.hidden = true;
          if (danger && !confirm(`确定删除“${record.name}”的蓝牙配对？`)) return;
          try {
            await this.call("/api/bluetooth/control", { action: radio ? 23 : 2, identity: record.identity, argument });
            this.notify("蓝牙设置已更新");
            await this.load(true);
          } catch (error) {
            this.notify(error);
          }
        };
        return button;
      }),
    );
    placeMenu(this.menu, event);
  }
}

export class KeyboardManager {
  constructor(root, { call, notify }) {
    this.root = root;
    this.call = call;
    this.notify = notify;
    this.connected = false;
    this.records = [];
    root.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <button data-action="listen">开始监听</button><button data-action="clear">清除</button
        ><button data-action="export" disabled>导出</button><span data-role="state" class="status">未监听</span>
      </div>
      <div class="manager-table">
        <table>
          <thead>
            <tr>
              <th>时间</th>
              <th>事件</th>
              <th>按键</th>
              <th>虚拟键</th>
              <th>扫描码</th>
              <th>标志</th>
            </tr>
          </thead>
          <tbody></tbody>
        </table>
        <div class="manager-empty">Client 未连接</div>
      </div>`;
    this.button = root.querySelector("[data-action=listen]");
    this.exportButton = root.querySelector("[data-action=export]");
    this.state = root.querySelector("[data-role=state]");
    this.body = root.querySelector("tbody");
    this.empty = root.querySelector(".manager-empty");
    this.button.onclick = () => (this.listening ? this.stop() : this.start());
    root.querySelector("[data-action=clear]").onclick = () => {
      this.records = [];
      this.body.replaceChildren();
      this.exportButton.disabled = true;
    };
    this.exportButton.onclick = () => this.export();
  }
  activate(connected) {
    this.connected = connected;
    this.button.disabled = !connected;
    this.empty.hidden = connected;
    this.empty.textContent = connected ? "" : "Client 未连接";
  }
  disconnect() {
    this.connected = false;
    this.stop();
    this.button.disabled = true;
    this.empty.hidden = false;
    this.empty.textContent = "Client 未连接";
  }
  start() {
    if (!this.connected || this.listening) return;
    this.listening = true;
    this.button.textContent = "停止监听";
    this.state.textContent = "正在监听（不保存按键记录）";
    this.empty.hidden = true;
    this.poll();
  }
  stop() {
    this.listening = false;
    this.button.textContent = "开始监听";
    this.state.textContent = "未监听";
  }
  async poll() {
    while (this.listening && this.connected) {
      try {
        const records = await this.call("/api/keyboard/wait");
        if (!this.listening) break;
        for (const record of records) this.append(record);
      } catch (error) {
        this.stop();
        this.notify(error);
        break;
      }
    }
  }
  append(record) {
    const values = [
        new Date().toLocaleString(),
        record.state === 0x100
          ? "按下"
          : record.state === 0x101
            ? "释放"
            : record.state === 0x104
              ? "系统按下"
              : record.state === 0x105
                ? "系统释放"
                : `0x${record.state.toString(16)}`,
        record.name,
        record.identity,
        record.description,
        `0x${record.flags.toString(16).toUpperCase()}`,
      ],
      row = this.body.insertRow(0);
    this.records.unshift(values);
    for (const value of values) {
      const cell = row.insertCell();
      cell.textContent = value;
    }
    if (this.records.length > 500) {
      this.records.pop();
      this.body.deleteRow(-1);
    }
    this.exportButton.disabled = false;
  }
  export() {
    if (!this.records.length) return;
    const quote = (value) => `"${String(value).replaceAll('"', '""')}"`,
      content =
        "\ufeff" +
        [["时间", "事件", "按键", "虚拟键", "扫描码", "标志"], ...this.records]
          .map((row) => row.map(quote).join(","))
          .join("\r\n"),
      url = URL.createObjectURL(new Blob([content], { type: "text/csv;charset=utf-8" })),
      link = document.createElement("a");
    link.href = url;
    link.download = `ZPigeon-Keyboard-${new Date().toISOString().replaceAll(":", "-")}.csv`;
    link.click();
    URL.revokeObjectURL(url);
  }
}

export class LocationManager {
  constructor(root, { call, notify }) {
    this.root = root;
    this.call = call;
    this.notify = notify;
    this.connected = false;
    root.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <button data-action="refresh">获取位置</button><span data-role="state" class="status"></span>
      </div>
      <div class="system-information">
        <section>
          <h2>当前位置</h2>
          <pre data-role="value">点击“获取位置”读取被控端位置服务当前报告。</pre>
        </section>
      </div>`;
    this.button = root.querySelector("button");
    this.state = root.querySelector("[data-role=state]");
    this.value = root.querySelector("[data-role=value]");
    this.button.onclick = () => this.load();
  }
  activate(connected) {
    this.connected = connected;
    this.button.disabled = !connected;
    this.state.textContent = connected ? "" : "Client 未连接";
  }
  disconnect() {
    this.connected = false;
    this.button.disabled = true;
    this.state.textContent = "Client 未连接";
    this.value.textContent = "点击“获取位置”读取被控端位置服务当前报告。";
  }
  async load() {
    if (!this.connected || this.loading) return;
    this.loading = true;
    this.button.disabled = true;
    this.state.textContent = "正在获取位置…";
    try {
      const record = (await this.call("/api/location"))[0];
      this.value.textContent = record
        ? `纬度: ${record.data.latitude.toFixed(8)}\n经度: ${record.data.longitude.toFixed(8)}\n误差半径: ${record.data.accuracy.toFixed(2)} 米\n海拔: ${record.data.altitude.toFixed(2)} 米\n海拔误差: ${record.data.altitudeAccuracy.toFixed(2)} 米`
        : "位置服务没有返回坐标";
      this.state.textContent = record
        ? `报告时间：${new Date(Number((BigInt(record.value) - 116444736000000000n) / 10000n)).toLocaleString()}`
        : "没有位置报告";
    } catch (error) {
      this.state.textContent = error.message;
      this.notify(error);
    } finally {
      this.loading = false;
      this.button.disabled = !this.connected;
    }
  }
}

export class FontManager extends SnapshotManager {
  constructor(root, { call, notify, filePicker }) {
    super(root, { call, notify, path: "/api/fonts", loading: "正在读取字体…" });
    this.filePicker = filePicker;
    root.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <input data-role="filter" placeholder="筛选字体" /><select data-role="scope">
          <option value="all">全部范围</option>
          <option value="user">当前用户</option>
          <option value="machine">本地计算机</option></select
        ><span data-role="summary" class="status"></span><span class="spacer"></span
        ><button data-action="install">安装</button><button data-action="refresh">刷新</button>
      </div>
      <div class="manager-table">
        <table>
          <thead>
            <tr>
              <th>名称</th>
              <th>范围</th>
              <th>文件</th>
            </tr>
          </thead>
          <tbody></tbody>
        </table>
        <div class="manager-empty">Client 未连接</div>
      </div>
      <div class="context-menu administration-menu" hidden><button class="danger">卸载</button></div>
      <dialog>
        <form>
          <h2>安装字体</h2>
          <label
            >范围<select data-field="scope">
              <option value="user">当前用户</option>
              <option value="machine">本地计算机</option>
            </select></label
          ><label
            >字体文件
            <div class="execution-program">
              <input data-field="path" required readonly /><button type="button" data-action="choose">浏览</button
              ><button type="button" data-action="upload">上传</button
              ><input data-field="upload" type="file" accept=".ttf,.otf,.ttc" hidden /></div
          ></label>
          <div class="dialog-actions">
            <button type="submit">安装</button><button type="button" data-action="cancel">取消</button>
          </div>
        </form>
      </dialog>`;
    this.filter = root.querySelector("[data-role=filter]");
    this.scope = root.querySelector("[data-role=scope]");
    this.summary = root.querySelector("[data-role=summary]");
    this.body = root.querySelector("tbody");
    this.empty = root.querySelector(".manager-empty");
    this.menu = root.querySelector(".context-menu");
    this.dialog = root.querySelector("dialog");
    this.upload = this.dialog.querySelector("[data-field=upload]");
    this.filter.oninput = this.scope.onchange = () => this.render();
    root.querySelector("[data-action=refresh]").onclick = () => this.load(true);
    root.querySelector("[data-action=install]").onclick = () => {
      this.dialog.querySelector("form").reset();
      this.dialog.showModal();
    };
    this.dialog.querySelector("[data-action=cancel]").onclick = () => this.dialog.close();
    this.dialog.querySelector("[data-action=choose]").onclick = async () => {
      const path = await this.filePicker.open({
        mode: "file",
        initialPath: this.dialog.querySelector("[data-field=path]").value,
      });
      if (path) {
        this.dialog.querySelector("[data-field=path]").value = path;
        this.upload.value = "";
      }
    };
    this.dialog.querySelector("[data-action=upload]").onclick = () => this.upload.click();
    this.upload.onchange = () => {
      if (this.upload.files[0]) this.dialog.querySelector("[data-field=path]").value = this.upload.files[0].name;
    };
    this.dialog.querySelector("form").onsubmit = (event) => {
      event.preventDefault();
      this.install();
    };
    this.body.oncontextmenu = (event) => {
      const row = event.target.closest("tr");
      if (!row) return;
      event.preventDefault();
      this.selected = row.record;
      placeMenu(this.menu, event);
    };
    this.menu.querySelector("button").onclick = () => {
      this.menu.hidden = true;
      this.uninstall(this.selected);
    };
    addEventListener("pointerdown", (event) => {
      if (!this.menu.contains(event.target)) this.menu.hidden = true;
    });
  }
  render() {
    const query = this.filter?.value.toLocaleLowerCase() || "",
      scope = this.scope?.value || "all",
      records = this.records.filter(
        (record) =>
          (scope === "all" || record.identity.startsWith(scope + "\n")) &&
          (!query || `${record.name} ${record.detail}`.toLocaleLowerCase().includes(query)),
      );
    this.body?.replaceChildren(
      ...records
        .sort((a, b) => a.name.localeCompare(b.name))
        .map((record) => {
          const row = document.createElement("tr");
          row.record = record;
          for (const value of [record.name, record.flags & 1 ? "当前用户" : "本地计算机", record.detail]) {
            const cell = row.insertCell();
            cell.textContent = value;
            cell.title = value;
          }
          return row;
        }),
    );
    if (this.summary) this.summary.textContent = this.loaded ? `${records.length} 个字体` : "";
    if (this.empty) {
      this.empty.hidden = records.length !== 0;
      if (this.connected && this.loaded && !records.length) this.empty.textContent = "没有字体";
    }
  }
  async install() {
    const scope = this.dialog.querySelector("[data-field=scope]").value,
      file = this.upload.files[0],
      submit = this.dialog.querySelector("[type=submit]");
    let path = this.dialog.querySelector("[data-field=path]").value,
      staging = null;
    if (!path) return;
    submit.disabled = true;
    submit.textContent = file ? "正在上传…" : "正在安装…";
    try {
      if (file) {
        staging = (await this.call("/api/execution/staging", { name: file.name })).path;
        const response = await fetch(apiUrl(`/api/file/upload?path=${encodeURIComponent(staging)}&overwrite=false`), {
          method: "PUT",
          body: file,
        });
        if (!response.ok) throw new Error((await response.text()) || `HTTP ${response.status}`);
        path = staging;
        submit.textContent = "正在安装…";
      }
      await this.call("/api/fonts/control", { action: 8, identity: scope, argument: path });
      this.dialog.close();
      this.notify("字体已安装");
      await this.load(true);
    } catch (error) {
      this.notify(error);
    } finally {
      if (staging)
        try {
          await this.call("/api/file/delete", { path: staging });
        } catch {}
      submit.disabled = false;
      submit.textContent = "安装";
    }
  }
  async uninstall(record) {
    if (!confirm(`确定卸载字体“${record.name}”？`)) return;
    try {
      await this.call("/api/fonts/control", { action: 9, identity: record.identity });
      this.notify("字体已卸载");
      await this.load(true);
    } catch (error) {
      this.notify(error);
    }
  }
}
