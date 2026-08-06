import { t } from "./i18n.mjs";

const PROFILE = 48,
  CAPABILITY = 49,
  BINARY = 50,
  CREATE = 1,
  DELETE = 2,
  CONFIGURE = 23,
  LOOPBACK = 1,
  PACKAGED = 2;

const CAPABILITIES = [
  ["internetClient", "Internet 客户端"],
  ["internetClientServer", "Internet 客户端和服务器"],
  ["privateNetworkClientServer", "专用网络客户端和服务器"],
  ["documentsLibrary", "文档库"],
  ["picturesLibrary", "图片库"],
  ["videosLibrary", "视频库"],
  ["musicLibrary", "音乐库"],
  ["removableStorage", "可移动存储"],
  ["sharedUserCertificates", "共享用户证书"],
  ["enterpriseAuthentication", "企业身份验证"],
  ["userAccountInformation", "用户账户信息"],
  ["broadFileSystemAccess", "广泛文件系统访问"],
  ["appointments", "约会"],
  ["contacts", "联系人"],
];

export class SandboxManager {
  constructor(host, { call, notify }) {
    this.host = host;
    this.call = call;
    this.notify = notify;
    this.records = [];
    this.profiles = [];
    this.connected = false;
    this.loaded = false;
    host.innerHTML = /* HTML */ `<div>
        <nav class="property-tabs sandbox-tabs">
          <button data-tab="appcontainer" class="active">AppContainer</button><button data-tab="wsb">WSB</button>
        </nav>
        <div class="manager-toolbar" data-sandbox-panel="appcontainer">
          <strong>Profile</strong><button data-action="create">＋</button
          ><button data-action="refresh">刷新</button
          ><input data-role="filter" placeholder="筛选 Profile 或规则" /><span class="spacer"></span
          ><span data-role="summary"></span>
        </div>
      </div>
      <div class="manager-table sandbox-table" data-sandbox-panel="appcontainer">
        <table>
          <thead>
            <tr>
              <th>显示名称</th>
              <th>Profile 名称</th>
              <th>SID</th>
              <th>能力</th>
              <th>二进制</th>
              <th>回环</th>
              <th>来源</th>
            </tr>
          </thead>
          <tbody></tbody>
        </table>
        <div class="manager-empty">进入页面后读取 AppContainer Profile</div>
      </div>
      <section class="system-information" data-sandbox-panel="wsb" hidden>
        <form data-role="wsb" class="sandbox-dialog">
          <h2>Windows Sandbox</h2>
          <p class="property-note">
            每次启动均为一次性环境；默认不共享网络、剪贴板、设备或宿主目录。
          </p>
          <fieldset>
            <legend>资源与隔离</legend>
            <label>内存 (MB)<input data-field="memory" type="number" min="1024" placeholder="系统默认" /></label>
            <label class="property-choice"><input data-field="vgpu" type="checkbox" />虚拟 GPU</label>
            <label class="property-choice"><input data-field="networking" type="checkbox" />网络</label>
            <label class="property-choice"><input data-field="clipboard" type="checkbox" />剪贴板重定向</label>
            <label class="property-choice"><input data-field="audio" type="checkbox" />音频输入</label>
            <label class="property-choice"><input data-field="video" type="checkbox" />视频输入</label>
            <label class="property-choice"><input data-field="printers" type="checkbox" />打印机重定向</label>
            <label class="property-choice"><input data-field="protected" type="checkbox" checked />受保护客户端</label>
          </fieldset>
          <fieldset>
            <legend>映射文件夹</legend>
            <div data-role="wsb-folders"></div>
            <button type="button" data-action="wsb-add-folder">＋ 添加文件夹</button>
          </fieldset>
          <label>登录命令<textarea data-field="command" rows="3" spellcheck="false"></textarea></label>
          <div class="dialog-actions"><button type="submit">启动 Windows Sandbox</button></div>
        </form>
      </section>
      <div class="context-menu administration-menu" data-role="menu" hidden>
        <button data-action="properties">属性</button><button data-action="delete" class="danger">删除</button>
      </div>
      <dialog data-role="create" class="sandbox-dialog">
        <form>
          <h2>新建 AppContainer Profile</h2>
          <label
            >Profile 名称<input
              data-field="name"
              maxlength="64"
              pattern="[-_. A-Za-z0-9]+"
              required
              autocomplete="off" /></label
          ><label>显示名称<input data-field="displayName" maxlength="512" required /></label
          ><label>描述<textarea data-field="description" maxlength="2048" rows="3"></textarea></label>
          <fieldset>
            <legend>能力规则</legend>
            <div class="sandbox-capabilities" data-role="capabilities"></div>
            <label
              >其它能力名称（每行一个）<textarea data-field="customCapabilities" rows="4" spellcheck="false"></textarea>
            </label>
          </fieldset>
          <label class="property-choice"><input data-field="loopback" type="checkbox" />允许回环通信</label>
          <div class="dialog-actions">
            <button type="submit">创建</button><button type="button" data-action="cancel-create">取消</button>
          </div>
        </form>
      </dialog>
      <dialog data-role="properties" class="sandbox-dialog">
        <form>
          <h2 data-role="properties-title">AppContainer Profile</h2>
          <div class="sandbox-property-grid">
            <label>Profile 名称<input data-field="profileName" readonly /></label
            ><label>显示名称<input data-field="profileDisplayName" readonly /></label
            ><label>SID<input data-field="sid" readonly /></label
            ><label>用户 SID<input data-field="userSid" readonly /></label
            ><label>工作目录<input data-field="workingDirectory" readonly /></label
            ><label>包全名<input data-field="packageFullName" readonly /></label
            ><label>描述<textarea data-field="profileDescription" rows="3" readonly></textarea></label>
          </div>
          <fieldset>
            <legend>能力规则</legend>
            <div data-role="profile-capabilities" class="sandbox-rule-list"></div>
          </fieldset>
          <fieldset>
            <legend>关联二进制</legend>
            <div data-role="profile-binaries" class="sandbox-rule-list"></div>
          </fieldset>
          <label class="property-choice"><input data-field="profileLoopback" type="checkbox" />允许回环通信</label>
          <div class="dialog-actions">
            <button type="submit">确定</button><button type="button" data-action="cancel-properties">取消</button>
          </div>
        </form>
      </dialog>`;
    this.body = host.querySelector("tbody");
    this.empty = host.querySelector(".manager-empty");
    this.menu = host.querySelector("[data-role=menu]");
    this.createDialog = host.querySelector("[data-role=create]");
    this.propertiesDialog = host.querySelector("[data-role=properties]");
    this.summary = host.querySelector("[data-role=summary]");
    this.filter = host.querySelector("[data-role=filter]");
    this.wsbForm = host.querySelector("[data-role=wsb]");
    for (const tab of host.querySelectorAll("[data-tab]")) tab.onclick = () => this.showTab(tab.dataset.tab);
    host.querySelector("[data-action=wsb-add-folder]").onclick = () => this.addWsbFolder();
    this.wsbForm.onsubmit = (event) => {
      event.preventDefault();
      this.startWsb();
    };
    this.filter.oninput = () => this.renderProfiles();
    host.querySelector("[data-role=capabilities]").replaceChildren(
      ...CAPABILITIES.map(([value, title]) => {
        const label = document.createElement("label"),
          input = document.createElement("input");
        label.className = "property-choice";
        input.type = "checkbox";
        input.value = value;
        label.append(input, title);
        label.title = value;
        return label;
      }),
    );
    this.action("create").onclick = () => this.showCreate();
    this.action("refresh").onclick = () => this.load();
    this.action("properties", this.menu).onclick = () => {
      this.hideMenu();
      this.showProperties();
    };
    this.action("delete", this.menu).onclick = () => {
      this.hideMenu();
      this.delete();
    };
    this.action("cancel-create", this.createDialog).onclick = () => this.createDialog.close();
    this.action("cancel-properties", this.propertiesDialog).onclick = () => this.propertiesDialog.close();
    this.createDialog.querySelector("form").onsubmit = (event) => {
      event.preventDefault();
      this.create();
    };
    this.propertiesDialog.querySelector("form").onsubmit = (event) => {
      event.preventDefault();
      this.configure();
    };
    host.addEventListener("pointerdown", (event) => {
      if (!this.menu.contains(event.target)) this.hideMenu();
    });
    addEventListener("blur", () => this.hideMenu());
  }
  action(name, root = this.host) {
    return root.querySelector(`[data-action="${name}"]`);
  }
  field(name, root) {
    return (root || this.host).querySelector(`[data-field="${name}"]`);
  }
  showTab(name) {
    for (const tab of this.host.querySelectorAll("[data-tab]"))
      tab.classList.toggle("active", tab.dataset.tab === name);
    for (const panel of this.host.querySelectorAll("[data-sandbox-panel]"))
      panel.hidden = panel.dataset.sandboxPanel !== name;
  }
  addWsbFolder() {
    const row = document.createElement("div");
    row.className = "sandbox-property-grid";
    row.innerHTML = /* HTML */ `<label>宿主文件夹<input data-field="host" required /></label
      ><label>沙箱文件夹<input data-field="sandbox" placeholder="自动" /></label
      ><label class="property-choice"><input data-field="read-only" type="checkbox" checked />只读</label
      ><button type="button" class="danger">删除</button>`;
    row.querySelector("button").onclick = () => row.remove();
    this.host.querySelector("[data-role=wsb-folders]").append(row);
  }
  async startWsb() {
    const field = (name) => this.field(name, this.wsbForm),
      submit = this.wsbForm.querySelector("[type=submit]"),
      mappedFolders = [...this.host.querySelectorAll("[data-role=wsb-folders] > div")].map((row) => ({
        hostFolder: row.querySelector("[data-field=host]").value.trim(),
        sandboxFolder: row.querySelector("[data-field=sandbox]").value.trim() || null,
        readOnly: row.querySelector("[data-field=read-only]").checked,
      }));
    submit.disabled = true;
    try {
      const job = await this.call("/api/sandbox/wsb/start", {
        vgpu: field("vgpu").checked,
        networking: field("networking").checked,
        clipboard: field("clipboard").checked,
        audioInput: field("audio").checked,
        videoInput: field("video").checked,
        protectedClient: field("protected").checked,
        printers: field("printers").checked,
        memoryMb: Number(field("memory").value) || 0,
        logonCommand: field("command").value.trim() || null,
        mappedFolders,
      });
      this.notify(job.processId ? t("sandbox.wsbStarted", { pid: job.processId }) : t("sandbox.wsbSubmitted"));
    } catch (error) {
      this.notify(error);
    } finally {
      submit.disabled = false;
    }
  }
  activate(connected) {
    this.connected = connected;
    this.action("create").disabled = this.action("refresh").disabled = !connected;
    this.wsbForm.querySelector("[type=submit]").disabled = !connected;
    if (!connected) {
      this.disconnect();
      return;
    }
    if (!this.loaded) this.load();
  }
  disconnect() {
    this.connected = false;
    this.loaded = false;
    this.records = [];
    this.profiles = [];
    this.selected = null;
    this.body.replaceChildren();
    this.empty.hidden = false;
    this.empty.textContent = "Client 未连接";
    this.summary.textContent = "";
    this.hideMenu();
    if (this.createDialog.open) this.createDialog.close();
    if (this.propertiesDialog.open) this.propertiesDialog.close();
  }
  async load() {
    if (!this.connected) return;
    this.loaded = false;
    this.selected = null;
    this.body.replaceChildren();
    this.empty.hidden = false;
    this.empty.textContent = "正在读取 AppContainer Profile…";
    this.summary.textContent = "";
    try {
      this.records = await this.call("/api/app-containers");
      const capabilities = new Map(),
        binaries = new Map();
      for (const record of this.records) {
        const target = record.kind === CAPABILITY ? capabilities : record.kind === BINARY ? binaries : null;
        if (target) {
          if (!target.has(record.identity)) target.set(record.identity, []);
          target.get(record.identity).push(record);
        }
      }
      this.profiles = this.records
        .filter((record) => record.kind === PROFILE)
        .map((record) => ({
          ...record,
          details: record.detail.split("\n"),
          capabilities: capabilities.get(record.identity) || [],
          binaries: binaries.get(record.identity) || [],
        }))
        .sort((a, b) => (a.name || a.identity).localeCompare(b.name || b.identity));
      this.loaded = true;
      this.renderProfiles();
    } catch (error) {
      this.empty.hidden = false;
      this.empty.textContent = error.message;
      this.notify(error);
    }
  }
  renderProfiles() {
    if (!this.loaded) return;
    const query = this.filter.value.trim().toLocaleLowerCase(),
      profiles = this.profiles.filter(
        (profile) =>
          !query ||
          [
            profile.name,
            profile.identity,
            profile.description,
            ...profile.details,
            ...profile.capabilities.flatMap((value) => [value.name, value.detail]),
            ...profile.binaries.map((value) => value.name),
          ].some((value) =>
            String(value || "")
              .toLocaleLowerCase()
              .includes(query),
          ),
      );
    this.selected = null;
    this.body.replaceChildren(...profiles.map((profile) => this.row(profile)));
    this.empty.hidden = profiles.length !== 0;
    this.empty.textContent = this.profiles.length ? "没有匹配的 AppContainer Profile" : "没有 AppContainer Profile";
    this.summary.textContent = `${profiles.length} / ${this.profiles.length} 个 Profile`;
  }
  row(profile) {
    const row = document.createElement("tr"),
      values = [
        profile.name || "(无显示名称)",
        profile.identity,
        profile.details[0] || "—",
        profile.state,
        profile.value,
        profile.flags & LOOPBACK ? "允许" : "不允许",
        profile.flags & PACKAGED ? "应用包" : "独立 Profile",
      ];
    for (const value of values) {
      const cell = row.insertCell();
      cell.textContent = value;
      cell.title = String(value);
    }
    row.onclick = () => this.select(row, profile);
    row.ondblclick = () => this.showProperties();
    row.oncontextmenu = (event) => this.context(event, row, profile);
    return row;
  }
  select(row, profile) {
    this.body.querySelector(".selected")?.classList.remove("selected");
    row.classList.add("selected");
    this.selected = profile;
  }
  context(event, row, profile) {
    event.preventDefault();
    this.select(row, profile);
    this.action("delete", this.menu).disabled = !!(profile.flags & PACKAGED);
    this.menu.hidden = false;
    const rect = this.menu.getBoundingClientRect();
    this.menu.style.left = `${Math.max(6, Math.min(event.clientX, innerWidth - rect.width - 6))}px`;
    this.menu.style.top = `${Math.max(6, Math.min(event.clientY, innerHeight - rect.height - 6))}px`;
  }
  hideMenu() {
    this.menu.hidden = true;
  }
  showCreate() {
    const form = this.createDialog.querySelector("form");
    form.reset();
    this.createDialog.showModal();
    this.field("name", this.createDialog).focus();
  }
  async create() {
    const field = (name) => this.field(name, this.createDialog),
      name = field("name").value.trim(),
      displayName = field("displayName").value.trim(),
      description = field("description").value,
      custom = field("customCapabilities")
        .value.split(/\r?\n/)
        .map((value) => value.trim())
        .filter(Boolean),
      capabilities = [...this.createDialog.querySelectorAll("[data-role=capabilities] input:checked")].map(
        (input) => input.value,
      );
    for (const value of custom)
      if (!capabilities.some((item) => item.toLocaleLowerCase() === value.toLocaleLowerCase()))
        capabilities.push(value);
    const submit = this.createDialog.querySelector("[type=submit]");
    submit.disabled = true;
    try {
      await this.call("/api/app-containers/control", {
        action: CREATE,
        identity: name,
        displayName,
        description,
        capabilities,
      });
      if (field("loopback").checked) {
        await this.load();
        const profile = this.profiles.find((value) => value.identity.toLocaleLowerCase() === name.toLocaleLowerCase());
        if (!profile) throw new Error("未找到新建的 AppContainer Profile");
        await this.call("/api/app-containers/control", {
          action: CONFIGURE,
          identity: profile.details[0],
          loopback: true,
        });
      }
      this.createDialog.close();
      await this.load();
    } catch (error) {
      this.notify(error);
      await this.load();
    } finally {
      submit.disabled = false;
    }
  }
  showProperties() {
    const profile = this.selected;
    if (!profile) return;
    const field = (name) => this.field(name, this.propertiesDialog),
      details = profile.details;
    this.propertiesDialog.querySelector("[data-role=properties-title]").textContent = profile.name || profile.identity;
    field("profileName").value = profile.identity;
    field("profileDisplayName").value = profile.name;
    field("sid").value = details[0] || "";
    field("userSid").value = details[1] || "";
    field("workingDirectory").value = details[2] || "";
    field("packageFullName").value = details[3] || "";
    field("profileDescription").value = profile.description;
    field("profileLoopback").checked = !!(profile.flags & LOOPBACK);
    this.propertiesDialog.querySelector("[data-role=profile-capabilities]").replaceChildren(
      ...this.rules(
        profile.capabilities.map((value) => ({
          name: value.name || value.detail,
          detail: value.name ? value.detail : "",
        })),
      ),
    );
    this.propertiesDialog
      .querySelector("[data-role=profile-binaries]")
      .replaceChildren(...this.rules(profile.binaries.map((value) => ({ name: value.name }))));
    this.propertiesDialog.showModal();
  }
  rules(values) {
    if (!values.length)
      return [Object.assign(document.createElement("span"), { textContent: "无", className: "muted" })];
    return values.map((value) => {
      const item = document.createElement("div"),
        name = document.createElement("span"),
        detail = document.createElement("code");
      name.textContent = value.name;
      detail.textContent = value.detail || "";
      item.append(name, detail);
      return item;
    });
  }
  async configure() {
    const profile = this.selected;
    if (!profile) return;
    const loopback = this.field("profileLoopback", this.propertiesDialog).checked;
    if (loopback == !!(profile.flags & LOOPBACK)) {
      this.propertiesDialog.close();
      return;
    }
    const submit = this.propertiesDialog.querySelector("[type=submit]");
    submit.disabled = true;
    try {
      await this.call("/api/app-containers/control", { action: CONFIGURE, identity: profile.details[0], loopback });
      this.propertiesDialog.close();
      await this.load();
    } catch (error) {
      this.notify(error);
    } finally {
      submit.disabled = false;
    }
  }
  async delete() {
    const profile = this.selected;
    if (!profile || profile.flags & PACKAGED || !confirm(`确定删除 AppContainer Profile“${profile.identity}”吗？`))
      return;
    try {
      await this.call("/api/app-containers/control", { action: DELETE, identity: profile.identity });
      await this.load();
    } catch (error) {
      this.notify(error);
    }
  }
}
