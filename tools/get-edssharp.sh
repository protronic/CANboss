#!/bin/sh
# EDSSharp-CLI (CANopenEditor) aus dem GitHub-Release nach
# tools/edssharp/ laden. Die CMake-Builds finden ihn dort automatisch
# und generieren die ODs dann zur Buildzeit aus eds/*.eds
# (siehe lib/od/od_codegen.cmake).
#
#   tools/get-edssharp.sh [TAG]     # Default: siehe unten
#
set -eu

TAG="${1:-${EDSSHARP_TAG:-cli-v4.2.3-protronic.1}}"
URL="https://github.com/protronic/CANopenEditor/releases/download/${TAG}/EDSSharp-linux-x64.tar.gz"
DEST="$(dirname "$0")/edssharp"

echo "Lade ${URL}"
mkdir -p "${DEST}"
curl -fL "${URL}" | tar -xz -C "${DEST}"
chmod +x "${DEST}/EDSSharp"

echo "OK: ${DEST}/EDSSharp"
"${DEST}/EDSSharp" 2>&1 | head -2 || true
