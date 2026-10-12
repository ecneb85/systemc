#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <simulation-executable>" >&2
    exit 2
fi

simulation_output="$("$1")"
printf '%s\n' "$simulation_output"

actual_phases="$(
    printf '%s\n' "$simulation_output" |
        sed -n -E 's/^([0-9]+ ns) \[(memory|cpu)\] (accepted |sending )(BEGIN_REQ|END_REQ|BEGIN_RESP|END_RESP)$/\1 \4/p'
)"
expected_phases="$(cat <<'EOF'
1 ns BEGIN_REQ
6 ns END_REQ
16 ns BEGIN_RESP
18 ns END_RESP
18 ns BEGIN_REQ
23 ns END_REQ
33 ns BEGIN_RESP
35 ns END_RESP
EOF
)"

if [[ "$actual_phases" != "$expected_phases" ]]; then
    echo "Unexpected TLM-2.0 phase order or timing." >&2
    echo "Expected:" >&2
    printf '%s\n' "$expected_phases" >&2
    echo "Actual:" >&2
    printf '%s\n' "$actual_phases" >&2
    exit 1
fi

echo "AT phase order and timing verified."
