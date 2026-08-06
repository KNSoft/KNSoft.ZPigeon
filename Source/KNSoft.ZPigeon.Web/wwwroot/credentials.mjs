const WINDOWS = 26,
  WEB = 27;
const fileTime = (value) => {
  const ticks = BigInt(value);
  return ticks ? new Date(Number((ticks - 116444736000000000n) / 10000n)).toLocaleString() : "—";
};
const typeName = (value) =>
  ({ 1: "普通凭据", 2: "Windows 凭据", 3: "基于证书的凭据", 5: "普通证书凭据", 6: "扩展域凭据" })[value] ||
  `类型 ${value}`;
const persistName = (flags) => ({ 1: "登录会话", 2: "本机", 3: "企业" })[(flags & 0x300) >> 8] || "—";

export class CredentialManager {
  constructor(root, { call, notify }) {
    this.root = root;
    this.call = call;
    this.notify = notify;
    this.records = [];
    this.kind = WINDOWS;
    this.connected = false;
    this.loaded = false;
    root.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <select data-role="store">
          <option value="26">Windows 凭据</option>
          <option value="27">Web 凭据</option></select
        ><input data-role="filter" placeholder="筛选凭据" /><span data-role="summary" class="status"></span
        ><span class="spacer"></span><button data-role="add">添加凭据</button><button data-role="refresh">刷新</button>
      </div>
      <div class="manager-table credential-list">
        <table>
          <thead>
            <tr>
              <th>目标</th>
              <th>用户名</th>
              <th>类型</th>
              <th>持久性</th>
              <th>上次修改</th>
            </tr>
          </thead>
          <tbody></tbody>
        </table>
        <div class="manager-empty">Client 未连接</div>
      </div>
      <div class="context-menu administration-menu" data-role="menu" hidden></div>
      <dialog data-role="editor">
        <form>
          <h2 data-role="editor-title"></h2>
          <label data-role="type-label"
            >类型<select data-field="type">
              <option value="2">Windows 凭据</option>
              <option value="1">普通凭据</option>
            </select></label
          ><label
            ><span data-role="target-label">Internet 或网络地址</span
            ><input class="dialog-input" data-field="target" required autocomplete="off" /></label
          ><label>用户名<input class="dialog-input" data-field="username" required autocomplete="off" /></label
          ><label
            >密码<input class="dialog-input" data-field="password" type="password" required autocomplete="new-password"
          /></label>
          <div class="dialog-actions">
            <button type="button" data-action="cancel">取消</button><button data-role="save">保存</button>
          </div>
        </form>
      </dialog>
      <dialog data-role="properties">
        <form method="dialog">
          <h2 data-role="properties-title">凭据属性</h2>
          <dl class="details-grid" data-role="details"></dl>
          <section class="credential-secret" data-role="secret" hidden>
            <label>密码<input class="dialog-input" type="password" readonly /></label>
            <div>
              <button type="button" data-action="reveal">显示</button
              ><button type="button" data-action="copy" disabled>复制</button>
            </div>
          </section>
          <div class="dialog-actions"><button value="close">关闭</button></div>
        </form>
      </dialog>`;
    this.store = root.querySelector("[data-role=store]");
    this.filter = root.querySelector("[data-role=filter]");
    this.body = root.querySelector("tbody");
    this.empty = root.querySelector(".manager-empty");
    this.summary = root.querySelector("[data-role=summary]");
    this.menu = root.querySelector("[data-role=menu]");
    this.editor = root.querySelector("[data-role=editor]");
    this.propertiesDialog = root.querySelector("[data-role=properties]");
    this.store.onchange = () => {
      this.kind = Number(this.store.value);
      this.render();
    };
    this.filter.oninput = () => this.render();
    root.querySelector("[data-role=refresh]").onclick = () => this.load(true);
    root.querySelector("[data-role=add]").onclick = () => this.edit();
    this.body.oncontextmenu = (event) => {
      const row = event.target.closest("tr");
      if (row) {
        event.preventDefault();
        this.openMenu(event, row.record);
      }
    };
    this.body.ondblclick = (event) => {
      const row = event.target.closest("tr");
      if (row) this.properties(row.record);
    };
    this.editor.querySelector("[data-action=cancel]").onclick = () => this.editor.close();
    this.editor.querySelector("form").onsubmit = (event) => {
      event.preventDefault();
      this.save();
    };
    this.editor.onclose = () => {
      this.editor.querySelector("[data-field=password]").value = "";
    };
    this.propertiesDialog.querySelector("[data-action=reveal]").onclick = () => this.reveal();
    this.propertiesDialog.querySelector("[data-action=copy]").onclick = () => this.copy();
    this.propertiesDialog.onclose = () => {
      this.secret = null;
      this.propertiesDialog.querySelector("[data-role=secret] input").value = "";
    };
    addEventListener("pointerdown", (event) => {
      if (!this.menu.contains(event.target)) this.menu.hidden = true;
    });
  }
  activate(connected) {
    this.connected = connected;
    if (!connected) {
      this.empty.hidden = false;
      this.empty.textContent = "Client 未连接";
      return;
    }
    if (!this.loaded) this.load();
  }
  disconnect() {
    this.connected = false;
    this.loaded = false;
    this.records = [];
    this.body.replaceChildren();
    this.empty.hidden = false;
    this.empty.textContent = "Client 未连接";
    this.summary.textContent = "";
    this.menu.hidden = true;
  }
  async load(force = false) {
    if (!this.connected || this.loading || (this.loaded && !force)) return;
    this.loading = true;
    this.empty.hidden = false;
    this.empty.textContent = "正在读取凭据…";
    try {
      this.records = await this.call("/api/credentials");
      this.loaded = true;
      this.render();
    } catch (error) {
      this.records = [];
      this.body.replaceChildren();
      this.empty.textContent = error.message;
      this.notify(error);
    } finally {
      this.loading = false;
    }
  }
  render() {
    const query = this.filter.value.toLocaleLowerCase(),
      all = this.records.filter((record) => record.kind === this.kind),
      records = all
        .filter(
          (record) =>
            !query || `${record.name} ${record.description} ${record.detail}`.toLocaleLowerCase().includes(query),
        )
        .sort((a, b) => a.name.localeCompare(b.name));
    this.body.replaceChildren(...records.map((record) => this.row(record)));
    this.summary.textContent = `${all.length} 项`;
    this.empty.hidden = records.length !== 0;
    if (this.connected && this.loaded && !records.length)
      this.empty.textContent = all.length ? "没有匹配的凭据" : "没有凭据";
  }
  row(record) {
    const row = document.createElement("tr"),
      values =
        record.kind === WINDOWS
          ? [
              record.name,
              record.description || "—",
              typeName(record.state),
              persistName(record.flags),
              fileTime(record.value),
            ]
          : [record.name, record.description || "—", "Web 凭据", "—", "—"];
    row.record = record;
    for (const value of values) {
      const cell = row.insertCell();
      cell.textContent = value;
      cell.title = value;
    }
    return row;
  }
  openMenu(event, record) {
    const editable = record.kind === WEB || record.state === 1 || record.state === 2,
      actions = [
        ["属性", () => this.properties(record)],
        ...(record.kind === WEB ? [["显示密码", () => this.properties(record, true)]] : []),
        ["修改", () => this.edit(record), false, !editable],
        ["删除", () => this.remove(record), true],
      ];
    this.menu.replaceChildren(
      ...actions.map(([title, action, danger, disabled]) => {
        const button = document.createElement("button");
        button.textContent = title;
        button.classList.toggle("danger", danger === true);
        button.disabled = disabled === true;
        button.onclick = () => {
          this.menu.hidden = true;
          action();
        };
        return button;
      }),
    );
    this.menu.hidden = false;
    const box = this.menu.getBoundingClientRect();
    this.menu.style.left = `${Math.max(6, Math.min(event.clientX, innerWidth - box.width - 6))}px`;
    this.menu.style.top = `${Math.max(6, Math.min(event.clientY, innerHeight - box.height - 6))}px`;
  }
  edit(record = null) {
    const web = (record?.kind ?? this.kind) === WEB,
      form = this.editor.querySelector("form"),
      target = this.editor.querySelector("[data-field=target]"),
      username = this.editor.querySelector("[data-field=username]"),
      type = this.editor.querySelector("[data-field=type]");
    this.target = record;
    this.editKind = web ? WEB : WINDOWS;
    form.reset();
    this.editor.querySelector("[data-role=editor-title]").textContent = record
      ? "修改凭据"
      : `添加${web ? " Web" : " Windows"}凭据`;
    this.editor.querySelector("[data-role=type-label]").hidden = web;
    this.editor.querySelector("[data-role=target-label]").textContent = web ? "网站或资源" : "Internet 或网络地址";
    type.value = String(record?.state || 2);
    type.disabled = record !== null;
    target.value = record?.name || "";
    target.disabled = record !== null;
    username.value = record?.description || "";
    username.disabled = record?.kind === WEB;
    this.editor.showModal();
    this.editor.querySelector(record ? "[data-field=password]" : "[data-field=target]").focus();
  }
  async save() {
    const field = (name) => this.editor.querySelector(`[data-field=${name}]`),
      target = field("target").value.trim(),
      userName = field("username").value.trim(),
      secret = field("password").value;
    if (!target || !userName) return;
    try {
      await this.call("/api/credentials/control", {
        action: this.target ? 23 : 1,
        store: this.editKind === WEB ? 2 : 1,
        type: Number(field("type").value),
        identity: this.target?.identity || null,
        target,
        userName,
        secret,
      });
      field("password").value = "";
      this.editor.close();
      this.notify(this.target ? "凭据已修改" : "凭据已添加");
      await this.load(true);
    } catch (error) {
      field("password").value = "";
      this.notify(error);
    }
  }
  async remove(record) {
    if (!confirm(`确定删除凭据“${record.name}”？`)) return;
    try {
      await this.call("/api/credentials/control", {
        action: 2,
        store: record.kind === WEB ? 2 : 1,
        type: record.state,
        identity: record.identity,
      });
      this.notify("凭据已删除");
      await this.load(true);
    } catch (error) {
      this.notify(error);
    }
  }
  properties(record, reveal = false) {
    this.propertyRecord = record;
    this.secret = null;
    this.propertiesDialog.querySelector("[data-role=properties-title]").textContent = `凭据属性 - ${record.name}`;
    const fields =
      record.kind === WINDOWS
        ? [
            ["目标", record.name],
            ["用户名", record.description || "—"],
            ["类型", typeName(record.state)],
            ["持久性", persistName(record.flags)],
            ["上次修改", fileTime(record.value)],
            ["备注", record.detail || "—"],
          ]
        : [
            ["网站或资源", record.name],
            ["用户名", record.description || "—"],
            ["类型", "Web 凭据"],
          ];
    this.propertiesDialog.querySelector("[data-role=details]").replaceChildren(
      ...fields.flatMap(([name, value]) => {
        const dt = document.createElement("dt"),
          dd = document.createElement("dd");
        dt.textContent = name;
        dd.textContent = value;
        return [dt, dd];
      }),
    );
    const section = this.propertiesDialog.querySelector("[data-role=secret]"),
      input = section.querySelector("input");
    section.hidden = record.kind !== WEB;
    input.type = "password";
    input.value = "";
    section.querySelector("[data-action=reveal]").textContent = "显示";
    section.querySelector("[data-action=copy]").disabled = true;
    this.propertiesDialog.showModal();
    if (reveal) this.reveal();
  }
  async reveal() {
    const record = this.propertyRecord;
    if (!record || record.kind !== WEB) return;
    const section = this.propertiesDialog.querySelector("[data-role=secret]"),
      button = section.querySelector("[data-action=reveal]"),
      input = section.querySelector("input");
    if (this.secret !== null) {
      input.type = input.type === "password" ? "text" : "password";
      button.textContent = input.type === "password" ? "显示" : "隐藏";
      return;
    }
    button.disabled = true;
    try {
      const value = await this.call("/api/credentials/secret", { identity: record.identity });
      this.secret = value.secret;
      input.value = this.secret;
      input.type = "text";
      button.textContent = "隐藏";
      section.querySelector("[data-action=copy]").disabled = false;
    } catch (error) {
      this.notify(error);
    } finally {
      button.disabled = false;
    }
  }
  async copy() {
    if (this.secret === null) return;
    try {
      await navigator.clipboard.writeText(this.secret);
      this.notify("密码已复制到管理端剪贴板");
    } catch (error) {
      this.notify(error);
    }
  }
}
