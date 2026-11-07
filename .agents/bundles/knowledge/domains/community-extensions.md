---
type: Domain
title: DuckDB Community Extensions Registry, Multi-Version Distribution & Invariants
description: Architecture of the official DuckDB community extensions registry, multi-track version routing (ref vs variegata), zero-compile-time-dependency packaging, and compatibility matrix mechanics.
scope: domain
domain: duckdb
status: stable
tags: [duckdb, community-extensions, distribution, ci-cd, versioning]
generated:
  by: flow/0.27.0
  at: 2026-11-07T17:36:00Z
verified:
  - by: agent:flow-setup
    at: 2026-11-07T17:36:00Z
    commit: 36539f3
sources:
  - id: desc-yml
    resource: description.yml
    title: Extension metadata, excluded platforms, and community documentation
  - id: cmake-lists
    resource: CMakeLists.txt
    title: Zero-SDK CMake build linking only against CMAKE_DL_LIBS
  - id: oci-api
    resource: src/oracle_oci_api.cpp::OCIInitClient
    title: Lazy runtime dlopen/LoadLibraryA OCI symbol resolution
  - id: bump-script
    resource: scripts/bump_duckdb_version.sh
    title: DuckDB version bump script preserving variegata commit refs
---

# DuckDB Community Extensions Registry, Multi-Version Distribution & Invariants

<!-- truth: start -->
- `description.yml` defines the extension metadata (`version: 0.3.1`, `license: MIT`, `excluded_platforms`) consumed by `duckdb/community-extensions`[^desc-yml].
- In `duckdb/community-extensions`, `repo.ref` points to the exact 40-char commit SHA for DuckDB `2.0.0` LTS (`fa61951bdd3edf18bb6bf8e7169255d5ca6de635`) and `repo.variegata` points to the exact 40-char commit SHA for DuckDB `1.5.6` (`79a0fe896398d0e12533241bf101d66e5650e6b6`, backporting the in-tree OCI C ABI lazy loader on `feat/duckdb-v1.5.6`)[^bump-script].
- Never write `vcpkg_commit` hashes into `repo.variegata`, and never link `libclntsh` at build time (`CMakeLists.txt` links only `${CMAKE_DL_LIBS}`) because upstream `generate_docs.yml` runs `FORCE INSTALL oracle; LOAD oracle;` on clean runners without Oracle Instant Client installed[^cmake-lists][^oci-api].
<!-- truth: end -->

DuckDB extensions distributed via the official community repository ([duckdb/community-extensions](https://github.com/duckdb/community-extensions)) are built from source on DuckDB's automated CI/CD runners, signed, and published to `https://community-extensions.duckdb.org`[^desc-yml].

## Community Extension Descriptor (`description.yml`)

The community registry tracks extensions via individual YAML descriptors under `extensions/<extension_name>/description.yml`[^desc-yml]:

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
  variegata: 79a0fe896398d0e12533241bf101d66e5650e6b6
```

### Key Field Semantics

1. **`extension.version`**: Semver version of the extension package (`0.3.1`), matching the release tag and internal `EXT_VERSION_ORACLE` string[^desc-yml].
2. **`extension.excluded_platforms`**: Semicolon-delimited list of target platforms skipped during distribution builds[^desc-yml]:
   - `wasm_mvp`, `wasm_eh`, `wasm_threads`: WebAssembly lacks dynamic socket and C ABI library loading (`dlopen`).
   - `windows_amd64_mingw`: DuckDB Windows releases use MSVC toolchains; MinGW is unsupported for OCI DLL bindings.
   - `windows_arm64`: Oracle Instant Client lacks official Windows ARM64 native binaries.
   - `linux_amd64_musl`, `linux_arm64_musl`: Alpine/musl toolchains lack glibc symbol parity required by Oracle Instant Client.
   - `osx_amd64`: Deprecated legacy x86_64 macOS runners.
3. **`repo.ref`**: Exact 40-character Git commit SHA used for building the current default/stable DuckDB release (DuckDB `2.0.0` LTS "Cyanoptera").
4. **`repo.variegata`**: Exact 40-character Git commit SHA on `feat/duckdb-v1.5.6` (`79a0fe896398d0e12533241bf101d66e5650e6b6`) used for building DuckDB `1.5.6` ("Variegata").

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
- **Exact Git Commit SHAs**: Both `repo.ref` and `repo.variegata` must be exact 40-character Git object commit SHAs from `cofin/duckdb-oracle` that compile without an external OCI SDK.
- **Never Use vcpkg Hashes**: Version bump scripts (`scripts/bump_duckdb_version.sh`) must never replace `variegata:` with `vcpkg_commit`[^bump-script]. A vcpkg commit is from Microsoft's vcpkg repository and causes `git checkout` to fail in community CI.
- **Preserve `variegata`**: Both `repo.ref` (`main`, DuckDB `2.0.0`) and `repo.variegata` (`feat/duckdb-v1.5.6`, DuckDB `1.5.6`) include the in-tree OCI C ABI lazy loader (`src/include/oracle_oci_api.hpp` + `src/oracle_oci_api.cpp`), enabling clean builds across both DuckDB tracks.

## Strict CI/CD Invariants: Zero Compile-Time Dependency & In-Tree C ABI Loading

Community extension distribution runners execute inside clean, ephemeral containers (`ubuntu-latest`, `macos-latest`, `windows-latest`) without proprietary database SDKs or client drivers installed.

### 1. In-Tree C ABI Declarations (`src/include/oracle_oci_api.hpp`)
- Proprietary headers (`<oci.h>`, `<oratypes.h>`) are never required at compile time.
- The extension defines all OCI types (`OCIEnv`, `OCISvcCtx`, `OCIError`, `OCIStmt`, `OCIDescriptor`, `OCIBind`, `OCIDefine`) and function signatures directly in-tree using pure C ABI declarations.
- `CMakeLists.txt` links solely against `${CMAKE_DL_LIBS}` (`libdl.so` on Linux, `dl` on macOS, system loader on Windows)[^cmake-lists].

### 2. Lazy Runtime Dynamic Loading (`src/oracle_oci_api.cpp`)
- Loading and installing the extension (`INSTALL oracle FROM community; LOAD oracle;`) succeeds with zero Oracle software on the host[^oci-api].
- The upstream documentation pipeline (`generate_docs.yml` / `scripts/generate_md.sh`) executes `FORCE INSTALL oracle; LOAD oracle;` on a runner with no Oracle software. Any load-time dynamic linker dependency (`DT_NEEDED` on `libclntsh.so`) crashes documentation generation.
- Resolving the Oracle Client occurs lazily when opening the first database connection (`OCIInitClient()`), scanning:
  1. `OCI_LIB_DIR` environment variable
  2. `ORACLE_HOME/lib` or `ORACLE_HOME`
  3. System dynamic library search paths (`/usr/lib`, `/usr/local/lib`, `ldconfig`, `PATH`)

## Upstream Documentation Compatibility Matrix (`scripts/compatibility_matrix.py`)

Upstream DuckDB docs dynamically generate a platform compatibility matrix by querying `https://community-extensions.duckdb.org` via HTTP `HEAD` requests:

```
https://community-extensions.duckdb.org/v{version}/{platform}/{extension}.duckdb_extension.{gz|wasm}
```

- DuckDB evaluates all active minor releases in `duckdb-web/_data/past_releases.csv` that have not reached End-of-Life (EOL), displaying the last 2 patch releases of each cycle (`1.4.4`, `1.4.5`, `1.5.5`, `1.5.6`, and `2.0.0`).
- Setting `repo.variegata` to `79a0fe896398d0e12533241bf101d66e5650e6b6` alongside `repo.ref` (`fa61951bdd3edf18bb6bf8e7169255d5ca6de635`) enables community CI to compile and publish binaries for both DuckDB `2.0.0` and `1.5.6`.

[^desc-yml]: `description.yml` — Extension metadata, excluded platforms, and community documentation.
[^cmake-lists]: `CMakeLists.txt` — Zero-SDK CMake build linking only against `CMAKE_DL_LIBS`.
[^oci-api]: `src/oracle_oci_api.cpp::OCIInitClient` — Lazy runtime `dlopen`/`LoadLibraryA` OCI symbol resolution.
[^bump-script]: `scripts/bump_duckdb_version.sh` — DuckDB version bump script preserving `variegata` commit refs.
