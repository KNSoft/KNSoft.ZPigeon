import { postData } from "./client-context.mjs";
import { t } from "./i18n.mjs";

export class CertificateInstaller {
  constructor({ call, notify }) {
    this.call = call;
    this.notify = notify;
    this.input = document.createElement("input");
    this.input.type = "file";
    this.input.accept = ".cer,.crt,.der,.pem,.pfx,.p12";
    this.input.hidden = true;
    this.dialog = document.createElement("dialog");
    this.dialog.className = "certificate-install";
    this.dialog.innerHTML = /* HTML */ `<form>
      <h2>${t("certificate.install.title")}</h2>
      <label>${t("certificate.install.file")}<input data-role="file" readonly /></label>
      <fieldset>
        <legend>${t("certificate.install.scope")}</legend>
        <label
          ><input type="radio" name="certificate-scope" value="user" checked />${t(
            "certificate.scope.currentUser",
          )}</label
        ><label
          ><input type="radio" name="certificate-scope" value="machine" />${t(
            "certificate.scope.localMachine",
          )}</label
        >
      </fieldset>
      <label
        >${t("certificate.install.store")}<select data-role="store" required></select
      ></label>
      <section data-role="pfx" hidden>
        <label
          >${t("certificate.install.password")}<input
            data-role="password"
            type="password"
            maxlength="1024"
            autocomplete="new-password"
        /></label>
        <label
          ><input data-role="exportable" type="checkbox" />${t("certificate.install.exportable")}</label
        >
      </section>
      <p class="property-note">${t("certificate.install.warning")}</p>
      <p class="status" data-role="status"></p>
      <div class="dialog-actions">
        <button type="button" data-action="cancel">${t("common.cancel")}</button
        ><button data-action="install">${t("certificate.install.action")}</button>
      </div>
    </form>`;
    document.body.append(this.input, this.dialog);
    this.form = this.dialog.querySelector("form");
    this.store = this.dialog.querySelector("[data-role=store]");
    this.password = this.dialog.querySelector("[data-role=password]");
    this.exportable = this.dialog.querySelector("[data-role=exportable]");
    this.status = this.dialog.querySelector("[data-role=status]");
    this.installButton = this.dialog.querySelector("[data-action=install]");
    this.input.onchange = () => {
      const file = this.input.files[0];
      if (file) this.open({ kind: "upload", file, name: file.name }, this.initialStore);
    };
    for (const input of this.dialog.querySelectorAll("[name=certificate-scope]"))
      input.onchange = () => this.renderStores();
    this.dialog.querySelector("[data-action=cancel]").onclick = () => this.dialog.close();
    this.form.onsubmit = (event) => {
      event.preventDefault();
      this.install();
    };
    this.dialog.addEventListener("close", () => {
      this.password.value = "";
      this.source = null;
    });
  }

  setStores(records) {
    this.stores = records.filter((record) => record.kind === 20);
  }

  invalidate() {
    this.stores = null;
  }

  chooseFile(initialStore) {
    this.initialStore = initialStore;
    this.input.value = "";
    this.input.click();
  }

  installFile(path, name) {
    return this.open({ kind: "path", path, name });
  }

  async open(source, initialStore) {
    try {
      if (!this.stores) this.setStores(await this.call("/api/certificates/stores"));
      this.source = source;
      this.dialog.querySelector("[data-role=file]").value = source.kind === "path" ? source.path : source.name;
      const pfx = /\.(?:pfx|p12)$/i.test(source.name);
      this.dialog.querySelector("[data-role=pfx]").hidden = !pfx;
      this.password.value = "";
      this.exportable.checked = false;
      this.status.textContent = "";
      this.form.querySelector("fieldset").disabled = false;
      this.store.disabled = false;
      this.installButton.disabled = false;
      const scope = initialStore?.identity.startsWith("machine\n") ? "machine" : "user";
      this.dialog.querySelector(`[name=certificate-scope][value=${scope}]`).checked = true;
      this.renderStores(initialStore?.identity);
      this.dialog.showModal();
      this.store.focus();
    } catch (error) {
      this.notify(error);
    }
  }

  renderStores(selectedIdentity) {
    const scope = this.dialog.querySelector("[name=certificate-scope]:checked").value,
      values = this.stores
        .filter((store) => !(store.flags & 1) && store.identity.startsWith(`${scope}\n`))
        .sort((left, right) => certificateStoreName(left.name).localeCompare(certificateStoreName(right.name)));
    this.store.replaceChildren(
      new Option(t("certificate.install.selectStore"), ""),
      ...values.map((store) => new Option(certificateStoreName(store.name), store.identity)),
    );
    this.store.value =
      selectedIdentity && values.some((store) => store.identity === selectedIdentity) ? selectedIdentity : "";
  }

  async install() {
    if (!this.source || !this.store.value) return;
    const source = this.source,
      pfx = /\.(?:pfx|p12)$/i.test(source.name),
      metadata = {
        storeIdentity: this.store.value,
        password: pfx ? this.password.value : null,
        exportable: pfx && this.exportable.checked,
      };
    this.form.querySelector("fieldset").disabled = true;
    this.store.disabled = true;
    this.installButton.disabled = true;
    this.status.textContent = t("certificate.install.installing");
    try {
      if (source.kind === "path")
        await this.call("/api/certificates/install-file", { ...metadata, path: source.path });
      else {
        const data = new Uint8Array(await source.file.arrayBuffer());
        try {
          await postData("/api/certificates/install", metadata, data);
        } finally {
          data.fill(0);
        }
      }
      this.password.value = "";
      this.dialog.close();
      this.notify(t("certificate.install.complete"));
      await this.installed?.();
    } catch (error) {
      this.status.textContent = error.message;
      this.notify(error);
    } finally {
      if (this.dialog.open) {
        this.form.querySelector("fieldset").disabled = false;
        this.store.disabled = false;
        this.installButton.disabled = false;
      }
    }
  }
}

export function certificateStoreName(value) {
  const key = `certificate.store.${value.toLocaleLowerCase()}`,
    name = t(key);
  return name === key ? value : name;
}
