# Archive: Release v1.0.0 Preparation

**Date**: 2025-11-24
**Status**: SUCCESS

## Summary
Completed final cleanup, documentation, and verification for the v1.0.0 release.

## Actions
1.  **Cleanup**: Verified removal of dead code and unused includes (`src/oracle_extension.cpp`).
2.  **Warning Fixes**: Silenced `-Wswitch` warnings for cleaner builds.
3.  **Platform Fixes**: Resolved Windows test failures (`/tmp` vs `.`) and build environment issues (`setenv` shim).
4.  **Documentation**:
    - Updated `README.md` with new features (Array Fetch, Connection Pooling) and test options.
    - Created `CHANGELOG.md` documenting all major changes for v1.0.0.
5.  **Release Prep**: Confirmed version is `1.0.0` in source.

## Artifacts
- `README.md` (Updated)
- `CHANGELOG.md` (Created)
- `src/oracle_extension.cpp` (Final version 1.0.0)
