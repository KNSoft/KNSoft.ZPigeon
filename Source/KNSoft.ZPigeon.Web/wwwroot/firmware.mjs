import { postBinary, postData } from "./client-context.mjs";

const VARIABLE = 28,
  BOOT = 29,
  UNORDERED = 0xffffffff,
  MISSING = 0x80000000,
  ORDER_MASK = 0x7fffffff;
const GLOBAL_GUID = "{8BE4DF61-93CA-11D2-AA0D-00E098032B8C}",
  tabs = [
    ["bios", "BIOS"],
    ["cpuid", "CPUID"],
    ["acpi", "ACPI"],
    ["smbios", "SMBIOS"],
    ["uefi", "UEFI"],
  ];
const attributeNames = [
  [1, "NV"],
  [2, "BS"],
  [4, "RT"],
  [8, "HW_ERROR"],
  [0x10, "AUTH"],
  [0x20, "TIME_AUTH"],
  [0x40, "APPEND"],
  [0x80, "ENHANCED_AUTH"],
];
const attributes = (value) =>
  attributeNames
    .filter(([flag]) => value & flag)
    .map(([, name]) => name)
    .join(" | ") || "0";
const hex = (value) => `0x${Number(value).toString(16).toUpperCase().padStart(8, "0")}`;
const ascii = (value) =>
  String.fromCharCode(value & 0xff, (value >>> 8) & 0xff, (value >>> 16) & 0xff, (value >>> 24) & 0xff);

export class FirmwareManager {
  constructor(root, { call, notify, hexEditor }) {
    this.root = root;
    this.call = call;
    this.notify = notify;
    this.hexEditor = hexEditor;
    this.view = "bios";
    this.records = new Map();
    this.errors = new Map();
    this.expanded = new Set();
    this.connected = false;
    this.request = 0;
    root.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <nav class="property-tabs firmware-tabs" data-role="tabs"></nav>
        <input data-role="filter" placeholder="筛选固件信息" /><span data-role="summary" class="status"></span
        ><span class="spacer"></span><button data-action="add" hidden>新建变量</button
        ><button data-action="save-order" hidden>保存启动顺序</button><button data-action="get">获取</button>
      </div>
      <div class="firmware-body">
        <div class="manager-table firmware-list">
          <table>
            <thead></thead>
            <tbody></tbody>
          </table>
          <div class="manager-empty">Client 未连接</div>
        </div>
        <section class="firmware-features" data-role="features" hidden>
          <h3></h3>
          <div></div>
        </section>
      </div>
      <div class="context-menu administration-menu" data-role="menu" hidden></div>
      <dialog data-role="variable">
        <form>
          <h2>新建 UEFI 变量</h2>
          <label>变量名<input data-field="name" required autocomplete="off" /></label
          ><label>供应商 GUID<input data-field="guid" required autocomplete="off" /></label
          ><label>属性（十六进制或十进制）<input data-field="attributes" required autocomplete="off" /></label
          ><label>初始数据（十六进制字节）<textarea data-field="data" rows="8" spellcheck="false"></textarea></label>
          <p class="property-note">写入固件变量可能导致系统无法启动。操作由被控端固件和权限最终决定。</p>
          <div class="dialog-actions">
            <button type="button" data-action="cancel">取消</button><button class="danger">写入</button>
          </div>
        </form>
      </dialog>
      <dialog data-role="attributes">
        <form>
          <h2>修改变量属性</h2>
          <label>属性（十六进制或十进制）<input data-field="attributes" required autocomplete="off" /></label>
          <p class="property-note">保存时会原样重写变量数据，只改变属性。</p>
          <div class="dialog-actions">
            <button type="button" data-action="cancel">取消</button><button class="danger">保存</button>
          </div>
        </form>
      </dialog>
      <dialog data-role="properties">
        <form method="dialog">
          <h2>固件属性</h2>
          <dl class="details-grid"></dl>
          <div class="dialog-actions"><button value="close">关闭</button></div>
        </form>
      </dialog>`;
    this.tabs = root.querySelector("[data-role=tabs]");
    this.filter = root.querySelector("[data-role=filter]");
    this.head = root.querySelector("thead");
    this.body = root.querySelector("tbody");
    this.empty = root.querySelector(".manager-empty");
    this.summary = root.querySelector("[data-role=summary]");
    this.getButton = root.querySelector("[data-action=get]");
    this.addButton = root.querySelector("[data-action=add]");
    this.saveOrderButton = root.querySelector("[data-action=save-order]");
    this.features = root.querySelector("[data-role=features]");
    this.menu = root.querySelector("[data-role=menu]");
    this.variableDialog = root.querySelector("[data-role=variable]");
    this.attributeDialog = root.querySelector("[data-role=attributes]");
    this.propertiesDialog = root.querySelector("[data-role=properties]");
    this.table = root.querySelector(".firmware-list table");
    this.tree = document.createElement("ul");
    this.tree.className = "administration-tree firmware-tree";
    this.table.before(this.tree);
    this.tabs.replaceChildren(
      ...tabs.map(([name, title]) => {
        const button = document.createElement("button");
        button.textContent = title;
        button.dataset.view = name;
        button.onclick = () => {
          this.errors.delete(name);
          this.view = name;
          this.updateToolbar();
          this.render();
          this.ensureLoaded();
        };
        return button;
      }),
    );
    this.filter.oninput = () => this.render();
    this.getButton.onclick = () => this.load(true);
    this.addButton.onclick = () => this.createVariable();
    this.saveOrderButton.onclick = () => this.saveBootOrder();
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
    this.variableDialog.querySelector("[data-action=cancel]").onclick = () => this.variableDialog.close();
    this.variableDialog.querySelector("form").onsubmit = (event) => {
      event.preventDefault();
      this.writeNewVariable();
    };
    this.attributeDialog.querySelector("[data-action=cancel]").onclick = () => this.attributeDialog.close();
    this.attributeDialog.querySelector("form").onsubmit = (event) => {
      event.preventDefault();
      this.writeAttributes();
    };
    addEventListener("pointerdown", (event) => {
      if (!this.menu.contains(event.target)) this.menu.hidden = true;
    });
    this.updateToolbar();
    this.render();
  }
  activate(connected) {
    this.connected = connected;
    this.render();
    this.ensureLoaded();
  }
  disconnect() {
    this.connected = false;
    this.loadingKey = null;
    this.request++;
    this.records.clear();
    this.errors.clear();
    this.expanded.clear();
    this.orderDirty = false;
    this.menu.hidden = true;
    this.render();
  }
  key() {
    return this.view;
  }
  endpoint(key = this.view) {
    return key === "uefi" ? "/api/firmware/variables" : `/api/firmware/${key}`;
  }
  updateToolbar() {
    for (const button of this.tabs.children) button.classList.toggle("active", button.dataset.view === this.view);
    const uefi = this.view === "uefi";
    this.addButton.hidden = !uefi;
    this.saveOrderButton.hidden = !uefi;
    this.getButton.hidden = !uefi;
    this.filter.placeholder = `筛选 ${tabs.find(([name]) => name === this.view)[1]} 信息`;
  }
  ensureLoaded() {
    if (this.connected && this.view !== "uefi" && !this.records.has(this.view) && !this.errors.has(this.view))
      this.load();
  }
  async load(force = false) {
    const key = this.key();
    if (!this.connected || this.loadingKey || (!force && this.records.has(key))) return;
    const request = this.request;
    this.loadingKey = key;
    this.errors.delete(key);
    this.render();
    try {
      const records = await this.call(this.endpoint(key));
      if (request !== this.request) return;
      this.records.set(key, records);
      this.orderDirty = false;
    } catch (error) {
      if (request !== this.request) return;
      this.records.delete(key);
      this.errors.set(key, error.message);
      this.notify(error);
    } finally {
      if (request === this.request) {
        this.loadingKey = null;
        this.render();
        this.ensureLoaded();
      }
    }
  }
  source() {
    const data = this.records.get(this.key());
    if (!data) return [];
    if (this.view === "cpuid") return data.records;
    if (this.view === "smbios") return data.structures;
    if (this.view === "uefi")
      return [
        ...data.filter((record) => record.kind === VARIABLE),
        ...data
          .filter((record) => record.kind === BOOT && record.flags !== UNORDERED)
          .sort((a, b) => (a.flags & ORDER_MASK) - (b.flags & ORDER_MASK)),
      ];
    return data;
  }
  render() {
    const key = this.key(),
      loaded = this.records.has(key),
      loading = this.loadingKey === key,
      query = this.filter.value.toLocaleLowerCase(),
      all = this.source(),
      treeView = ["bios", "cpuid", "smbios", "acpi"].includes(this.view),
      records = all.filter((record) => !query || contains(record, query));
    this.table.hidden = treeView;
    this.tree.hidden = !treeView;
    this.features.hidden = true;
    if (treeView) this.renderTree(query);
    else {
      this.renderHead();
      this.body.replaceChildren(...records.map((record) => this.row(record)));
    }
    this.summary.textContent = loaded ? `${all.length} 项` : "";
    this.empty.hidden = !loading && (treeView ? this.tree.childElementCount !== 0 : records.length !== 0);
    if (!this.connected) this.empty.textContent = "Client 未连接";
    else if (loading) this.empty.textContent = "正在读取被控端固件信息…";
    else if (this.errors.has(key)) this.empty.textContent = this.errors.get(key);
    else if (!loaded)
      this.empty.textContent = this.view === "uefi" ? "点击“获取”读取被控端 UEFI 信息" : "正在准备读取…";
    else if (!(treeView ? this.tree.childElementCount : records.length))
      this.empty.textContent = all.length ? "没有匹配的信息" : "没有信息";
    this.getButton.disabled = !this.connected || !!this.loadingKey;
    this.getButton.textContent = loading ? "正在获取…" : loaded ? "重新获取" : "获取";
    this.saveOrderButton.disabled = this.view !== "uefi" || !this.bootOrderVariable() || this.orderDirty !== true;
  }
  renderTree(query) {
    const include = (value) => !query || contains(value, query),
      leaf = (name, value) => ({ name, detail: String(value ?? "—"), children: [] }),
      data = this.records.get(this.key());
    let nodes = [];
    if (!data) {
      this.tree.replaceChildren();
      return;
    }
    if (this.view === "bios") {
      nodes = data.map((record) => ({
        name: record.name,
        detail: record.identity === "secureBoot" ? (Number(record.value) ? "已启用" : "未启用") : record.detail || "—",
        children: [],
      }));
    } else if (this.view === "cpuid") {
      const leaves = new Map();
      for (const record of data.records) {
        if (!leaves.has(record.leaf)) leaves.set(record.leaf, []);
        leaves.get(record.leaf).push(record);
      }
      nodes = [...leaves]
        .sort(([left], [right]) => left - right)
        .map(([value, records]) => ({
          id: `cpuid:${value}`,
          name: `Leaf ${hex(value)}`,
          detail: "",
          children: records
            .sort((left, right) => left.subLeaf - right.subLeaf)
            .map((record) => {
              const prefix = `cpuid:${record.leaf}:${record.subLeaf}`,
                register = (name) => {
                  const features = data.features
                    .filter(
                      (feature) =>
                        feature.leaf === record.leaf &&
                        feature.subLeaf === record.subLeaf &&
                        feature.register.toLocaleUpperCase() === name,
                    )
                    .map((feature) => leaf(feature.name, `Bit ${feature.bit}`));
                  return {
                    id: `${prefix}:${name}`,
                    name,
                    detail: hex(record[name.toLocaleLowerCase()]),
                    children: features,
                  };
                };
              const vendor =
                record.leaf === 0 && record.subLeaf === 0
                  ? (ascii(record.ebx) + ascii(record.edx) + ascii(record.ecx)).replace(/\0+$/, "")
                  : "";
              return {
                id: prefix,
                name: `Subleaf ${hex(record.subLeaf)}${vendor ? ` (${vendor})` : ""}`,
                detail: "",
                record,
                children: ["EAX", "EBX", "ECX", "EDX"].map(register),
              };
            }),
        }));
    } else if (this.view === "smbios") {
      nodes = data.structures.map((record) => ({
        id: record.identity,
        name: `Type ${record.type} · ${record.name}`,
        detail: `Handle 0x${record.handle.toString(16).padStart(4, "0").toUpperCase()}`,
        record,
        lazy: !record.loaded,
        load: () => this.loadDetail(record, "/api/firmware/smbios/structure"),
        children: record.loading
          ? [leaf("状态", "正在读取…")]
          : record.loadError
            ? [leaf("读取失败", record.loadError)]
            : !record.loaded
              ? []
              : [
                  leaf("偏移", `${record.offset} 字节`),
                  leaf("格式长度", `${record.formattedLength} 字节`),
                  leaf("总长度", `${record.totalLength} 字节`),
                  ...record.fields.map((field) => leaf(field.name, field.value)),
                ],
      }));
    } else {
      nodes = data.map((record) => ({
        id: record.identity,
        name: `${record.signature} · ${record.description}`,
        detail: record.loadError || record.error || (!record.loaded ? "" : `${record.length} 字节`),
        record,
        lazy: !record.loaded,
        load: () => this.loadDetail(record, "/api/firmware/acpi/table"),
        children: record.loading
          ? [leaf("状态", "正在读取…")]
          : record.loadError
            ? [leaf("读取失败", record.loadError)]
            : !record.loaded
              ? []
              : [
                  leaf("OEM ID", record.oemId),
                  leaf("OEM Table ID", record.oemTableId),
                  leaf("修订", record.revision),
                  ...(record.error
                    ? [leaf("解析错误", record.error)]
                    : record.fields.map((field) => leaf(field.name, field.value))),
                ],
      }));
    }
    const matches = (node) => include(node) || node.children.some(matches);
    this.tree.replaceChildren(...nodes.filter(matches).map((node) => this.treeNode(node, matches)));
  }
  treeNode(node, matches) {
    const li = document.createElement("li"),
      row = document.createElement("div"),
      arrow = document.createElement("button"),
      label = document.createElement("button"),
      detail = document.createElement("span"),
      list = document.createElement("ul"),
      children = node.children.filter(matches),
      expandable = node.lazy || children.length,
      expanded = node.id && this.expanded.has(node.id);
    row.className = "administration-node-row";
    arrow.className = "administration-arrow";
    arrow.textContent = expandable ? (expanded ? "▾" : "▸") : "";
    arrow.disabled = !expandable;
    label.className = "administration-node-label";
    label.textContent = node.name;
    detail.className = "status";
    detail.textContent = node.detail;
    list.hidden = !expanded;
    row.append(arrow, label, detail);
    li.append(row, list);
    if (expanded) list.append(...children.map((child) => this.treeNode(child, matches)));
    const toggle = async () => {
      if (!expandable) return;
      if (expanded) this.expanded.delete(node.id);
      else {
        this.expanded.add(node.id);
        if (node.lazy) await node.load();
      }
      this.render();
    };
    arrow.onclick = label.onclick = toggle;
    if (node.record) {
      row.oncontextmenu = (event) => {
        event.preventDefault();
        this.openMenu(event, node.record);
      };
      label.ondblclick = () => this.properties(node.record);
    }
    return li;
  }
  async loadDetail(record, endpoint) {
    if (record.loading || record.loaded) return;
    const request = this.request;
    record.loading = true;
    delete record.loadError;
    this.render();
    try {
      const metadata = { identity: record.identity };
      if (endpoint.includes("/smbios/")) {
        const snapshot = this.records.get("smbios");
        Object.assign(metadata, {
          majorVersion: snapshot.majorVersion,
          minorVersion: snapshot.minorVersion,
          dmiRevision: snapshot.dmiRevision,
        });
      }
      const detail = await this.call(endpoint, metadata);
      if (request === this.request) Object.assign(record, detail);
    } catch (error) {
      if (request === this.request) {
        record.loadError = error.message;
        this.notify(error);
      }
    } finally {
      if (request === this.request) {
        record.loading = false;
        this.render();
      }
    }
  }
  renderHead() {
    const row = document.createElement("tr");
    for (const name of ["类型", "名称", "标识", "属性 / 状态", "大小 / 顺序", "调整"]) {
      const cell = document.createElement("th");
      cell.textContent = name;
      row.append(cell);
    }
    this.head.replaceChildren(row);
  }
  row(record) {
    const row = document.createElement("tr");
    ((row.record = record),
      (boot = record.kind === BOOT),
      (ordered = this.source().filter((item) => item.kind === BOOT)),
      (index = boot ? ordered.indexOf(record) : -1),
      (values = boot
        ? [
            "启动项",
            record.flags & MISSING ? "（变量不存在）" : record.name || "（无描述）",
            record.description,
            record.state & 1 ? "活动" : "未激活",
            String(index + 1),
          ]
        : ["变量", record.name, record.description, attributes(record.state), `${record.value} 字节`]));
    for (const value of values) {
      const cell = row.insertCell();
      cell.textContent = value;
      cell.title = value;
    }
    const cell = row.insertCell();
    if (boot) {
      const up = document.createElement("button"),
        down = document.createElement("button");
      up.textContent = "↑";
      down.textContent = "↓";
      up.disabled = index === 0;
      down.disabled = index === ordered.length - 1;
      up.onclick = (event) => {
        event.stopPropagation();
        this.moveBoot(index, -1);
      };
      down.onclick = (event) => {
        event.stopPropagation();
        this.moveBoot(index, 1);
      };
      cell.append(up, down);
    }
    return row;
  }
  moveBoot(index, direction) {
    const ordered = this.source().filter((record) => record.kind === BOOT),
      other = ordered[index + direction],
      current = ordered[index];
    if (!other) return;
    const value = current.flags & ORDER_MASK;
    current.flags = ((current.flags & MISSING) | (other.flags & ORDER_MASK)) >>> 0;
    other.flags = ((other.flags & MISSING) | value) >>> 0;
    this.orderDirty = true;
    this.render();
  }
  bootOrderVariable() {
    return (this.records.get("uefi") || []).find(
      (record) =>
        record.kind === VARIABLE &&
        record.name === "BootOrder" &&
        record.description.toLocaleUpperCase() === GLOBAL_GUID,
    );
  }
  async saveBootOrder() {
    const variable = this.bootOrderVariable();
    if (!variable || !confirm("确定修改远端系统的 UEFI 启动顺序？错误的顺序可能导致系统无法启动。")) return;
    const entries = this.source().filter((record) => record.kind === BOOT),
      data = new Uint8Array(entries.length * 2);
    entries.forEach((entry, index) => {
      const id = Number(entry.value);
      data[index * 2] = id & 0xff;
      data[index * 2 + 1] = id >> 8;
    });
    try {
      await postData(
        "/api/firmware/variable/write",
        { action: 23, identity: variable.identity, attributes: variable.state },
        data,
      );
      this.notify("启动顺序已保存");
      await this.load(true);
    } catch (error) {
      this.notify(error);
    }
  }
  openMenu(event, record) {
    const actions = [["属性", () => this.properties(record)]];
    if (record.loaded || record.kind === VARIABLE) actions.unshift(["查看原始数据", () => this.openData(record)]);
    if (record.kind === VARIABLE)
      actions.push(
        ["编辑二进制", () => this.openData(record, true)],
        ["修改属性", () => this.editAttributes(record)],
        ["删除", () => this.removeVariable(record), true],
      );
    this.menu.replaceChildren(
      ...actions.map(([title, action, danger]) => {
        const button = document.createElement("button");
        button.textContent = title;
        button.classList.toggle("danger", danger === true);
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
  openData(record, writable = false) {
    const size = record.totalLength ?? record.length ?? Number(record.value);
    this.hexEditor.open({
      title: `${record.name || record.signature || record.description} — 固件数据`,
      size,
      read: (offset, length) =>
        postBinary("/api/firmware/data", { identity: record.identity, offset: String(offset), length }),
      write: writable
        ? (offset, data) =>
            postData(
              "/api/firmware/variable/range/write",
              { identity: record.identity, attributes: record.state, offset: String(offset) },
              data,
            )
        : null,
    });
  }
  properties(record) {
    let fields;
    if (this.view === "cpuid")
      fields = [
        ["Leaf", hex(record.leaf)],
        ["Subleaf", hex(record.subLeaf)],
        ["EAX", hex(record.eax)],
        ["EBX", hex(record.ebx)],
        ["ECX", hex(record.ecx)],
        ["EDX", hex(record.edx)],
      ];
    else if (this.view === "smbios")
      fields = [
        ["类型", `Type ${record.type} · ${record.name}`],
        ["Handle", `0x${record.handle.toString(16).padStart(4, "0").toUpperCase()}`],
        ["偏移", `${record.offset} 字节`],
        ["格式长度", `${record.formattedLength} 字节`],
        ["总长度", `${record.totalLength} 字节`],
        ...record.fields.map((field) => [field.name, field.value]),
      ];
    else if (this.view === "acpi")
      fields = [
        ["签名", record.signature],
        ["说明", record.description],
        ["OEM ID", record.oemId || "—"],
        ["OEM Table ID", record.oemTableId || "—"],
        ["修订", !record.loaded ? "—" : String(record.revision)],
        ["长度", !record.loaded ? "—" : `${record.length} 字节`],
        ...(record.error ? [["解析错误", record.error]] : record.fields.map((field) => [field.name, field.value])),
      ];
    else if (record.kind === VARIABLE)
      fields = [
        ["变量名", record.name],
        ["供应商 GUID", record.description],
        ["属性", `0x${record.state.toString(16).toUpperCase().padStart(8, "0")} (${attributes(record.state)})`],
        ["大小", `${record.value} 字节`],
      ];
    else
      fields = [
        ["变量", record.description],
        ["描述", record.name || "—"],
        ["启动 ID", `0x${Number(record.value).toString(16).padStart(4, "0").toUpperCase()}`],
        ["加载属性", `0x${record.state.toString(16).toUpperCase()}`],
        ["顺序", String((record.flags & ORDER_MASK) + 1)],
      ];
    this.propertiesDialog.querySelector("dl").replaceChildren(
      ...fields.flatMap(([name, value]) => {
        const dt = document.createElement("dt"),
          dd = document.createElement("dd");
        dt.textContent = name;
        dd.textContent = value;
        return [dt, dd];
      }),
    );
    this.propertiesDialog.showModal();
  }
  createVariable() {
    const form = this.variableDialog.querySelector("form");
    form.reset();
    form.querySelector("[data-field=guid]").value = GLOBAL_GUID;
    form.querySelector("[data-field=attributes]").value = "0x7";
    this.variableDialog.showModal();
    form.querySelector("[data-field=name]").focus();
  }
  parseAttributes(value) {
    const number = Number(value);
    if (!Number.isInteger(number) || number < 0 || number > 0xffffffff) throw new Error("变量属性无效");
    return number;
  }
  parseHex(value) {
    const compact = value.replace(/\s+/g, "");
    if (compact.length % 2 || !/^[0-9a-f]*$/i.test(compact)) throw new Error("变量数据必须是十六进制字节");
    return Uint8Array.from(compact.match(/../g) || [], (item) => parseInt(item, 16));
  }
  async writeNewVariable() {
    const form = this.variableDialog.querySelector("form");
    try {
      const data = this.parseHex(form.querySelector("[data-field=data]").value);
      if (!confirm("确定创建并写入此 UEFI 变量？错误的变量可能导致系统无法启动。")) return;
      await postData("/api/firmware/variable/write", {
        action: 1,
        name: form.querySelector("[data-field=name]").value.trim(),
        vendorGuid: form.querySelector("[data-field=guid]").value.trim(),
        attributes: this.parseAttributes(form.querySelector("[data-field=attributes]").value.trim()),
      }, data);
      this.variableDialog.close();
      this.notify("UEFI 变量已创建");
      await this.load(true);
    } catch (error) {
      this.notify(error);
    }
  }
  editAttributes(record) {
    this.attributeRecord = record;
    this.attributeDialog.querySelector("[data-field=attributes]").value =
      `0x${record.state.toString(16).toUpperCase()}`;
    this.attributeDialog.showModal();
  }
  async readAll(record) {
    const size = Number(record.value),
      data = new Uint8Array(size);
    for (let offset = 0; offset < size; offset += 0x10000) {
      const result = await postBinary("/api/firmware/data", {
        identity: record.identity,
        offset: String(offset),
        length: Math.min(0x10000, size - offset),
      });
      data.set(result.data, offset);
    }
    return data;
  }
  async writeAttributes() {
    const record = this.attributeRecord;
    try {
      const attributes = this.parseAttributes(
          this.attributeDialog.querySelector("[data-field=attributes]").value.trim(),
        ),
        data = await this.readAll(record);
      if (!confirm("确定重写此 UEFI 变量并修改属性？")) return;
      await postData("/api/firmware/variable/write", {
        action: 23,
        identity: record.identity,
        attributes,
      }, data);
      this.attributeDialog.close();
      this.notify("变量属性已修改");
      await this.load(true);
    } catch (error) {
      this.notify(error);
    }
  }
  async removeVariable(record) {
    if (!confirm(`确定删除 UEFI 变量“${record.name}”？此操作可能导致系统无法启动。`)) return;
    try {
      await postData("/api/firmware/variable/write", {
        action: 2,
        identity: record.identity,
        attributes: 0,
      }, new Uint8Array());
      this.notify("UEFI 变量已删除");
      await this.load(true);
    } catch (error) {
      this.notify(error);
    }
  }
}

function contains(value, query) {
  if (value == null) return false;
  if (typeof value !== "object") return String(value).toLocaleLowerCase().includes(query);
  for (const item of Object.values(value)) if (contains(item, query)) return true;
  return false;
}
