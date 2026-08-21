#!/bin/sh
# EDSSharp-CLI (CANopenEditor) nach tools/edssharp/ laden. CMake ruft
# das Skript beim Configure auf, wenn kein Binary da ist
# (lib/od/od_codegen.cmake).
#
#   tools/get-edssharp.sh              # latest linux-x64
#   tools/get-edssharp.sh --update     # erneut laden, auch wenn schon da
#   tools/get-edssharp.sh [TAG]        # festes Release, z.B. protronic-v4.2.3.5
#   EDSSHARP_URL=... tools/get-edssharp.sh
#
set -eu

LATEST_URL="https://github.com/protronic/CANopenEditor/releases/latest/download/EDSSharp-linux-x64.tar.gz"
DEST="$(CDPATH= cd -- "$(dirname "$0")" && pwd)/edssharp"
FORCE=0

if [ "${1:-}" = "--update" ] || [ "${1:-}" = "--force" ]; then
    FORCE=1
    shift
fi

if [ -n "${EDSSHARP_URL:-}" ]; then
    URL="${EDSSHARP_URL}"
elif [ -n "${1:-}" ]; then
    URL="https://github.com/protronic/CANopenEditor/releases/download/${1}/EDSSharp-linux-x64.tar.gz"
elif [ -n "${EDSSHARP_TAG:-}" ]; then
    URL="https://github.com/protronic/CANopenEditor/releases/download/${EDSSHARP_TAG}/EDSSharp-linux-x64.tar.gz"
else
    URL="${LATEST_URL}"
fi

if [ "${FORCE}" -eq 0 ] && [ -x "${DEST}/EDSSharp" ]; then
    echo "EDSSharp bereits vorhanden: ${DEST}/EDSSharp"
    echo "(erneut laden: $0 --update)"
    "${DEST}/EDSSharp" --help >/dev/null 2>&1 || true
    exit 0
fi

echo "Lade ${URL}"
mkdir -p "${DEST}"
curl -fL "${URL}" | tar -xz -C "${DEST}"
chmod +x "${DEST}/EDSSharp"

echo "OK: ${DEST}/EDSSharp"
"${DEST}/EDSSharp" 2>&1 | head -2 || true
