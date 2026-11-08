#!/bin/bash
# Diagnostic observation only: run the existing helper/job without editing it.
# Keep each real fio result before a comparison direction overwrites /tmp files.
set -u
trace=$1
expected=$2
job=$3
shift 3
mkdir -p "$trace"
export EVSSIM_FIO_TRACE=$trace
export EVSSIM_FIO_BINARY
EVSSIM_FIO_BINARY=$(command -v fio) || exit 1

fio() {
    local invocation status started
    invocation=$(mktemp -d "$EVSSIM_FIO_TRACE/invocation-XXXXXXXX") || return 1
    chmod a+rx "$invocation" || return 1
    printf '%s\n' "${EVSSIM_FIO_ORIGIN:-wrapper}" > "$invocation/origin.txt"
    printf '%q ' "$EVSSIM_FIO_BINARY" "$@" > "$invocation/command.txt"
    printf '\n' >> "$invocation/command.txt"
    date +%s%N > "$invocation/started.txt"
    local argument
    for argument in "$@"; do
        if [[ "$argument" == *.fio ]]; then
            sha256sum "$argument" >> "$invocation/jobs.sha256" || return 1
        fi
    done
    started=$SECONDS
    if "$EVSSIM_FIO_BINARY" "$@" > "$invocation/stdout.log" 2> "$invocation/stderr.log"; then
        status=0
    else
        status=$?
    fi
    printf '%s\n' "$status" > "$invocation/exit-status.txt"
    printf '%s\n' "$((SECONDS - started))" > "$invocation/elapsed-seconds.txt"
    # Reproduce the child's output bytes; its caller keeps its original logic.
    cat "$invocation/stdout.log"
    cat "$invocation/stderr.log" >&2
    return "$status"
}
export -f fio

if "$@"; then wrapper_status=0; else wrapper_status=$?; fi
printf '%s\n' "$wrapper_status" > "$trace/wrapper-exit-status.txt"
count=$(find "$trace" -maxdepth 1 -type d -name 'invocation-*' | wc -l)
status=$wrapper_status
if [ "$count" -lt "$expected" ]; then
    # Keep the skipped-wrapper finding. These are separately identified
    # diagnostic invocations, never evidence that the original wrapper passed.
    printf '%s\n' "$((expected - count))" > "$trace/missing-wrapper-invocations.txt"
    status=1
    export EVSSIM_FIO_ORIGIN=diagnostic-fallback
    while [ "$count" -lt "$expected" ]; do
        if fio "$job"; then :; else status=1; fi
        count=$((count + 1))
    done
fi
reference="reference_results/$(basename "$job" .fio)_ref_res"
if [ -f "$reference" ]; then cp "$reference" "$trace/reference.log" || status=1; fi
for invocation in "$trace"/invocation-*; do
    if [ ! -f "$invocation/exit-status.txt" ] || [ "$(cat "$invocation/exit-status.txt")" != 0 ]; then
        status=1
    fi
done
exit "$status"
