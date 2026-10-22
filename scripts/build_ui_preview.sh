#!/usr/bin/env bash
set -euo pipefail
SOURCE="$(cd "$(dirname "$0")/.." && pwd)"
PREVIEW_BUILD="$HOME/ps4dev/moonlight-preview"
cmake -S "$SOURCE/preview" -B "$PREVIEW_BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$PREVIEW_BUILD" -j 4
ctest --test-dir "$PREVIEW_BUILD" --output-on-failure
preview_args=("$@")
use_local=1
for argument in "$@"; do
    if [[ "$argument" == --demo || "$argument" == --catalog ]]; then use_local=0; fi
done
sunshine_apps='/mnt/c/Program Files/Sunshine/config/apps.json'
if [[ "$use_local" == 1 && -f "$sunshine_apps" ]]; then
    python3 "$SOURCE/scripts/export_preview_catalog.py" --source "$sunshine_apps" \
        --output "$PREVIEW_BUILD/catalog.cfg"
    preview_args+=(--catalog "$PREVIEW_BUILD/catalog.cfg")
fi
exec "$PREVIEW_BUILD/moonlight-ui-preview" "${preview_args[@]}"
