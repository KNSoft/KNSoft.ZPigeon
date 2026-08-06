const associations = new Map();

function register(suffixes, icon, typeKey, contextMenu = []) {
  const value = Object.freeze({ icon, typeKey, contextMenu: Object.freeze(contextMenu) });
  for (const suffix of suffixes) associations.set(suffix, value);
}

register(
  [
    ".txt",
    ".md",
    ".log",
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hpp",
    ".inl",
    ".asm",
    ".inc",
    ".cs",
    ".vb",
    ".fs",
    ".java",
    ".kt",
    ".py",
    ".rb",
    ".php",
    ".lua",
    ".go",
    ".rs",
    ".sql",
    ".html",
    ".htm",
    ".css",
    ".scss",
    ".less",
    ".mjs",
    ".ts",
    ".jsx",
    ".tsx",
    ".vue",
    ".svelte",
    ".ini",
    ".cfg",
    ".conf",
    ".config",
    ".properties",
    ".env",
    ".editorconfig",
    ".gitignore",
    ".gitattributes",
    ".sln",
    ".slnx",
    ".csproj",
    ".fsproj",
    ".vcxproj",
    ".props",
    ".targets",
    ".nuspec",
    ".manifest",
  ],
  "📄",
  "file.type.text",
  ["preview-text"],
);
register([".json", ".xml", ".csv"], "🧾", "file.type.structured", [
  "preview-text",
  "view-structured",
]);
register([".yaml", ".yml", ".toml"], "🧾", "file.type.structured", ["preview-text"]);
register(
  [".cmd", ".bat", ".ps1", ".vbs", ".js", ".wsf", ".hta", ".py", ".pyw", ".go", ".mjs", ".cjs"],
  "📜",
  "file.type.script",
  ["preview-text", "run"],
);
register([".inf"], "⚙️", "file.type.setupInformation", ["preview-text", "install-inf"]);
register([".reg"], "⚙️", "file.type.registry", ["preview-text", "import-reg"]);
register([".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx"], "📑", "file.type.office");
register([".pdf"], "📑", "file.type.office", ["preview-pdf"]);
register(
  [
    ".jpg",
    ".jpeg",
    ".jfif",
    ".png",
    ".gif",
    ".webp",
    ".avif",
    ".bmp",
    ".tif",
    ".tiff",
    ".svg",
    ".heic",
    ".ico",
  ],
  "🖼️",
  "file.type.image",
  ["preview-image"],
);
register([".mp4", ".mkv", ".avi", ".mov", ".wmv", ".webm", ".m4v", ".mpeg", ".mpg"], "🎞️", "file.type.video", [
  "preview-video",
]);
register([".mp3", ".wav", ".flac", ".aac", ".m4a", ".ogg", ".opus", ".wma"], "🎵", "file.type.audio", [
  "preview-audio",
]);
register(
  [
    ".tar.gz",
    ".tar.bz2",
    ".tar.xz",
    ".tgz",
    ".tbz2",
    ".txz",
    ".zip",
    ".7z",
    ".rar",
    ".tar",
    ".gz",
    ".bz2",
    ".xz",
    ".cab",
  ],
  "🗜️",
  "file.type.archive",
  ["browse-archive"],
);
register(
  [".msi", ".msix", ".appx", ".appxbundle", ".msixbundle", ".appinstaller"],
  "📦",
  "file.type.installer",
  ["install-package"],
);
register([".exe", ".com"], "⚙️", "file.type.executable", ["run"]);
register([".dll", ".sys", ".drv", ".ocx"], "⚙️", "file.type.system");
register([".lnk", ".url"], "🔗", "file.type.shortcut", ["view-target", "open-target"]);
register([".ttf", ".otf", ".ttc", ".fon"], "🔤", "file.type.font", ["preview-font", "install-font"]);
register([".cer", ".crt"], "🔐", "file.type.certificate", ["view-certificate", "install-certificate"]);
register([".pfx", ".p12"], "🔐", "file.type.certificate", ["install-certificate"]);

const suffixes = [...associations.keys()].sort((left, right) => right.length - left.length);

export function fileAssociation(name) {
  const value = name.toLowerCase();
  const suffix = suffixes.find((candidate) => value.endsWith(candidate));
  return suffix ? associations.get(suffix) : null;
}

export const contextMenuItems = Object.freeze({
  "preview-text": "file.menu.previewText",
  "view-structured": "file.menu.viewStructured",
  run: "file.menu.run",
  "install-inf": "file.menu.installInf",
  "preview-image": "file.menu.previewImage",
  "preview-pdf": "file.menu.previewPdf",
  "preview-video": "file.menu.playVideo",
  "preview-audio": "file.menu.playAudio",
  "browse-archive": "file.menu.browseArchive",
  "install-package": "file.menu.installPackage",
  "view-target": "file.menu.viewTarget",
  "open-target": "file.menu.openTarget",
  "import-reg": "file.menu.importRegistry",
  "preview-font": "file.menu.previewFont",
  "install-font": "file.menu.installFont",
  "view-certificate": "file.menu.viewCertificate",
  "install-certificate": "file.menu.installCertificate",
});
