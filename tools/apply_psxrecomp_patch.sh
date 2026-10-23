#!/usr/bin/env bash
# Apply the versioned patch series in patches/ to the psxrecomp submodule:
#   1. psxrecomp-screen-x-interp.patch      dirty-RAM interpreter honours
#                                           [widescreen.cull] screen_x_sites
#   2. psxrecomp-windows-toolchain.patch    native-Windows overlay/BIOS compiler
#                                           selection and cached-DLL validation
# Patches are applied in that order to the working tree only (never committed
# to the submodule), so the pinned gitlink stays on the upstream commit. Later
# patches may build on earlier ones (both touch runtime/src/main.cpp).
#
# Idempotent: series already applied -> no-op; a partially applied series
# (a prefix) is completed. Anything else unexpected -> error.
# Usage: tools/apply_psxrecomp_patch.sh [--check]   (--check changes nothing)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SUB="${ROOT}/psxrecomp"
PATCHES=(
  "${ROOT}/patches/psxrecomp-screen-x-interp.patch"
  "${ROOT}/patches/psxrecomp-windows-toolchain.patch"
)
EXPECTED_BASE="7cddab8237e1f73b7067b8401af602d90a96b80d"
CHECK_ONLY=0
[[ "${1:-}" == "--check" ]] && CHECK_ONLY=1

die() { echo "apply_psxrecomp_patch: ERROR: $*" >&2; exit 1; }

for p in "${PATCHES[@]}"; do
  [[ -f "${p}" ]] || die "patch not found: ${p}"
done
git -C "${SUB}" rev-parse --git-dir >/dev/null 2>&1 \
  || die "psxrecomp submodule not initialised (git submodule update --init --recursive)"

BASE="$(git -C "${SUB}" rev-parse HEAD)"
[[ "${BASE}" == "${EXPECTED_BASE}" ]] \
  || die "psxrecomp is at ${BASE}, patches are built for ${EXPECTED_BASE}; do not upgrade the pin without regenerating the patches"

# Normalise line endings so a Windows autocrlf checkout does not break apply.
APPLY=(git -C "${SUB}" -c core.autocrlf=false apply --whitespace=nowarn)

# Largest N such that patches[0..N) are already applied. Reverse-checking in
# reverse order lets a later patch's changes be undone before an earlier one.
N=${#PATCHES[@]}
while :; do
  REV=()
  for ((i = N - 1; i >= 0; i--)); do REV+=("${PATCHES[i]}"); done
  if [[ ${N} -eq 0 ]] || "${APPLY[@]}" -R --check "${REV[@]}" >/dev/null 2>&1; then
    break
  fi
  N=$((N - 1))
done

if [[ ${N} -eq ${#PATCHES[@]} ]]; then
  echo "apply_psxrecomp_patch: already applied (OK)"
  exit 0
fi

PENDING=("${PATCHES[@]:N}")
ERR="$(mktemp "${TMPDIR:-/tmp}/psx_patch_err.XXXXXX")"
trap 'rm -f "${ERR}"' EXIT
if ! "${APPLY[@]}" --check "${PENDING[@]}" 2>"${ERR}"; then
  cat "${ERR}" >&2
  die "patch series neither applied nor applicable: psxrecomp has conflicting local changes (git -C psxrecomp status)"
fi

if [[ ${CHECK_ONLY} -eq 1 ]]; then
  echo "apply_psxrecomp_patch: ${#PENDING[@]} of ${#PATCHES[@]} patch(es) not applied, but apply cleanly (--check, nothing changed)"
  exit 0
fi

for p in "${PENDING[@]}"; do
  "${APPLY[@]}" "${p}"
done

REV=()
for ((i = ${#PATCHES[@]} - 1; i >= 0; i--)); do REV+=("${PATCHES[i]}"); done
"${APPLY[@]}" -R --check "${REV[@]}" >/dev/null 2>&1 \
  || die "verification failed after applying"
echo "apply_psxrecomp_patch: applied ${#PENDING[@]} patch(es) (OK)"
