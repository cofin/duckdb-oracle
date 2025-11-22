# Project Overview

This repository builds the DuckDB Oracle extension. It is C++-based, built with CMake (≥3.10) via the provided `Makefile`, and uses vcpkg for third‑party dependencies (OpenSSL).

## Building and Running

### Dependencies

* CMake
* C++ compiler
* vcpkg

### Building

1. **Clone the repository:**

    ```sh
    git clone --recurse-submodules https://github.com/<you>/<your-new-extension-repo>.git
    ```

2. **Install dependencies with vcpkg:**
    Follow the instructions in the `vcpkg/` submodule to bootstrap vcpkg.
3. **Install Oracle Instant Client (Basic + SDK):**
    Set `ORACLE_HOME` to the Instant Client root containing `sdk/include/oci.h` and `lib/libclntsh.so`. The build falls back to `/usr/share/oracle/instantclient_23_26` if `ORACLE_HOME` is unset.
4. **Build the extension:**

    ```sh
    make
    ```

    This will create the following binaries:
    * `./build/release/duckdb`: DuckDB shell with the extension pre-loaded.
    * `./build/release/test/unittest`: Test runner for the extension.
    * `./build/release/extension/oracle/oracle.duckdb_extension`: The loadable extension binary.

### Running

To run the extension, use the pre-loaded DuckDB shell:

```sh
./build/release/duckdb
```

Then, you can use the custom functions defined in the extension:

```sql
D select oracle('Jane') as result;
┌─────────────────────────────────────┐
│                result               │
│               varchar               │
├─────────────────────────────────────┤
│ Oracle Jane 🐥                      │
└─────────────────────────────────────┘
```

## Development Conventions

* **Testing:** The primary way of testing is through SQL tests located in the `./test/sql` directory. To run the tests, use the command `make test`.
* **Dependency Management:** Dependencies are managed using vcpkg. Add dependencies to the `vcpkg.json` file and use `find_package` in `CMakeLists.txt` to link them.
* **Distribution:** The repository is set up with GitHub Actions to automatically build and upload binaries for distribution. The recommended way to distribute the extension is through the community extensions repository.
* **Oracle connectivity:** Connection strings are passed directly to `OCILogon` (embed credentials, e.g., `user/password@//host:port/service`). Wallet auth uses `oracle_attach_wallet('/path')` to set `TNS_ADMIN`.
