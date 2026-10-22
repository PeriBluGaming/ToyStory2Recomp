#!/usr/bin/env bash
# Apply patches/psxrecomp-screen-x-interp.patch to the psxrecomp submodule.
#
# The patch makes the dirty-RAM interpreter honour [widescreen.cull]
# screen_x_sites. It is applied to the working tree only (never committed to
# the submodule), so the pinned gitlink stays on the upstream commit.
#
# Idempotent: already applied -> no-op. Anything else unexpected -> error.
# Usage: tools/apply_psxrecomp_patch.sh [--check]   (--check changes nothing)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SUB="${ROOT}/psxrecomp"
PATCH="${ROOT}/patches/psxrecomp-screen-x-interp.patch"
EXPECTED_BASE="7cddab8237e1f73b7067b8401af602d90a96b80d"
CHECK_ONLY=0
[[ "${1:-}" == "--check" ]] && CHECK_ONLY=1

die() { echo "apply_psxrecomp_patch: ERROR: $*" >&2; exit 1; }

[[ -f "${PATCH}" ]] || die "patch not found: ${PATCH}"
git -C "${SUB}" rev-parse --git-dir >/dev/null 2>&1 \
  || die "psxrecomp submodule not initialised (git submodule update --init --recursive)"

BASE="$(git -C "${SUB}" rev-parse HEAD)"
[[ "${BASE}" == "${EXPECTED_BASE}" ]] \
  || die "psxrecomp is at ${BASE}, patch is built for ${EXPECTED_BASE}; do not upgrade the pin without regenerating the patch"

# Normalise line endings so a Windows autocrlf checkout does not break apply.
APPLY=(git -C "${SUB}" -c core.autocrlf=false apply --whitespace=nowarn)

if "${APPLY[@]}" -R --check "${PATCH}" >/dev/null 2>&1; then
  echo "apply_psxrecomp_patch: already applied (OK)"
  exit 0
fi

if ! "${APPLY[@]}" --check "${PATCH}" 2>/tmp/psx_patch_err.$$; then
  cat /tmp/psx_patch_err.$$ >&2; rm -f /tmp/psx_patch_err.$$
  die "patch neither applied nor applicable: psxrecomp has conflicting local changes (git -C psxrecomp status)"
fi
rm -f /tmp/psx_patch_err.$$

if [[ ${CHECK_ONLY} -eq 1 ]]; then
  echo "apply_psxrecomp_patch: not applied, but applies cleanly (--check, nothing changed)"
  exit 0
fi

"${APPLY[@]}" "${PATCH}"
"${APPLY[@]}" -R --check "${PATCH}" >/dev/null 2>&1 \
  || die "verification failed after applying"
echo "apply_psxrecomp_patch: applied (OK)"
