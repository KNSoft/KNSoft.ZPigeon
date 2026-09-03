import { windowsLocales, windowsTimeZones } from "./windows-options.mjs";
import { apiUrl, postBinary } from "./client-context.mjs";
import { certificateStoreName } from "./certificate-install.mjs";
import { t } from "./i18n.mjs";

const text = (value) => value ?? "";
const fileTime = (value) => {
  const ticks = BigInt(value);
  return ticks ? new Date(Number((ticks - 116444736000000000n) / 10000n)).toLocaleString() : "";
};
const logonType = (value) =>
  ({
    2: "交互式",
    3: "网络",
    4: "批处理",
    5: "服务",
    7: "解锁",
    8: "网络（明文凭据）",
    9: "新凭据",
    10: "远程交互式",
    11: "缓存交互式",
    12: "缓存远程交互式",
    13: "缓存解锁",
  })[value] || value;
const statusHex = (value) => `0x${Number(value).toString(16).toUpperCase().padStart(8, "0")}`;
const logonDetail = (record) =>
  record.flags & 0x80000000
    ? `NTSTATUS: ${statusHex(record.state)}`
    : `会话 ${record.data.sessionId} · ${record.data.authenticationPackage}\nUPN: ${record.data.userPrincipalName}\n登录服务器: ${record.data.logonServer}\nDNS 域: ${record.data.dnsDomain}`;
const details = (fields) =>
  fields.flatMap(([name, value]) => {
    const dt = document.createElement("dt"),
      dd = document.createElement("dd");
    dt.textContent = name;
    dd.textContent = text(value) || "—";
    return [dt, dd];
  });
const placeMenu = (menu, event) => {
  menu.hidden = false;
  const box = menu.getBoundingClientRect();
  menu.style.left = `${Math.max(6, Math.min(event.clientX, innerWidth - box.width - 6))}px`;
  menu.style.top = `${Math.max(6, Math.min(event.clientY, innerHeight - box.height - 6))}px`;
};

export class AdministrationManager {
  constructor(root, { call, notify, path, columns, actions = [] }) {
    this.root = root;
    this.call = call;
    this.notify = notify;
    this.path = path;
    this.columns = columns;
    this.actions = actions;
    this.records = [];
    this.loaded = false;
    this.connected = false;
    root.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <input data-role="filter" placeholder="筛选" /><span class="spacer"></span
        ><button data-role="refresh">刷新</button>
      </div>
      <div class="manager-table">
        <table>
          <thead>
            <tr>
              ${columns.map((value) => `<th>${value.title}</th>`).join("")}
            </tr>
          </thead>
          <tbody></tbody>
        </table>
        <div class="manager-empty">Client 未连接</div>
      </div>
      <div class="context-menu administration-menu" hidden></div>`;
    this.filter = root.querySelector("[data-role=filter]");
    this.body = root.querySelector("tbody");
    this.empty = root.querySelector(".manager-empty");
    this.menu = root.querySelector(".administration-menu");
    this.filter.oninput = () => this.render();
    root.querySelector("[data-role=refresh]").onclick = () => this.load(true);
    this.body.oncontextmenu = (event) => {
      const row = event.target.closest("tr");
      if (!row) return;
      event.preventDefault();
      this.openMenu(event, row.record);
    };
    document.addEventListener("pointerdown", (event) => {
      if (!this.menu.hidden && !this.menu.contains(event.target)) this.menu.hidden = true;
    });
  }
  activate(connected) {
    this.connected = connected;
    if (!connected) {
      this.empty.textContent = "Client 未连接";
      return;
    }
    if (!this.loaded) this.load();
  }
  disconnect() {
    this.connected = false;
    this.loaded = this.loading = false;
    this.request = (this.request || 0) + 1;
    this.records = [];
    this.render();
    this.empty.textContent = "Client 未连接";
  }
  async load(force = false) {
    if (!this.connected || this.loading || (this.loaded && !force)) return;
    const request = (this.request || 0) + 1;
    this.request = request;
    this.loading = true;
    this.records = [];
    this.body.replaceChildren();
    this.empty.hidden = false;
    this.empty.textContent = "正在读取…";
    try {
      const records = await this.call(`/api/${this.path}`);
      if (request !== this.request) return;
      this.records = records;
      this.loaded = true;
      this.render();
    } catch (error) {
      if (request !== this.request) return;
      this.empty.textContent = error.message;
      this.notify(error);
    } finally {
      if (request === this.request) this.loading = false;
    }
  }
  render() {
    const query = this.filter.value.toLocaleLowerCase(),
      values = this.records.filter(
        (record) => !query || Object.values(record).some((value) => String(value).toLocaleLowerCase().includes(query)),
      );
    this.body.replaceChildren(
      ...values.map((record) => {
        const row = document.createElement("tr");
        row.record = record;
        for (const column of this.columns) {
          const cell = row.insertCell();
          cell.textContent = text(column.value(record));
          cell.title = cell.textContent;
        }
        return row;
      }),
    );
    this.empty.hidden = values.length !== 0;
    if (this.connected && this.loaded && values.length === 0) this.empty.textContent = "没有项目";
  }
  openMenu(event, record) {
    this.menu.replaceChildren(
      ...this.actions
        .filter((action) => !action.visible || action.visible(record))
        .map((action) => {
          const button = document.createElement("button");
          button.textContent = action.title;
          button.classList.toggle("danger", action.danger === true);
          button.onclick = () => {
            this.menu.hidden = true;
            this.execute(action, record);
          };
          return button;
        }),
    );
    if (this.menu.children.length) placeMenu(this.menu, event);
  }
  async execute(action, record) {
    try {
      const request = await action.request(record);
      if (request === null) return;
      await this.call(`/api/${this.path}/control`, request);
      this.notify("操作成功");
      await this.load(true);
    } catch (error) {
      this.notify(error);
    }
  }
}

export class UserManager extends AdministrationManager {
  constructor(root, options) {
    super(root, options);
    this.kind = 1;
    this.userColumns = this.columns;
    this.userActions = this.actions;
    this.datasets = new Map();
    const selector = document.createElement("select");
    selector.append(
      new Option("用户", "1"),
      new Option(t("profile.title"), "69"),
      new Option("会话", "40"),
      new Option("登录会话", "41"),
    );
    selector.onchange = () => {
      this.kind = Number(selector.value);
      this.records = this.datasets.get(this.kind) || [];
      this.loaded = this.datasets.has(this.kind);
      this.render();
      if (this.connected && !this.loaded) this.load();
    };
    this.filter.before(selector);
    const button = document.createElement("button");
    button.textContent = "新建用户";
    button.dataset.role = "new-user";
    root.querySelector("[data-role=refresh]").before(button);
    root.insertAdjacentHTML(
      "beforeend",
      /* HTML */ `<dialog data-role="user-editor">
          <form>
            <h2></h2>
            <label>用户名<input class="dialog-input" data-field="name" required autocomplete="off" /></label
            ><label data-role="description"
              >描述<input class="dialog-input" data-field="description" autocomplete="off" /></label
            ><label
              >密码<input
                class="dialog-input"
                data-field="password"
                type="password"
                required
                autocomplete="new-password" /></label
            ><label
              >确认密码<input
                class="dialog-input"
                data-field="confirmation"
                type="password"
                required
                autocomplete="new-password"
            /></label>
            <div class="dialog-actions">
              <button type="button" data-action="cancel">取消</button><button data-role="submit"></button>
            </div>
          </form>
        </dialog>
        <dialog data-role="user-rename">
          <form>
            <h2>重命名用户</h2>
            <label>新用户名<input class="dialog-input" required autocomplete="off" /></label>
            <div class="dialog-actions">
              <button type="button" data-action="cancel">取消</button><button>重命名</button>
            </div>
          </form>
        </dialog>
        <dialog data-role="profile-type">
          <form>
            <h2>${t("profile.changeType")}</h2>
            <div class="dialog-summary" data-field="account"></div>
            <label>${t("common.type")}<select data-field="type">
              <option value="local">${t("profile.local")}</option>
              <option value="roaming">${t("profile.roaming")}</option>
            </select></label>
            <label data-role="roaming-path">${t("profile.roamingPath")}
              <input data-field="path" maxlength="32767" placeholder="\\\\server\\profiles\\user" />
            </label>
            <div class="dialog-actions">
              <button type="button" data-action="cancel">${t("common.cancel")}</button
              ><button>${t("common.apply")}</button>
            </div>
          </form>
        </dialog>
        <dialog data-role="profile-copy">
          <form>
            <h2>${t("profile.copyTo")}</h2>
            <div class="dialog-summary" data-field="source"></div>
            <label>${t("profile.destination")}<input data-field="path" maxlength="32767" required /></label>
            <p class="muted">${t("profile.copyHint")}</p>
            <div class="dialog-actions">
              <button type="button" data-action="cancel">${t("common.cancel")}</button
              ><button>${t("common.copy")}</button>
            </div>
          </form>
        </dialog>`,
    );
    this.dialog = root.querySelector("[data-role=user-editor]");
    this.renameDialog = root.querySelector("[data-role=user-rename]");
    this.profileTypeDialog = root.querySelector("[data-role=profile-type]");
    this.profileCopyDialog = root.querySelector("[data-role=profile-copy]");
    button.onclick = () => this.edit();
    this.dialog.querySelector("[data-action=cancel]").onclick = () => this.dialog.close();
    this.dialog.querySelector("form").onsubmit = (event) => {
      event.preventDefault();
      this.save();
    };
    this.renameDialog.querySelector("[data-action=cancel]").onclick = () => this.renameDialog.close();
    this.renameDialog.querySelector("form").onsubmit = (event) => {
      event.preventDefault();
      this.saveRename();
    };
    const profileType = this.profileTypeDialog.querySelector("[data-field=type]");
    profileType.onchange = () => {
      const roaming = profileType.value === "roaming",
        path = this.profileTypeDialog.querySelector("[data-field=path]");
      this.profileTypeDialog.querySelector("[data-role=roaming-path]").hidden = !roaming;
      path.required = roaming;
    };
    this.profileTypeDialog.querySelector("[data-action=cancel]").onclick = () => this.profileTypeDialog.close();
    this.profileTypeDialog.querySelector("form").onsubmit = (event) => this.saveProfileType(event);
    this.profileCopyDialog.querySelector("[data-action=cancel]").onclick = () => this.profileCopyDialog.close();
    this.profileCopyDialog.querySelector("form").onsubmit = (event) => this.saveProfileCopy(event);
  }
  disconnect() {
    this.datasets.clear();
    super.disconnect();
  }
  async load(force = false) {
    if (!this.connected || this.loading || (this.datasets.has(this.kind) && !force)) return;
    const kind = this.kind,
      path = kind === 1 ? "users" : kind === 69 ? "user-profiles" : kind === 40 ? "sessions" : "logon-sessions";
    this.loading = true;
    this.empty.hidden = false;
    this.empty.textContent = "正在读取…";
    try {
      const records = await this.call(`/api/${path}`);
      this.datasets.set(kind, records);
      if (this.kind === kind) {
        this.records = records;
        this.loaded = true;
        this.render();
      }
    } catch (error) {
      if (this.kind === kind) {
        this.empty.textContent = error.message;
        this.notify(error);
      }
    } finally {
      this.loading = false;
      if (this.kind !== kind && !this.datasets.has(this.kind)) this.load();
    }
  }
  render() {
    const definitions = {
        1: { columns: this.userColumns, empty: "没有用户" },
        69: {
          columns: [
            { title: t("profile.name"), value: (r) => r.name },
            { title: t("common.path"), value: (r) => r.description },
            { title: t("profile.size"), value: (r) => formatBytes(r.value) },
            { title: t("common.type"), value: (r) => (r.flags & 4 ? t("profile.roaming") : t("profile.local")) },
            {
              title: t("common.status"),
              value: (r) =>
                r.flags & 1 ? t("profile.inUse") : r.flags & 2 ? t("profile.system") : t("profile.available"),
            },
            { title: t("profile.lastUsed"), value: (r) => fileTime(String(r.detail || "").split("\n")[0]) },
          ],
          empty: t("profile.empty"),
        },
        40: {
          columns: [
            { title: "会话 ID", value: (r) => (r.flags & 0x80000000 ? "—" : r.value) },
            { title: "用户", value: (r) => (r.flags & 0x80000000 ? "—" : r.name) },
            { title: "Microsoft 账户", value: (r) => (r.flags & 0x80000000 ? "—" : r.detail || "—") },
            { title: "会话名称", value: (r) => (r.flags & 0x80000000 ? "—" : r.description) },
            {
              title: "状态",
              value: (r) =>
                r.flags & 0x80000000
                  ? "无法枚举"
                  : [
                      "活动",
                      "已连接",
                      "正在连接",
                      "影子",
                      "已断开",
                      "空闲",
                      "正在侦听",
                      "正在重置",
                      "已关闭",
                      "正在初始化",
                    ][r.state] || r.state,
            },
            { title: "详细信息", value: (r) => (r.flags & 0x80000000 ? `Win32: ${statusHex(r.state)}` : "") },
          ],
          empty: "没有会话",
        },
        41: {
          columns: [
            { title: "登录 ID", value: (r) => (r.flags & 0x80000000 ? "—" : r.identity.slice(6)) },
            { title: "用户", value: (r) => (r.flags & 0x80000000 ? "—" : r.name) },
            { title: "域", value: (r) => (r.flags & 0x80000000 ? "—" : r.description) },
            {
              title: "Microsoft 账户 / UPN",
              value: (r) => (r.flags & 0x80000000 ? "—" : r.data.userPrincipalName || "—"),
            },
            { title: "登录类型", value: (r) => (r.flags & 0x80000000 ? "无法枚举" : logonType(r.state)) },
            { title: "登录时间", value: (r) => (r.flags & 0x80000000 ? "—" : fileTime(r.value)) },
            { title: "详细信息", value: logonDetail },
          ],
          empty: "没有登录会话",
        },
      },
      definition = definitions[this.kind],
      query = this.filter.value.toLocaleLowerCase(),
      values = this.records.filter(
        (record) =>
          !query ||
          Object.values(record).some((value) =>
            (value && typeof value === "object" ? JSON.stringify(value) : String(value))
              .toLocaleLowerCase()
              .includes(query),
          ),
      );
    this.columns = definition.columns;
    this.root.querySelector("thead tr").innerHTML = this.columns.map((value) => `<th>${value.title}</th>`).join("");
    this.root.querySelector("[data-role=new-user]").hidden = this.kind !== 1;
    this.body.replaceChildren(
      ...values.map((record) => {
        const row = document.createElement("tr");
        row.record = record;
        for (const column of this.columns) {
          const cell = row.insertCell();
          cell.textContent = text(column.value(record));
          cell.title = record.flags & 0x80000000
            ? record.kind === 40
              ? `Win32: ${statusHex(record.state)}`
              : `NTSTATUS: ${statusHex(record.state)}`
            : cell.textContent;
        }
        return row;
      }),
    );
    this.empty.hidden = values.length !== 0;
    if (this.connected && this.loaded && !values.length) this.empty.textContent = definition.empty;
  }
  openMenu(event, record) {
    if (record.kind === 1) {
      this.actions = this.userActions;
      super.openMenu(event, record);
      return;
    }
    if (record.kind === 69) {
      this.openProfileMenu(event, record);
      return;
    }
    if (record.kind !== 40) return;
    this.menu.replaceChildren(
      ...[
        ["断开", 25],
        ["注销", 19],
      ].map(([title, action]) => {
        const button = document.createElement("button");
        button.textContent = title;
        button.onclick = async () => {
          this.menu.hidden = true;
          try {
            await this.call("/api/users/control", { action, identity: record.identity });
            this.notify("操作成功");
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
  openProfileMenu(event, record) {
    const loaded = Boolean(record.flags & 1),
      special = Boolean(record.flags & 2),
      entries = [
        [t("profile.changeType"), () => this.changeProfileType(record), false, special],
        [t("common.delete"), () => this.deleteProfile(record), true, loaded || special],
        [t("profile.copyTo"), () => this.copyProfile(record), false, loaded || special],
      ];
    this.menu.replaceChildren(
      ...entries.map(([title, handler, danger, disabled]) => {
        const button = document.createElement("button");
        button.textContent = title;
        button.classList.toggle("danger", danger);
        button.disabled = disabled;
        button.onclick = () => {
          this.menu.hidden = true;
          handler();
        };
        return button;
      }),
    );
    placeMenu(this.menu, event);
  }
  changeProfileType(record) {
    this.profileTarget = record;
    const detail = String(record.detail || "").split("\n"),
      type = this.profileTypeDialog.querySelector("[data-field=type]"),
      path = this.profileTypeDialog.querySelector("[data-field=path]");
    this.profileTypeDialog.querySelector("[data-field=account]").textContent = `${record.name} · ${record.identity}`;
    type.value = record.flags & 4 ? "roaming" : "local";
    path.value = detail[2] || "";
    type.onchange();
    this.profileTypeDialog.showModal();
    (type.value === "roaming" ? path : type).focus();
  }
  async saveProfileType(event) {
    event.preventDefault();
    const roaming = this.profileTypeDialog.querySelector("[data-field=type]").value === "roaming",
      path = this.profileTypeDialog.querySelector("[data-field=path]").value.trim();
    if (roaming && !path) return;
    if (await this.runProfile({ action: 23, identity: this.profileTarget.identity, argument: roaming ? path : null },
      t("profile.typeChanged"))) this.profileTypeDialog.close();
  }
  copyProfile(record) {
    this.profileTarget = record;
    this.profileCopyDialog.querySelector("[data-field=source]").textContent = record.description;
    const path = this.profileCopyDialog.querySelector("[data-field=path]");
    path.value = "";
    this.profileCopyDialog.showModal();
    path.focus();
  }
  async saveProfileCopy(event) {
    event.preventDefault();
    const path = this.profileCopyDialog.querySelector("[data-field=path]").value.trim();
    if (!path) return;
    if (await this.runProfile({ action: 1, identity: this.profileTarget.identity, argument: path },
      t("profile.copied"))) this.profileCopyDialog.close();
  }
  async deleteProfile(record) {
    if (!confirm(t("profile.confirmDelete", { name: record.name, path: record.description }))) return;
    await this.runProfile({ action: 2, identity: record.identity }, t("profile.deleted"));
  }
  async runProfile(request, message) {
    try {
      await this.call("/api/user-profiles/control", request);
      this.notify(message);
      await this.load(true);
      return true;
    } catch (error) {
      this.notify(error);
      return false;
    }
  }
  edit(record = null) {
    this.target = record;
    this.dialog.querySelector("form").reset();
    const name = this.dialog.querySelector("[data-field=name]");
    name.value = record?.identity || "";
    name.disabled = record !== null;
    this.dialog.querySelector("h2").textContent = record ? "设置密码" : "新建用户";
    this.dialog.querySelector("[data-role=description]").hidden = record !== null;
    this.dialog.querySelector("[data-role=submit]").textContent = record ? "保存" : "创建";
    this.dialog.showModal();
    this.dialog.querySelector(record ? "[data-field=password]" : "[data-field=name]").focus();
  }
  rename(record) {
    this.renameTarget = record;
    const input = this.renameDialog.querySelector("input");
    input.value = record.identity;
    this.renameDialog.showModal();
    input.select();
    return null;
  }
  async save() {
    const value = (name) => this.dialog.querySelector(`[data-field=${name}]`).value,
      secret = value("password");
    if (secret !== value("confirmation")) {
      this.notify("两次输入的密码不一致");
      return;
    }
    try {
      await this.call("/api/users/control", {
        action: this.target ? 5 : 1,
        identity: this.target?.identity || value("name").trim(),
        argument: this.target ? null : value("description"),
        secret,
      });
      this.dialog.close();
      this.notify(this.target ? "密码已修改" : "用户已创建");
      await this.load(true);
    } catch (error) {
      this.notify(error);
    }
  }
  async saveRename() {
    const name = this.renameDialog.querySelector("input").value.trim();
    if (!name || name === this.renameTarget.identity) {
      this.renameDialog.close();
      return;
    }
    try {
      await this.call("/api/users/control", { action: 11, identity: this.renameTarget.identity, argument: name });
      this.renameDialog.close();
      this.notify("用户已重命名");
      await this.load(true);
    } catch (error) {
      this.notify(error);
    }
  }
}

export class SoftwareManager extends AdministrationManager {
  constructor(root, { revealRegistry, ...options }) {
    super(root, {
      ...options,
      path: "software",
      columns: [
        { title: t("common.name"), value: (record) => record.name },
        { title: t("software.publisher"), value: (record) => record.description },
        { title: t("software.version"), value: (record) => record.detail },
        { title: t("software.identity"), value: (record) => record.identity },
      ],
    });
    this.revealRegistry = revealRegistry;
    this.softwareColumns = this.columns;
    this.kind = 2;
    this.datasets = new Map();
    this.featureError = "";
    this.root.classList.add("software-manager");
    const selector = document.createElement("select");
    selector.append(
      new Option(t("software.traditional"), "2"),
      new Option(t("software.windowsApp"), "3"),
      new Option(t("software.windowsFeatures"), "4"),
    );
    selector.onchange = () => {
      this.kind = Number(selector.value);
      this.render();
      this.load();
    };
    this.filter.before(selector);
    this.table = root.querySelector(".manager-table");
    this.features = document.createElement("div");
    this.features.className = "administration-tree software-features";
    this.features.hidden = true;
    this.featureTree = document.createElement("ul");
    this.featureEmpty = document.createElement("div");
    this.featureEmpty.className = "manager-empty";
    this.features.append(this.featureTree, this.featureEmpty);
    this.menu.before(this.features);
  }
  disconnect() {
    this.datasets.clear();
    this.featureError = "";
    this.featureTree.replaceChildren();
    super.disconnect();
    if (this.kind === 4) {
      this.featureEmpty.hidden = false;
      this.featureEmpty.textContent = t("common.clientDisconnected");
    }
  }
  async load(force = false) {
    if (!this.connected || this.loading || (!force && this.datasets.has(this.kind))) return;
    const kind = this.kind,
      request = (this.request || 0) + 1,
      feature = kind === 4,
      empty = feature ? this.featureEmpty : this.empty;
    this.request = request;
    this.loading = true;
    empty.hidden = false;
    empty.textContent = t(feature ? "software.loadingFeatures" : "common.fetching");
    try {
      const records = await this.call(feature ? "/api/features" : "/api/software");
      if (request !== this.request) return;
      if (feature) {
        this.datasets.set(4, records);
        this.featureError = "";
      } else {
        this.datasets.set(2, records.filter((record) => record.kind === 2));
        this.datasets.set(3, records.filter((record) => record.kind === 3));
      }
      this.loaded = true;
      this.render();
    } catch (error) {
      if (request !== this.request) return;
      if (feature) this.featureError = error.message;
      empty.textContent = error.message;
      this.notify(error);
    } finally {
      if (request === this.request) {
        this.loading = false;
        if (kind !== this.kind) this.load();
      }
    }
  }
  render() {
    const feature = this.kind === 4;
    this.setColumns(this.softwareColumns);
    this.table.hidden = feature;
    this.features.hidden = !feature;
    if (feature) {
      this.renderFeatures();
      return;
    }
    this.records = this.datasets.get(this.kind) || [];
    super.render();
    if (this.connected && this.datasets.has(this.kind) && !this.empty.hidden)
      this.empty.textContent = t("common.noItems");
  }
  setColumns(columns) {
    if (this.columns === columns) return;
    this.columns = columns;
    this.table
      .querySelector("thead tr")
      .replaceChildren(
        ...columns.map((column) => Object.assign(document.createElement("th"), { textContent: column.title })),
      );
  }
  renderFeatures() {
    const records = this.datasets.get(4) || [],
      nodes = records.filter((record) => record.kind === 4),
      byId = new Map(nodes.map((record) => [record.identity, record])),
      children = new Map(),
      parents = new Map();
    for (const relation of records.filter((record) => record.kind === 76)) {
      if (!byId.has(relation.identity) || !byId.has(relation.name)) continue;
      if (!children.has(relation.name)) children.set(relation.name, new Map());
      children.get(relation.name).set(relation.identity, byId.get(relation.identity));
      if (!parents.has(relation.identity)) parents.set(relation.identity, new Set());
      parents.get(relation.identity).add(relation.name);
    }
    for (const [identity, values] of children)
      children.set(identity, [...values.values()].sort((a, b) => a.name.localeCompare(b.name)));
    nodes.sort((a, b) => a.name.localeCompare(b.name));
    const roots = nodes.filter((record) => !parents.has(record.identity)),
      reachable = new Set(),
      markReachable = (record) => {
        const pending = [record];
        while (pending.length) {
          const item = pending.pop();
          if (reachable.has(item.identity)) continue;
          reachable.add(item.identity);
          pending.push(...(children.get(item.identity) || []));
        }
      };
    roots.forEach(markReachable);
    for (const record of nodes)
      if (!reachable.has(record.identity)) {
        roots.push(record);
        markReachable(record);
      }
    const query = this.filter.value.toLocaleLowerCase(),
      matches = (record) =>
        !query || (() => {
          const pending = [record],
            visited = new Set();
          while (pending.length) {
            const item = pending.pop();
            if (visited.has(item.identity)) continue;
            visited.add(item.identity);
            if (`${item.name} ${item.identity} ${item.description}`.toLocaleLowerCase().includes(query)) return true;
            pending.push(...(children.get(item.identity) || []));
          }
          return false;
        })(),
      visibleRoots = roots.filter(matches);
    this.featureTree.replaceChildren(
      ...visibleRoots.map((record) => this.featureNode(record, children, matches, new Set())),
    );
    this.featureEmpty.hidden = visibleRoots.length !== 0;
    this.featureEmpty.textContent =
      this.featureError ||
      t(nodes.length ? "software.noMatchingFeatures" : "software.noFeatures");
  }
  featureNode(record, children, matches, ancestors) {
    const item = document.createElement("li"),
      row = document.createElement("div"),
      arrow = document.createElement("button"),
      input = document.createElement("input"),
      label = document.createElement("button"),
      list = document.createElement("ul"),
      data = record.data,
      path = new Set(ancestors).add(record.identity),
      values = (children.get(record.identity) || []).filter(
        (child) => !path.has(child.identity) && matches(child),
      ),
      state = data.currentState,
      pending = [1, 3, 5, 6].includes(state),
      mutable = [-19, 0, 2, 4, 7].includes(state);
    row.className = "administration-node-row";
    arrow.className = "administration-arrow";
    arrow.textContent = values.length ? "▸" : "";
    arrow.disabled = !values.length;
    arrow.tabIndex = -1;
    input.type = "checkbox";
    input.checked = state === 6 || state === 7 || state === 8;
    input.indeterminate = state === -19;
    input.disabled = !mutable;
    label.className = "administration-node-label";
    label.textContent = record.name || record.identity;
    if (pending) label.textContent += t("software.featurePending");
    const states = t("software.featureStates", {
        current: this.featureStateName(data.currentState),
        intended: this.featureStateName(data.intendedState),
        requested: this.featureStateName(data.requestedState),
      }),
      capabilities = t("software.featureCapabilities", {
        applicability: this.featureApplicabilityName(data.applicability),
        selectability: this.featureSelectabilityName(data.selectability),
      });
    label.title = [record.identity, record.description, states, capabilities].filter(Boolean).join("\n");
    list.hidden = true;
    row.append(arrow, input, label);
    item.append(row, list);
    const toggle = () => {
      if (!values.length) return;
      list.hidden = !list.hidden;
      arrow.textContent = list.hidden ? "▸" : "▾";
      if (!list.hidden && !list.childElementCount)
        list.append(...values.map((child) => this.featureNode(child, children, matches, path)));
    };
    arrow.onclick = label.onclick = toggle;
    input.onchange = () => this.setFeature(record, input.checked, input);
    return item;
  }
  featureStateName(state) {
    const key = {
      [-19]: "partiallyInstalled",
      0: "absent",
      1: "resolving",
      2: "resolved",
      3: "staging",
      4: "staged",
      5: "uninstallRequested",
      6: "installRequested",
      7: "installed",
      8: "permanent",
    }[state];
    return key ? t(`software.featureState.${key}`) : t("software.featureState.unknown", { state });
  }
  featureApplicabilityName(value) {
    const key = { [-1]: "invalid", 0: "all", 1: "notApplicable", 2: "needsParent", 4: "applicable" }[value];
    return key ? t(`software.featureApplicability.${key}`) : String(value);
  }
  featureSelectabilityName(value) {
    const key = { [-1]: "invalid", 0: "all", 1: "child", 2: "root" }[value];
    return key ? t(`software.featureSelectability.${key}`) : String(value);
  }
  async setFeature(record, enabled, input) {
    input.disabled = true;
    this.featureEmpty.hidden = false;
    this.featureEmpty.textContent = t(enabled ? "software.enablingFeature" : "software.disablingFeature");
    try {
      const result = await this.call("/api/features/control", {
        action: enabled ? 3 : 4,
        identity: record.identity,
      });
      this.notify(t(result.requiredAction === 1 ? "software.featureUpdatedRestart" : "software.featureUpdated"));
    } catch (error) {
      this.notify(error);
    } finally {
      await this.load(true);
    }
  }
  openMenu(event, record) {
    this.menu.replaceChildren();
    if (record.kind === 2) {
      this.addMenuButton(t("software.openRegistry"), () => {
        const base =
          record.flags === 2
            ? "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
            : record.flags === 3
              ? "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
              : "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
        this.revealRegistry(record.flags === 2 ? 2 : 3, `${base}\\${record.identity}`);
      });
    } else if (record.kind === 3) {
      this.addMenuButton(t("packages.action.uninstall"), () => this.uninstallWindowsApp(record), true);
    }
    if (this.menu.children.length) placeMenu(this.menu, event);
  }
  addMenuButton(title, action, danger = false) {
    const button = document.createElement("button");
    button.textContent = title;
    button.classList.toggle("danger", danger);
    button.onclick = () => {
      this.menu.hidden = true;
      action();
    };
    this.menu.append(button);
  }
  async uninstallWindowsApp(record) {
    const name = record.name || record.identity;
    if (!confirm(t("packages.confirmAction", { action: t("packages.action.uninstall"), name }))) return;
    try {
      await this.call("/api/software/windows-app/uninstall", { identity: record.identity, name });
      this.notify(t("packages.jobSubmitted"));
    } catch (error) {
      this.notify(error);
    }
  }
}

export class InputMethodManager extends AdministrationManager {
  constructor(root, options) {
    super(root, {
      ...options,
      path: "input-methods",
      columns: [
        { title: t("common.name"), value: (record) => record.name },
        {
          title: t("inputMethod.language"),
          value: (record) => (record.detail ? `${record.description} (${record.detail})` : record.description),
        },
        {
          title: t("common.type"),
          value: (record) => t(record.flags & 8 ? "inputMethod.inputProcessor" : "inputMethod.keyboardLayout"),
        },
        { title: t("common.status"), value: (record) => this.status(record) },
        { title: t("inputMethod.identity"), value: (record) => record.identity },
      ],
    });
    this.account = document.createElement("span");
    this.account.className = "muted";
    this.filter.before(this.account);
  }
  async load(force = false) {
    if (!this.connected || this.loading || (this.loaded && !force)) return;
    const request = (this.request || 0) + 1;
    this.request = request;
    this.loading = true;
    this.body.replaceChildren();
    this.empty.hidden = false;
    this.empty.textContent = t("inputMethod.loading");
    try {
      const records = await this.call("/api/input-methods");
      if (request !== this.request) return;
      this.context = records.find((record) => record.kind === 61) || null;
      this.records = records.filter((record) => record.kind === 62);
      this.loaded = true;
      this.render();
    } catch (error) {
      if (request !== this.request) return;
      this.empty.textContent = error.message;
      this.notify(error);
    } finally {
      if (request === this.request) this.loading = false;
    }
  }
  disconnect() {
    this.context = null;
    this.account.textContent = t("common.currentAccountPlain");
    super.disconnect();
  }
  render() {
    const context = this.context;
    this.account.textContent = context?.name
      ? t("common.currentAccount", { username: context.name })
      : t("common.currentAccountPlain");
    if (context && !(context.flags & 1)) this.account.textContent += ` · ${t("inputMethod.nonInteractive")}`;
    super.render();
    if (this.connected && this.loaded && !this.records.length) this.empty.textContent = t("inputMethod.empty");
  }
  status(record) {
    const values = [t(record.flags & 1 ? "common.enabled" : "common.disabled")];
    if (record.flags & 2) values.push(t("inputMethod.active"));
    if (record.flags & 4) values.push(t("inputMethod.default"));
    return values.join(" · ");
  }
  openMenu(event, record) {
    const enabled = Boolean(record.flags & 1),
      active = Boolean(record.flags & 2),
      interactive = Boolean(this.context?.flags & 1),
      enabledCount = this.records.filter((value) => value.flags & 1).length,
      actions = [];
    if (!enabled) actions.push([t("common.enable"), 3]);
    else {
      if (!active && interactive) actions.push([t("inputMethod.makeActive"), 21]);
      if (!(record.flags & 4)) actions.push([t("inputMethod.setDefault"), 29]);
      if (!active && enabledCount > 1) actions.push([t("common.disable"), 4]);
    }
    this.menu.replaceChildren(
      ...actions.map(([title, action]) => {
        const button = document.createElement("button");
        button.textContent = title;
        button.onclick = () => {
          this.menu.hidden = true;
          this.control(action, record);
        };
        return button;
      }),
    );
    if (this.menu.children.length) placeMenu(this.menu, event);
  }
  async control(action, record) {
    try {
      await this.call("/api/input-methods/control", { action, identity: record.identity });
      this.notify(t("inputMethod.updated"));
      await this.load(true);
    } catch (error) {
      this.notify(error);
    }
  }
}

export class PackageManager extends AdministrationManager {
  constructor(root, options) {
    super(root, {
      ...options,
      path: "packages/list",
      columns: [
        { title: t("common.name"), value: (record) => record.name },
        { title: t("packages.identity"), value: (record) => record.identity },
        { title: t("packages.version"), value: (record) => record.detail },
        { title: t("packages.source"), value: (record) => record.description },
      ],
    });
    this.providers = [];
    this.datasets = new Map();
    this.jobStates = new Map();
    this.root.classList.add("package-manager");
    this.providerTabs = document.createElement("div");
    this.providerTabs.className = "package-tabs";
    this.providerStatus = document.createElement("span");
    this.providerStatus.className = "muted";
    this.providerBar = document.createElement("div");
    this.providerBar.className = "package-provider-bar";
    this.providerBar.append(this.providerTabs, this.providerStatus);
    this.root.querySelector(".manager-table").before(this.providerBar);
    const toolbar = this.root.querySelector(".manager-toolbar"),
      refresh = toolbar.querySelector("[data-role=refresh]");
    this.uploadButton = document.createElement("button");
    this.uploadButton.textContent = t("packages.uploadInstaller");
    this.installButton = document.createElement("button");
    this.installButton.textContent = t("packages.installPackage");
    this.upgradeAllButton = document.createElement("button");
    this.upgradeAllButton.textContent = t("packages.upgradeAll");
    refresh.before(this.uploadButton, this.installButton, this.upgradeAllButton);
    refresh.onclick = () => (this.provider ? this.load(true) : this.loadProviders(true));
    this.menu.insertAdjacentHTML(
      "beforebegin",
      `<section class="software-jobs">
        <header>
          <b>${t("packages.jobs")}</b><span data-role="job-summary"></span
          ><button data-action="refresh-jobs">${t("common.refresh")}</button>
        </header>
        <div class="manager-table">
          <table>
            <thead>
              <tr>
                <th>${t("packages.operation")}</th>
                <th>${t("packages.provider")}</th>
                <th>${t("common.name")}</th>
                <th>${t("common.status")}</th>
                <th>${t("packages.progress")}</th>
                <th>${t("packages.resultCode")}</th>
                <th>${t("packages.submitted")}</th>
              </tr>
            </thead>
            <tbody></tbody>
          </table>
          <div class="manager-empty">${t("packages.noJobs")}</div>
        </div>
      </section>
      <dialog data-role="package-install">
        <form>
          <h2 data-role="title"></h2>
          <label>${t("packages.identity")}<input
            class="dialog-input" data-field="identity" maxlength="260" autocomplete="off" required
          /></label>
          <label data-role="version">${t("packages.versionOptional")}<input
            class="dialog-input" data-field="version" maxlength="128" autocomplete="off"
          /></label>
          <label data-role="source">${t("packages.source")}<select data-field="source">
            <option value="winget">WinGet</option><option value="msstore">Microsoft Store</option>
          </select></label>
          <label data-role="scope">${t("packages.scope")}<select data-field="scope"></select></label>
          <p class="status">${t("packages.nonInteractiveNote")}</p>
          <div class="dialog-actions">
            <button type="button" data-action="cancel">${t("common.cancel")}</button
            ><button data-action="install">${t("packages.action.install")}</button>
          </div>
        </form>
      </dialog>
      <dialog data-role="installer-upload">
        <form>
          <h2>${t("packages.uploadInstaller")}</h2>
          <label>${t("packages.installer")}<input
            data-field="package" type="file"
            accept=".msi,.appx,.appxbundle,.msix,.msixbundle,.appinstaller" required
          /></label>
          <label>${t("packages.dependenciesOptional")}<input
            data-field="dependencies" type="file" accept=".appx,.msix" multiple
          /></label>
          <p class="status">${t("packages.uploadNote")}</p>
          <div class="dialog-actions">
            <button type="button" data-action="cancel">${t("common.cancel")}</button
            ><button data-action="install">${t("packages.action.install")}</button>
          </div>
        </form>
      </dialog>`,
    );
    this.jobsPanel = root.querySelector(".software-jobs");
    this.jobsBody = this.jobsPanel.querySelector("tbody");
    this.jobsEmpty = this.jobsPanel.querySelector(".manager-empty");
    this.jobSummary = this.jobsPanel.querySelector("[data-role=job-summary]");
    this.installDialog = root.querySelector("[data-role=package-install]");
    this.uploadDialog = root.querySelector("[data-role=installer-upload]");
    this.uploadButton.onclick = () => this.openUpload();
    this.installButton.onclick = () => this.openInstall();
    this.upgradeAllButton.onclick = () => this.controlPackage(4);
    this.jobsPanel.querySelector("[data-action=refresh-jobs]").onclick = () => this.loadJobs(true);
    this.installDialog.querySelector("[data-action=cancel]").onclick = () => this.installDialog.close();
    this.uploadDialog.querySelector("[data-action=cancel]").onclick = () => this.uploadDialog.close();
    this.installDialog.querySelector("form").onsubmit = (event) => {
      event.preventDefault();
      this.installProviderPackage();
    };
    this.uploadDialog.querySelector("form").onsubmit = (event) => {
      event.preventDefault();
      this.uploadInstaller();
    };
    this.installDialog.querySelector("[data-field=source]").onchange = () => this.syncInstallDialog();
    this.installDialog.querySelector("[data-field=identity]").oninput = (event) => {
      if (/^(https:\/\/apps\.microsoft\.com\/|ms-windows-store:)/i.test(event.target.value.trim())) {
        this.installDialog.querySelector("[data-field=source]").value = "msstore";
        this.syncInstallDialog();
      }
    };
    this.syncActions();
  }
  activate(connected) {
    this.connected = connected;
    this.syncActions();
    if (!connected) {
      this.deactivate();
      this.empty.textContent = t("common.clientDisconnected");
      return;
    }
    this.updateJobTimer();
    if (this.providersLoaded) {
      this.renderProviders();
      this.render();
    } else this.loadProviders();
  }
  deactivate() {
    clearInterval(this.jobTimer);
    this.jobTimer = 0;
  }
  disconnect() {
    this.deactivate();
    this.providerRequest = (this.providerRequest || 0) + 1;
    this.jobRequest = (this.jobRequest || 0) + 1;
    this.providersLoaded = this.providersLoading = this.jobsLoading = false;
    this.providers = [];
    this.provider = null;
    this.context = null;
    this.datasets.clear();
    this.jobStates.clear();
    this.providerTabs.replaceChildren();
    this.providerStatus.textContent = "";
    this.jobsBody.replaceChildren();
    this.jobsEmpty.hidden = false;
    this.jobsEmpty.textContent = t("common.clientDisconnected");
    this.jobSummary.textContent = "";
    super.disconnect();
    this.syncActions();
  }
  updateJobTimer() {
    clearInterval(this.jobTimer);
    this.loadJobs();
    this.jobTimer = setInterval(() => this.loadJobs(), 2000);
  }
  async loadProviders(force = false) {
    if (!this.connected || this.providersLoading || (this.providersLoaded && !force)) return;
    const request = (this.providerRequest || 0) + 1;
    this.providerRequest = request;
    this.providersLoading = true;
    this.empty.hidden = false;
    this.empty.textContent = t("packages.detecting");
    try {
      const records = await this.call("/api/packages/providers"),
        context = records.find((record) => record.kind === 63),
        providers = records.filter((record) => record.kind === 64);
      if (request !== this.providerRequest) return;
      if (force) {
        this.datasets.clear();
        this.request = (this.request || 0) + 1;
        this.loading = false;
      }
      this.providers = providers;
      this.context = context;
      this.providersLoaded = true;
      this.provider = providers.find((provider) => provider.identity === this.provider?.identity) || null;
      this.renderProviders(context);
      this.records = this.provider ? this.datasets.get(this.provider.identity) || [] : [];
      this.render();
    } catch (error) {
      if (request !== this.providerRequest) return;
      this.empty.textContent = error.message;
      this.notify(error);
    } finally {
      if (request === this.providerRequest) this.providersLoading = false;
    }
  }
  renderProviders(context = this.context) {
    this.providerTabs.replaceChildren(
      ...this.providers.map((provider) => {
        const button = document.createElement("button");
        button.textContent = this.providerName(provider.identity);
        button.classList.toggle("active", provider === this.provider);
        button.onclick = () => this.selectProvider(provider);
        return button;
      }),
    );
    this.renderProviderStatus(context);
  }
  renderProviderStatus(context) {
    const account = context?.name
        ? t("common.currentAccount", { username: context.name })
        : t("common.currentAccountPlain"),
      details = this.provider
        ? [this.provider.description, this.provider.detail].filter(Boolean).join(" · ")
        : "";
    this.providerStatus.textContent = details ? `${account} · ${details}` : account;
    this.syncActions();
  }
  syncActions() {
    const capabilities = this.provider?.flags || 0;
    this.uploadButton.disabled = !this.connected;
    this.installButton.disabled = !this.connected || !(capabilities & 1);
    this.upgradeAllButton.hidden = !(capabilities & 4);
    this.upgradeAllButton.disabled = !this.connected;
  }
  selectProvider(provider) {
    if (provider === this.provider) {
      if (!this.datasets.has(provider.identity)) this.load();
      return;
    }
    this.provider = provider;
    this.loaded = this.datasets.has(provider.identity);
    this.records = this.datasets.get(provider.identity) || [];
    this.renderProviders();
    this.render();
    this.load();
  }
  async load(force = false) {
    if (!this.connected || !this.provider || this.loading || (!force && this.datasets.has(this.provider.identity)))
      return;
    const provider = this.provider,
      request = (this.request || 0) + 1;
    this.request = request;
    this.loading = true;
    this.empty.hidden = false;
    this.empty.textContent = t("packages.loading", { provider: this.providerName(provider.identity) });
    try {
      const records = await this.call("/api/packages/list", { provider: provider.identity });
      if (request !== this.request) return;
      this.datasets.set(provider.identity, records.filter((record) => record.kind === 59));
      this.loaded = true;
      this.render();
    } catch (error) {
      if (request !== this.request) return;
      this.empty.textContent = error.message;
      this.notify(error);
    } finally {
      if (request === this.request) {
        this.loading = false;
        if (provider !== this.provider) this.load();
      }
    }
  }
  render() {
    this.records = this.provider ? this.datasets.get(this.provider.identity) || [] : [];
    super.render();
    if (!this.connected || this.empty.hidden) return;
    if (!this.provider && this.providersLoaded)
      this.empty.textContent = this.providers.length ? t("packages.chooseProvider") : t("packages.noProviders");
    else if (this.provider && this.datasets.has(this.provider.identity)) this.empty.textContent = t("packages.noPackages");
  }
  providerName(identity) {
    return t(`packages.provider.${identity}`);
  }
  openMenu(event, record) {
    this.menu.replaceChildren();
    const capabilities = this.provider?.flags || 0;
    if (capabilities & 2)
      this.addMenuButton(t("packages.action.upgrade"), () => this.controlPackage(2, record.identity));
    if (capabilities & 8)
      this.addMenuButton(t("packages.action.uninstall"), () => this.controlPackage(3, record.identity), true);
    if (this.menu.children.length) placeMenu(this.menu, event);
  }
  addMenuButton(title, action, danger = false) {
    const button = document.createElement("button");
    button.textContent = title;
    button.classList.toggle("danger", danger);
    button.onclick = () => {
      this.menu.hidden = true;
      action();
    };
    this.menu.append(button);
  }
  openInstall() {
    const provider = this.provider;
    if (!provider) return;
    this.installDialog.querySelector("[data-role=title]").textContent = t("packages.installWith", {
      provider: this.providerName(provider.identity),
    });
    this.installDialog.querySelector("[data-field=identity]").value = "";
    this.installDialog.querySelector("[data-field=version]").value = "";
    this.installDialog.querySelector("[data-field=source]").value = "winget";
    this.syncInstallDialog();
    this.installDialog.showModal();
  }
  syncInstallDialog() {
    const provider = this.provider?.identity,
      winget = provider === "winget",
      source = this.installDialog.querySelector("[data-field=source]"),
      scope = this.installDialog.querySelector("[data-field=scope]");
    this.installDialog.querySelector("[data-role=source]").hidden = !winget;
    this.installDialog.querySelector("[data-role=version]").hidden = winget;
    this.installDialog.querySelector("[data-role=scope]").hidden = provider !== "winget" && provider !== "pip";
    scope.replaceChildren(new Option(t("packages.scopeAuto"), ""));
    if (winget) {
      scope.append(new Option(t("packages.scopeUser"), "user"), new Option(t("packages.scopeMachine"), "machine"));
      scope.disabled = source.value === "msstore";
    } else if (provider === "pip") {
      scope.append(new Option(t("packages.scopeUser"), "user"));
      scope.disabled = false;
    }
  }
  async installProviderPackage() {
    const submit = this.installDialog.querySelector("[data-action=install]"),
      provider = this.provider?.identity;
    if (!provider) return;
    submit.disabled = true;
    submit.textContent = t("packages.submitting");
    try {
      const source = provider === "winget" ? this.installDialog.querySelector("[data-field=source]").value : null,
        scope = this.installDialog.querySelector("[data-field=scope]");
      await this.controlPackage(
        1,
        this.installDialog.querySelector("[data-field=identity]").value,
        provider === "winget" ? null : this.installDialog.querySelector("[data-field=version]").value || null,
        source,
        scope.closest("label").hidden || scope.disabled ? null : scope.value || null,
        false,
      );
      this.installDialog.close();
      this.notify(t("packages.jobSubmitted"));
      await this.loadJobs(true);
    } catch (error) {
      this.notify(error);
    } finally {
      submit.disabled = false;
      submit.textContent = t("packages.action.install");
    }
  }
  openUpload() {
    this.uploadDialog.querySelector("[data-field=package]").value = "";
    this.uploadDialog.querySelector("[data-field=dependencies]").value = "";
    this.uploadDialog.showModal();
  }
  async uploadInstaller() {
    const submit = this.uploadDialog.querySelector("[data-action=install]"),
      packageFile = this.uploadDialog.querySelector("[data-field=package]").files[0];
    if (!packageFile) return;
    submit.disabled = true;
    submit.textContent = t("packages.uploading");
    const dependencies = [...this.uploadDialog.querySelector("[data-field=dependencies]").files],
      staged = [];
    try {
      for (const file of [packageFile, ...dependencies]) {
        const result = await this.call("/api/packages/staging", { name: file.name });
        staged.push(result.path);
        const response = await fetch(
          apiUrl(`/api/file/upload?path=${encodeURIComponent(result.path)}&overwrite=false`),
          { method: "PUT", body: file },
        );
        if (!response.ok) throw new Error((await response.text()) || `HTTP ${response.status}`);
      }
      submit.textContent = t("packages.submitting");
      const job = await this.call("/api/packages/install", {
        path: staged[0],
        name: packageFile.name,
        dependencies: staged.slice(1),
      });
      this.jobStates.set(job.id, job.state);
      staged.length = 0;
      this.uploadDialog.close();
      this.notify(t("packages.jobSubmitted"));
      await this.loadJobs(true);
    } catch (error) {
      this.notify(error);
    } finally {
      await Promise.allSettled(staged.map((path) => this.call("/api/packages/staging/delete", { path })));
      submit.disabled = false;
      submit.textContent = t("packages.action.install");
    }
  }
  async controlPackage(
    action,
    identity = null,
    version = null,
    source = null,
    scope = null,
    confirmAction = true,
  ) {
    const provider = this.provider?.identity;
    if (!provider) return;
    const actionName = t(action === 3 ? "packages.action.uninstall" : "packages.action.upgrade");
    if (
      confirmAction &&
      !confirm(
        t(action === 4 ? "packages.confirmUpgradeAll" : "packages.confirmAction", {
          action: actionName,
          name: identity,
          provider: this.providerName(provider),
        }),
      )
    )
      return;
    try {
      const job = await this.call("/api/packages/control", {
        provider,
        action,
        identity,
        version,
        source:
          provider === "winget" && action !== 3 && action !== 4
            ? source || this.packageSource(identity)
            : null,
        scope,
      });
      this.jobStates.set(job.id, job.state);
      if (confirmAction) {
        this.notify(t("packages.jobSubmitted"));
        await this.loadJobs(true);
      }
    } catch (error) {
      if (confirmAction) this.notify(error);
      else throw error;
    }
  }
  packageSource(identity) {
    const record = this.records.find((value) => value.identity === identity);
    return record?.description?.toLowerCase() === "msstore" ? "msstore" : "winget";
  }
  async loadJobs(report = false) {
    if (!this.connected || this.jobsLoading) return;
    const request = (this.jobRequest || 0) + 1;
    this.jobRequest = request;
    this.jobsLoading = true;
    try {
      const jobs = await this.call("/api/packages/jobs");
      if (request !== this.jobRequest) return;
      const completed = jobs.filter((job) => {
        const previous = this.jobStates.get(job.id);
        return previous != null && previous < 5 && job.state >= 5;
      });
      this.jobsBody.replaceChildren(
        ...jobs.map((job) => {
          const row = document.createElement("tr"),
            error = [job.errorCode, job.installerErrorCode]
              .filter((value) => value != null)
              .map((value) => `0x${value.toString(16).padStart(8, "0").toUpperCase()}`)
              .join(" / "),
            values = [
              t(
                job.action === 3
                  ? "packages.action.uninstall"
                  : job.action === 1
                    ? "packages.action.install"
                    : "packages.action.upgrade",
              ),
              this.engineName(job.engine),
              job.name,
              this.jobStatus(job),
              `${(job.progress / 100).toLocaleString(undefined, { maximumFractionDigits: 2 })}%`,
              error,
              new Date(job.createdTime).toLocaleString(),
            ];
          for (const value of values) row.insertCell().textContent = value;
          if (job.errorText) row.cells[3].title = job.errorText;
          this.jobStates.set(job.id, job.state);
          return row;
        }),
      );
      this.jobsEmpty.hidden = jobs.length !== 0;
      this.jobsEmpty.textContent = t("packages.noJobs");
      this.jobSummary.textContent = t("packages.runningJobs", {
        count: jobs.filter((job) => job.state < 5).length,
      });
      if (completed.length) {
        const currentChanged = completed.some((job) => job.engine === this.provider?.state);
        if (currentChanged) {
          this.datasets.delete(this.provider.identity);
          this.loaded = false;
        }
        this.notify(
          t(completed.some((job) => job.rebootRequired) ? "packages.finishedReboot" : "packages.finished"),
        );
        if (currentChanged) await this.load();
      }
    } catch (error) {
      if (request !== this.jobRequest) return;
      if (report) this.notify(error);
    } finally {
      if (request === this.jobRequest) this.jobsLoading = false;
    }
  }
  jobStatus(job) {
    if (job.state === 1) return t("packages.status.queued");
    if (job.state === 2) return t("packages.status.resolving");
    if (job.state === 3) return t("packages.status.downloading");
    if (job.state === 4) return t("packages.status.installing");
    if (job.state === 5)
      return t(job.rebootRequired ? "packages.status.completedReboot" : "packages.status.completed");
    return t(job.rebootRequired ? "packages.status.failedReboot" : "packages.status.failed");
  }
  engineName(engine) {
    return t(
      ({
        1: "packages.provider.msi",
        2: "packages.provider.appx",
        3: "packages.provider.appinstaller",
        4: "packages.provider.winget",
        5: "packages.provider.pip",
        6: "packages.provider.npm",
        7: "packages.provider.chocolatey",
        8: "packages.provider.dotnet",
      })[engine] || "common.unknown",
    );
  }
}


export class HardwareManager {
  constructor(root, { call, notify, revealRegistry, revealService }) {
    this.root = root;
    this.call = call;
    this.notify = notify;
    this.revealRegistry = revealRegistry;
    this.revealService = revealService;
    this.connected = false;
    root.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <input data-role="filter" placeholder="筛选设备" /><span data-role="summary" class="status"></span
        ><span class="spacer"></span><button data-role="refresh">刷新</button>
      </div>
      <div class="administration-tree">
        <ul data-role="tree"></ul>
        <div class="manager-empty">Client 未连接</div>
      </div>
      <div class="context-menu administration-menu" data-role="menu" hidden>
        <button data-action="enable">启用</button><button data-action="disable">禁用</button
        ><button data-action="restart">重启</button><button data-action="uninstall" class="danger">卸载</button>
        <hr />
        <button data-action="service">转到服务</button><button data-action="registry">转到注册表</button>
        <hr />
        <button data-action="properties">属性</button>
      </div>
      <dialog data-role="properties">
        <form method="dialog">
          <h2 data-role="title"></h2>
          <dl class="details-grid" data-role="body"></dl>
          <div class="dialog-actions"><button value="close">关闭</button></div>
        </form>
      </dialog>`;
    this.filter = root.querySelector("[data-role=filter]");
    this.tree = root.querySelector("[data-role=tree]");
    this.empty = root.querySelector(".manager-empty");
    this.menu = root.querySelector("[data-role=menu]");
    this.filter.oninput = () => this.render();
    root.querySelector("[data-role=refresh]").onclick = () => this.load();
    for (const action of ["enable", "disable", "restart", "uninstall", "service", "registry", "properties"])
      root.querySelector(`[data-action=${action}]`).onclick = () => {
        this.menu.hidden = true;
        this.invoke(action);
      };
    document.addEventListener("pointerdown", (event) => {
      if (!this.menu.contains(event.target)) this.menu.hidden = true;
    });
  }
  activate(connected) {
    this.connected = connected;
    if (!connected) {
      this.empty.textContent = "Client 未连接";
      return;
    }
    if (!this.loaded) this.load();
  }
  disconnect() {
    this.connected = false;
    this.loaded = false;
    this.records = [];
    this.selected = null;
    this.tree.replaceChildren();
    this.empty.hidden = false;
    this.empty.textContent = "Client 未连接";
  }
  async load() {
    if (!this.connected || this.loading) return;
    this.loading = true;
    this.empty.hidden = false;
    this.empty.textContent = "正在读取硬件…";
    try {
      this.records = await this.call("/api/hardware");
      this.records.sort(
        (a, b) => (a.detail || "其他设备").localeCompare(b.detail || "其他设备") || a.name.localeCompare(b.name),
      );
      this.loaded = true;
      this.render();
    } catch (error) {
      this.empty.textContent = error.message;
      this.notify(error);
    } finally {
      this.loading = false;
    }
  }
  render() {
    const records = this.records || [],
      byId = new Map(records.map((item) => [Number(BigInt(item.value) & 0xffffffffn), item])),
      children = new Map();
    for (const item of records) {
      const parent = Number(BigInt(item.value) >> 32n),
        key = byId.has(parent) ? parent : 0;
      if (!children.has(key)) children.set(key, []);
      children.get(key).push(item);
    }
    const query = this.filter.value.toLocaleLowerCase(),
      matches = (item) =>
        !query ||
        Object.values(item).some((value) => String(value).toLocaleLowerCase().includes(query)) ||
        (children.get(Number(BigInt(item.value) & 0xffffffffn)) || []).some(matches),
      roots = (children.get(0) || []).filter(matches);
    this.tree.replaceChildren(...roots.map((item) => this.device(item, children, matches, true)));
    this.empty.hidden = this.tree.childElementCount !== 0;
    this.empty.textContent = records.length ? "没有匹配的设备" : "没有设备";
    this.root.querySelector("[data-role=summary]").textContent = `${records.length} 个设备`;
  }
  device(item, children, matches, expanded = false) {
    expanded = expanded || this.filter.value !== "";
    const li = document.createElement("li"),
      row = document.createElement("div"),
      arrow = document.createElement("button"),
      label = document.createElement("button"),
      list = document.createElement("ul"),
      id = Number(BigInt(item.value) & 0xffffffffn),
      values = (children.get(id) || []).filter(matches);
    row.className = "administration-node-row";
    arrow.className = "administration-arrow";
    arrow.textContent = values.length ? (expanded ? "▾" : "▸") : "";
    arrow.disabled = !values.length;
    arrow.tabIndex = -1;
    label.className = "administration-node-label";
    label.textContent = `${item.name || item.identity}${item.state ? ` — 问题 ${item.state}` : ""}`;
    label.title = `${label.textContent}${item.detail ? ` [${item.detail}]` : ""}`;
    list.hidden = !expanded;
    if (expanded) list.append(...values.map((child) => this.device(child, children, matches)));
    row.append(arrow, label);
    li.append(row, list);
    const toggle = () => {
      if (!values.length) return;
      list.hidden = !list.hidden;
      arrow.textContent = list.hidden ? "▸" : "▾";
      if (!list.hidden && !list.childElementCount)
        list.append(...values.map((child) => this.device(child, children, matches)));
    };
    arrow.onclick = toggle;
    label.onclick = () => this.select(row, item);
    label.ondblclick = toggle;
    row.oncontextmenu = (event) => {
      event.preventDefault();
      this.select(row, item);
      this.menu.querySelector("[data-action=enable]").disabled = item.state !== 22;
      this.menu.querySelector("[data-action=disable]").disabled = item.state === 22;
      placeMenu(this.menu, event);
    };
    return li;
  }
  select(row, item) {
    this.tree.querySelector(".selected")?.classList.remove("selected");
    row.classList.add("selected");
    this.selected = item;
  }
  async invoke(action) {
    const item = this.selected;
    if (!item) return;
    if (action === "properties") {
      this.properties();
      return;
    }
    if (action === "registry") {
      this.revealRegistry(3, `SYSTEM\\CurrentControlSet\\Enum\\${item.identity}`);
      return;
    }
    if (action === "service") {
      await this.goService(item);
      return;
    }
    const value = { enable: 3, disable: 4, restart: 12, uninstall: 9 }[action];
    if (
      (action === "restart" || action === "uninstall") &&
      !confirm(`确定${action === "restart" ? "重启" : "卸载"}设备“${item.name}”？`)
    )
      return;
    try {
      await this.call("/api/hardware/control", { action: value, identity: item.identity });
      this.notify("操作成功");
      await this.load();
    } catch (error) {
      this.notify(error);
    }
  }
  async goService(item) {
    try {
      const value = await postBinary("/api/registry/value/query", {
          root: 3,
          path: `SYSTEM\\CurrentControlSet\\Enum\\${item.identity}`,
          name: "Service",
        }),
        service = new TextDecoder("utf-16le").decode(value.data).replace(/\0+$/, "");
      if (!service) {
        this.notify("该设备没有关联服务");
        return;
      }
      this.revealService(service);
    } catch (error) {
      this.notify(error);
    }
  }
  properties() {
    const item = this.selected;
    if (!item) return;
    this.root.querySelector("[data-role=title]").textContent = item.name || item.identity;
    this.root.querySelector("[data-role=body]").replaceChildren(
      ...details([
        ["设备名称", item.name],
        ["制造商", item.description],
        ["设备类别", item.detail],
        ["状态", item.state ? `问题 ${item.state}` : "正常"],
        ["实例 ID", item.identity],
        ["状态标志", `0x${item.flags.toString(16).padStart(8, "0").toUpperCase()}`],
        ["注册表路径", `HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Enum\\${item.identity}`],
      ]),
    );
    this.root.querySelector("[data-role=properties]").showModal();
  }
}

export class UpdateManager extends AdministrationManager {
  constructor(root, options) {
    super(root, { ...options, path: "updates", columns: [] });
    this.mode = 6;
    this.timer = 0;
    const select = document.createElement("select");
    select.append(new Option("当前更新状态", "6"), new Option("已安装更新（更新历史）", "8"));
    select.onchange = () => {
      this.mode = Number(select.value);
      this.render();
    };
    this.filter.before(select);
    this.checkButton = document.createElement("button");
    this.checkButton.textContent = "立即检查更新";
    this.checkButton.onclick = () => this.check();
    root.querySelector("[data-role=refresh]").before(this.checkButton);
  }
  activate(connected) {
    super.activate(connected);
    this.checkButton.disabled = !connected;
    this.deactivate();
    if (connected) {
      this.syncChecking();
      this.timer = setInterval(() => this.syncChecking(), 2000);
    }
  }
  deactivate() {
    clearInterval(this.timer);
    this.timer = 0;
  }
  disconnect() {
    this.deactivate();
    super.disconnect();
    this.checkButton.disabled = true;
    this.setChecking(false);
  }
  setChecking(checking) {
    this.checking = checking;
    this.checkButton.disabled = checking || !this.connected;
    this.checkButton.textContent = checking ? "正在检查更新……" : "立即检查更新";
  }
  async syncChecking() {
    if (!this.connected || this.syncing) return;
    this.syncing = true;
    try {
      const response = await fetch(apiUrl("/api/updates/check-state"));
      if (response.ok) this.setChecking((await response.json()).checking);
    } catch {
    } finally {
      this.syncing = false;
    }
  }
  async check() {
    if (!this.connected || this.checking) return;
    this.setChecking(true);
    try {
      await this.call("/api/updates/check");
      await this.load(true);
    } catch (error) {
      this.notify(error);
    } finally {
      this.setChecking(false);
    }
  }
  render() {
    const current = this.records.filter((record) => record.kind === this.mode),
      history = this.mode === 8,
      columns = history
        ? [
            ["标题", (r) => r.name],
            ["日期", (r) => fileTime(r.value)],
            [
              "结果",
              (r) =>
                ({ 0: "未开始", 1: "正在进行", 2: "成功", 3: "成功但有错误", 4: "失败", 5: "已中止" })[r.state] ||
                r.state,
            ],
            ["操作", (r) => ({ 1: "安装", 2: "卸载" })[r.flags] || r.flags],
            ["错误", (r) => r.detail],
            ["说明", (r) => r.description],
          ]
        : [
            ["标题", (r) => r.name],
            ["大小", (r) => formatBytes(r.value)],
            ["状态", (r) => (r.flags & 1 ? "已下载" : "可用")],
            ["必须", (r) => (r.flags & 2 ? "是" : "否")],
            ["说明", (r) => r.description],
          ];
    this.root.querySelector("thead tr").replaceChildren(
      ...columns.map(([title]) => {
        const th = document.createElement("th");
        th.textContent = title;
        return th;
      }),
    );
    const query = this.filter.value.toLocaleLowerCase(),
      values = current.filter(
        (record) => !query || Object.values(record).some((value) => String(value).toLocaleLowerCase().includes(query)),
      );
    this.body.replaceChildren(
      ...values.map((record) => {
        const row = document.createElement("tr");
        for (const [, value] of columns) {
          const cell = row.insertCell();
          cell.textContent = text(value(record));
          cell.title = cell.textContent;
        }
        return row;
      }),
    );
    this.empty.hidden = values.length !== 0;
    if (this.connected && this.loaded && !values.length)
      this.empty.textContent = history ? "没有更新历史" : "当前没有可用更新";
  }
}

export class TaskManager {
  constructor(root, { call, notify }) {
    this.root = root;
    this.call = call;
    this.notify = notify;
    this.connected = false;
    this.folder = "\\";
    root.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <input data-role="filter" placeholder="筛选任务" /><span class="spacer"></span
        ><button data-role="refresh">刷新</button>
      </div>
      <div class="administration-split">
        <aside class="administration-tree"><ul data-role="tree"></ul></aside>
        <div class="manager-table">
          <table>
            <thead>
              <tr>
                <th>任务名称</th>
                <th>状态</th>
                <th>已启用</th>
                <th>下次运行</th>
                <th>上次结果</th>
              </tr>
            </thead>
            <tbody></tbody>
          </table>
          <div class="manager-empty">Client 未连接</div>
        </div>
      </div>
      <div class="context-menu administration-menu" data-role="menu" hidden>
        <button data-action="run">运行</button><button data-action="stop">停止</button
        ><button data-action="enable">启用</button><button data-action="disable">禁用</button>
        <hr />
        <button data-action="delete" class="danger">删除</button>
        <hr />
        <button data-action="properties">属性</button>
      </div>
      <dialog data-role="properties">
        <form method="dialog">
          <h2 data-role="title"></h2>
          <dl class="details-grid" data-role="body"></dl>
          <label>任务 XML</label>
          <textarea class="task-xml" data-role="xml" spellcheck="false"></textarea>
          <div class="dialog-actions">
            <button type="button" data-action="cancel-xml">取消</button><button>保存 XML</button>
          </div>
        </form>
      </dialog>`;
    this.filter = root.querySelector("[data-role=filter]");
    this.tree = root.querySelector("[data-role=tree]");
    this.body = root.querySelector("tbody");
    this.empty = root.querySelector(".manager-empty");
    this.menu = root.querySelector("[data-role=menu]");
    this.propertiesDialog = root.querySelector("[data-role=properties]");
    this.filter.oninput = () => this.renderTasks();
    root.querySelector("[data-role=refresh]").onclick = () => this.load();
    for (const action of ["run", "stop", "enable", "disable", "delete", "properties"])
      root.querySelector(`[data-action=${action}]`).onclick = () => {
        this.menu.hidden = true;
        this.invoke(action);
      };
    document.addEventListener("pointerdown", (event) => {
      if (!this.menu.contains(event.target)) this.menu.hidden = true;
    });
    this.propertiesDialog.querySelector("[data-action=cancel-xml]").onclick = () => this.propertiesDialog.close();
    this.propertiesDialog.querySelector("form").onsubmit = (event) => {
      event.preventDefault();
      this.saveXml();
    };
  }
  activate(connected) {
    this.connected = connected;
    if (!connected) {
      this.empty.textContent = "Client 未连接";
      return;
    }
    if (!this.loaded) this.load();
  }
  disconnect() {
    this.connected = false;
    this.loaded = false;
    this.records = [];
    this.selected = null;
    this.folder = "\\";
    this.tree.replaceChildren();
    this.body.replaceChildren();
    this.empty.hidden = false;
    this.empty.textContent = "Client 未连接";
  }
  async load() {
    if (!this.connected || this.loading) return;
    this.loading = true;
    this.empty.hidden = false;
    this.empty.textContent = "正在读取任务…";
    try {
      this.records = await this.call("/api/tasks");
      this.loaded = true;
      this.buildTree();
      this.renderTasks();
    } catch (error) {
      this.empty.textContent = error.message;
      this.notify(error);
    } finally {
      this.loading = false;
    }
  }
  buildTree() {
    const folders = new Set(this.records.filter((record) => record.kind === 9).map((record) => record.identity));
    for (const task of this.records.filter((record) => record.kind === 7))
      folders.add(task.identity.slice(0, task.identity.lastIndexOf("\\")) || "\\");
    folders.add("\\");
    const nodes = new Map([...folders].map((path) => [path, { path, children: [] }]));
    for (const node of nodes.values()) {
      if (node.path === "\\") continue;
      const index = node.path.lastIndexOf("\\"),
        parent = node.path.slice(0, index) || "\\";
      if (!nodes.has(parent)) nodes.set(parent, { path: parent, children: [] });
      nodes.get(parent).children.push(node);
    }
    for (const node of nodes.values()) node.children.sort((a, b) => a.path.localeCompare(b.path));
    this.folder = nodes.has(this.folder) ? this.folder : "\\";
    this.tree.replaceChildren(...nodes.get("\\").children.map((node) => this.folderNode(node, true)));
  }
  folderNode(node, expanded = false) {
    const li = document.createElement("li"),
      row = document.createElement("div"),
      arrow = document.createElement("button"),
      label = document.createElement("button"),
      list = document.createElement("ul");
    row.className = "administration-node-row";
    arrow.className = "administration-arrow";
    arrow.textContent = node.children.length ? (expanded ? "▾" : "▸") : "";
    arrow.disabled = !node.children.length;
    arrow.tabIndex = -1;
    label.className = "administration-node-label";
    label.textContent = node.path === "\\" ? "任务计划程序库" : node.path.slice(node.path.lastIndexOf("\\") + 1);
    list.hidden = !expanded;
    if (expanded) list.append(...node.children.map((child) => this.folderNode(child)));
    row.classList.toggle("selected", node.path === this.folder);
    row.append(arrow, label);
    li.append(row, list);
    const toggle = () => {
      if (!node.children.length) return;
      list.hidden = !list.hidden;
      arrow.textContent = list.hidden ? "▸" : "▾";
      if (!list.hidden && !list.childElementCount) list.append(...node.children.map((child) => this.folderNode(child)));
    };
    arrow.onclick = toggle;
    label.onclick = () => {
      this.folder = node.path;
      this.tree.querySelector(".selected")?.classList.remove("selected");
      row.classList.add("selected");
      this.renderTasks();
      toggle();
    };
    return li;
  }
  renderTasks() {
    const query = this.filter.value.toLocaleLowerCase(),
      tasks = (this.records || []).filter(
        (record) =>
          record.kind === 7 &&
          (record.identity.slice(0, record.identity.lastIndexOf("\\")) || "\\") === this.folder &&
          (!query || Object.values(record).some((value) => String(value).toLocaleLowerCase().includes(query))),
      );
    this.body.replaceChildren(
      ...tasks.map((task) => {
        const row = document.createElement("tr");
        for (const value of [
          task.name,
          taskState(task.state),
          task.flags & 1 ? "是" : "否",
          fileTime(task.value),
          task.description,
        ]) {
          const cell = row.insertCell();
          cell.textContent = text(value);
          cell.title = cell.textContent;
        }
        row.onclick = () => {
          this.body.querySelector(".selected")?.classList.remove("selected");
          row.classList.add("selected");
          this.selected = task;
        };
        row.ondblclick = () => this.properties();
        row.oncontextmenu = (event) => {
          event.preventDefault();
          row.click();
          this.menu.querySelector("[data-action=enable]").disabled = !!(task.flags & 1);
          this.menu.querySelector("[data-action=disable]").disabled = !(task.flags & 1);
          placeMenu(this.menu, event);
        };
        return row;
      }),
    );
    this.empty.hidden = tasks.length !== 0;
    this.empty.textContent = this.connected ? "此文件夹没有任务" : "Client 未连接";
  }
  async invoke(action) {
    const task = this.selected;
    if (!task) return;
    if (action === "properties") {
      this.properties();
      return;
    }
    const value = { run: 6, stop: 7, enable: 3, disable: 4, delete: 2 }[action];
    if (action === "delete" && !confirm(`确定删除任务“${task.name}”？`)) return;
    try {
      await this.call("/api/tasks/control", { action: value, identity: task.identity });
      this.notify("操作成功");
      await this.load();
    } catch (error) {
      this.notify(error);
    }
  }
  properties() {
    const task = this.selected;
    if (!task) return;
    this.root.querySelector("[data-role=title]").textContent = task.name;
    this.root.querySelector("[data-role=body]").replaceChildren(
      ...details([
        ["名称", task.name],
        ["路径", task.identity],
        ["状态", taskState(task.state)],
        ["已启用", task.flags & 1 ? "是" : "否"],
        ["下次运行", fileTime(task.value)],
        ["上次结果", task.description],
      ]),
    );
    this.root.querySelector("[data-role=xml]").value = task.detail;
    this.propertiesDialog.showModal();
  }
  async saveXml() {
    const task = this.selected;
    if (!task) return;
    try {
      await this.call("/api/tasks/control", {
        action: 23,
        identity: task.identity,
        argument: this.root.querySelector("[data-role=xml]").value,
      });
      this.propertiesDialog.close();
      this.notify("任务 XML 已保存");
      await this.load();
    } catch (error) {
      this.notify(error);
    }
  }
}

export class FirewallManager {
  constructor(root, { call, notify, revealEvents }) {
    this.root = root;
    this.call = call;
    this.notify = notify;
    this.revealEvents = revealEvents;
    this.connected = false;
    this.records = [];
    root.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <select data-role="direction">
          <option value="0">全部规则</option>
          <option value="1">入站规则</option>
          <option value="2">出站规则</option></select
        ><input data-role="filter" placeholder="筛选防火墙规则" /><button data-action="events">查看防火墙事件</button
        ><span class="spacer"></span><button data-action="refresh">刷新</button>
      </div>
      <section class="firewall-profiles" data-role="profiles"></section>
      <div class="manager-table firewall-table">
        <table>
          <thead>
            <tr>
              <th>名称</th>
              <th>方向</th>
              <th>操作</th>
              <th>状态</th>
              <th>配置文件</th>
              <th>组</th>
              <th>程序</th>
              <th>协议</th>
            </tr>
          </thead>
          <tbody></tbody>
        </table>
        <div class="manager-empty">Client 未连接</div>
      </div>
      <div class="context-menu administration-menu" data-role="menu" hidden>
        <button data-action="enable">启用</button><button data-action="disable">禁用</button
        ><button data-action="allow">允许</button><button data-action="block">阻止</button>
        <hr />
        <button data-action="properties">属性</button>
      </div>
      <dialog class="firewall-properties" data-role="properties">
        <form>
          <h2 data-role="title"></h2>
          <dl class="details-grid" data-role="details"></dl>
          <label class="property-choice"><input type="checkbox" data-field="enabled" />启用此规则</label
          ><fieldset class="property-radio-group">
            <legend>操作</legend>
            <label class="property-choice"
              ><input type="radio" name="firewall-action" value="1" />允许连接</label
            ><label class="property-choice"
              ><input type="radio" name="firewall-action" value="0" />阻止连接</label
            >
          </fieldset>
          <div class="dialog-actions">
            <button type="button" data-action="cancel">取消</button><button type="submit">确定</button>
          </div>
        </form>
      </dialog>`;
    this.direction = root.querySelector("[data-role=direction]");
    this.filter = root.querySelector("[data-role=filter]");
    this.profiles = root.querySelector("[data-role=profiles]");
    this.body = root.querySelector("tbody");
    this.empty = root.querySelector(".manager-empty");
    this.menu = root.querySelector("[data-role=menu]");
    this.dialog = root.querySelector("[data-role=properties]");
    this.direction.onchange = this.filter.oninput = () => this.render();
    root.querySelector("[data-action=refresh]").onclick = () => this.load();
    root.querySelector("[data-action=events]").onclick = () => this.revealEvents("Microsoft-Windows-WFP/Operational");
    for (const action of ["enable", "disable", "allow", "block", "properties"])
      this.menu.querySelector("[data-action=" + action + "]").onclick = () => {
        this.menu.hidden = true;
        this.invoke(action);
      };
    this.dialog.querySelector("[data-action=cancel]").onclick = () => this.dialog.close();
    this.dialog.onsubmit = (event) => {
      event.preventDefault();
      this.save();
    };
    document.addEventListener("pointerdown", (event) => {
      if (!this.menu.contains(event.target)) this.menu.hidden = true;
    });
  }
  activate(connected) {
    this.connected = connected;
    if (!connected) {
      this.empty.textContent = "Client 未连接";
      return;
    }
    if (!this.loaded) this.load();
  }
  disconnect() {
    this.connected = false;
    this.loaded = false;
    this.records = [];
    this.selected = null;
    this.profiles.replaceChildren();
    this.body.replaceChildren();
    this.empty.hidden = false;
    this.empty.textContent = "Client 未连接";
  }
  async load() {
    if (!this.connected || this.loading) return;
    this.loading = true;
    this.empty.hidden = false;
    this.empty.textContent = "正在读取防火墙策略…";
    try {
      this.records = await this.call("/api/firewall");
      this.loaded = true;
      this.renderProfiles();
      this.render();
    } catch (error) {
      this.empty.textContent = error.message;
      this.notify(error);
    } finally {
      this.loading = false;
    }
  }
  renderProfiles() {
    this.profiles.replaceChildren(
      ...this.records
        .filter((record) => record.kind === 10)
        .map((profile) => {
          const label = document.createElement("label"),
            input = document.createElement("input"),
            caption = document.createElement("span");
          input.type = "checkbox";
          input.checked = profile.state !== 0;
          input.onchange = async () => {
            input.disabled = true;
            try {
              await this.control(input.checked ? 3 : 4, profile.identity);
              profile.state = input.checked ? 1 : 0;
              this.notify("防火墙状态已更新");
            } catch (error) {
              input.checked = !input.checked;
              this.notify(error);
            } finally {
              input.disabled = false;
            }
          };
          caption.textContent =
            (profile.identity === "Private" ? "专用网络" : "来宾或公用网络") +
            (profile.flags & 1 ? "（当前网络）" : "");
          label.append(input, caption);
          return label;
        }),
    );
  }
  render() {
    const direction = Number(this.direction.value),
      query = this.filter.value.toLocaleLowerCase(),
      rules = this.records
        .filter(
          (record) =>
            record.kind === 11 &&
            (!direction || (record.flags & 3) === direction) &&
            (!query || Object.values(record).some((value) => String(value).toLocaleLowerCase().includes(query))),
        )
        .sort((a, b) => firewallRuleName(a).localeCompare(firewallRuleName(b)));
    this.body.replaceChildren(
      ...rules.map((rule) => {
        const row = document.createElement("tr");
        row.record = rule;
        for (const value of [
          firewallRuleName(rule),
          firewallDirection(rule.flags),
          rule.flags & 4 ? "允许" : "阻止",
          rule.state ? "已启用" : "已禁用",
          firewallProfiles(rule.flags),
          rule.name,
          rule.detail,
          firewallProtocol(rule.value),
        ]) {
          const cell = row.insertCell();
          cell.textContent = text(value);
          cell.title = cell.textContent;
        }
        row.onclick = () => {
          this.body.querySelector(".selected")?.classList.remove("selected");
          row.classList.add("selected");
          this.selected = rule;
        };
        row.ondblclick = () => this.properties(rule);
        row.oncontextmenu = (event) => {
          event.preventDefault();
          row.click();
          this.menu.querySelector("[data-action=enable]").disabled = rule.state !== 0;
          this.menu.querySelector("[data-action=disable]").disabled = rule.state === 0;
          this.menu.querySelector("[data-action=allow]").disabled = !!(rule.flags & 4);
          this.menu.querySelector("[data-action=block]").disabled = !(rule.flags & 4);
          placeMenu(this.menu, event);
        };
        return row;
      }),
    );
    this.empty.hidden = rules.length !== 0;
    this.empty.textContent = this.connected
      ? this.records.length
        ? "没有匹配的规则"
        : "没有防火墙规则"
      : "Client 未连接";
  }
  async control(action, identity) {
    await this.call("/api/firewall/control", { action, identity });
  }
  async invoke(action) {
    const rule = this.selected;
    if (!rule) return;
    if (action === "properties") {
      this.properties(rule);
      return;
    }
    const value = { enable: 3, disable: 4, allow: 14, block: 15 }[action];
    try {
      await this.control(value, rule.identity);
      this.notify("操作成功");
      await this.load();
    } catch (error) {
      this.notify(error);
    }
  }
  properties(rule) {
    this.propertyRule = rule;
    this.dialog.querySelector("[data-role=title]").textContent = firewallRuleName(rule);
    this.dialog.querySelector("[data-role=details]").replaceChildren(
      ...details([
        ["名称", firewallRuleName(rule)],
        ["说明", rule.description],
        ["组", rule.name],
        ["方向", firewallDirection(rule.flags)],
        ["配置文件", firewallProfiles(rule.flags)],
        ["程序", rule.detail],
        ["协议", firewallProtocol(rule.value)],
        ["边缘遍历", rule.flags & 64 ? "是" : "否"],
      ]),
    );
    this.dialog.querySelector("[data-field=enabled]").checked = rule.state !== 0;
    this.dialog.querySelector(`[name=firewall-action][value="${rule.flags & 4 ? 1 : 0}"]`).checked = true;
    this.dialog.showModal();
  }
  async save() {
    const rule = this.propertyRule,
      enabled = this.dialog.querySelector("[data-field=enabled]").checked,
      allow = this.dialog.querySelector("[name=firewall-action]:checked").value === "1";
    try {
      if (enabled !== (rule.state !== 0)) await this.control(enabled ? 3 : 4, rule.identity);
      if (allow !== !!(rule.flags & 4)) await this.control(allow ? 14 : 15, rule.identity);
      this.dialog.close();
      this.notify("规则已保存");
      await this.load();
    } catch (error) {
      this.notify(error);
    }
  }
}

export class PowerManager {
  constructor(root, { call, notify }) {
    this.root = root;
    this.call = call;
    this.notify = notify;
    this.connected = false;
    this.records = [];
    root.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <strong>电源管理</strong><span class="spacer"></span><button data-action="refresh">刷新</button>
      </div>
      <div class="power-body">
        <section class="card">
          <h2>系统操作</h2>
          <div class="power-actions">
            <button data-action="display">关闭显示器</button><button data-action="lock">锁屏</button
            ><button data-action="signout">注销</button><button data-action="sleep">睡眠</button
            ><button data-action="hibernate">休眠</button><button data-action="restart">重启</button
            ><button data-action="firmware">重启到固件设置</button
            ><button class="danger" data-action="shutdown">关机</button>
          </div>
        </section>
        <section class="card">
          <h2>启动设置</h2>
          <label class="property-choice"><input type="checkbox" data-role="fast-startup" />启用快速启动</label>
          <p class="property-note" data-role="fast-startup-note"></p>
        </section>
        <section class="card">
          <h2>电源计划</h2>
          <select class="power-plans" data-role="plans" aria-label="电源计划"></select>
        </section>
        <section class="card">
          <h2>电池</h2>
          <dl class="details-grid" data-role="battery"></dl>
          <p class="property-note" data-role="battery-empty">未检测到电池</p>
        </section>
        <section class="card">
          <h2>UPS</h2>
          <dl class="details-grid" data-role="ups"></dl>
          <p class="property-note" data-role="ups-empty">未检测到 UPS</p>
        </section>
      </div>
      <div class="manager-empty power-empty">Client 未连接</div>`;
    this.empty = root.querySelector(".power-empty");
    this.fastStartup = root.querySelector("[data-role=fast-startup]");
    this.fastStartupNote = root.querySelector("[data-role=fast-startup-note]");
    this.plans = root.querySelector("[data-role=plans]");
    this.battery = root.querySelector("[data-role=battery]");
    this.batteryEmpty = root.querySelector("[data-role=battery-empty]");
    this.ups = root.querySelector("[data-role=ups]");
    this.upsEmpty = root.querySelector("[data-role=ups-empty]");
    this.buttons = Object.fromEntries(
      ["display", "lock", "signout", "sleep", "hibernate", "restart", "firmware", "shutdown"].map((name) => [
        name,
        root.querySelector("[data-action=" + name + "]"),
      ]),
    );
    root.querySelector("[data-action=refresh]").onclick = () => this.load();
    for (const [name, button] of Object.entries(this.buttons)) button.onclick = () => this.execute(name);
    this.fastStartup.onchange = () => this.setFastStartup();
    this.plans.onchange = () =>
      this.activatePlan(this.records.find((record) => record.kind === 13 && record.identity === this.plans.value));
  }
  activate(connected) {
    this.connected = connected;
    this.setEnabled();
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
    this.setEnabled();
    this.plans.replaceChildren();
    this.battery.replaceChildren();
    this.ups.replaceChildren();
    this.empty.hidden = false;
    this.empty.textContent = "Client 未连接";
  }
  setEnabled() {
    for (const button of Object.values(this.buttons)) button.disabled = !this.connected;
    this.fastStartup.disabled = !this.connected;
    this.plans.disabled = !this.connected;
  }
  async load() {
    if (!this.connected || this.loading) return;
    this.loading = true;
    this.empty.hidden = false;
    this.empty.textContent = "正在读取电源状态…";
    try {
      this.records = await this.call("/api/power");
      this.loaded = true;
      this.render();
      this.empty.hidden = true;
    } catch (error) {
      this.empty.textContent = error.message;
      this.notify(error);
    } finally {
      this.loading = false;
    }
  }
  render() {
    const settings = new Map(
        this.records.filter((record) => record.kind === 12).map((record) => [record.identity, record]),
      ),
      fast = settings.get("FastStartup"),
      sleep = settings.get("Sleep"),
      hibernate = settings.get("Hibernate");
    this.fastStartup.checked = !!fast?.state;
    this.fastStartup.disabled = !this.connected || !fast?.flags;
    this.fastStartupNote.textContent = fast?.flags ? "更改在下一次完整关机后生效。" : "当前系统不支持快速启动。";
    this.buttons.sleep.disabled = !this.connected || !sleep?.state;
    this.buttons.hibernate.disabled = !this.connected || !hibernate?.state;
    const plans = this.records.filter((record) => record.kind === 13),
      active = plans.find((plan) => plan.state);
    this.plans.replaceChildren(...plans.map((plan) => new Option(plan.name, plan.identity)));
    this.plans.value = active?.identity ?? "";
    this.plans.disabled = !this.connected || !plans.length;
    this.renderSupply(this.records.find((record) => record.kind === 53), this.battery, this.batteryEmpty);
    this.renderSupply(this.records.find((record) => record.kind === 14), this.ups, this.upsEmpty);
  }
  renderSupply(supply, body, empty) {
    body.replaceChildren();
    empty.hidden = !!supply;
    if (supply) {
      const value = BigInt(supply.value),
        remaining = Number(value & 0xffffffffn),
        maximum = Number(value >> 32n),
        estimated = supply.data.estimatedTime;
      body.replaceChildren(
        ...details([
          ["电源", supply.flags & 1 ? "市电" : "电池"],
          ["状态", supply.flags & 2 ? "正在充电" : supply.flags & 4 ? "正在放电" : "空闲"],
          ["剩余容量", maximum ? Math.round((remaining * 100) / maximum) + "%" : "未知"],
          ["预计时间", estimated === 0xffffffff ? "未知" : formatDuration(estimated)],
          ["类型", supply.kind === 14 ? "短时备用电源" : supply.flags & 8 ? "短时电池" : "系统电池"],
        ]),
      );
    }
  }
  async control(action, identity = "System") {
    await this.call("/api/power/control", { action, identity });
  }
  async setFastStartup() {
    this.fastStartup.disabled = true;
    try {
      await this.control(this.fastStartup.checked ? 3 : 4, "FastStartup");
      this.notify("快速启动设置已更新");
      await this.load();
    } catch (error) {
      this.fastStartup.checked = !this.fastStartup.checked;
      this.notify(error);
    } finally {
      this.fastStartup.disabled =
        !this.connected || !this.records.find((record) => record.identity === "FastStartup")?.flags;
    }
  }
  async activatePlan(plan) {
    if (!plan) return;
    this.plans.disabled = true;
    try {
      await this.control(21, plan.identity);
      this.notify("电源计划已切换");
    } catch (error) {
      this.notify(error);
    } finally {
      await this.load();
      this.plans.disabled = !this.connected;
    }
  }
  async execute(name) {
    const labels = {
        display: "关闭显示器",
        lock: "锁定当前会话",
        signout: "注销当前用户",
        sleep: "使系统进入睡眠",
        hibernate: "使系统进入休眠",
        restart: "重新启动系统",
        firmware: "重新启动并进入固件设置",
        shutdown: "关闭系统",
      },
      actions = {
        display: 27,
        lock: 20,
        signout: 19,
        sleep: 16,
        hibernate: 17,
        restart: 12,
        firmware: 22,
        shutdown: 18,
      };
    if (!["display", "lock"].includes(name) && !confirm("确定" + labels[name] + "？")) return;
    try {
      await this.control(actions[name]);
      this.notify(labels[name] + "命令已发送");
    } catch (error) {
      this.notify(error);
    }
  }
}

export class EventViewer {
  constructor(root, { call, notify }) {
    this.root = root;
    this.call = call;
    this.notify = notify;
    this.pageStarts = [""];
    this.pageIndex = 0;
    this.nextBookmark = "";
    this.connected = false;
    this.channelsLoaded = false;
    this.customViews = this.loadViews();
    this.builtInViews = [
      { name: "防火墙事件", channel: "Microsoft-Windows-WFP/Operational", query: "*", builtIn: true },
      { name: "DNS 事件", channel: "Microsoft-Windows-DNS-Client/Operational", query: "*", builtIn: true },
    ];
    root.innerHTML = /* HTML */ `<div class="event-toolbar">
        <input data-role="channel" value="System" aria-label="频道" /><input
          data-role="query"
          placeholder="XPath 查询（可空）"
        /><button data-role="query-button">查询</button><button data-role="next" disabled>下一页</button
        ><button data-action="enable-channel" hidden>启用频道</button
        ><button data-action="download">下载已有数据</button><button data-action="stream">流式下载新增事件</button
        ><button data-action="stop-stream" class="danger" hidden>停止下载</button
        ><span data-role="stream-state" class="status"></span>
      </div>
      <div class="event-body">
        <aside data-role="channels"></aside>
        <section class="event-list">
          <table>
            <thead>
              <tr>
                <th>级别</th>
                <th>日期和时间</th>
                <th>来源</th>
                <th>事件 ID</th>
                <th>计算机</th>
              </tr>
            </thead>
            <tbody></tbody>
          </table>
          <div class="manager-empty">Client 未连接</div>
          <pre data-role="details">选择事件查看 XML</pre>
        </section>
      </div>
      <div class="context-menu administration-menu" data-role="menu" hidden></div>
      <dialog class="event-properties" data-role="properties">
        <form>
          <h2 data-role="property-title"></h2>
          <div class="event-property-grid">
            <label>全名<input data-field="full-name" readonly /></label
            ><label>日志路径<input data-field="log-path" readonly /></label
            ><label>日志大小<input data-field="file-size" readonly /></label
            ><label>创建时间<input data-field="creation-time" readonly /></label
            ><label>修改时间<input data-field="write-time" readonly /></label
            ><label>访问时间<input data-field="access-time" readonly /></label>
          </div>
          <label class="property-choice"><input type="checkbox" data-field="enabled" />启用日志记录</label
          ><label>日志最大大小 (KB)<input type="number" min="1" step="64" data-field="maximum-size" required /></label>
          <fieldset>
            <legend>达到事件日志最大大小时</legend>
            <label class="property-choice"
              ><input type="radio" name="event-retention" value="0" />按需要覆盖事件（旧事件优先）</label
            ><label class="property-choice"
              ><input type="radio" name="event-retention" value="1" />日志满时将其存档，不要覆盖事件</label
            ><label class="property-choice"
              ><input type="radio" name="event-retention" value="2" />不覆盖事件（手动清除日志）</label
            >
          </fieldset>
          <div class="dialog-actions">
            <button type="button" class="danger" data-action="clear">清除日志</button><span class="spacer"></span
            ><button type="button" data-action="cancel">取消</button><button type="submit">确定</button>
          </div>
        </form>
      </dialog>
      <dialog class="event-view-editor" data-role="view-editor">
        <form>
          <h2 data-role="view-title">新建自定义视图</h2>
          <label>名称<input data-field="name" maxlength="128" required /></label
          ><label>频道<input data-field="channel" maxlength="512" required /></label
          ><label
            >XPath 查询<textarea data-field="query" maxlength="8192" rows="7" placeholder="*" required></textarea>
          </label>
          <div class="dialog-actions">
            <button type="button" data-action="cancel">取消</button><button type="submit">保存</button>
          </div>
        </form>
      </dialog>`;
    this.channelInput = root.querySelector("[data-role=channel]");
    this.queryInput = root.querySelector("[data-role=query]");
    this.body = root.querySelector("tbody");
    this.empty = root.querySelector(".manager-empty");
    this.details = root.querySelector("[data-role=details]");
    this.next = root.querySelector("[data-role=next]");
    this.channelList = root.querySelector("[data-role=channels]");
    this.menu = root.querySelector("[data-role=menu]");
    this.dialog = root.querySelector("[data-role=properties]");
    this.viewDialog = root.querySelector("[data-role=view-editor]");
    this.enableChannelButton = root.querySelector("[data-action=enable-channel]");
    this.downloadButton = root.querySelector("[data-action=download]");
    this.streamButton = root.querySelector("[data-action=stream]");
    this.stopStreamButton = root.querySelector("[data-action=stop-stream]");
    this.streamState = root.querySelector("[data-role=stream-state]");
    root.querySelector("[data-role=query-button]").onclick = async () => {
      await this.refreshChannelState();
      this.query(false);
    };
    this.next.onclick = () => this.query(true);
    this.enableChannelButton.onclick = () => this.enableSelectedChannel();
    this.downloadButton.onclick = () => this.downloadExisting();
    this.streamButton.onclick = () => this.startStream();
    this.stopStreamButton.onclick = () => this.stopStream();
    this.dialog.querySelector("[data-action=cancel]").onclick = () => this.dialog.close();
    this.dialog.querySelector("[data-action=clear]").onclick = () => this.clear(this.propertyChannel);
    this.dialog.onsubmit = (event) => {
      event.preventDefault();
      this.saveProperties();
    };
    this.viewDialog.querySelector("[data-action=cancel]").onclick = () => this.viewDialog.close();
    this.viewDialog.onsubmit = (event) => {
      event.preventDefault();
      this.saveView();
    };
    document.addEventListener("pointerdown", (event) => {
      if (!this.menu.contains(event.target)) this.menu.hidden = true;
    });
  }
  activate(connected) {
    if (!this.previous) {
      this.previous = document.createElement("button");
      this.previous.textContent = "上一页";
      this.previous.disabled = true;
      this.previous.onclick = () => this.query(-1);
      this.next.before(this.previous);
    }
    this.connected = connected;
    this.downloadButton.disabled = !connected;
    this.streamButton.disabled = !connected;
    if (!connected) {
      this.empty.textContent = "Client 未连接";
      return;
    }
    if (!this.channelsLoaded) this.loadChannels();
    if (!this.body.children.length) this.empty.textContent = "选择频道后查询事件";
  }
  disconnect() {
    this.connected = false;
    this.channelsLoaded = false;
    this.pageStarts = [""];
    this.pageIndex = 0;
    this.nextBookmark = "";
    if (this.previous) this.previous.disabled = true;
    this.next.disabled = true;
    this.downloadButton.disabled = true;
    this.streamButton.disabled = true;
    this.enableChannelButton.hidden = true;
    this.stopStream(true);
    this.body.replaceChildren();
    this.channelList.replaceChildren();
    this.empty.hidden = false;
    this.empty.textContent = "Client 未连接";
  }
  openChannel(channel) {
    this.selectView({ name: channel, channel, query: "" });
  }
  async loadChannels() {
    try {
      const channels = await this.call("/api/eventlog/channels");
      this.channelsLoaded = true;
      this.renderChannels(channels);
    } catch (error) {
      this.notify(error);
    }
  }
  renderChannels(channels) {
    const windows = new Set(["Application", "Security", "Setup", "System", "ForwardedEvents"]),
      section = (title) => {
        const value = document.createElement("section"),
          heading = document.createElement("strong");
        heading.textContent = title;
        value.append(heading);
        return value;
      },
      views = section("内置视图"),
      viewsTitle = views.querySelector("strong"),
      addView = document.createElement("button"),
      windowsSection = section("Windows 日志"),
      appsSection = section("应用程序和服务日志"),
      root = new Map();
    addView.textContent = "＋";
    addView.title = "新建视图";
    addView.onclick = () => this.editView();
    viewsTitle.append(addView);
    views.append(...this.builtInViews.concat(this.customViews).map((view) => this.viewButton(view)));
    for (const channel of channels.filter((value) => !windows.has(value))) {
      const parts = eventChannelParts(channel);
      let children = root;
      for (const [index, name] of parts.entries()) {
        if (!children.has(name)) children.set(name, { channel: null, children: new Map() });
        const node = children.get(name);
        if (index === parts.length - 1) node.channel = channel;
        children = node.children;
      }
    }
    windowsSection.append(
      ...channels
        .filter((channel) => windows.has(channel))
        .sort()
        .map((channel) => this.channelButton(channel, channel, true)),
    );
    const list = document.createElement("ul");
    list.className = "event-channel-tree";
    list.append(
      ...[...root].sort(([a], [b]) => a.localeCompare(b)).map(([name, node]) => this.channelNode(name, node)),
    );
    appsSection.append(list);
    this.channelList.replaceChildren(views, windowsSection, appsSection);
  }
  channelButton(channel, label = channel, windowsLog = false) {
    const button = document.createElement("button");
    button.textContent = label;
    button.title = channel;
    button.onclick = () => this.selectView({ name: label, channel, query: "" });
    button.oncontextmenu = (event) => {
      event.preventDefault();
      this.openMenu(event, channel, windowsLog);
    };
    return button;
  }
  viewButton(view) {
    const button = document.createElement("button");
    button.textContent = view.name;
    button.title = `${view.channel}\n${view.query}`;
    button.onclick = () => this.selectView(view);
    button.oncontextmenu = (event) => {
      event.preventDefault();
      this.openViewMenu(event, view);
    };
    return button;
  }
  async selectView(view) {
    this.channelInput.value = view.channel;
    this.queryInput.value = view.query;
    this.streamName = view.name;
    await this.refreshChannelState();
    if (this.connected) this.query(false);
  }
  async refreshChannelState() {
    this.enableChannelButton.hidden = true;
    this.channelEnabled = true;
    try {
      const info = await this.call("/api/eventlog/channel/info", { channelPath: this.channelInput.value });
      this.channelEnabled = info.enabled;
      this.enableChannelButton.hidden = info.enabled;
      this.enableChannelButton.textContent = `启用 ${this.streamName || this.channelInput.value}`;
      this.streamButton.disabled = !this.connected || !info.enabled;
    } catch (error) {
      this.streamButton.disabled = !this.connected;
      this.notify(error);
    }
  }
  async enableSelectedChannel() {
    const channel = this.channelInput.value;
    if (!channel || !(await this.setEnabled(channel, true))) return;
    await this.refreshChannelState();
    await this.query(false);
  }
  channelNode(name, node) {
    const li = document.createElement("li");
    if (!node.children.size) {
      li.append(this.channelButton(node.channel, name));
      return li;
    }
    const row = document.createElement("div"),
      arrow = document.createElement("button"),
      label = document.createElement("button"),
      list = document.createElement("ul");
    row.className = "event-channel-row";
    arrow.textContent = "▸";
    arrow.tabIndex = -1;
    label.textContent = name;
    list.hidden = true;
    row.append(arrow, label);
    li.append(row, list);
    const toggle = () => {
      list.hidden = !list.hidden;
      arrow.textContent = list.hidden ? "▸" : "▾";
      if (!list.hidden && !list.childElementCount) {
        if (node.channel) list.append(this.channelButton(node.channel, "(默认)"));
        list.append(
          ...[...node.children]
            .sort(([a], [b]) => a.localeCompare(b))
            .map(([childName, child]) => this.channelNode(childName, child)),
        );
      }
    };
    arrow.onclick = label.onclick = toggle;
    return li;
  }
  async query(direction) {
    if (!this.connected) return;
    if (!direction) {
      this.pageStarts = [""];
      this.pageIndex = 0;
    } else if (direction < 0) {
      if (this.pageIndex === 0) return;
      this.pageIndex--;
    } else {
      if (!this.nextBookmark) return;
      this.pageIndex++;
      this.pageStarts[this.pageIndex] = this.nextBookmark;
      this.pageStarts.length = this.pageIndex + 1;
    }
    this.previous.disabled = this.next.disabled = true;
    this.body.replaceChildren();
    this.details.textContent = "选择事件查看 XML";
    this.empty.hidden = false;
    this.empty.textContent = "正在读取事件…";
    try {
      const page = await this.call("/api/eventlog/query", {
        channelPath: this.channelInput.value,
        query: this.queryInput.value || null,
        bookmark: this.pageStarts[this.pageIndex] || null,
        maxEvents: 100,
      });
      this.nextBookmark = page.nextBookmark;
      this.previous.disabled = this.pageIndex === 0;
      this.next.disabled = !page.hasMore;
      this.render(page.records);
    } catch (error) {
      this.previous.disabled = this.pageIndex === 0;
      this.empty.textContent = this.details.textContent = error.message;
      this.notify(error);
    }
  }
  render(records) {
    this.body.replaceChildren(
      ...records.map((record) => {
        const xml = new DOMParser().parseFromString(record.xml, "application/xml"),
          value = (selector) =>
            xml.querySelector(selector)?.getAttribute("Name") ?? xml.querySelector(selector)?.textContent ?? "",
          level = value("Level"),
          timestamp = xml.querySelector("TimeCreated")?.getAttribute("SystemTime"),
          row = document.createElement("tr");
        for (const item of [
          { 1: "关键", 2: "错误", 3: "警告", 4: "信息", 5: "详细" }[level] || level,
          timestamp ? new Date(timestamp).toLocaleString() : "",
          xml.querySelector("Provider")?.getAttribute("Name") || "",
          value("EventID"),
          value("Computer"),
        ]) {
          const cell = row.insertCell();
          cell.textContent = item;
        }
        row.onclick = () => (this.details.textContent = record.xml);
        return row;
      }),
    );
    this.empty.hidden = records.length !== 0;
    if (!records.length)
      this.empty.textContent = this.channelEnabled ? "没有事件" : "频道未启用；启用后才会记录新的事件";
  }
  async openMenu(event, channel, windowsLog) {
    this.menu.replaceChildren();
    const loading = document.createElement("button");
    loading.textContent = "正在读取…";
    loading.disabled = true;
    this.menu.append(loading);
    placeMenu(this.menu, event);
    try {
      const info = await this.call("/api/eventlog/channel/info", { channelPath: channel }),
        actions = [];
      if (!windowsLog)
        actions.push({
          title: info.enabled ? "禁用日志" : "启用日志",
          run: () => this.setEnabled(channel, !info.enabled),
        });
      actions.push(
        { title: "清除日志", danger: true, run: () => this.clear(channel) },
        { title: "属性", run: () => this.properties(channel, windowsLog, info) },
      );
      this.menu.replaceChildren(
        ...actions.map((action) => {
          const button = document.createElement("button");
          button.textContent = action.title;
          button.classList.toggle("danger", action.danger === true);
          button.onclick = () => {
            this.menu.hidden = true;
            action.run();
          };
          return button;
        }),
      );
      placeMenu(this.menu, event);
    } catch (error) {
      this.menu.hidden = true;
      this.notify(error);
    }
  }
  openViewMenu(event, view) {
    const actions = [{ title: "编辑", run: () => this.editView(view) }];
    if (!view.builtIn) actions.push({ title: "删除", danger: true, run: () => this.deleteView(view) });
    this.menu.replaceChildren(
      ...actions.map((action) => {
        const button = document.createElement("button");
        button.textContent = action.title;
        button.classList.toggle("danger", action.danger === true);
        button.onclick = () => {
          this.menu.hidden = true;
          action.run();
        };
        return button;
      }),
    );
    placeMenu(this.menu, event);
  }
  editView(view = null) {
    this.editingView = view;
    const readOnly = view?.builtIn === true,
      fields = ["name", "channel", "query"];
    this.viewDialog.querySelector("[data-role=view-title]").textContent = readOnly
      ? "内置视图"
      : view
        ? "编辑视图"
        : "新建视图";
    this.viewDialog.querySelector("[data-field=name]").value = view?.name ?? "";
    this.viewDialog.querySelector("[data-field=channel]").value = view?.channel ?? this.channelInput.value;
    this.viewDialog.querySelector("[data-field=query]").value = view?.query ?? this.queryInput.value ?? "*";
    for (const name of fields) this.viewDialog.querySelector(`[data-field=${name}]`).readOnly = readOnly;
    this.viewDialog.querySelector("[type=submit]").hidden = readOnly;
    this.viewDialog.querySelector("[data-action=cancel]").textContent = readOnly ? "关闭" : "取消";
    this.viewDialog.showModal();
  }
  saveView() {
    if (this.editingView?.builtIn) {
      this.viewDialog.close();
      return;
    }
    const name = this.viewDialog.querySelector("[data-field=name]").value.trim(),
      channel = this.viewDialog.querySelector("[data-field=channel]").value.trim(),
      query = this.viewDialog.querySelector("[data-field=query]").value.trim();
    if (!name || !channel || !query) return;
    const view = { id: this.editingView?.id ?? crypto.randomUUID(), name, channel, query },
      index = this.customViews.findIndex((value) => value.id === view.id);
    if (index < 0) this.customViews.push(view);
    else this.customViews[index] = view;
    localStorage.setItem("zpigeon.eventViews", JSON.stringify(this.customViews));
    this.viewDialog.close();
    if (this.channelsLoaded) this.loadChannels();
  }
  deleteView(view) {
    if (!confirm(`确定删除自定义视图 ${view.name}？`)) return;
    this.customViews = this.customViews.filter((value) => value.id !== view.id);
    localStorage.setItem("zpigeon.eventViews", JSON.stringify(this.customViews));
    if (this.channelsLoaded) this.loadChannels();
  }
  loadViews() {
    try {
      const values = JSON.parse(localStorage.getItem("zpigeon.eventViews") || "[]");
      return Array.isArray(values)
        ? values
            .filter(
              (value) =>
                value &&
                typeof value.id === "string" &&
                typeof value.name === "string" &&
                typeof value.channel === "string" &&
                typeof value.query === "string",
            )
            .map(({ id, name, channel, query }) => ({ id, name, channel, query }))
        : [];
    } catch {
      return [];
    }
  }
  async chooseEventFile() {
    try {
      const now = new Date(),
        stamp = [
          now.getFullYear(),
          String(now.getMonth() + 1).padStart(2, "0"),
          String(now.getDate()).padStart(2, "0"),
          "-",
          String(now.getHours()).padStart(2, "0"),
          String(now.getMinutes()).padStart(2, "0"),
          String(now.getSeconds()).padStart(2, "0"),
        ].join(""),
        name = (this.streamName || this.channelInput.value).replace(/[<>:"/\\|?*]/g, "_");
      return await showSaveFilePicker({
        suggestedName: `${name}-${stamp}.log`,
        types: [{ description: "事件日志文本", accept: { "text/plain": [".log", ".txt"] } }],
      });
    } catch (error) {
      if (error.name !== "AbortError") this.notify(error);
    }
  }
  beginDownload(message) {
    this.streamId = crypto.randomUUID();
    this.downloadStopped = false;
    this.downloadButton.disabled = this.streamButton.disabled = true;
    this.stopStreamButton.hidden = false;
    this.streamState.textContent = message;
  }
  async downloadExisting() {
    if (!this.connected || this.streamId) return;
    const file = await this.chooseEventFile();
    if (!file) return;
    this.beginDownload("正在下载已有数据…");
    let writable;
    try {
      writable = await file.createWritable();
      const encoder = new TextEncoder();
      let bookmark = "";
      do {
        const page = await this.call("/api/eventlog/query", {
          channelPath: this.channelInput.value,
          query: this.queryInput.value || null,
          bookmark: bookmark || null,
          maxEvents: 256,
        });
        for (const record of page.records) await writable.write(encoder.encode(record.xml + "\r\n"));
        bookmark = page.nextBookmark;
        if (!page.hasMore) break;
      } while (!this.downloadStopped);
      this.finishStream(this.downloadStopped ? "下载已停止" : "已有数据下载完成");
    } catch (error) {
      this.finishStream("已有数据下载失败");
      this.notify(error);
    } finally {
      if (writable) await writable.close();
    }
  }
  async startStream() {
    if (!this.connected || this.streamId || !this.channelEnabled) return;
    const channel = this.channelInput.value.trim();
    if (!channel) return;
    const file = await this.chooseEventFile();
    if (!file) return;
    this.beginDownload("正在流式下载新增事件…");
    this.streamAbort = new AbortController();
    let writable;
    try {
      writable = await file.createWritable();
      const parameters = new URLSearchParams({ channelPath: channel, name: this.streamName || channel });
      if (this.queryInput.value.trim()) parameters.set("query", this.queryInput.value.trim());
      const response = await fetch(apiUrl(`/api/eventlog/stream/${this.streamId}?${parameters}`), {
        signal: this.streamAbort.signal,
      });
      if (!response.ok) throw new Error((await response.text()) || `HTTP ${response.status}`);
      const reader = response.body.getReader();
      for (;;) {
        const { done, value } = await reader.read();
        if (done) break;
        await writable.write(value);
      }
      this.finishStream("流式下载已结束");
    } catch (error) {
      if (error.name === "AbortError") this.finishStream("流式下载已停止");
      else {
        this.finishStream("流式下载失败");
        this.notify(error);
      }
    } finally {
      if (writable) await writable.close();
    }
  }
  async stopStream(silent = false) {
    if (!this.streamId) return;
    this.downloadStopped = true;
    if (!this.streamAbort) return;
    const id = this.streamId;
    this.streamAbort.abort();
    try {
      await fetch(apiUrl(`/api/eventlog/stream/${id}/stop`), { method: "POST" });
    } catch (error) {
      if (!silent) this.notify(error);
    }
  }
  finishStream(message) {
    this.streamId = null;
    this.streamAbort = null;
    this.downloadButton.disabled = !this.connected;
    this.streamButton.disabled = !this.connected || !this.channelEnabled;
    this.stopStreamButton.hidden = true;
    this.streamState.textContent = message;
  }
  async setEnabled(channel, enabled) {
    try {
      await this.call("/api/eventlog/channel", { channelPath: channel, enabled });
      this.notify("操作成功");
      return true;
    } catch (error) {
      this.notify(error);
      return false;
    }
  }
  async clear(channel) {
    if (!channel || !confirm(`确定清除 ${channel} 的全部事件？`)) return;
    try {
      await this.call("/api/eventlog/clear", { channelPath: channel });
      this.notify("日志已清除");
      if (this.channelInput.value === channel) await this.query(false);
      if (this.dialog.open && this.propertyChannel === channel) await this.properties(channel, this.propertyWindowsLog);
    } catch (error) {
      this.notify(error);
    }
  }
  async properties(channel, windowsLog, info = null) {
    try {
      info ??= await this.call("/api/eventlog/channel/info", { channelPath: channel });
      this.propertyChannel = channel;
      this.propertyWindowsLog = windowsLog;
      const type = ["管理", "操作", "分析", "调试"][info.type] ?? info.type;
      this.dialog.querySelector("[data-role=property-title]").textContent =
        `日志属性 - ${channel.split("/").pop()} (类型: ${type})`;
      const set = (name, value) => (this.dialog.querySelector(`[data-field=${name}]`).value = value);
      set("full-name", channel);
      set("log-path", info.logFilePath);
      set("file-size", formatBytes(info.fileSize));
      set("creation-time", fileTime(info.creationTime));
      set("write-time", fileTime(info.lastWriteTime));
      set("access-time", fileTime(info.lastAccessTime));
      const enabled = this.dialog.querySelector("[data-field=enabled]");
      enabled.checked = info.enabled;
      enabled.disabled = windowsLog;
      set("maximum-size", (BigInt(info.maximumSize) / 1024n).toString());
      this.dialog.querySelector(`input[name=event-retention][value="${info.retentionMode}"]`).checked = true;
      if (!this.dialog.open) this.dialog.showModal();
    } catch (error) {
      this.notify(error);
    }
  }
  async saveProperties() {
    const maximum = BigInt(this.dialog.querySelector("[data-field=maximum-size]").value) * 1024n,
      retentionMode = Number(this.dialog.querySelector("input[name=event-retention]:checked").value),
      enabled = this.dialog.querySelector("[data-field=enabled]").checked;
    try {
      await this.call("/api/eventlog/channel/configure", {
        channelPath: this.propertyChannel,
        enabled,
        retentionMode,
        maximumSize: maximum.toString(),
      });
      this.dialog.close();
      this.notify("属性已保存");
    } catch (error) {
      this.notify(error);
    }
  }
}

export class WslManager {
  constructor(root, { call, notify, openFiles }) {
    this.root = root;
    this.call = call;
    this.notify = notify;
    this.openFiles = openFiles;
    root.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <input data-role="filter" /><span data-role="summary" class="status"></span><span class="spacer"></span
        ><button data-action="refresh"></button>
      </div>
      <div class="manager-table">
        <table>
          <thead><tr></tr></thead>
          <tbody></tbody>
        </table>
        <div class="manager-empty"></div>
      </div>`;
    this.filter = root.querySelector("[data-role=filter]");
    this.body = root.querySelector("tbody");
    this.empty = root.querySelector(".manager-empty");
    this.filter.oninput = () => this.render();
    root.querySelector("[data-action=refresh]").onclick = () => this.load(true);
    this.localize();
  }
  localize() {
    this.filter.placeholder = t("wsl.filter");
    this.root.querySelector("[data-action=refresh]").textContent = t("common.refresh");
    this.root.querySelector("thead tr").replaceChildren(
      ...["common.name", "common.status", "wsl.version", "wsl.defaultUser", "wsl.integration", "common.actions"].map(
        (key) => {
          const cell = document.createElement("th");
          cell.textContent = t(key);
          return cell;
        },
      ),
    );
  }
  activate(connected) {
    this.connected = connected;
    if (!connected) {
      this.empty.hidden = false;
      this.empty.textContent = t("common.clientDisconnected");
    } else if (!this.loaded) this.load();
  }
  disconnect() {
    this.connected = false;
    this.loaded = false;
    this.records = [];
    this.body.replaceChildren();
    this.empty.hidden = false;
    this.empty.textContent = t("common.clientDisconnected");
  }
  async load(force = false) {
    if (!this.connected || this.loading || (this.loaded && !force)) return;
    this.loading = true;
    this.empty.hidden = false;
    this.empty.textContent = t("common.fetching");
    try {
      this.records = await this.call("/api/wsl");
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
      records = (this.records || []).filter((record) => record.identity.toLocaleLowerCase().includes(query));
    this.body.replaceChildren(...records.map((record) => this.row(record)));
    this.empty.hidden = records.length !== 0;
    if (!records.length && this.loaded) this.empty.textContent = t("wsl.empty");
    this.root.querySelector("[data-role=summary]").textContent = t("wsl.summary", {
      value: this.records?.length || 0,
    });
  }
  row(record) {
    const row = document.createElement("tr"),
      packed = BigInt(record.value),
      version = Number(packed >> 32n),
      defaultUser = Number(packed & 0xffffffffn),
      flags = record.flags & 0x7fffffff,
      integration = [
        flags & 1 ? t("wsl.interop") : null,
        flags & 2 ? t("wsl.appendPath") : null,
        flags & 4 ? t("wsl.mountDrives") : null,
      ]
        .filter(Boolean)
        .join(", "),
      name = `${record.identity}${record.flags & 0x80000000 ? ` (${t("wsl.default")})` : ""}`,
      actions = document.createElement("td");
    for (const value of [
      name,
      record.state ? t("common.running") : t("common.stopped"),
      `WSL ${version}`,
      String(defaultUser),
      integration || t("common.none"),
    ]) {
      const cell = document.createElement("td");
      cell.textContent = value;
      cell.title = value;
      row.append(cell);
    }
    const buttons = record.state
      ? [
          [t("common.stop"), 7, true],
          [t("common.restart"), 12, true],
        ]
      : [[t("common.start"), 6, false]];
    if (!(record.flags & 0x80000000)) buttons.push([t("wsl.setDefault"), 21, false]);
    const fileButton = document.createElement("button");
    fileButton.textContent = t("wsl.files");
    fileButton.onclick = async () => {
      if (!/^[^\\/:*?"<>|\u0000-\u001f]{1,64}$/.test(record.identity)) {
        this.notify(t("wsl.invalidName"));
        return;
      }
      try {
        await this.openFiles(`\\\\wsl.localhost\\${record.identity}\\`);
      } catch (error) {
        this.notify(error);
      }
    };
    actions.append(fileButton);
    for (const [label, action, confirmAction] of buttons) {
      const button = document.createElement("button");
      button.textContent = label;
      button.onclick = () => {
        if (!confirmAction || confirm(t("wsl.confirm", { action: label, name: record.identity })))
          this.control(record.identity, action);
      };
      actions.append(button);
    }
    row.append(actions);
    return row;
  }
  async control(identity, action) {
    try {
      await this.call("/api/wsl/control", { action, identity });
      await this.load(true);
    } catch (error) {
      this.notify(error);
    }
  }
}

const systemInformationFields = {
  computerName: ["计算机名", "系统"],
  fullComputerName: ["完整计算机名", "系统"],
  systemRoot: ["系统目录", "系统"],
  bootTime: ["启动时间", "系统"],
  timeZone: ["时区", "区域"],
  locale: ["系统区域", "区域"],
  firmware: ["固件类型", "固件"],
  productName: ["产品名称", "Windows"],
  displayVersion: ["显示版本", "Windows"],
  edition: ["版本", "Windows"],
  installationType: ["安装类型", "Windows"],
  buildLab: ["完整版本", "Windows"],
  registeredOwner: ["注册所有者", "Windows"],
  registeredOrganization: ["注册组织", "Windows"],
  manufacturer: ["制造商", "硬件"],
  model: ["型号", "硬件"],
  family: ["产品系列", "硬件"],
  sku: ["系统 SKU", "硬件"],
  baseboardManufacturer: ["主板制造商", "硬件"],
  baseboardProduct: ["主板型号", "硬件"],
  biosVendor: ["BIOS 制造商", "固件"],
  biosVersion: ["BIOS 版本", "固件"],
  biosDate: ["BIOS 日期", "固件"],
  processor: ["处理器", "硬件"],
  installTime: ["安装时间", "Windows"],
  secureBoot: ["安全启动", "固件"],
  remoteDesktopEnabled: ["允许远程桌面连接", "远程桌面"],
  remoteDesktopPort: ["监听端口", "远程桌面"],
};

export class SystemInformationManager {
  constructor(root, { call, notify, filePicker, hardwareOnly = false }) {
    this.root = root;
    this.call = call;
    this.notify = notify;
    this.filePicker = filePicker;
    this.hardwareOnly = hardwareOnly;
    this.connected = false;
    this.records = [];
    root.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <input data-role="filter" placeholder="筛选系统信息" /><span class="spacer"></span
        ><button data-role="refresh">刷新</button>
      </div>
      <div class="system-information" data-role="body"></div>
      <div class="manager-empty">Client 未连接</div>
      <dialog data-role="editor">
        <form>
          <h2>修改系统信息</h2>
          <label><span data-role="field-name"></span><span data-role="editor-control"></span></label>
          <p class="property-note" data-role="note"></p>
          <div class="dialog-actions">
            <button type="submit">保存</button><button type="button" data-action="cancel">取消</button>
          </div>
        </form>
      </dialog>
      <dialog data-role="environment" class="environment-editor">
        <form>
          <h2 data-role="environment-title"></h2>
          <label>名称<input data-field="environment-name" maxlength="32767" required /></label
          ><label data-role="environment-value">值<input data-field="environment-value" maxlength="32767" /></label>
          <div data-role="environment-path" hidden>
            <div class="environment-path-list" data-role="environment-path-list"></div>
            <button type="button" data-action="environment-add-path">＋</button>
          </div>
          <div class="dialog-actions">
            <button type="submit">保存</button><button type="button" data-action="environment-cancel">取消</button>
          </div>
        </form>
      </dialog>`;
    this.filter = root.querySelector("[data-role=filter]");
    this.body = root.querySelector("[data-role=body]");
    this.empty = root.querySelector(".manager-empty");
    this.dialog = root.querySelector("[data-role=editor]");
    this.editorControl = this.dialog.querySelector("[data-role=editor-control]");
    this.environmentDialog = root.querySelector("[data-role=environment]");
    this.filter.oninput = () => this.render();
    root.querySelector("[data-role=refresh]").onclick = () => this.load(true);
    this.dialog.querySelector("[data-action=cancel]").onclick = () => this.dialog.close();
    this.dialog.onsubmit = (event) => {
      event.preventDefault();
      this.save();
    };
    this.environmentDialog.querySelector("[data-field=environment-name]").oninput = () => this.syncEnvironmentMode();
    this.environmentDialog.querySelector("[data-action=environment-cancel]").onclick = () =>
      this.environmentDialog.close();
    this.environmentDialog.querySelector("[data-action=environment-add-path]").onclick = () => this.addPath("");
    this.environmentDialog.onsubmit = (event) => {
      event.preventDefault();
      this.saveEnvironment();
    };
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
  }
  async load(force = false) {
    if (!this.connected || this.loading || (this.loaded && !force)) return;
    this.loading = true;
    this.empty.hidden = false;
    this.empty.textContent = "正在读取系统信息…";
    try {
      const [baseResult, recordsResult] = await Promise.allSettled([
          this.call("/api/system"),
          this.call("/api/system-details"),
        ]),
        base = baseResult.status === "fulfilled" ? baseResult.value : null,
        records = recordsResult.status === "fulfilled" ? recordsResult.value : [];
      if (baseResult.status === "rejected" && recordsResult.status === "rejected") throw baseResult.reason;
      if (baseResult.status === "rejected") this.notify(baseResult.reason);
      if (recordsResult.status === "rejected") this.notify(recordsResult.reason);
      const architecture = base && ({ 1: "x86", 2: "x64", 3: "ARM64" }[base.architecture] || base.architecture),
        computer = records.find((record) => record.identity === "computerName") || {
          identity: "computerName",
        };
      let display = 0;
      this.records = [
        ...(base
          ? [
              { ...computer, detail: base.computerName },
              {
                name: "Windows 版本",
                description: "Windows",
                detail: `${base.majorVersion}.${base.minorVersion}.${base.buildNumber}`,
              },
              { name: "平台", description: "系统", detail: architecture },
              { name: "逻辑处理器", description: "硬件", detail: String(base.processorCount) },
              { name: "物理内存", description: "硬件", detail: formatBytes(base.physicalMemoryBytes) },
            ]
          : []),
        ...records.filter((record) => !base || record.identity !== "computerName"),
      ].map((record) => {
        if (record.kind === 19)
          return {
            ...record,
            description: record.flags & 0x200 ? "系统环境变量" : "用户环境变量",
          };
        if (record.flags & 4) {
          const data = record.data,
            mode = data.width ? `${data.width} × ${data.height} · ${data.frequency} Hz` : "";
          return {
            ...record,
            name: `显示器 ${++display}`,
            description: "显示器",
            detail: [record.name, mode, record.description].filter(Boolean).join(" · "),
          };
        }
        const field = systemInformationFields[record.identity];
        return field ? { ...record, name: field[0], description: field[1] } : record;
      });
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
  value(record) {
    if (record.identity === "bootTime" || record.identity === "installTime") return fileTime(record.value);
    if (record.identity === "secureBoot") return Number(record.value) ? "已启用" : "未启用";
    if (record.identity === "remoteDesktopEnabled") return Number(record.value) ? "已启用" : "已禁用";
    if (record.identity === "remoteDesktopPort") return record.value;
    if (record.identity === "firmware") return { 1: "BIOS", 2: "UEFI" }[record.value] || "未知";
    return record.detail;
  }
  render() {
    const query = this.filter.value.toLocaleLowerCase(),
      excluded = new Set([
        "remoteDesktopEnabled",
        "remoteDesktopPort",
        "firmware",
        "secureBoot",
        "biosVendor",
        "biosVersion",
        "biosDate",
      ]),
      records = this.records.filter(
        (record) =>
          !excluded.has(record.identity) &&
          ["硬件", "显示器"].includes(record.description) === this.hardwareOnly &&
          (!query || `${record.name} ${record.description} ${this.value(record)}`.toLocaleLowerCase().includes(query)),
      ),
      groups = new Map();
    for (const record of records) {
      const name = record.description || "系统";
      if (!groups.has(name)) groups.set(name, []);
      groups.get(name).push(record);
    }
    this.body.replaceChildren(...[...groups].map(([name, values]) => this.section(name, values)));
    this.empty.hidden = records.length !== 0;
    if (this.connected && this.loaded && !records.length)
      this.empty.textContent = this.hardwareOnly ? "没有匹配的硬件信息" : "没有匹配的系统信息";
  }
  section(name, values) {
    const section = document.createElement("section"),
      header = document.createElement("header"),
      title = document.createElement("h2"),
      list = document.createElement("dl"),
      environment = values.some((record) => record.kind === 19);
    title.textContent = name;
    list.className = "system-information-grid";
    header.className = "system-section-title";
    header.append(title);
    if (environment) {
      const button = document.createElement("button");
      button.textContent = "＋";
      button.title = "新建环境变量";
      button.onclick = () => this.editEnvironment(null, name === "系统环境变量" ? "system" : "user");
      header.append(button);
    }
    section.append(header);
    for (const record of values) {
      const dt = document.createElement("dt"),
        dd = document.createElement("dd"),
        value = document.createElement("span");
      dt.textContent = record.name;
      value.textContent = this.value(record) || "—";
      value.title = value.textContent;
      dd.append(value);
      if (record.kind === 19) {
        const edit = document.createElement("button"),
          remove = document.createElement("button");
        edit.textContent = "…";
        edit.title = "修改";
        remove.textContent = "−";
        remove.title = "删除";
        edit.onclick = () => this.editEnvironment(record);
        remove.onclick = () => this.deleteEnvironment(record);
        dd.append(edit, remove);
      } else if (record.flags & 1) {
        const button = document.createElement("button");
        button.textContent = "修改";
        button.onclick = () => this.edit(record);
        dd.append(button);
      }
      list.append(dt, dd);
    }
    section.append(list);
    return section;
  }
  edit(record) {
    this.editing = record;
    this.dialog.querySelector("[data-role=field-name]").textContent = record.name;
    this.options =
      record.identity === "timeZone" ? windowsTimeZones : record.identity === "locale" ? windowsLocales : null;
    if (this.options) {
      const search = document.createElement("input"),
        select = document.createElement("select"),
        original = this.value(record);
      let selected = original;
      const render = () => {
        const query = search.value.toLocaleLowerCase();
        select.replaceChildren(
          ...this.options
            .filter(([value, label]) => !query || `${value} ${label}`.toLocaleLowerCase().includes(query))
            .map(([value, label]) => new Option(`${label} — ${value}`, value)),
        );
        if ([...select.options].some((option) => option.value === selected)) select.value = selected;
      };
      search.type = "search";
      search.className = "dialog-input";
      search.placeholder = `搜索${record.name}`;
      select.className = "system-option-list";
      select.size = 12;
      select.required = true;
      select.onchange = () => (selected = select.value);
      search.oninput = render;
      render();
      if ([...select.options].some((option) => option.value === original)) select.value = original;
      this.input = select;
      this.editorControl.replaceChildren(search, select);
      this.dialog.showModal();
      search.focus();
    } else {
      const input = document.createElement("input");
      input.className = "dialog-input";
      input.required = true;
      input.type = "text";
      input.maxLength = 32767;
      input.value = this.value(record);
      this.input = input;
      this.editorControl.replaceChildren(input);
      this.dialog.showModal();
      input.select();
    }
    this.dialog.querySelector("[data-role=note]").textContent = record.flags & 2 ? "此修改在重新启动后完全生效。" : "";
  }
  async save() {
    const argument = this.input.value.trim();
    if (this.editing.identity === "computerName" && !argument) return;
    if (this.options && !this.options.some(([value]) => value === argument)) {
      this.notify(`请选择有效的${this.editing.name}`);
      return;
    }
    try {
      await this.call("/api/system-details/control", { action: 23, identity: this.editing.identity, argument });
      this.dialog.close();
      this.notify(this.editing.flags & 2 ? "已保存，重新启动后完全生效" : "已保存");
      await this.load(true);
    } catch (error) {
      this.notify(error);
    }
  }
  editEnvironment(record, scope) {
    this.environmentEditing = record;
    this.environmentScope = scope || (record?.flags & 0x200 ? "system" : "user");
    const dialog = this.environmentDialog,
      name = dialog.querySelector("[data-field=environment-name]"),
      path = (record?.name || "").toLocaleLowerCase() === "path",
      scopeName = this.environmentScope === "system" ? "系统" : "用户";
    dialog.querySelector("[data-role=environment-title]").textContent =
      `${record ? "修改" : "新建"}${scopeName}环境变量`;
    name.value = record?.name || "";
    name.readOnly = !!record;
    dialog.querySelector("[data-role=environment-value]").hidden = path;
    dialog.querySelector("[data-role=environment-path]").hidden = !path;
    dialog.querySelector("[data-field=environment-value]").value = record?.detail || "";
    const list = dialog.querySelector("[data-role=environment-path-list]");
    list.replaceChildren();
    if (path) for (const value of (record?.detail || "").split(";")) this.addPath(value);
    dialog.showModal();
    name.focus();
  }
  syncEnvironmentMode() {
    const dialog = this.environmentDialog,
      path = dialog.querySelector("[data-field=environment-name]").value.trim().toLocaleLowerCase() === "path",
      value = dialog.querySelector("[data-role=environment-value]"),
      paths = dialog.querySelector("[data-role=environment-path]"),
      list = dialog.querySelector("[data-role=environment-path-list]");
    if (path && !value.hidden && !list.children.length)
      for (const entry of dialog.querySelector("[data-field=environment-value]").value.split(";")) this.addPath(entry);
    value.hidden = path;
    paths.hidden = !path;
  }
  addPath(value) {
    const row = document.createElement("div"),
      input = document.createElement("input"),
      choose = document.createElement("button"),
      remove = document.createElement("button");
    row.className = "environment-path-row";
    input.value = value;
    input.spellcheck = false;
    choose.type = remove.type = "button";
    choose.textContent = "选择…";
    remove.textContent = "−";
    remove.title = "删除";
    choose.onclick = async () => {
      const path = await this.filePicker?.open({ mode: "folder", initialPath: input.value });
      if (path) input.value = path;
    };
    remove.onclick = () => row.remove();
    row.append(input, choose, remove);
    this.environmentDialog.querySelector("[data-role=environment-path-list]").append(row);
  }
  async saveEnvironment() {
    const dialog = this.environmentDialog,
      name = dialog.querySelector("[data-field=environment-name]").value.trim();
    if (!name || name.includes("=")) {
      this.notify("环境变量名称无效");
      return;
    }
    const path = name.toLocaleLowerCase() === "path",
      argument = path
        ? [...dialog.querySelectorAll(".environment-path-row input")].map((input) => input.value).join(";")
        : dialog.querySelector("[data-field=environment-value]").value,
      identity = this.environmentEditing?.identity || `environment:${this.environmentScope}:${name}`;
    try {
      await this.call("/api/system-details/control", { action: 23, identity, argument });
      dialog.close();
      this.notify("环境变量已保存，新进程将读取新值");
      await this.load(true);
    } catch (error) {
      this.notify(error);
    }
  }
  async deleteEnvironment(record) {
    if (!confirm(`确定删除环境变量“${record.name}”？`)) return;
    try {
      await this.call("/api/system-details/control", { action: 2, identity: record.identity });
      this.notify("环境变量已删除");
      await this.load(true);
    } catch (error) {
      this.notify(error);
    }
  }
}

export class WlanManager {
  constructor(root, { call, notify }) {
    this.root = root;
    this.call = call;
    this.notify = notify;
    this.connected = false;
    this.records = [];
    root.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <select data-role="interface" aria-label="WLAN 接口"></select
        ><input data-role="filter" placeholder="筛选 WLAN" /><span data-role="summary" class="status"></span
        ><span class="spacer"></span><button data-role="refresh">刷新</button>
      </div>
      <div class="wlan-content">
        <section>
          <h2>可用网络</h2>
          <div class="manager-table">
            <table>
              <thead>
                <tr>
                  <th>SSID</th>
                  <th>信号</th>
                  <th>安全</th>
                  <th>配置文件</th>
                  <th>状态</th>
                </tr>
              </thead>
              <tbody data-kind="17"></tbody>
            </table>
          </div>
        </section>
        <section>
          <h2>已保存的配置文件</h2>
          <div class="manager-table">
            <table>
              <thead>
                <tr>
                  <th>名称</th>
                </tr>
              </thead>
              <tbody data-kind="18"></tbody>
            </table>
          </div>
        </section>
      </div>
      <div class="manager-empty">Client 未连接</div>
      <div class="context-menu administration-menu" data-role="menu" hidden></div>
      <dialog data-role="profile">
        <form method="dialog">
          <h2 data-role="profile-title">WLAN 配置文件</h2>
          <pre data-role="profile-content"></pre>
          <div class="dialog-actions"><button value="close">关闭</button></div>
        </form>
      </dialog>`;
    this.interface = root.querySelector("[data-role=interface]");
    this.filter = root.querySelector("[data-role=filter]");
    this.summary = root.querySelector("[data-role=summary]");
    this.empty = root.querySelector(".manager-empty");
    this.menu = root.querySelector("[data-role=menu]");
    this.profileDialog = root.querySelector("[data-role=profile]");
    this.filter.oninput = () => this.render();
    this.interface.onchange = () => this.render();
    root.querySelector("[data-role=refresh]").onclick = () => this.load(true);
    document.addEventListener("pointerdown", (event) => {
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
    this.interface.replaceChildren();
    this.summary.textContent = "";
    for (const body of this.root.querySelectorAll("tbody")) body.replaceChildren();
    this.empty.hidden = false;
    this.empty.textContent = "Client 未连接";
  }
  async load(force = false) {
    if (!this.connected || this.loading || (this.loaded && !force)) return;
    this.loading = true;
    this.empty.hidden = false;
    this.empty.textContent = "正在读取 WLAN…";
    try {
      const selected = this.interface.value;
      this.records = await this.call("/api/wlan");
      const interfaces = this.records.filter((record) => record.kind === 16);
      this.interface.replaceChildren(
        ...interfaces.map((record) => new Option(`${record.name} · ${wlanState(record.state)}`, record.identity)),
      );
      this.interface.value = interfaces.some((record) => record.identity === selected)
        ? selected
        : (interfaces.find((record) => record.state === 1) ?? interfaces[0])?.identity || "";
      this.loaded = true;
      this.render();
    } catch (error) {
      this.records = [];
      this.render();
      this.empty.hidden = false;
      this.empty.textContent = error.message;
      this.notify(error);
    } finally {
      this.loading = false;
    }
  }
  render() {
    const selected = this.interface.value,
      query = this.filter.value.toLocaleLowerCase(),
      records = this.records.filter(
        (record) =>
          record.kind !== 16 &&
          record.identity.startsWith(`${selected}|`) &&
          (!query ||
            `${record.name} ${record.description} ${record.detail} ${record.identity}`
              .toLocaleLowerCase()
              .includes(query)),
      );
    for (const kind of [17, 18]) {
      const body = this.root.querySelector(`tbody[data-kind="${kind}"]`),
        values = records.filter((record) => record.kind === kind);
      body.replaceChildren(...values.map((record) => this.row(record)));
    }
    const interfaces = this.records.filter((record) => record.kind === 16).length,
      networks = records.filter((record) => record.kind === 17).length;
    this.summary.textContent = interfaces ? `${networks} 个网络` : "";
    this.empty.hidden = interfaces !== 0;
    if (this.connected && this.loaded && !interfaces) this.empty.textContent = "未发现 WLAN 接口";
  }
  row(record) {
    const row = document.createElement("tr"),
      values =
        record.kind === 17
          ? [
              record.name || "(隐藏网络)",
              `${record.state}%`,
              record.flags & 8 ? "是" : "否",
              record.detail || "—",
              record.flags & 1 ? "已连接" : record.flags & 4 ? "可连接" : "不可连接",
            ]
          : [record.name];
    row.record = record;
    for (const value of values) {
      const cell = row.insertCell();
      cell.textContent = value;
      cell.title = value;
    }
    row.oncontextmenu = (event) => {
      event.preventDefault();
      this.openMenu(event, record);
    };
    return row;
  }
  openMenu(event, record) {
    this.menu.hidden = true;
    const actions = [];
    if ((record.kind === 16 && record.state === 1) || (record.kind === 17 && record.flags & 1))
      actions.push({ title: "断开连接", action: 25, identity: record.identity.split("|")[0] });
    if (record.kind === 17 && record.flags & 1 && record.flags & 2)
      actions.push({ title: "显示密码", query: "password", identity: record.identity, disabled: !(record.flags & 16) });
    if ((record.kind === 17 && !(record.flags & 1) && record.flags & 2) || record.kind === 18)
      actions.push({ title: "连接", action: 24, identity: record.identity });
    if (record.kind === 18)
      actions.push(
        { title: "显示配置文件", query: "profile", identity: record.identity },
        { title: t("common.download"), query: "download", identity: record.identity },
        { title: "删除配置文件", action: 2, identity: record.identity, danger: true },
      );
    this.menu.replaceChildren(
      ...actions.map((action) => {
        const button = document.createElement("button");
        button.textContent = action.title;
        button.disabled = action.disabled === true;
        button.classList.toggle("danger", action.danger === true);
        button.onclick = () => {
          this.menu.hidden = true;
          action.query ? this.showProfile(action) : this.execute(action);
        };
        return button;
      }),
    );
    if (this.menu.children.length) placeMenu(this.menu, event);
  }
  async showProfile(action) {
    try {
      const records = await this.call("/api/wlan/profile", { identity: action.identity }),
        xml = records[0]?.detail ?? "";
      if (action.query === "password") {
        const password =
          new DOMParser().parseFromString(xml, "application/xml").querySelector("keyMaterial")?.textContent ?? "";
        await navigator.clipboard.writeText(password).catch(() => {});
        alert(`Wi-Fi 密码：\n\n${password}\n\n已尝试复制到管理端剪贴板。`);
        return;
      }
      if (action.query === "download") {
        const link = document.createElement("a"),
          url = URL.createObjectURL(new Blob([xml], { type: "application/xml;charset=utf-8" }));
        link.href = url;
        link.download = `${(records[0]?.name || "WLAN").replace(/[<>:\x22/\\|?*]/g, "_")}.xml`;
        link.click();
        URL.revokeObjectURL(url);
        return;
      }
      this.profileDialog.querySelector("[data-role=profile-title]").textContent =
        `WLAN 配置文件 - ${records[0]?.name || ""}`;
      this.profileDialog.querySelector("[data-role=profile-content]").textContent = xml;
      this.profileDialog.showModal();
    } catch (error) {
      this.notify(error);
    }
  }
  async execute(action) {
    if (action.danger && !confirm(`确定${action.title}？`)) return;
    try {
      await this.call("/api/wlan/control", { action: action.action, identity: action.identity });
      this.notify("操作已提交");
      await this.load(true);
    } catch (error) {
      this.notify(error);
    }
  }
}

export class CertificateManager {
  constructor(root, { call, notify, installer }) {
    this.root = root;
    this.call = call;
    this.notify = notify;
    this.installer = installer;
    this.installer.installed = () => this.load(true);
    this.records = [];
    this.connected = false;
    root.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <input data-role="filter" placeholder="筛选证书" /><span data-role="summary" class="status"></span
        ><span class="spacer"></span><button data-role="refresh">刷新</button>
      </div>
      <div class="administration-split certificate-body">
        <aside class="administration-tree">
          <ul data-role="tree"></ul>
          <div class="manager-empty">Client 未连接</div>
        </aside>
        <section class="manager-table certificate-list">
          <table>
            <thead>
              <tr>
                <th>颁发给</th>
                <th>颁发者</th>
                <th>到期日期</th>
                <th>预期目的</th>
                <th>友好名称</th>
                <th>状态</th>
              </tr>
            </thead>
            <tbody></tbody>
          </table>
          <div class="manager-empty" data-role="list-empty">请选择证书存储</div>
        </section>
      </div>
      <div class="context-menu administration-menu" data-role="menu" hidden></div>
      <dialog class="certificate-properties" data-role="properties">
        <form method="dialog">
          <h2 data-role="title">证书</h2>
          <div class="property-tabs">
            <button type="button" data-tab="general" class="active">常规</button
            ><button type="button" data-tab="details">详细信息</button
            ><button type="button" data-tab="path">证书路径</button>
          </div>
          <section data-page="general">
            <div class="certificate-summary">
              <strong data-role="certificate-status"></strong><span data-role="certificate-purpose"></span>
            </div>
            <dl class="details-grid" data-role="general"></dl>
            <p class="property-note" data-role="private-key" hidden>此证书具有对应的私钥。</p>
          </section>
          <section data-page="details" hidden>
            <div class="certificate-detail-list" data-role="details"></div>
            <pre data-role="detail-value"></pre>
          </section>
          <section data-page="path" hidden>
            <div class="certificate-chain" data-role="chain"></div>
            <pre data-role="chain-status"></pre>
          </section>
          <div class="dialog-actions"><button value="close">关闭</button></div>
        </form>
      </dialog>`;
    this.filter = root.querySelector("[data-role=filter]");
    this.tree = root.querySelector("[data-role=tree]");
    this.body = root.querySelector("tbody");
    this.empty = root.querySelector(".administration-tree .manager-empty");
    this.listEmpty = root.querySelector("[data-role=list-empty]");
    this.summary = root.querySelector("[data-role=summary]");
    this.menu = root.querySelector("[data-role=menu]");
    this.dialog = root.querySelector("[data-role=properties]");
    this.filter.oninput = () => this.renderList();
    root.querySelector("[data-role=refresh]").onclick = () => this.load(true);
    this.body.oncontextmenu = (event) => {
      event.preventDefault();
      const row = event.target.closest("tr");
      this.openMenu(event, row?.record || null);
    };
    for (const tab of this.dialog.querySelectorAll("[data-tab]"))
      tab.onclick = () => this.selectPropertyTab(tab.dataset.tab);
    document.addEventListener("pointerdown", (event) => {
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
    this.selectedStore = null;
    this.installer.invalidate();
    this.tree.replaceChildren();
    this.body.replaceChildren();
    this.summary.textContent = "";
    this.empty.hidden = false;
    this.empty.textContent = "Client 未连接";
    this.listEmpty.hidden = false;
    this.listEmpty.textContent = "Client 未连接";
  }
  async load(force = false) {
    if (!this.connected || this.loading || (this.loaded && !force)) return;
    this.loading = true;
    this.empty.hidden = false;
    this.empty.textContent = "正在读取证书存储…";
    const selected = this.selectedStore?.identity;
    try {
      this.records = await this.call("/api/certificates");
      this.installer.setStores(this.records);
      this.loaded = true;
      this.selectedStore = this.records.find((record) => record.kind === 20 && record.identity === selected) || null;
      this.renderTree();
      if (!this.selectedStore) this.selectStore(this.records.find((record) => record.kind === 20));
      else this.renderList();
    } catch (error) {
      this.records = [];
      this.tree.replaceChildren();
      this.body.replaceChildren();
      this.empty.textContent = error.message;
      this.listEmpty.hidden = false;
      this.listEmpty.textContent = error.message;
      this.notify(error);
    } finally {
      this.loading = false;
    }
  }
  renderTree() {
    const stores = this.records.filter((record) => record.kind === 20),
      scopes = [
        ["user", t("certificate.scope.currentUser")],
        ["machine", "本地计算机"],
      ];
    this.tree.replaceChildren(
      ...scopes.map(([scope, title]) =>
        this.scopeNode(
          scope,
          title,
          stores.filter((store) => store.identity.startsWith(scope + "\n")),
        ),
      ),
    );
    this.empty.hidden = stores.length !== 0;
    if (!stores.length) this.empty.textContent = "没有证书存储";
  }
  scopeNode(scope, title, stores) {
    const li = document.createElement("li"),
      row = document.createElement("div"),
      arrow = document.createElement("button"),
      label = document.createElement("button"),
      list = document.createElement("ul");
    row.className = "administration-node-row";
    arrow.className = "administration-arrow";
    arrow.textContent = "▾";
    label.className = "administration-node-label";
    label.textContent = title;
    list.append(
      ...stores
        .sort((a, b) => certificateStoreName(a.name).localeCompare(certificateStoreName(b.name)))
        .map((store) => this.storeNode(store)),
    );
    row.append(arrow, label);
    li.append(row, list);
    const toggle = () => {
      list.hidden = !list.hidden;
      arrow.textContent = list.hidden ? "▸" : "▾";
    };
    arrow.onclick = label.onclick = toggle;
    return li;
  }
  storeNode(store) {
    const li = document.createElement("li"),
      row = document.createElement("div"),
      arrow = document.createElement("button"),
      label = document.createElement("button");
    row.className = "administration-node-row";
    row.classList.toggle("selected", this.selectedStore?.identity === store.identity);
    arrow.className = "administration-arrow";
    arrow.textContent = "";
    arrow.disabled = true;
    label.className = "administration-node-label";
    label.textContent = certificateStoreName(store.name) + (store.flags & 1 ? " ⚠" : "");
    label.title = store.flags & 1 ? `无法打开，Win32: ${hex32(store.state)}` : `${store.value} 个证书`;
    label.onclick = () => this.selectStore(store);
    row.oncontextmenu = (event) => {
      event.preventDefault();
      this.selectStore(store);
      this.openMenu(event, null);
    };
    row.append(arrow, label);
    li.append(row);
    return li;
  }
  selectStore(store) {
    if (!store) return;
    this.selectedStore = store;
    this.renderTree();
    this.renderList();
  }
  certificates() {
    if (!this.selectedStore) return [];
    const prefix = this.selectedStore.identity + "\n";
    return this.records.filter((record) => record.kind === 21 && record.identity.startsWith(prefix));
  }
  renderList() {
    const query = this.filter.value.toLocaleLowerCase(),
      all = this.certificates(),
      values = all.filter(
        (record) =>
          !query ||
          `${record.name} ${record.description} ${JSON.stringify(record.data)}`.toLocaleLowerCase().includes(query),
      );
    this.body.replaceChildren(...values.sort((a, b) => a.name.localeCompare(b.name)).map((record) => this.row(record)));
    this.summary.textContent = this.selectedStore ? `${all.length} 个证书` : "";
    this.listEmpty.hidden = values.length !== 0;
    if (!this.selectedStore) this.listEmpty.textContent = "请选择证书存储";
    else if (this.selectedStore.flags & 1)
      this.listEmpty.textContent = `无法打开证书存储，Win32: ${hex32(this.selectedStore.state)}`;
    else this.listEmpty.textContent = all.length ? "没有匹配的证书" : "此存储中没有证书";
  }
  row(record) {
    const row = document.createElement("tr"),
      purpose = record.data.allPurposes ? "所有" : record.data.enhancedKeyUsages.join(", "),
      values = [
        record.name,
        record.description,
        fileTime(record.value),
        purpose,
        record.data.friendlyName,
        certificateStatus(record.state),
      ];
    row.record = record;
    for (const value of values) {
      const cell = row.insertCell();
      cell.textContent = value || "—";
      cell.title = cell.textContent;
    }
    row.ondblclick = () => this.properties(record);
    return row;
  }
  openMenu(event, record) {
    if (!this.selectedStore) return;
    const actions = record
      ? [
          ["打开", () => this.properties(record)],
          ["导出 DER…", () => this.exportCertificate(record, false)],
          ["导出 PEM…", () => this.exportCertificate(record, true)],
          ["删除", () => this.deleteCertificate(record), true],
        ]
      : [["导入证书…", () => this.installer.chooseFile(this.selectedStore)]];
    this.menu.replaceChildren(
      ...actions.map(([title, action, danger]) => {
        const button = document.createElement("button");
        button.textContent = title;
        button.classList.toggle("danger", danger === true);
        button.disabled = !!(this.selectedStore.flags & 1);
        button.onclick = () => {
          this.menu.hidden = true;
          action();
        };
        return button;
      }),
    );
    placeMenu(this.menu, event);
  }
  async deleteCertificate(record) {
    if (!confirm(`确定从“${certificateStoreName(this.selectedStore.name)}”删除证书“${record.name}”？`)) return;
    try {
      await this.call("/api/certificates/delete", { identity: record.identity });
      this.notify("证书已删除");
      await this.load(true);
    } catch (error) {
      this.notify(error);
    }
  }
  async query(record) {
    return this.call("/api/certificates/details", { identity: record.identity });
  }
  async exportCertificate(record, pem) {
    try {
      const value = await this.query(record),
        { data: bytes } = await postBinary("/api/certificates/data", { identity: record.identity }),
        encoded = bytesToBase64(bytes),
        pemBody = encoded.match(/.{1,64}/g).join("\r\n"),
        data = pem ? `-----BEGIN CERTIFICATE-----\r\n${pemBody}\r\n-----END CERTIFICATE-----\r\n` : bytes,
        blob = new Blob([data], { type: pem ? "application/x-pem-file" : "application/pkix-cert" }),
        link = document.createElement("a");
      link.href = URL.createObjectURL(blob);
      link.download = `${value.thumbprint}.${pem ? "pem" : "cer"}`;
      link.click();
      setTimeout(() => URL.revokeObjectURL(link.href), 0);
    } catch (error) {
      this.notify(error);
    }
  }
  async properties(record) {
    this.dialog.querySelector("[data-role=title]").textContent = `证书 - ${record.name}`;
    this.dialog.querySelector("[data-role=general]").replaceChildren();
    this.dialog.querySelector("[data-role=details]").textContent = "正在读取证书…";
    this.dialog.querySelector("[data-role=chain]").replaceChildren();
    this.dialog.querySelector("[data-role=chain-status]").textContent = "";
    this.selectPropertyTab("general");
    this.dialog.showModal();
    try {
      const value = await this.query(record),
        status = certificateStatus(record.state);
      this.dialog.querySelector("[data-role=certificate-status]").textContent =
        status === "有效" ? "此证书正常。" : `证书状态：${status}`;
      this.dialog.querySelector("[data-role=certificate-purpose]").textContent =
        (value.purposes || []).join("、") || "所有颁发策略";
      this.dialog.querySelector("[data-role=private-key]").hidden = !(value.flags & 1);
      this.dialog.querySelector("[data-role=general]").replaceChildren(
        ...details([
          ["颁发给", value.subjectName],
          ["颁发者", value.issuerName],
          ["有效期自", new Date(value.notBefore).toLocaleString()],
          ["有效期至", new Date(value.notAfter).toLocaleString()],
          ["指纹", value.thumbprint],
        ]),
      );
      this.renderCertificateDetails(value);
      this.renderCertificateChain(value);
    } catch (error) {
      this.dialog.close();
      this.notify(error);
    }
  }
  renderCertificateDetails(value) {
    const fields = [
        ["版本", `V${value.version}`],
        ["序列号", value.serialNumber],
        ["签名算法", value.signatureAlgorithm],
        ["颁发者", value.issuer],
        ["有效期自", new Date(value.notBefore).toLocaleString()],
        ["有效期至", new Date(value.notAfter).toLocaleString()],
        ["使用者", value.subject],
        ["公钥算法", value.publicKeyAlgorithm],
        ["指纹", value.thumbprint],
        ...(value.extensions || []).map((extension) => [
          extension.name || extension.oid,
          `${extension.critical ? t("certificate.criticalPrefix") : ""}${extension.value}`,
        ]),
      ],
      body = this.dialog.querySelector("[data-role=details]"),
      preview = this.dialog.querySelector("[data-role=detail-value]");
    body.replaceChildren(
      ...fields.map(([name, value], index) => {
        const item = document.createElement("button"),
          strong = document.createElement("strong"),
          span = document.createElement("span");
        item.type = "button";
        strong.textContent = name;
        span.textContent = value || "—";
        item.title = span.textContent;
        item.onclick = () => {
          for (const child of body.children) child.classList.toggle("selected", child === item);
          preview.textContent = span.textContent;
        };
        item.append(strong, span);
        if (index === 0) queueMicrotask(() => item.click());
        return item;
      }),
    );
  }
  renderCertificateChain(value) {
    const chain = this.dialog.querySelector("[data-role=chain]");
    chain.replaceChildren(
      ...value.chain.map((item, index) => {
        const row = document.createElement("div");
        row.textContent = `${"　".repeat(index)}${index ? "└─ " : ""}${item.name}`;
        row.title = item.description;
        row.classList.toggle("error", item.state !== 0);
        return row;
      }),
    );
    const errors = value.chain.filter((item) => item.state !== 0).map((item) => `${item.name}: ${hex32(item.state)}`);
    if (value.chainError) errors.unshift(`无法构建证书路径，Win32: ${hex32(value.chainError)}`);
    this.dialog.querySelector("[data-role=chain-status]").textContent = errors.join("\n") || "证书路径验证正常。";
  }
  selectPropertyTab(name) {
    for (const tab of this.dialog.querySelectorAll("[data-tab]"))
      tab.classList.toggle("active", tab.dataset.tab === name);
    for (const page of this.dialog.querySelectorAll("[data-page]")) page.hidden = page.dataset.page !== name;
  }
}

function taskState(value) {
  return ["未知", "禁用", "排队", "就绪", "运行"][value] || value;
}
function wlanState(value) {
  return (
    ["未就绪", "已连接", "已形成临时网络", "正在断开", "已断开", "正在关联", "正在发现", "正在验证"][value] || value
  );
}
function firewallDirection(flags) {
  return (flags & 3) === 1 ? "入站" : "出站";
}
function firewallRuleName(rule) {
  return rule.identity.slice(rule.identity.indexOf("\n") + 1);
}
function firewallProfiles(flags) {
  const values = [];
  if (flags & 32) values.push("域");
  if (flags & 8) values.push("专用");
  if (flags & 16) values.push("公用");
  return values.join("、") || "全部";
}
function firewallProtocol(value) {
  return { 1: "ICMPv4", 6: "TCP", 17: "UDP", 58: "ICMPv6", 256: "任何" }[value] || value;
}
function formatDuration(seconds) {
  if (seconds < 60) return t("common.duration.seconds", { value: seconds });
  if (seconds < 3600) return t("common.duration.minutes", { value: Math.round(seconds / 60) });
  return t("common.duration.hours", { value: Math.round(seconds / 360) / 10 });
}
function formatBytes(value) {
  let number = Number(value);
  if (!number) return "0 B";
  const units = ["B", "KB", "MB", "GB", "TB"];
  let index = 0;
  while (number >= 1024 && index < units.length - 1) {
    number /= 1024;
    index++;
  }
  return `${number.toFixed(index ? 1 : 0)} ${units[index]}`;
}
function certificateStatus(value) {
  return value === 0 ? "有效" : value === 1 ? "尚未生效" : value === 2 ? "已过期" : `状态 ${value}`;
}
function hex32(value) {
  return `0x${Number(value).toString(16).padStart(8, "0").toUpperCase()}`;
}
function bytesToBase64(bytes) {
  let value = "";
  for (let offset = 0; offset < bytes.length; offset += 0x8000)
    value += String.fromCharCode(...bytes.subarray(offset, offset + 0x8000));
  return btoa(value);
}
function eventChannelParts(channel) {
  const parts = channel.split("/"),
    provider = parts.shift();
  if (provider.startsWith("Microsoft-Windows-")) return ["Microsoft", "Windows", provider.slice(18), ...parts];
  if (provider.startsWith("Microsoft-")) return ["Microsoft", provider.slice(10), ...parts];
  return [provider, ...parts];
}
