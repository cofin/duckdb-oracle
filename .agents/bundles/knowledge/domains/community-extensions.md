---
type: Domain
title: DuckDB Community Extensions Registry, Multi-Version Distribution & Invariants
description: Architecture of the official DuckDB community extensions registry, multi-track version routing (ref vs variegata), zero-compile-time-dependency packaging, and compatibility matrix mechanics.
scope: domain
domain: duckdb
tags: [duckdb, community-extensions, distribution, ci-cd, versioning]
status: stable
---

# DuckDB Community Extensions Registry, Multi-Version Distribution & Invariants

DuckDB extensions distributed via the official community repository ([duckdb/community-extensions](https://github.com/duckdb/community-extensions)) are built from source on DuckDB's automated CI/CD runners, signed, and published to `https://community-extensions.duckdb.org`.

## Community Extension Descriptor (`description.yml`)

The community registry tracks extensions via individual YAML descriptors under `extensions/<extension_name>/description.yml`.

```yaml
extension:
  name: oracle
  description: Connect DuckDB directly to Oracle databases for reading and writing tables, with catalog integration, pushdown, spatial geometry, and vector support
  version: 0.3.1
  language: C++
  build: cmake
  license: MIT
  excluded_platforms: "wasm_mvp;wasm_eh;wasm_threads;windows_amd64_mingw;windows_arm64;linux_amd64_musl;linux_arm64_musl;osx_amd64"
  maintainers:
    - cofin

repo:
  github: cofin/duckdb-oracle
  ref: fa61951bdd3edf18bb6bf8e7169255d5ca6de635
  variegata: 8454f9cce7cc6a82dbebac08869bcbf87812f046
```

### Key Field Semantics

1. **`extension.version`**: The semver version of the extension package. Must match the tag and internal `EXTENSION_VERSION` string.
2. **`extension.excluded_platforms`**: Semicolon-delimited list of target platforms to skip during distribution builds. For `oracle`:
   - `wasm_mvp`, `wasm_eh`, `wasm_threads`: WebAssembly lacks dynamic socket and C ABI library loading (`dlopen`).
   - `windows_amd64_mingw`: DuckDB Windows releases use MSVC toolchains; MinGW is unsupported for OCI DLL bindings.
   - `windows_arm64`: Oracle Instant Client currently lacks official Windows ARM64 native binaries.
   - `linux_amd64_musl`, `linux_arm64_musl`: Alpine/musl toolchains lack glibc symbol parity required by Oracle Instant Client.
   - `osx_amd64`: Deprecated legacy x86_64 macOS runners.
3. **`repo.ref`**: The active git commit hash used for building the current default/stable DuckDB release (DuckDB `2.0.0` LTS "Cyanoptera").
4. **`repo.variegata`**: The git commit hash used for building DuckDB `1.5.6` ("Variegata").

---

## Multi-Track Version Routing in `scripts/build.py`

The upstream community build orchestrator (`scripts/build.py`) inspects the target DuckDB version and maps to the appropriate repository reference:

```python
if os.environ['DUCKDB_VERSION'] == 'v1.4.5':
    if 'andium' in desc['repo']:
        extension_ref = desc['repo']['andium']
elif os.environ['DUCKDB_VERSION'] == 'v1.5.6':
    if 'variegata' in desc['repo']:
        extension_ref = desc['repo']['variegata']
elif os.environ['DUCKDB_VERSION'] != os.environ['DUCKDB_LATEST_STABLE']:
    if desc['repo'].get('ref_next'):
        extension_ref = desc['repo']['ref_next']
```

### Critical Rules
- **Exact Git Commit SHAs**: Both `repo.ref` and `repo.variegata` **must** be exact 40-character Git object commit SHAs from the upstream repository (`repo.github`).
- **Never Use vcpkg Hashes**: Script automations (such as version bump scripts) must never replace `variegata:` with `vcpkg_commit` (e.g. `cd61e1e...` or `84bab45...`). A vcpkg commit is from Microsoft's vcpkg repository and will cause `git checkout` to fail in community CI.
- **Do Not Remove `variegata`**: When maintaining compatibility across DuckDB major/minor lines, omitting `variegata` causes community CI to either skip DuckDB 1.5.6 builds or attempt to build `ref` against 1.5.6 (which fails due to C++ API breaking changes).

---

## Strict CI/CD Invariants: Zero Compile-Time Dependency & In-Tree C ABI Loading

Community extension distribution runners execute inside clean, ephemeral Docker containers (`ubuntu-latest`, `macos-latest`, `windows-latest`) without proprietary database SDKs or client drivers installed.

### 1. In-Tree C ABI Declarations (`src/include/oracle_oci_api.hpp`)
- Proprietary headers (`<oci.h>`, `<oratypes.h>`) are **never** required at compile time.
- The extension defines all OCI types (`OCIEnv`, `OCISvcCtx`, `OCIError`, `OCIStmt`, `OCIDescriptor`, `OCIBind`, `OCIDefine`) and function signatures directly in-tree using pure C ABI declarations.
- No build-time `find_library(clntsh)` or `find_path(OCI_INCLUDE_DIR)` in `CMakeLists.txt`. The build links solely against `CMAKE_DL_LIBS` (`libdl.so` on Linux, `dl` on macOS, system loader on Windows).

### 2. Lazy Runtime Dynamic Loading (`src/oracle_oci_api.cpp`)
- Loading and installing the extension (`INSTALL oracle FROM community; LOAD oracle;`) **must** succeed with zero Oracle software on the host.
- The upstream documentation pipeline (`generate_docs.yml` / `scripts/generate_md.sh`) executes:
  ```bash
  FORCE INSTALL oracle;
  LOAD oracle;
  ```
  on a runner with no Oracle software. Any load-time dynamic linker dependency (`DT_NEEDED` on `libclntsh.so`) causes documentation generation to crash.
- Resolving the Oracle Client occurs lazily when opening the first database connection (`OCIInitClient()`), scanning:
  1. `OCI_LIB_DIR` environment variable
  2. `ORACLE_HOME/lib` or `ORACLE_HOME`
  3. System dynamic library search paths (`/usr/lib`, `/usr/local/lib`, `ldconfig`, `PATH`)

---

## Upstream Documentation Compatibility Matrix (`scripts/compatibility_matrix.py`)

Upstream DuckDB docs dynamically generate a platform compatibility matrix by querying `https://community-extensions.duckdb.org` via HTTP `HEAD` requests:

```
https://community-extensions.duckdb.org/v{version}/{platform}/{extension}.duckdb_extension.{gz|wasm}
```

- DuckDB evaluates all active minor releases in `duckdb-web/_data/past_releases.csv` that have not reached End-of-Life (EOL), displaying the last 2 patch releases of each cycle (currently `1.4.4`, `1.4.5`, `1.5.5`, `1.5.6`, and `2.0.0`).
- If an artifact returns `HTTP 200`, the table renders a green check (`✅`).
- If an artifact returns `HTTP 404`, the table renders a red mark (`❌`).
- Setting `repo.variegata` to a working DuckDB 1.5.6 commit enables community CI to compile and publish DuckDB 1.5.6 binaries, turning the official documentation matrix green for both 2.0.0 and 1.5.6.
