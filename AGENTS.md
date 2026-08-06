# AGENTS.md

## Scope
- Applies to the entire repository.

## Quick Start (Read First)
- `README.md`
- Other `AGENTS.md` files in subdirectories

## Hard Rules
- CRITICAL: Follow `.editorconfig`, including SAL annotations conventions.
- CRITICAL: Preserve original file encoding (usually UTF8 or UTF8-BOM) and line-ending style (usually CRLF) when editing files.
- CRITICAL: Keep diffs minimal when you touch Visual Studio project files (*.sln, *.slnx, *.vcxproj, *.props, *.targets, ...).
- CRITICAL: Build the full solution with Visual Studio 2026/MSBuild 18. Visual Studio 2022/MSBuild 17 is incompatible
  with the repository's .NET 10 toolchain; do not use or hard-code its MSBuild path, and do not split native and
  managed builds to work around that version mismatch.
- CRITICAL: Do not add any unnecessary code or fallback logic, including initializing output parameters.
- CRITICAL: Security and efficiency are the highest priorities; keep code and abstractions minimal.
- CRITICAL: Target Windows 10 and later. Prefer modern platform capabilities that improve security, efficiency, or user experience; do not carry compatibility or fallback burden for older systems.
- CRITICAL: Prefer KNSoft.NDK and NT-layer system interfaces over unnecessary high-level wrappers, and reuse KNSoft.MakeLifeEasier instead of duplicating common functionality.
- CRITICAL: KNSoft.MakeLifeEasier may be extended when a helper has independent common-library value. Ask the Owner
  before modifying it or its package; after approval, implement and validate the helper in the parent repository before
  updating the package, and do not duplicate equivalent shared code in ZPigeon.
- Keep diffs minimal; do not refactor unrelated code.
- Use concise, technical comments only when needed.

## Rules
- Some files are auto-generated and usually end with `.g.*` (for example, `I18N.xml.g.c` and `I18N.xml.g.h`); do not modify them manually.
- The output directory is usually named `OutDir` and is located next to the solution file; the exact path depends on `.props` files and project settings.

## Tool
- You can use Visual Studio and the Windows SDK when needed.

## Build

- Resolve the Visual Studio 2026 installation with `vswhere` and use its `MSBuild\Current\Bin\MSBuild.exe` to build
  the entire solution (`*.sln`, `*.slnx`).
