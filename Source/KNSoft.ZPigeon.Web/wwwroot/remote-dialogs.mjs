const DIRECTORY = 0x10;
const WELL_KNOWN = new Map([
  ["SY", "SYSTEM"],
  ["BA", "BUILTIN\\Administrators"],
  ["BU", "BUILTIN\\Users"],
  ["WD", "Everyone"],
  ["AU", "Authenticated Users"],
  ["LS", "LOCAL SERVICE"],
  ["NS", "NETWORK SERVICE"],
  ["CO", "CREATOR OWNER"],
  ["CG", "CREATOR GROUP"],
  ["OW", "OWNER RIGHTS"],
  ["RC", "RESTRICTED"],
  ["AC", "APPLICATION PACKAGE AUTHORITY\\ALL APPLICATION PACKAGES"],
]);
const FILE_RIGHTS = [
  ["0x001F01FF", "完全控制"],
  ["0x001301BF", "修改"],
  ["0x001200A9", "读取和执行"],
  ["0x00120089", "读取"],
  ["0x00120116", "写入"],
];
const RIGHTS = {
  file: FILE_RIGHTS,
  registry: [
    ["KA", "完全控制"],
    ["KR", "读取"],
    ["KW", "写入"],
  ],
  share: FILE_RIGHTS,
};

export class RemoteFilePicker {
  constructor({ call, notify }) {
    this.call = call;
    this.notify = notify;
    this.dialog = document.createElement("dialog");
    this.dialog.className = "remote-file-picker";
    this.dialog.innerHTML = /* HTML */ `<form>
      <h2 data-role="title"></h2>
      <div class="picker-toolbar">
        <button type="button" data-action="up">向上</button><input data-role="path" spellcheck="false" /><button
          type="button"
          data-action="go"
        >
          转到
        </button>
      </div>
      <div class="picker-filter">
        <input data-role="query" placeholder="搜索此文件夹（支持 * 和 ?）" spellcheck="false" />
        <button type="button" data-action="search">搜索</button><button type="button" data-action="clear-search">
          清除
        </button>
      </div>
      <div class="picker-groups" data-role="groups"></div>
      <div class="picker-list">
        <table>
          <thead>
            <tr>
              <th>名称</th>
              <th>类型</th>
              <th>修改日期</th>
            </tr>
          </thead>
          <tbody></tbody>
        </table>
        <div class="manager-empty" data-role="empty"></div>
      </div>
      <div class="picker-footer">
        <p class="status" data-role="selection"></p>
        <div class="picker-pages">
          <button type="button" data-action="previous">上一页</button><span data-role="page"></span
          ><button type="button" data-action="next">下一页</button>
        </div>
      </div>
      <div class="dialog-actions">
        <button type="button" data-action="cancel">取消</button><button type="submit" data-action="select">选择</button>
      </div>
    </form>`;
    document.body.append(this.dialog);
    this.pathInput = this.dialog.querySelector("[data-role=path]");
    this.body = this.dialog.querySelector("tbody");
    this.empty = this.dialog.querySelector("[data-role=empty]");
    this.selection = this.dialog.querySelector("[data-role=selection]");
    this.queryInput = this.dialog.querySelector("[data-role=query]");
    this.pageText = this.dialog.querySelector("[data-role=page]");
    this.previous = this.dialog.querySelector("[data-action=previous]");
    this.next = this.dialog.querySelector("[data-action=next]");
    this.groups = this.dialog.querySelector("[data-role=groups]");
    this.groups.replaceChildren(
      ...[..."ABCDEFGHIJKLMNOPQRSTUVWXYZ", "#"].map((group) => {
        const button = document.createElement("button");
        button.type = "button";
        button.textContent = group;
        button.dataset.group = group;
        button.onclick = () => {
          this.group = this.group === group ? "" : group;
          this.startQuery();
        };
        return button;
      }),
    );
    this.dialog.querySelector("[data-action=up]").onclick = () => {
      const path = parentPath(this.path);
      if (path !== null) this.load(path);
    };
    this.dialog.querySelector("[data-action=go]").onclick = () => this.load(this.pathInput.value);
    this.dialog.querySelector("[data-action=search]").onclick = () => {
      this.query = this.queryInput.value.trim();
      this.startQuery();
    };
    this.dialog.querySelector("[data-action=clear-search]").onclick = () => {
      this.query = "";
      this.queryInput.value = "";
      this.startQuery();
    };
    this.previous.onclick = () => this.showPage(this.pageIndex - 1);
    this.next.onclick = () => this.showNextPage();
    this.dialog.querySelector("[data-action=cancel]").onclick = () => this.close(null);
    this.pathInput.onkeydown = (event) => {
      if (event.key === "Enter") {
        event.preventDefault();
        this.load(this.pathInput.value);
      }
    };
    this.queryInput.onkeydown = (event) => {
      if (event.key === "Enter") {
        event.preventDefault();
        this.query = this.queryInput.value.trim();
        this.startQuery();
      }
    };
    this.dialog.querySelector("form").onsubmit = (event) => {
      event.preventDefault();
      this.choose();
    };
    this.dialog.oncancel = (event) => {
      event.preventDefault();
      this.close(null);
    };
  }
  open({ mode = "file", initialPath = "" } = {}) {
    if (this.resolve) this.close(null);
    this.mode = mode;
    this.selected = null;
    this.dialog.querySelector("[data-role=title]").textContent = mode === "folder" ? "选择文件夹" : "选择文件";
    this.dialog.querySelector("[data-action=select]").textContent = mode === "folder" ? "选择此文件夹" : "选择文件";
    this.dialog.showModal();
    this.load(initialPath);
    return new Promise((resolve) => (this.resolve = resolve));
  }
  close(value) {
    this.request = (this.request || 0) + 1;
    this.releaseEnumeration();
    this.dialog.close();
    const resolve = this.resolve;
    this.resolve = null;
    resolve?.(value);
  }
  async load(value) {
    this.path = normalizePath(value);
    this.pathInput.value = this.path;
    this.query = "";
    this.queryInput.value = "";
    this.group = "";
    await this.startQuery();
  }
  async startQuery() {
    const request = (this.request || 0) + 1;
    this.request = request;
    await this.releaseEnumeration();
    if (request !== this.request) return;
    this.pages = [];
    this.pageIndex = 0;
    this.nextEnumerationId = 0;
    this.selected = null;
    this.selection.textContent = this.mode === "folder" && this.path ? `将选择：${this.path}` : "";
    this.body.replaceChildren();
    this.empty.hidden = false;
    this.empty.textContent = "正在读取…";
    this.updatePager();
    this.updateGroups();
    try {
      const page = await this.readPage(0);
      if (request !== this.request) {
        this.releaseEnumeration(page.enumerationId);
        return;
      }
      this.pages.push(page.records);
      this.nextEnumerationId = page.enumerationId;
      this.showPage(0);
    } catch (error) {
      if (request !== this.request) return;
      this.empty.textContent = error.message;
      this.notify(error);
    }
  }
  async showNextPage() {
    if (this.pageIndex + 1 < this.pages.length) {
      this.showPage(this.pageIndex + 1);
      return;
    }
    if (!this.nextEnumerationId) return;
    const request = this.request,
      enumerationId = this.nextEnumerationId;
    this.next.disabled = true;
    try {
      const page = await this.readPage(enumerationId);
      if (request !== this.request) {
        this.releaseEnumeration(page.enumerationId);
        return;
      }
      this.pages.push(page.records);
      this.nextEnumerationId = page.enumerationId;
      this.showPage(this.pages.length - 1);
    } catch (error) {
      if (request === this.request) this.notify(error);
    } finally {
      if (request === this.request) this.updatePager();
    }
  }
  async readPage(enumerationId) {
    const page = await this.call("/api/files/picker", {
      path: enumerationId ? null : this.path,
      query: enumerationId ? null : this.query,
      group: enumerationId ? null : this.group,
      enumerationId,
    });
    page.enumerationId = Number(page.enumerationId) || 0;
    return page;
  }
  async releaseEnumeration(enumerationId = this.nextEnumerationId) {
    if (!enumerationId) return;
    if (enumerationId === this.nextEnumerationId) this.nextEnumerationId = 0;
    await this.call("/api/files/picker/close", { enumerationId }).catch(() => {});
  }
  showPage(index) {
    if (index < 0 || index >= this.pages.length) return;
    this.pageIndex = index;
    this.selected = null;
    this.selection.textContent = this.mode === "folder" && this.path ? `将选择：${this.path}` : "";
    this.render(this.pages[index]);
    this.updatePager();
  }
  updatePager() {
    this.previous.disabled = !this.pages?.length || this.pageIndex === 0;
    this.next.disabled = !this.pages?.length ||
      (this.pageIndex === this.pages.length - 1 && !this.nextEnumerationId);
    this.pageText.textContent = this.pages?.length ? `第 ${this.pageIndex + 1} 页` : "";
  }
  updateGroups() {
    for (const button of this.groups.children) button.classList.toggle("active", button.dataset.group === this.group);
  }
  render(records) {
    this.body.replaceChildren(
      ...records.map((item) => {
        const row = document.createElement("tr"),
          directory = !!(item.attributes & DIRECTORY),
          path = this.path ? joinPath(this.path, item.name) : item.name;
        row.innerHTML = "<td></td><td></td><td></td>";
        row.children[0].textContent = item.name;
        row.children[1].textContent = directory ? "文件夹" : "文件";
        row.children[2].textContent = item.lastWriteTime ? new Date(item.lastWriteTime).toLocaleString() : "";
        row.onclick = () => {
          this.body.querySelector(".selected")?.classList.remove("selected");
          row.classList.add("selected");
          this.selected = { path, directory };
          this.selection.textContent = directory ? `文件夹：${path}` : `文件：${path}`;
        };
        row.ondblclick = () => (directory ? this.load(path) : this.mode === "file" && this.close(path));
        return row;
      }),
    );
    this.empty.hidden = records.length !== 0;
    this.empty.textContent = "此文件夹为空";
  }
  choose() {
    if (this.mode === "folder") {
      this.close(this.selected?.directory ? this.selected.path : this.path || null);
      return;
    }
    if (this.selected && !this.selected.directory) this.close(this.selected.path);
  }
}

export class AclEditor {
  constructor({ call, notify }) {
    this.call = call;
    this.notify = notify;
    this.dialog = document.createElement("dialog");
    this.dialog.className = "acl-editor";
    this.dialog.innerHTML = /* HTML */ `<form>
      <h2 data-role="title">权限</h2>
      <dl class="details-grid acl-owner">
        <dt>所有者</dt>
        <dd data-role="owner"></dd>
        <dt>主要组</dt>
        <dd data-role="group"></dd>
      </dl>
      <div class="manager-table acl-list">
        <table>
          <thead>
            <tr>
              <th>类型</th>
              <th>主体</th>
              <th>权限</th>
              <th>应用于</th>
            </tr>
          </thead>
          <tbody></tbody>
        </table>
        <div class="manager-empty" data-role="empty"></div>
      </div>
      <div class="acl-actions">
        <button type="button" data-action="add">添加</button
        ><button type="button" data-action="edit" disabled>编辑</button
        ><button type="button" data-action="remove" disabled>删除</button
        ><span class="status" data-role="status"></span>
      </div>
      <label class="property-choice acl-inheritance"
        ><input type="checkbox" data-role="inheritance" />包括可从父对象继承的权限</label
      >
      <details>
        <summary>高级 DACL SDDL</summary>
        <textarea data-role="sddl" spellcheck="false"></textarea>
      </details>
      <div class="dialog-actions">
        <button type="button" data-action="cancel">取消</button><button type="submit">确定</button>
      </div>
    </form>`;
    document.body.append(this.dialog);
    this.entry = document.createElement("dialog");
    this.entry.className = "acl-entry-editor";
    this.entry.innerHTML = /* HTML */ `<form>
      <h2 data-role="entry-title"></h2>
      <label>用户或组<input data-role="account" required /></label
      ><label
        >类型<select data-role="type">
          <option value="A">允许</option>
          <option value="D">拒绝</option>
        </select></label
      ><label>权限<select data-role="rights"></select></label
      ><label
        >应用于<select data-role="inherit">
          <option value="">仅此对象</option>
          <option value="OICI">此文件夹、子文件夹和文件</option>
          <option value="CI">此文件夹和子文件夹</option>
          <option value="OI">此文件夹和文件</option>
        </select></label
      >
      <div class="dialog-actions">
        <button type="button" data-action="entry-cancel">取消</button><button type="submit">确定</button>
      </div>
    </form>`;
    document.body.append(this.entry);
    this.body = this.dialog.querySelector("tbody");
    this.status = this.dialog.querySelector("[data-role=status]");
    this.inheritance = this.dialog.querySelector("[data-role=inheritance]");
    this.raw = this.dialog.querySelector("[data-role=sddl]");
    this.dialog.querySelector("[data-action=cancel]").onclick = () => this.dialog.close();
    this.dialog.querySelector("[data-action=add]").onclick = () => this.editEntry();
    this.dialog.querySelector("[data-action=edit]").onclick = () => this.editEntry(this.selected);
    this.dialog.querySelector("[data-action=remove]").onclick = () => this.remove();
    this.dialog.querySelector("form").onsubmit = (event) => {
      event.preventDefault();
      this.save();
    };
    this.entry.querySelector("[data-action=entry-cancel]").onclick = () => this.entry.close();
    this.entry.querySelector("form").onsubmit = (event) => {
      event.preventDefault();
      this.finishEntry();
    };
    this.raw.onchange = () => {
      try {
        const descriptor = parseSddl(this.raw.value);
        descriptor.owner = this.descriptor.owner;
        descriptor.group = this.descriptor.group;
        this.descriptor = descriptor;
        this.render();
      } catch (error) {
        this.notify(error);
      }
    };
    this.inheritance.onchange = () => this.setInheritance(this.inheritance.checked);
  }
  async open({ title, objectType = "file", container = false, load, save }) {
    this.objectType = objectType;
    this.saveDescriptor = save;
    this.dialog.querySelector("[data-role=title]").textContent = title;
    const details = this.dialog.querySelector("details");
    details.open = true;
    details.querySelector("summary").textContent = "DACL SDDL";
    const inherit = this.entry.querySelector("[data-role=inherit]");
    inherit.replaceChildren(new Option("仅此对象", ""));
    if (container) {
      if (objectType === "registry") {
        inherit.add(new Option("此项和所有子项", "CI"));
        inherit.add(new Option("仅子项", "CIIO"));
      } else {
        inherit.add(new Option("此文件夹、子文件夹和文件", "OICI"));
        inherit.add(new Option("此文件夹和子文件夹", "CI"));
        inherit.add(new Option("此文件夹和文件", "OI"));
        inherit.add(new Option("仅子文件夹和文件", "OICIIO"));
        inherit.add(new Option("仅子文件夹", "CIIO"));
        inherit.add(new Option("仅文件", "OIIO"));
      }
    }
    this.body.replaceChildren();
    this.dialog.querySelector("[data-role=empty]").hidden = false;
    this.dialog.querySelector("[data-role=empty]").textContent = "正在读取权限…";
    this.dialog.showModal();
    try {
      const value = await load();
      this.descriptor = parseSddl(value.sddl);
      this.daclProtected = value.daclProtected;
      await this.resolveNames();
      this.render();
    } catch (error) {
      this.dialog.close();
      this.notify(error);
    }
  }
  async resolveNames() {
    const trustees = new Set(
      [this.descriptor.owner, this.descriptor.group, ...this.descriptor.aces.map((ace) => ace.sid)].filter((value) =>
        value?.startsWith("S-"),
      ),
    );
    this.names = new Map();
    await Promise.all(
      [...trustees].map(async (sid) => {
        try {
          const result = await this.call("/api/security/account", { value: sid, sid: true });
          this.names.set(sid, result.value);
        } catch {}
      }),
    );
  }
  displaySid(sid) {
    return WELL_KNOWN.get(sid) || this.names.get(sid) || sid || "—";
  }
  render() {
    const inherited = this.descriptor.aces.some((ace) => ace.flags.includes("ID"));
    this.selected = null;
    this.raw.value = buildDacl(this.descriptor);
    this.raw.readOnly = inherited;
    this.inheritance.checked = !this.daclProtected;
    this.dialog.querySelector("[data-role=owner]").textContent = this.displaySid(this.descriptor.owner);
    this.dialog.querySelector("[data-role=group]").textContent = this.displaySid(this.descriptor.group);
    this.body.replaceChildren(
      ...this.descriptor.aces.map((ace, index) => {
        const row = document.createElement("tr");
        row.innerHTML = "<td></td><td></td><td></td><td></td>";
        row.children[0].textContent = ace.type === "A" ? "允许" : ace.type === "D" ? "拒绝" : ace.type;
        row.children[1].textContent = this.displaySid(ace.sid);
        row.children[1].title = ace.sid;
        row.children[2].textContent = rightName(this.objectType, ace.rights);
        row.children[3].textContent = inheritName(this.objectType, ace.flags);
        row.onclick = () => {
          this.body.querySelector(".selected")?.classList.remove("selected");
          row.classList.add("selected");
          this.selected = { ace, index };
          const inherited = ace.flags.includes("ID"),
            editable = (ace.type === "A" || ace.type === "D") && !inherited;
          this.dialog.querySelector("[data-action=edit]").disabled = !editable;
          this.dialog.querySelector("[data-action=remove]").disabled = inherited;
        };
        return row;
      }),
    );
    const empty = this.dialog.querySelector("[data-role=empty]");
    empty.hidden = this.descriptor.aces.length !== 0;
    empty.textContent = "没有权限项";
    this.status.textContent = inherited ? "继承的权限项不可直接修改。" : "";
  }
  setInheritance(enabled) {
    this.daclProtected = !enabled;
    if (enabled) {
      this.descriptor.aces = this.descriptor.aces.filter((ace) => !ace.flags.includes("ID"));
    } else {
      for (const ace of this.descriptor.aces) {
        if (!ace.flags.includes("ID")) continue;
        ace.flags = ace.flags.replace(/ID/g, "");
        ace.modified = true;
      }
    }
    this.canonicalize();
    this.render();
  }
  editEntry(selected = null) {
    this.editing = selected;
    const ace = selected?.ace;
    this.entry.querySelector("[data-role=entry-title]").textContent = ace ? "编辑权限项" : "添加权限项";
    const account = this.entry.querySelector("[data-role=account]");
    account.value = ace ? this.displaySid(ace.sid) : "";
    account.readOnly = !!ace;
    this.entry.querySelector("[data-role=type]").value = ace?.type || "A";
    const rights = this.entry.querySelector("[data-role=rights]");
    rights.replaceChildren(...RIGHTS[this.objectType].map(([value, name]) => new Option(name, value)));
    if (ace && !RIGHTS[this.objectType].some((item) => sameRight(item[0], ace.rights)))
      rights.add(new Option(ace.rights, ace.rights));
    rights.value =
      RIGHTS[this.objectType].find((item) => sameRight(item[0], ace?.rights))?.[0] ||
      ace?.rights ||
      RIGHTS[this.objectType][0][0];
    const inherit = this.entry.querySelector("[data-role=inherit]"),
      flags = ace?.flags.replace(/ID/g, "") || "";
    if (ace && ![...inherit.options].some((option) => option.value === flags)) inherit.add(new Option(flags, flags));
    inherit.value = flags;
    this.entry.showModal();
    account.focus();
  }
  async finishEntry() {
    const account = this.entry.querySelector("[data-role=account]").value.trim();
    if (!account) return;
    try {
      let sid = this.editing?.ace.sid || account;
      if (!this.editing && !sid.startsWith("S-") && !WELL_KNOWN.has(sid)) {
        const result = await this.call("/api/security/account", { value: sid, sid: false });
        sid = result.value;
        this.names.set(sid, account);
      }
      const ace = {
        type: this.entry.querySelector("[data-role=type]").value,
        flags: this.entry.querySelector("[data-role=inherit]").value,
        rights: this.entry.querySelector("[data-role=rights]").value,
        object: "",
        inheritObject: "",
        sid,
        extra: [],
        modified: true,
      };
      if (this.editing) this.descriptor.aces[this.editing.index] = ace;
      else this.descriptor.aces.push(ace);
      this.canonicalize();
      this.entry.close();
      this.render();
    } catch (error) {
      this.notify(error);
    }
  }
  canonicalize() {
    this.descriptor.aces.sort(
      (a, b) =>
        Number(a.flags.includes("ID")) - Number(b.flags.includes("ID")) ||
        (a.type === "D" ? 0 : 1) - (b.type === "D" ? 0 : 1),
    );
  }
  remove() {
    if (!this.selected || this.selected.ace.flags.includes("ID")) return;
    this.descriptor.aces.splice(this.selected.index, 1);
    this.render();
  }
  async save() {
    try {
      const sddl = buildDacl(this.descriptor);
      await this.saveDescriptor(sddl, this.daclProtected);
      this.dialog.close();
      this.notify("权限已保存");
    } catch (error) {
      this.notify(error);
    }
  }
}

function parseSddl(value) {
  const sections = {};
  let current = null,
    start = 0,
    depth = 0;
  for (let index = 0; index < value.length - 1; index++) {
    const character = value[index];
    if (character === "(") depth++;
    else if (character === ")") depth--;
    if (depth === 0 && "OGDS".includes(character) && value[index + 1] === ":") {
      if (current) sections[current] = value.slice(start, index);
      current = character;
      start = index + 2;
      index++;
    }
  }
  if (current) sections[current] = value.slice(start);
  const dacl = sections.D || "",
    first = dacl.indexOf("("),
    flags = first < 0 ? dacl : dacl.slice(0, first),
    aces = [];
  for (const raw of splitAces(first < 0 ? "" : dacl.slice(first))) {
    const fields = raw.slice(1, -1).split(";");
    aces.push({
      type: fields[0] || "",
      flags: fields[1] || "",
      rights: fields[2] || "",
      object: fields[3] || "",
      inheritObject: fields[4] || "",
      sid: fields[5] || "",
      extra: fields.slice(6),
      raw,
    });
  }
  return { owner: sections.O || "", group: sections.G || "", daclFlags: flags, aces };
}
function splitAces(value) {
  const result = [];
  let start = -1,
    depth = 0;
  for (let index = 0; index < value.length; index++) {
    if (value[index] === "(") {
      if (depth++ === 0) start = index;
    } else if (value[index] === ")") {
      if (--depth < 0) throw new Error("DACL 格式无效");
      if (depth === 0) result.push(value.slice(start, index + 1));
    } else if (depth === 0 && !/\s/.test(value[index])) throw new Error("DACL 格式无效");
  }
  if (depth !== 0) throw new Error("DACL 格式无效");
  return result;
}
function buildDacl(value) {
  const aces = value.aces
    .map((ace) => {
      if (!ace.modified) return ace.raw;
      const fields = [ace.type, ace.flags, ace.rights, ace.object, ace.inheritObject, ace.sid, ...ace.extra];
      return `(${fields.join(";")})`;
    })
    .join("");
  return `D:${value.daclFlags}${aces}`;
}
function rightName(type, value) {
  const aliases =
    type === "registry"
      ? { GA: "完全控制", GR: "读取", GW: "写入" }
      : {
          FA: "完全控制",
          GA: "完全控制",
          FR: "读取",
          FW: "写入",
          GXGR: "读取和执行",
          GRGX: "读取和执行",
          FRFX: "读取和执行",
        };
  return aliases[value.toUpperCase()] || RIGHTS[type].find((item) => sameRight(item[0], value))?.[1] || value;
}
function sameRight(left, right) {
  if (!left || !right) return false;
  const a = left.toUpperCase(),
    b = right.toUpperCase();
  return a === b || (a.startsWith("0X") && b.startsWith("0X") && Number.parseInt(a, 16) === Number.parseInt(b, 16));
}
function inheritName(type, flags) {
  if (flags.includes("ID")) return "从父对象继承";
  const object = flags.includes("OI"),
    container = flags.includes("CI"),
    only = flags.includes("IO");
  if (type === "registry") return container ? (only ? "仅子项" : "此项和子项") : "仅此项";
  if (object && container) return only ? "仅子文件夹和文件" : "此对象、子文件夹和文件";
  if (container) return only ? "仅子文件夹" : "此对象和子文件夹";
  if (object) return only ? "仅文件" : "此对象和文件";
  return "仅此对象";
}
function normalizePath(value) {
  const path = (value || "").trim().replace(/\//g, "\\").replace(/\\+$/, "");
  return /^[A-Za-z]:$/.test(path) ? `${path}\\` : path;
}
function joinPath(path, name) {
  return `${path.replace(/[\\/]+$/, "")}\\${name}`;
}
function parentPath(value) {
  const path = normalizePath(value);
  if (!path || /^[A-Za-z]:\\$/.test(path)) return "";
  const index = path.lastIndexOf("\\");
  return index === 2 ? path.slice(0, 3) : index < 0 ? "" : path.slice(0, index);
}
