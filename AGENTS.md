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
- CRITICAL: Do not add any unnecessary code or fallback logic, including initializing output parameters.
- CRITICAL: Security and efficiency are the highest priorities; keep code and abstractions minimal.
- CRITICAL: Target Windows 10 and later. Prefer modern platform capabilities that improve security, efficiency, or user experience; do not carry compatibility or fallback burden for older systems.
- CRITICAL: Prefer KNSoft.NDK and NT-layer system interfaces over unnecessary high-level wrappers, and reuse KNSoft.MakeLifeEasier instead of duplicating common functionality.
- CRITICAL: KNSoft.MakeLifeEasier may be extended when a helper has independent common-library value, but ask the Owner before modifying it or updating its package.
- Keep diffs minimal; do not refactor unrelated code.
- Use concise, technical comments only when needed.

## Rules
- Some files are auto-generated and usually end with `.g.*` (for example, `I18N.xml.g.c` and `I18N.xml.g.h`); do not modify them manually.
- The output directory is usually named `OutDir` and is located next to the solution file; the exact path depends on `.props` files and project settings.

## Tool
- You can use Visual Studio and the Windows SDK when needed.

## Build

- Use `msbuild` to build the entire solution (`*.sln`, `*.slnx`).
