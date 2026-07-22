#!/usr/bin/env bash
#
# bump_duckdb_version.sh — bump the DuckDB version this extension targets.
#
# Updates, in one place, everything that drifts when DuckDB releases a version:
#   1. duckdb submodule              -> checked out at vX.Y.Z
#   2. extension-ci-tools submodule  -> vX.Y.Z tag (fallback: vX.Y branch)
#   3. main-distribution-pipeline.yml -> the 6 version refs (precise, never
#      touches actions/*@v4 refs the way the old inline sed could)
#   4. README.md                     -> updates the supported-version banner
#   5. extension version             -> patch-bumped via bump-my-version
#      (description.yml; the source of truth auto-tag.yml reads)
#   6. docs/COMPATIBILITY.md         -> appends a new compatibility row
#
# It does NOT commit — review, build, test, then commit. The DuckDB Update Check
# workflow calls this script (instead of inline sed), then opens a PR.
#
# Usage:
#   scripts/bump_duckdb_version.sh <duckdb_version> [--no-ext-bump] [--dry-run]
#     scripts/bump_duckdb_version.sh v1.5.5
#     scripts/bump_duckdb_version.sh 1.5.5 --dry-run
#     scripts/bump_duckdb_version.sh v1.5.5 --no-ext-bump
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PIPELINE=".github/workflows/main-distribution-pipeline.yml"
COMPAT="docs/COMPATIBILITY.md"
README="README.md"

err() { echo "ERROR: $*" >&2; exit 1; }
note() { echo "==> $*"; }

# --- args -------------------------------------------------------------------
[ $# -ge 1 ] || err "usage: $0 <duckdb_version e.g. v1.5.4> [--no-ext-bump] [--dry-run]"

RAW="$1"; shift
EXT_BUMP=1
DRY_RUN=0
for arg in "$@"; do
  case "$arg" in
    --no-ext-bump) EXT_BUMP=0 ;;
    --dry-run)     DRY_RUN=1 ;;
    *) err "unknown arg: $arg" ;;
  esac
done

# Normalize to vX.Y.Z and validate strict semver.
VER="${RAW#v}"
[[ "$VER" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || err "invalid DuckDB version: '$RAW' (expected vX.Y.Z)"
TAG="v${VER}"
MINOR="v$(echo "$VER" | cut -d. -f1-2)"   # e.g. v1.5

[ -f "$PIPELINE" ] || err "missing $PIPELINE"
[ -f "$README" ] || err "missing $README"
[ -f "$COMPAT" ] || err "missing $COMPAT"
[ -f description.yml ] || err "missing description.yml"

CUR=$(grep -oE 'duckdb_version:[[:space:]]*v[0-9.]+' "$PIPELINE" | head -1 | grep -oE 'v[0-9.]+') \
  || err "could not read current duckdb_version from $PIPELINE"

echo "DuckDB target: ${CUR} -> ${TAG}   (ext-bump=${EXT_BUMP}, dry-run=${DRY_RUN})"
if [ "$CUR" = "$TAG" ]; then
  echo "Pipeline already references ${TAG}; continuing to verify submodules and metadata."
fi

run() {  # echo + execute, unless dry-run
  echo "    \$ $*"
  if [ "$DRY_RUN" -eq 0 ]; then "$@"; fi
}

next_patch_version() {
  local current base major minor patch
  current="$1"
  base="${current%%-*}"
  IFS=. read -r major minor patch <<< "$base"
  [[ "$major" =~ ^[0-9]+$ && "$minor" =~ ^[0-9]+$ && "$patch" =~ ^[0-9]+$ ]] \
    || err "invalid extension version in description.yml: '$current'"
  printf '%s.%s.%s\n' "$major" "$minor" "$((patch + 1))"
}

# --- 1. duckdb submodule ----------------------------------------------------
note "duckdb submodule -> ${TAG}"
if [ -d duckdb/.git ] || [ -f duckdb/.git ]; then
  run git -C duckdb fetch --tags --quiet
  run git -C duckdb checkout --quiet "$TAG"
else
  err "duckdb submodule not initialized (run: git submodule update --init)"
fi

# --- 2. extension-ci-tools submodule (tag, fallback to vX.Y branch) ----------
note "extension-ci-tools submodule -> ${TAG} (fallback ${MINOR})"
run git -C extension-ci-tools fetch --tags --quiet
if [ "$DRY_RUN" -eq 1 ]; then
  echo "    \$ (checkout ${TAG} tag if it exists, else origin/${TAG}, else origin/${MINOR})"
elif git -C extension-ci-tools rev-parse -q --verify "refs/tags/${TAG}" >/dev/null; then
  git -C extension-ci-tools checkout --quiet "$TAG"
elif git -C extension-ci-tools rev-parse -q --verify "origin/${TAG}" >/dev/null; then
  git -C extension-ci-tools checkout --quiet -B "$TAG" "origin/${TAG}"
else
  git -C extension-ci-tools fetch --quiet origin "$MINOR" 2>/dev/null || true
  if git -C extension-ci-tools rev-parse -q --verify "origin/${MINOR}" >/dev/null; then
    git -C extension-ci-tools checkout --quiet -B "$MINOR" "origin/${MINOR}"
  else
    echo "WARN: extension-ci-tools has no ${TAG} tag or ${MINOR} branch; left unchanged" >&2
  fi
fi

# --- 3. pipeline version refs (precise; only ci-tools @v and *_version:) -----
note "$PIPELINE (6 version refs ${CUR} -> ${TAG})"
if [ "$DRY_RUN" -eq 0 ]; then
  sed -i -E "s#(extension-ci-tools/\.github/workflows/[^@]+)@v[0-9.]+#\1@${TAG}#g" "$PIPELINE"
  sed -i -E "s#(duckdb_version:[[:space:]]*)v[0-9.]+#\1${TAG}#g" "$PIPELINE"
  sed -i -E "s#(ci_tools_version:[[:space:]]*)v[0-9.]+#\1${TAG}#g" "$PIPELINE"
fi
echo "    (updated @v refs, duckdb_version, ci_tools_version)"

# --- 4. README supported-version banner --------------------------------------
note "$README (supported DuckDB version ${CUR} -> ${TAG})"
if [ "$DRY_RUN" -eq 0 ]; then
  sed -i -E "s#(\*\*Supported DuckDB version\*\*: )v[0-9.]+#\1${TAG}#" "$README"
fi
echo "    (updated supported-version banner)"

# --- 5. extension version bump (description.yml + configured version files) --
EXT_VER=$(grep -E '^  version:' description.yml | awk '{print $2}')
if [ "$EXT_BUMP" -eq 1 ]; then
  note "extension version: bump-my-version patch"
  if [ "$CUR" = "$TAG" ]; then
    echo "    (skipped: pipeline already references ${TAG})"
  elif [ "$DRY_RUN" -eq 0 ]; then
    uvx bump-my-version bump patch
    EXT_VER=$(grep -E '^  version:' description.yml | awk '{print $2}')
  else
    EXT_VER=$(next_patch_version "$EXT_VER")
    echo "    \$ uvx bump-my-version bump patch  (dry-run -> ${EXT_VER})"
  fi
fi

# --- 6. COMPATIBILITY.md row ------------------------------------------------
note "$COMPAT (+ row: ${TAG} / v${EXT_VER})"
DATE=$(date -u +%Y-%m-%d)
ROW="| ${TAG} | v${EXT_VER} | ✅ Compatible | ${DATE} | DuckDB ${TAG} upgrade |"
if [ "$DRY_RUN" -eq 0 ] && ! grep -qF "| ${TAG} |" "$COMPAT"; then
  printf '%s\n' "$ROW" >> "$COMPAT"
fi
echo "    ${ROW}"

echo ""
echo "Done (${TAG}, ext v${EXT_VER}). Next:"
echo "  review the diff and run the validation appropriate for the change"
echo "  git commit -m 'chore(deps): upgrade DuckDB to ${TAG}'"
