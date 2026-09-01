const tabs = [
  ["processor", "处理器"],
  ["mainboard", "主板"],
  ["memory", "内存"],
  ["display", "显示器"],
  ["storage", "存储"],
  ["network", "网络"],
  ["power", "电源"],
];
const smbiosTypes = { mainboard: new Set([0, 1, 2, 3]), memory: new Set([16, 17, 19, 20]) };

export class HardwareInformationManager {
  constructor(root, { call, notify }) {
    this.root = root;
    this.call = call;
    this.notify = notify;
    this.connected = false;
    this.current = "processor";
    this.cache = new Map();
    root.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <div class="property-tabs" data-role="tabs"></div>
        <span class="spacer"></span><button data-action="refresh">刷新</button
        ><span class="status" data-role="status">进入页面后按需读取硬件信息</span>
      </div>
      <div class="hardware-information" data-role="body"></div>`;
    this.tabHost = root.querySelector("[data-role=tabs]");
    this.body = root.querySelector("[data-role=body]");
    this.status = root.querySelector("[data-role=status]");
    this.tabHost.replaceChildren(
      ...tabs.map(([id, title]) => {
        const button = document.createElement("button");
        button.textContent = title;
        button.dataset.tab = id;
        button.onclick = () => this.select(id);
        return button;
      }),
    );
    root.querySelector("[data-action=refresh]").onclick = () => {
      this.cache.delete(this.current);
      this.load();
    };
    this.select(this.current);
  }
  activate(connected) {
    this.connected = connected;
    if (connected && !this.cache.has(this.current)) this.load();
    else if (!connected) this.status.textContent = "Client 未连接";
  }
  disconnect() {
    this.connected = false;
    this.cache.clear();
    this.body.replaceChildren();
    this.status.textContent = "Client 未连接";
  }
  select(id) {
    this.current = id;
    for (const tab of this.tabHost.children) tab.classList.toggle("active", tab.dataset.tab === id);
    const data = this.cache.get(id);
    if (data) this.render(data);
    else {
      this.body.innerHTML = /* HTML */ `<div class="manager-empty">尚未读取此项</div>`;
      if (this.connected) this.load();
    }
  }
  async load() {
    if (!this.connected) return;
    const id = this.current,
      title = tabs.find(([key]) => key === id)[1];
    this.status.textContent = `正在读取${title}…`;
    this.body.innerHTML = /* HTML */ `<div class="manager-empty">正在读取硬件信息…</div>`;
    try {
      const data = await this.query(id);
      this.cache.set(id, data);
      if (this.current === id) this.render(data);
    } catch (error) {
      if (this.current === id) {
        this.body.innerHTML = /* HTML */ `<div class="manager-empty"></div>`;
        this.body.firstElementChild.textContent = error.message;
        this.status.textContent = error.message;
      }
      this.notify(error);
    }
  }
  async query(id) {
    if (id === "processor") {
      const data = await this.call("/api/firmware/cpuid");
      return [
        {
          title: data.processorName || data.vendorId,
          fields: [
            ["制造商", data.vendorId],
            ["处理器", data.processorName],
            ["CPUID 记录", data.records.length],
            ["支持的功能", data.features.length],
            ...data.records
              .filter((record) => record.leaf === 1 && record.subLeaf === 0)
              .flatMap((record) => [
                ["签名", hex(record.eax)],
                ["特性 ECX", hex(record.ecx)],
                ["特性 EDX", hex(record.edx)],
              ]),
          ],
        },
      ];
    }
    if (id === "mainboard" || id === "memory") {
      const data = await this.call("/api/firmware/smbios");
      return data.structures
        .filter((record) => smbiosTypes[id].has(record.type))
        .map((record) => ({
          title: `Type ${record.type} · ${record.name}`,
          fields: [
            ["Handle", `0x${record.handle.toString(16).padStart(4, "0").toUpperCase()}`],
            ...record.fields.map((field) => [field.name, field.value]),
          ],
        }));
    }
    if (id === "display") {
      const records = await this.call("/api/system-details");
      return records
        .filter((record) => record.description === "显示器")
        .map((record) => ({
          title: record.name,
          fields: [
            ["设备标识", record.identity],
            ["当前模式", value(record)],
          ],
        }));
    }
    if (id === "storage") {
      const page = await this.call("/api/files", { path: "", enumerationId: null }),
        volumes = await Promise.allSettled(
          page.records.map((record) => this.call("/api/file/volume", { path: record.name })),
        );
      return volumes.map((result, index) => {
        const record = page.records[index];
        if (result.status === "rejected")
          return { title: record.name, fields: [["状态", result.reason?.message || result.reason]] };
        const volume = result.value;
        return {
          title: record.name,
          fields: [
            ["卷标", volume.label || "—"],
            ["文件系统", volume.fileSystem],
            ["容量", formatBytes(volume.totalBytes)],
            ["可用空间", formatBytes(volume.freeBytes)],
            ["已用空间", formatBytes(volume.totalBytes - volume.freeBytes)],
            ["卷序列号", Number(volume.serialNumber).toString(16).padStart(8, "0").toUpperCase()],
          ],
        };
      });
    }
    if (id === "network") {
      const records = await this.call("/api/network-adapters");
      return records.map((record) => recordCard(record));
    }
    const records = await this.call("/api/power");
    return records.map((record) => recordCard(record));
  }
  render(cards) {
    this.body.replaceChildren(
      ...cards.map((card) => {
        const section = document.createElement("section"),
          title = document.createElement("h2"),
          list = document.createElement("dl");
        section.className = "hardware-card";
        title.textContent = card.title || "硬件信息";
        list.className = "details-grid";
        for (const [name, text] of card.fields) {
          const term = document.createElement("dt"),
            description = document.createElement("dd");
          term.textContent = name;
          description.textContent = text === null || text === undefined || text === "" ? "—" : String(text);
          description.title = description.textContent;
          list.append(term, description);
        }
        section.append(title, list);
        return section;
      }),
    );
    if (!cards.length) this.body.innerHTML = /* HTML */ `<div class="manager-empty">没有可用信息</div>`;
    this.status.textContent = `${tabs.find(([id]) => id === this.current)[1]} · ${cards.length} 项`;
  }
}

function value(record) {
  return record.detail || record.name || record.description || record.value || "—";
}
function recordCard(record) {
  return {
    title: record.name || record.identity || "硬件",
    fields: [
      ["标识", record.identity],
      ["描述", record.description],
      ["详细信息", record.detail],
      ["状态", record.state],
      ["标志", hex(record.flags)],
      ["数值", record.value],
    ],
  };
}
function hex(value) {
  return `0x${Number(value).toString(16).toUpperCase().padStart(8, "0")}`;
}
function formatBytes(value) {
  value = Number(value);
  const units = ["B", "KB", "MB", "GB", "TB"];
  let index = 0;
  while (value >= 1024 && index < units.length - 1) {
    value /= 1024;
    index++;
  }
  return `${value.toFixed(index ? 2 : 0)} ${units[index]}`;
}
