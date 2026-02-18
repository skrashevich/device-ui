---
source: PlatformIO official docs (latest)
library: PlatformIO
package: platformio
topic: local library folders in lib_deps
fetched: 2026-02-18T00:00:00Z
official_docs: https://docs.platformio.org/en/latest/core/userguide/pkg/cmd_install.html#local-folder
---

Relevant sections from PlatformIO "Package Specifications" and `lib_deps` docs:

1) Local folder spec syntax

- `[<name>=]file://<folder>`
  - Copies files from local folder into Package Manager storage.
  - Changes in source folder do NOT affect installed package.
- `[<name>=]symlink://<folder>`
  - Creates symbolic link from Package Manager storage to source folder.
  - Changes in source folder DO affect installed package.

2) Alias naming behavior (`name = <source>`)

- PlatformIO supports prefixing a package spec with `<name>=` to override package folder name in Package Manager storage.
- This applies to local folder specs and other package sources.

3) Path support (absolute paths)

- Official examples use absolute paths:
  - `file:///local/path/to/the/package/dir` (Unix)
  - `symlink://C:/local/path/to/the/package/dir` (Windows)

4) Notes relevant to `lib_deps`

- `lib_deps` accepts Package Specifications used by `pio pkg install`.
- Local folder/package must contain manifest (`library.json`, `platform.json`, or `package.json`) with `name` and `version`.
- For shared dependencies across environments, docs recommend using common `[env]` or interpolation.

5) Single-environment override pattern (from env/interpolation + lib_ignore usage)

- Keep common dependencies in `[env]`.
- In one target environment, add `lib_ignore` (library name, not folder name) to exclude the common one.
- Add replacement source in that environment's `lib_deps`.

Official pages used:
- https://docs.platformio.org/en/latest/projectconf/sections/env/options/library/lib_deps.html
- https://docs.platformio.org/en/latest/core/userguide/pkg/cmd_install.html#local-folder
