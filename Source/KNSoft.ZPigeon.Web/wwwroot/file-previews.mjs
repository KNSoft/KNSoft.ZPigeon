import { apiUrl, postBinary } from "./client-context.mjs";
import { fileAssociation } from "./file-associations.mjs";
import { t } from "./i18n.mjs";

const TEXT_PAGE_SIZE = 0x10000;
const STRUCTURED_LIMIT = 0x800000;

export class FilePreview {
  constructor(notify) {
    this.notify = notify;
    this.request = 0;
    this.dialog = document.createElement("dialog");
    this.dialog.className = "file-preview-dialog";
    this.dialog.innerHTML = /* HTML */ `<form method="dialog">
      <header><h2></h2><span class="spacer"></span><button value="close">${t("common.close")}</button></header>
      <div class="file-preview-toolbar"></div>
      <div class="file-preview-content"></div>
    </form>`;
    document.body.append(this.dialog);
    this.title = this.dialog.querySelector("h2");
    this.toolbar = this.dialog.querySelector(".file-preview-toolbar");
    this.content = this.dialog.querySelector(".file-preview-content");
    this.dialog.onclose = () => this.clear();
  }

  clear() {
    this.media?.pause();
    this.media = null;
    if (this.font) document.fonts.delete(this.font);
    this.font = null;
    if (this.objectUrl) URL.revokeObjectURL(this.objectUrl);
    this.objectUrl = null;
    this.request++;
    this.toolbar.replaceChildren();
    this.content.replaceChildren();
  }

  show(name) {
    this.clear();
    this.title.textContent = name;
    if (!this.dialog.open) this.dialog.showModal();
  }

  async text(path, name) {
    this.show(name);
    const request = ++this.request,
      encoding = document.createElement("select"),
      previous = document.createElement("button"),
      next = document.createElement("button"),
      status = document.createElement("span"),
      output = document.createElement("pre");
    for (const [label, value] of [
      [t("file.preview.autoEncoding"), "auto"],
      ["UTF-8", "utf-8"],
      ["UTF-16 LE", "utf-16le"],
      ["UTF-16 BE", "utf-16be"],
      ["GB18030", "gb18030"],
      ["Big5", "big5"],
      ["Windows-1252", "windows-1252"],
      ["Shift_JIS", "shift_jis"],
    ])
      encoding.add(new Option(label, value));
    previous.type = next.type = "button";
    previous.textContent = t("file.preview.previousPage");
    next.textContent = t("file.preview.nextPage");
    status.className = "status";
    output.className = "file-text-preview";
    this.toolbar.append(encoding, previous, next, status);
    this.content.append(output);
    let offset = 0n,
      size = 0n,
      data;
    const render = async () => {
      previous.disabled = true;
      next.disabled = true;
      status.textContent = t("common.fetching");
      try {
        const result = await postBinary("/api/file/range", {
          path,
          offset: String(offset),
          length: TEXT_PAGE_SIZE,
        });
        if (request !== this.request) return;
        data = result.data;
        size = BigInt(result.size);
        output.textContent = decodeText(data, encoding.value);
        previous.disabled = offset === 0n;
        next.disabled = offset + BigInt(data.length) >= size;
        status.textContent = t("file.preview.pageStatus", {
          current: String(offset / BigInt(TEXT_PAGE_SIZE) + 1n),
          total: String(size === 0n ? 1n : (size + BigInt(TEXT_PAGE_SIZE - 1)) / BigInt(TEXT_PAGE_SIZE)),
        });
      } catch (error) {
        if (request === this.request) {
          status.textContent = error.message;
          this.notify(error);
        }
      }
    };
    previous.onclick = () => {
      offset = offset < BigInt(TEXT_PAGE_SIZE) ? 0n : offset - BigInt(TEXT_PAGE_SIZE);
      render();
    };
    next.onclick = () => {
      offset += BigInt(TEXT_PAGE_SIZE);
      render();
    };
    encoding.onchange = () => {
      if (data) output.textContent = decodeText(data, encoding.value);
    };
    await render();
  }

  async structured(path, name, size) {
    if (BigInt(size) > BigInt(STRUCTURED_LIMIT)) {
      this.notify(t("file.preview.structuredTooLarge"));
      return;
    }
    this.show(name);
    const request = ++this.request;
    this.content.textContent = t("common.fetching");
    try {
      const response = await fetch(contentUrl(path), { headers: { Range: `bytes=0-${STRUCTURED_LIMIT}` } });
      if (!response.ok) throw new Error((await response.text()) || `HTTP ${response.status}`);
      const data = new Uint8Array(await response.arrayBuffer());
      if (request !== this.request) return;
      if (data.length > STRUCTURED_LIMIT) throw new Error(t("file.preview.structuredTooLarge"));
      const text = decodeText(data, "auto"),
        suffix = extension(name);
      this.content.replaceChildren(
        suffix === ".json" ? renderJson(JSON.parse(text)) : suffix === ".xml" ? renderXml(text) : renderCsv(text),
      );
    } catch (error) {
      if (request === this.request) {
        this.content.textContent = error.message;
        this.notify(error);
      }
    }
  }

  async image(path, name) {
    this.show(name);
    const request = ++this.request,
      quality = document.createElement("select"),
      status = document.createElement("span"),
      image = document.createElement("img");
    for (const [label, value] of [
      [t("file.preview.imageLow"), "1"],
      [t("file.preview.imageMedium"), "2"],
      [t("file.preview.imageHigh"), "3"],
      [t("file.preview.imageOriginal"), "original"],
    ])
      quality.add(new Option(label, value));
    if ([".gif", ".svg"].includes(extension(name))) quality.value = "original";
    status.className = "status";
    image.className = "file-image-preview";
    image.alt = name;
    image.onerror = () => this.notify(t("file.preview.unsupportedImage"));
    this.toolbar.append(quality, status);
    this.content.append(image);
    const load = async () => {
      const current = quality.value;
      status.textContent = t("common.fetching");
      try {
        let objectUrl = null,
          url;
        if (current === "original") url = contentUrl(path);
        else {
          const { data } = await postBinary("/api/file/image-preview", { path, quality: Number(current) });
          if (request !== this.request || quality.value !== current) return;
          objectUrl = URL.createObjectURL(new Blob([data], { type: "image/jpeg" }));
          url = objectUrl;
        }
        if (request !== this.request || quality.value !== current) {
          if (objectUrl) URL.revokeObjectURL(objectUrl);
          return;
        }
        if (this.objectUrl) URL.revokeObjectURL(this.objectUrl);
        this.objectUrl = objectUrl;
        image.src = url;
        status.textContent = "";
      } catch (error) {
        if (request === this.request && quality.value === current) {
          status.textContent = error.message;
          this.notify(error);
        }
      }
    };
    quality.onchange = load;
    await load();
  }

  pdf(path, name) {
    this.show(name);
    const frame = document.createElement("iframe");
    frame.className = "file-pdf-preview";
    frame.title = name;
    frame.src = contentUrl(path);
    this.content.append(frame);
  }

  media(path, name, video) {
    this.show(name);
    const media = document.createElement(video ? "video" : "audio");
    media.className = video ? "file-video-preview" : "file-audio-preview";
    media.controls = true;
    media.preload = "metadata";
    media.src = contentUrl(path);
    media.onerror = () => this.notify(t("file.preview.unsupportedMedia"));
    this.media = media;
    this.content.append(media);
  }

  async fontPreview(path, name) {
    this.show(name);
    const request = ++this.request,
      sample = document.createElement("div");
    sample.className = "file-font-preview";
    sample.textContent = t("file.preview.fontSample");
    this.content.append(sample);
    try {
      const font = new FontFace("ZPigeonFilePreview", `url(${JSON.stringify(String(contentUrl(path)))})`);
      await font.load();
      if (request !== this.request) return;
      document.fonts.add(font);
      this.font = font;
      sample.style.fontFamily = "ZPigeonFilePreview";
    } catch (error) {
      if (request === this.request) {
        sample.textContent = error.message;
        this.notify(error);
      }
    }
  }

  archive(path, name, call) {
    this.show(name);
    const request = ++this.request,
      table = document.createElement("table"),
      head = document.createElement("thead"),
      body = document.createElement("tbody"),
      more = document.createElement("button"),
      status = document.createElement("span");
    table.className = "file-archive-preview";
    head.innerHTML = `<tr><th class="file-icon"></th><th>${t("common.name")}</th><th>${t("common.type")}</th><th>${t(
      "file.modified",
    )}</th><th>${t("file.size")}</th></tr>`;
    table.append(head, body);
    more.type = "button";
    more.textContent = t("file.loadMore");
    status.className = "status";
    this.toolbar.append(more, status);
    this.content.append(table);
    let enumerationId = "0",
      count = 0;
    const load = async () => {
      more.disabled = true;
      status.textContent = t("common.fetching");
      try {
        const page = await call("/api/file/archive", {
          path: enumerationId === "0" ? path : null,
          enumerationId,
        });
        if (request !== this.request) return;
        for (const item of page.records) {
          const row = document.createElement("tr"),
            directory = !!(item.attributes & 0x10),
            association = directory ? null : fileAssociation(item.name);
          row.innerHTML = "<td></td><td></td><td></td><td></td><td></td>";
          row.children[0].className = "file-icon";
          row.children[0].textContent = directory ? "📁" : association?.icon || "";
          row.children[1].textContent = item.name;
          row.children[2].textContent = directory ? t("file.type.folder") : association ? t(association.typeKey) : "";
          row.children[3].textContent = item.lastWriteTime ? new Date(item.lastWriteTime).toLocaleString() : "";
          row.children[4].textContent = directory ? "" : formatBytes(item.size);
          body.append(row);
        }
        count += page.records.length;
        enumerationId = String(page.enumerationId);
        more.hidden = !page.enumerationId;
        more.disabled = false;
        status.textContent = t("file.archive.entries", { count });
      } catch (error) {
        if (request === this.request) {
          status.textContent = error.message;
          this.notify(error);
        }
      }
    };
    more.onclick = load;
    load();
  }

  target(name, value) {
    this.show(name);
    const label = document.createElement("strong"),
      target = document.createElement("code"),
      copy = document.createElement("button");
    label.textContent = t("file.shortcut.target");
    target.className = "file-shortcut-target";
    target.textContent = value;
    copy.type = "button";
    copy.textContent = t("common.copy");
    copy.onclick = () => navigator.clipboard.writeText(value);
    this.toolbar.append(copy);
    this.content.append(label, target);
  }

  certificate(name, value) {
    this.show(name);
    const list = document.createElement("dl"),
      fields = [
        [t("file.certificate.subject"), value.subject],
        [t("file.certificate.issuer"), value.issuer],
        [t("file.certificate.validFrom"), new Date(value.notBefore).toLocaleString()],
        [t("file.certificate.validTo"), new Date(value.notAfter).toLocaleString()],
        [t("file.certificate.serialNumber"), value.serialNumber],
        [t("file.certificate.thumbprint"), value.thumbprint],
        [t("file.certificate.signatureAlgorithm"), value.signatureAlgorithm],
        [t("file.certificate.publicKeyAlgorithm"), value.publicKeyAlgorithm],
        ...value.extensions.map((extension) => [
          extension.name || extension.oid,
          `${extension.critical ? `${t("file.certificate.critical")}; ` : ""}${extension.value}`,
        ]),
      ];
    list.className = "file-certificate-preview";
    for (const [term, description] of fields) {
      const dt = document.createElement("dt"),
        dd = document.createElement("dd");
      dt.textContent = term;
      dd.textContent = description || "—";
      list.append(dt, dd);
    }
    this.content.append(list);
  }
}

function contentUrl(path) {
  const url = apiUrl("/api/file/content");
  url.searchParams.set("path", path);
  return url;
}

function extension(name) {
  const index = name.lastIndexOf(".");
  return index < 0 ? "" : name.slice(index).toLowerCase();
}

function decodeText(data, encoding) {
  if (encoding !== "auto") return encoding === "utf-8" ? decodeUtf8Page(data) : new TextDecoder(encoding).decode(data);
  if (data[0] === 0xef && data[1] === 0xbb && data[2] === 0xbf) return decodeUtf8Page(data.subarray(3));
  if (data[0] === 0xff && data[1] === 0xfe) return new TextDecoder("utf-16le").decode(data.subarray(2));
  if (data[0] === 0xfe && data[1] === 0xff) return new TextDecoder("utf-16be").decode(data.subarray(2));
  const pairs = Math.min(data.length - (data.length % 2), 4096);
  let evenNulls = 0,
    oddNulls = 0;
  for (let index = 0; index < pairs; index += 2) {
    if (data[index] === 0) evenNulls++;
    if (data[index + 1] === 0) oddNulls++;
  }
  if (oddNulls > pairs / 8) return new TextDecoder("utf-16le").decode(data);
  if (evenNulls > pairs / 8) return new TextDecoder("utf-16be").decode(data);
  try {
    return decodeUtf8Page(data, true);
  } catch {}
  return new TextDecoder("windows-1252").decode(data);
}

function decodeUtf8Page(data, fatal = false) {
  let start = 0;
  while (start < Math.min(3, data.length) && (data[start] & 0xc0) === 0x80) start++;
  return new TextDecoder("utf-8", { fatal }).decode(data.subarray(start), { stream: true });
}

function renderJson(value) {
  const root = document.createElement("div");
  root.className = "file-structured-preview";
  let count = 0;
  const render = (item, key, depth) => {
    if (++count > 20000 || depth > 100) throw new Error(t("file.preview.structureTooComplex"));
    if (item !== null && typeof item === "object") {
      const details = document.createElement("details"),
        summary = document.createElement("summary"),
        entries = Object.entries(item);
      details.open = depth < 2;
      summary.textContent = `${key}${Array.isArray(item) ? ` [${entries.length}]` : ` {${entries.length}}`}`;
      details.append(summary, ...entries.map(([name, child]) => render(child, name, depth + 1)));
      return details;
    }
    const row = document.createElement("div");
    row.className = "file-structure-value";
    row.textContent = `${key}: ${JSON.stringify(item)}`;
    return row;
  };
  root.append(render(value, "$", 0));
  return root;
}

function renderXml(text) {
  const documentValue = new DOMParser().parseFromString(text, "application/xml"),
    error = documentValue.querySelector("parsererror");
  if (error) throw new Error(error.textContent);
  const root = document.createElement("div");
  root.className = "file-structured-preview";
  let count = 0;
  const render = (node, depth) => {
    if (++count > 20000 || depth > 100) throw new Error(t("file.preview.structureTooComplex"));
    const details = document.createElement("details"),
      summary = document.createElement("summary"),
      attributes = [...node.attributes].map((value) => `${value.name}=${JSON.stringify(value.value)}`).join(" "),
      children = [...node.children];
    details.open = depth < 2;
    summary.textContent = `<${node.tagName}${attributes ? ` ${attributes}` : ""}>`;
    details.append(summary);
    const content = [...node.childNodes]
      .filter((child) => child.nodeType === Node.TEXT_NODE)
      .map((child) => child.textContent.trim())
      .filter(Boolean)
      .join(" ");
    if (content) {
      const value = document.createElement("div");
      value.className = "file-structure-value";
      value.textContent = content;
      details.append(value);
    }
    details.append(...children.map((child) => render(child, depth + 1)));
    return details;
  };
  root.append(render(documentValue.documentElement, 0));
  return root;
}

function renderCsv(text) {
  const rows = parseCsv(text),
    table = document.createElement("table"),
    body = document.createElement("tbody");
  table.className = "file-csv-preview";
  for (const values of rows) {
    const row = document.createElement("tr");
    for (const value of values) row.insertCell().textContent = value;
    body.append(row);
  }
  table.append(body);
  return table;
}

function parseCsv(text) {
  const rows = [];
  let row = [],
    field = "",
    quoted = false;
  for (let index = 0; index < text.length; index++) {
    const character = text[index];
    if (quoted) {
      if (character !== '"') field += character;
      else if (text[index + 1] === '"') {
        field += '"';
        index++;
      } else quoted = false;
    } else if (character === '"' && field.length === 0) quoted = true;
    else if (character === ",") {
      row.push(field);
      field = "";
    } else if (character === "\r" || character === "\n") {
      if (character === "\r" && text[index + 1] === "\n") index++;
      row.push(field);
      rows.push(row);
      if (rows.length > 5000 || row.length > 256) throw new Error(t("file.preview.structureTooComplex"));
      row = [];
      field = "";
    } else field += character;
  }
  if (quoted) throw new Error(t("file.preview.invalidCsv"));
  if (field || row.length) {
    row.push(field);
    rows.push(row);
    if (rows.length > 5000 || row.length > 256) throw new Error(t("file.preview.structureTooComplex"));
  }
  return rows;
}

function formatBytes(value) {
  let size = Number(value);
  if (size < 1024) return `${size} B`;
  const units = ["KB", "MB", "GB", "TB"];
  let index = -1;
  do {
    size /= 1024;
    index++;
  } while (size >= 1024 && index < units.length - 1);
  return `${size.toFixed(size < 10 ? 2 : 1)} ${units[index]}`;
}
