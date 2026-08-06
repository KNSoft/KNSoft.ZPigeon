import { t } from "./i18n.mjs";

const PROXY = 57;

export class ProxyVpnManager {
  constructor(root, { call, notify }) {
    this.root = root;
    this.call = call;
    this.notify = notify;
    root.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <strong>代理与 VPN</strong><span class="spacer"></span><button data-action="refresh">刷新</button>
      </div>
      <div class="system-information">
        <section>
          <header class="system-section-title"><h2>当前用户 / 浏览器代理</h2></header>
          <form data-role="wininet" class="sandbox-property-grid">
            <label class="property-choice"><input data-field="manual" type="checkbox" />手动代理</label
            ><label>代理服务器<input data-field="proxy" placeholder="http=host:port;https=host:port" /></label
            ><label>绕过列表<input data-field="bypass" placeholder="&lt;local&gt;;*.example.com" /></label
            ><label class="property-choice"><input data-field="auto-detect" type="checkbox" />自动检测</label
            ><label>自动配置脚本<input data-field="pac" type="url" /></label
            ><div class="dialog-actions"><button type="submit">保存</button></div>
          </form>
        </section>
        <section>
          <header class="system-section-title"><h2>WinHTTP 代理</h2></header>
          <form data-role="winhttp" class="sandbox-property-grid">
            <label class="property-choice"><input data-field="manual" type="checkbox" />使用代理</label
            ><label>代理服务器<input data-field="proxy" placeholder="host:port" /></label
            ><label>绕过列表<input data-field="bypass" placeholder="&lt;local&gt;;*.example.com" /></label
            ><div class="dialog-actions"><button type="submit">保存</button></div>
          </form>
        </section>
        <section>
          <header class="system-section-title"><h2>VPN 配置</h2></header>
          <div class="manager-table">
            <table><thead><tr><th>名称</th><th>状态</th><th>操作</th></tr></thead><tbody></tbody></table>
            <div class="manager-empty">Client 未连接</div>
          </div>
        </section>
      </div>`;
    this.empty = root.querySelector(".manager-empty");
    this.body = root.querySelector("tbody");
    this.wininet = root.querySelector("[data-role=wininet]");
    this.winhttp = root.querySelector("[data-role=winhttp]");
    root.querySelector("[data-action=refresh]").onclick = () => this.load(true);
    this.wininet.onsubmit = (event) => {
      event.preventDefault();
      this.saveWinInet();
    };
    this.winhttp.onsubmit = (event) => {
      event.preventDefault();
      this.saveWinHttp();
    };
  }
  field(form, name) {
    return form.querySelector(`[data-field="${name}"]`);
  }
  activate(connected) {
    this.connected = connected;
    for (const button of this.root.querySelectorAll("button")) button.disabled = !connected;
    if (connected && !this.loaded) this.load();
  }
  disconnect() {
    this.connected = false;
    this.loaded = false;
    this.records = [];
    this.body.replaceChildren();
    this.empty.hidden = false;
    this.empty.textContent = "Client 未连接";
  }
  async load(force = false) {
    if (!this.connected || this.loading || (this.loaded && !force)) return;
    this.loading = true;
    this.empty.hidden = false;
    this.empty.textContent = "正在读取代理与 VPN 配置…";
    try {
      this.records = await this.call("/api/proxy-vpn");
      this.renderProxy("wininet", this.wininet);
      this.renderProxy("winhttp", this.winhttp);
      this.renderVpn();
      this.loaded = true;
    } catch (error) {
      this.empty.textContent = error.message;
      this.notify(error);
    } finally {
      this.loading = false;
    }
  }
  renderProxy(identity, form) {
    const record = this.records.find((value) => value.kind === PROXY && value.identity === identity),
      values = (record?.detail || "").split("\n");
    if (identity === "wininet") {
      this.field(form, "manual").checked = !!(record?.flags & 2);
      this.field(form, "proxy").value = values[0] || "";
      this.field(form, "bypass").value = values[1] || "";
      this.field(form, "pac").value = values[2] || "";
      this.field(form, "auto-detect").checked = !!(record?.flags & 8);
    } else {
      this.field(form, "manual").checked = !!record?.state;
      this.field(form, "proxy").value = values[0] || "";
      this.field(form, "bypass").value = values[1] || "";
    }
  }
  renderVpn() {
    const records = this.records.filter((record) => record.kind !== PROXY);
    this.body.replaceChildren(
      ...records.map((record) => {
        const row = document.createElement("tr"),
          actions = document.createElement("td"),
          disconnect = document.createElement("button"),
          remove = document.createElement("button");
        for (const value of [record.name, record.state ? "已连接" : "未连接"]) {
          const cell = row.insertCell();
          cell.textContent = value;
        }
        disconnect.textContent = "断开";
        disconnect.disabled = !record.state;
        disconnect.onclick = () => this.controlVpn(record, 25);
        remove.textContent = "删除";
        remove.className = "danger";
        remove.onclick = () => this.controlVpn(record, 2);
        actions.append(disconnect, remove);
        row.append(actions);
        return row;
      }),
    );
    this.empty.hidden = records.length !== 0;
    this.empty.textContent = "没有 VPN 配置";
  }
  async saveWinInet() {
    const manual = this.field(this.wininet, "manual").checked,
      proxy = this.field(this.wininet, "proxy").value.trim(),
      bypass = this.field(this.wininet, "bypass").value.trim(),
      pac = this.field(this.wininet, "pac").value.trim(),
      flags = 1 | (manual && proxy ? 2 : 0) | (pac ? 4 : 0) | (this.field(this.wininet, "auto-detect").checked ? 8 : 0);
    await this.saveProxy("wininet", `${flags}\n${proxy}\n${bypass}\n${pac}`);
  }
  async saveWinHttp() {
    const proxy = this.field(this.winhttp, "manual").checked
        ? this.field(this.winhttp, "proxy").value.trim()
        : "",
      bypass = this.field(this.winhttp, "bypass").value.trim();
    await this.saveProxy("winhttp", `${proxy}\n${bypass}`);
  }
  async saveProxy(identity, argument) {
    try {
      await this.call("/api/proxy-vpn/control", { action: 23, identity, argument });
      await this.load(true);
    } catch (error) {
      this.notify(error);
    }
  }
  async controlVpn(record, action) {
    const message = t(action === 2 ? "proxy.confirmDelete" : "proxy.confirmDisconnect", { name: record.identity });
    if (!confirm(message)) return;
    try {
      await this.call("/api/proxy-vpn/control", { action, identity: record.identity });
      await this.load(true);
    } catch (error) {
      this.notify(error);
    }
  }
}
