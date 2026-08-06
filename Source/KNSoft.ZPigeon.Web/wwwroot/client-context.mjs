import { t } from "./i18n.mjs";

const value = new URL(location.href).searchParams.get("client");

if (!value || !/^[1-9]\d*$/.test(value)) {
  location.replace("/");
  throw new Error(t("error.clientIdentifier"));
}

export const clientId = value;

export function apiUrl(input) {
  const url = new URL(input, location.href);
  url.searchParams.set("client", clientId);
  return url;
}

async function request(input, init) {
  let response;
  try {
    response = await fetch(apiUrl(input), init);
  } catch {
    throw new Error(t("error.noHttpResponse"));
  }
  if (response.ok) return response;
  let text;
  try {
    text = await response.text();
  } catch {
    throw new Error(t("error.incompleteHttpResponse"));
  }
  let error;
  try {
    error = JSON.parse(text);
  } catch {}
  const typed =
    Number.isInteger(error?.type) && Number.isInteger(error?.code)
      ? t("error.typed", {
          type: error.type,
          code: error.code.toString(16).padStart(8, "0").toUpperCase(),
        })
      : "";
  throw new Error(error?.message || typed || text || `HTTP ${response.status}`);
}

export async function postJson(input, body) {
  const response = await request(input, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: body == null ? null : JSON.stringify(body),
  });
  let text;
  try {
    text = await response.text();
  } catch {
    throw new Error(t("error.incompleteHttpResponse"));
  }
  return text ? JSON.parse(text) : t("common.success");
}

export async function postBinary(input, body) {
  const response = await request(input, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: body == null ? null : JSON.stringify(body),
    }),
    data = new Uint8Array(await response.arrayBuffer()),
    size = response.headers.get("X-ZPigeon-Size"),
    type = response.headers.get("X-ZPigeon-Type");
  return { size: size ?? String(data.length), data, type: type == null ? undefined : Number(type) };
}

export async function postRecords(input, body) {
  const response = await request(input, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: body == null ? null : JSON.stringify(body),
    }),
    data = new Uint8Array(await response.arrayBuffer());
  if (data.length < 4) throw new Error(t("error.incompleteRecordResponse"));
  const metadataLength = new DataView(data.buffer, data.byteOffset, 4).getUint32(0, true);
  if (metadataLength > data.length - 4) throw new Error(t("error.incompleteRecordMetadata"));
  return {
    metadata: JSON.parse(new TextDecoder().decode(data.subarray(4, 4 + metadataLength))),
    data: data.subarray(4 + metadataLength),
  };
}

export async function postData(input, metadata, data) {
  const form = new FormData();
  form.set("metadata", JSON.stringify(metadata));
  form.set("data", new Blob([data], { type: "application/octet-stream" }), "data.bin");
  await request(input, { method: "POST", body: form });
}
