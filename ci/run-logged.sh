#!/usr/bin/env bash
# Esegue un comando salvandone l'output. Se fallisce, riporta le righe di errore
# come "annotation" di GitHub Actions, cosi' sono leggibili anche senza scaricare i log.
#   uso: ci/run-logged.sh <nome> <comando...>
set -o pipefail

name="$1"; shift
log="$(mktemp)"

"$@" 2>&1 | tee "$log"
status=${PIPESTATUS[0]}

if [ "$status" -ne 0 ]; then
    grep -E "error|Error|ERROR|fatal|FAILED|What went wrong|not found|No such file" "$log" \
        | grep -v "^\s*$" | tail -n 9 \
        | while IFS= read -r line; do
            line="${line//'%'/'%25'}"
            echo "::error title=${name}::${line:0:600}"
          done
    echo "::error title=${name} (ultime righe)::$(tail -n 15 "$log" | tr '\n' '|' | cut -c1-1500)"
fi

exit "$status"
