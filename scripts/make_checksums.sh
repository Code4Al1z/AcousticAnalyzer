#!/usr/bin/env bash
# Writes SHA256SUMS.txt for every .zip in a folder (default: dist/).
# Usage: scripts/make_checksums.sh [dir]
set -euo pipefail

dir="${1:-$(cd "$(dirname "$0")/.." && pwd)/dist}"
cd "$dir"

shopt -s nullglob
zips=(*.zip)
if [ ${#zips[@]} -eq 0 ]; then
    echo "No .zip files in $dir" >&2
    exit 1
fi

if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "${zips[@]}" > SHA256SUMS.txt
else
    shasum -a 256 "${zips[@]}" > SHA256SUMS.txt
fi

echo "Wrote $dir/SHA256SUMS.txt"
cat SHA256SUMS.txt
