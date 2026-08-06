const presets = {
  tcp: { name: "通用 TCP", host: "127.0.0.1", port: "" },
  rdp: { name: "RDP", host: "127.0.0.1", port: 3389 },
  cdp: { name: "CDP（已有调试端点）", host: "127.0.0.1", port: 9222 },
  "windbg-process": { name: "WinDbg 进程服务器", host: "127.0.0.1", port: 5005 },
  "windbg-server": { name: "WinDbg 调试服务器", host: "127.0.0.1", port: 5005 },
};

export class PortForwardManager {
  constructor(host, { call, notify }) {
    this.host = host;
    this.call = call;
    this.notify = notify;
    this.connected = false;
    this.timer = 0;
    this.render();
    this.dialog.querySelector("[value=cancel]").formNoValidate = true;
  }
  render() {
    this.host.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <button data-action="new">新建规则</button><button data-action="refresh">刷新</button
        ><span class="spacer"></span><span data-role="status"></span>
      </div>
      <div class="manager-table port-forward-table">
        <table>
          <thead>
            <tr>
              <th>类型</th>
              <th>协议</th>
              <th>允许来源 IP</th>
              <th>管理端入口</th>
              <th>被控端目标</th>
              <th>连接/映射</th>
              <th>状态</th>
              <th>空闲超时</th>
              <th>自动关闭</th>
              <th>操作</th>
            </tr>
          </thead>
          <tbody></tbody>
        </table>
        <div class="manager-empty" data-role="empty">暂无转发规则</div>
      </div>
      <dialog data-role="editor">
        <form method="dialog">
          <h2>新建端口转发规则</h2>
          <label
            >内置规则<select data-field="kind">
              ${Object.entries(presets)
                .map(([value, item]) => `<option value="${value}">${item.name}</option>`)
                .join("")}
            </select></label
          ><label
            >协议<select data-field="protocol">
              <option value="tcp">TCP</option>
              <option value="udp">UDP</option>
            </select></label
          ><label>允许来源 IP<input data-field="source" required /></label
          ><label>被控端目标主机<input data-field="host" maxlength="255" required /></label
          ><label>被控端目标端口<input data-field="port" type="number" min="1" max="65535" required /></label
          ><label
            >空闲超时（秒）<input data-field="timeout" type="number" min="1" max="86400" value="3600" required
          /></label>
          <p class="muted">TCP 有活动连接时不计空闲时间；UDP 按最后一次数据收发重新计时。</p>
          <div class="dialog-actions"><button value="cancel">取消</button><button value="create">创建</button></div>
        </form>
      </dialog>`;
    this.body = this.host.querySelector("tbody");
    this.empty = this.host.querySelector("[data-role=empty]");
    this.status = this.host.querySelector("[data-role=status]");
    this.dialog = this.host.querySelector("[data-role=editor]");
    this.host.querySelector("[data-action=new]").onclick = () => this.open();
    this.host.querySelector("[data-action=refresh]").onclick = () => this.load();
    this.field("kind").onchange = () => this.applyPreset();
    this.field("protocol").onchange = () => {
      if (this.field("protocol").value === "udp" && this.field("kind").value !== "tcp")
        this.field("kind").value = "tcp";
    };
    this.dialog.onclose = () => {
      if (this.dialog.returnValue === "create") this.create();
    };
  }
  field(name) {
    return this.host.querySelector(`[data-field=${name}]`);
  }
  activate(connected) {
    this.connected = connected;
    this.host.querySelector("[data-action=new]").disabled = !connected;
    this.load();
    clearInterval(this.timer);
    this.timer = setInterval(() => this.load(false), 2000);
  }
  deactivate() {
    clearInterval(this.timer);
    this.timer = 0;
  }
  disconnect() {
    this.connected = false;
    this.deactivate();
    this.host.querySelector("[data-action=new]").disabled = true;
  }
  async load(report = true) {
    try {
      const result = await this.call("/api/remote/forwards");
      this.sourceAddress = result.sourceAddress;
      if (!this.field("source").value) this.field("source").value = this.sourceAddress;
      this.renderRules(result.rules);
    } catch (error) {
      if (report) this.notify(error);
    }
  }
  renderRules(rules) {
    this.body.replaceChildren();
    const publicAddress = publicHost();
    for (const rule of rules) {
      const row = document.createElement("tr"),
        entry = `${publicAddress}:${rule.port}`,
        target = `${rule.targetHost}:${rule.targetPort}`,
        error = rule.status
          ? `${statusType(rule.status.type)} 0x${rule.status.code.toString(16).toUpperCase().padStart(8, "0")}`
          : "",
        values = [
          kindName(rule.kind),
          rule.protocol,
          rule.sourceAddress,
          entry,
          target,
          rule.activeCount,
          stateName(rule.state, rule.protocol),
          `${rule.idleTimeoutSeconds} 秒`,
          rule.idleExpires ? new Date(rule.idleExpires).toLocaleString() : "活动连接中",
        ];
      for (const value of values) {
        const cell = row.insertCell();
        cell.textContent = value;
      }
      const actions = row.insertCell(),
        copy = document.createElement("button"),
        close = document.createElement("button");
      copy.textContent = "复制";
      copy.onclick = () => navigator.clipboard.writeText(copyValue(rule, entry));
      close.textContent = "关闭";
      close.className = "danger";
      close.onclick = () => this.close(rule.id);
      actions.append(copy, close);
      if (error) row.title = error;
      this.body.append(row);
    }
    this.empty.hidden = rules.length !== 0;
    this.status.textContent = `${rules.length} 条规则`;
  }
  open() {
    this.field("kind").value = "tcp";
    this.field("source").value = this.sourceAddress ?? "";
    this.field("timeout").value = "3600";
    this.applyPreset();
    this.dialog.returnValue = "";
    this.dialog.showModal();
    this.field("kind").focus();
  }
  applyPreset() {
    const preset = presets[this.field("kind").value];
    this.field("protocol").value = "tcp";
    this.field("host").value = preset.host;
    this.field("port").value = preset.port;
  }
  async create() {
    try {
      await this.call("/api/remote/forward", {
        kind: this.field("kind").value,
        protocol: this.field("protocol").value,
        sourceAddress: this.field("source").value.trim(),
        host: this.field("host").value.trim(),
        port: Number(this.field("port").value),
        idleTimeoutSeconds: Number(this.field("timeout").value),
      });
      await this.load();
    } catch (error) {
      this.notify(error);
    }
  }
  async close(id) {
    try {
      await this.call(`/api/remote/forward/${id}/close`);
      await this.load();
    } catch (error) {
      this.notify(error);
    }
  }
}

const kindName = (value) =>
  ({
    TCP: "通用",
    UDP: "通用",
    RDP: "RDP",
    CDP: "CDP",
    WinDbgProcess: "WinDbg 进程服务器",
    WinDbgServer: "WinDbg 调试服务器",
  })[value] ?? value;
const stateName = (value, protocol) =>
  value === "Waiting"
    ? protocol === "UDP"
      ? "等待数据"
      : "等待连接"
    : value === "Connected" && protocol === "UDP"
      ? "有活动映射"
      : ({ Connected: "已连接", Expired: "已超时", Closed: "已关闭", Failed: "失败" }[value] ?? value);
const statusType = (value) =>
  ["Success", "NTSTATUS", "Win32", "Winsock", "HRESULT", "Security", "QUIC", "ProcessExit", "ConfigurationManager"][
    value
  ] ?? `Type ${value}`;
const publicHost = () => (location.hostname.includes(":") ? `[${location.hostname}]` : location.hostname);
const copyValue = (rule, address) =>
  rule.kind === "WinDbgProcess"
    ? `windbg -premote tcp:server=${publicHost()},port=${rule.port}`
    : rule.kind === "WinDbgServer"
      ? `windbg -remote tcp:server=${publicHost()},port=${rule.port}`
      : rule.kind === "CDP"
        ? `http://${address}/`
        : address;
