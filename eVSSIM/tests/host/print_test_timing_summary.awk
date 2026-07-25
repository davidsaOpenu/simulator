# Parses GoogleTest console output and prints a per-test and per-test-suite
# timing summary table for every log file given on the command line, plus an
# overall grand total across all files.
#
# Usage: awk -f print_test_timing_summary.awk file1.log file2.log ...
# Each log file's basename (without .log) is used as the binary label.

function reset_binary() {
    ntests = 0
    delete test_name
    delete test_time
    delete test_status
    nsuites = 0
    delete suite_order
    delete suite_time
    delete suite_seen
    binary_total_ms = 0
}

function repeat(ch, n,    s, i) {
    s = ""
    for (i = 0; i < n; i++) s = s ch
    return s
}

function print_binary_summary(name,    i, s, line) {
    printf "\n%s\n", repeat("=", 70)
    printf "Test binary: %s\n", name
    printf "%s\n", repeat("=", 70)

    printf "\nPer-test timing:\n"
    printf "  %-55s %8s  %s\n", "TEST", "TIME", "STATUS"
    printf "  %s\n", repeat("-", 70)
    for (i = 1; i <= ntests; i++) {
        printf "  %-55s %6d ms  %s\n", test_name[i], test_time[i], test_status[i]
    }

    printf "\nPer-test-suite timing:\n"
    printf "  %-55s %8s\n", "TEST SUITE", "TIME"
    printf "  %s\n", repeat("-", 70)
    for (i = 1; i <= nsuites; i++) {
        s = suite_order[i]
        printf "  %-55s %6d ms\n", s, suite_time[s]
    }
    printf "  %s\n", repeat("-", 70)
    printf "  %-55s %6d ms\n", "TOTAL", binary_total_ms

    binary_names[++nbinaries] = name
    binary_totals[name] = binary_total_ms
}

BEGIN {
    nbinaries = 0
    grand_total_ms = 0
}

function label_for_file(fname,    slash, i, label) {
    label = fname
    slash = 0
    for (i = length(label); i >= 1; i--) {
        if (substr(label, i, 1) == "/") {
            slash = i
            break
        }
    }
    if (slash > 0) label = substr(label, slash + 1)
    sub(/\.log$/, "", label)
    return label
}

FNR == 1 {
    if (NR != 1) {
        print_binary_summary(prev_label)
    }
    reset_binary()
    prev_label = label_for_file(FILENAME)
}

# [       OK ] Suite.Test (12 ms)
# [  FAILED  ] Suite.Test (12 ms)
# [  SKIPPED ] Suite.Test (0 ms)
$0 ~ /^\[.*\] .*\([0-9]+ ms\)$/ && $0 !~ /ms total\)$/ {
    line = $0
    bracket_end = index(line, "] ")
    rest = substr(line, bracket_end + 2)
    paren = index(rest, " (")
    if (paren > 0) {
        name = substr(rest, 1, paren - 1)
        timepart = substr(rest, paren + 2)
        split(timepart, tarr, " ")
        t = tarr[1] + 0

        status = "OK"
        if (index(line, "FAILED") > 0) status = "FAILED"
        else if (index(line, "SKIPPED") > 0) status = "SKIPPED"

        ntests++
        test_name[ntests] = name
        test_time[ntests] = t
        test_status[ntests] = status
    }
}

# [----------] 5 tests from SectorUnitTest (23 ms total)
$0 ~ /^\[----------\]/ && index($0, "ms total)") > 0 {
    line = $0
    bracket_end = index(line, "] ")
    rest = substr(line, bracket_end + 2)
    from_pos = index(rest, " from ")
    if (from_pos > 0) {
        after_from = substr(rest, from_pos + 6)
        paren = index(after_from, " (")
        if (paren > 0) {
            suite = substr(after_from, 1, paren - 1)
            timepart = substr(after_from, paren + 2)
            split(timepart, tarr, " ")
            t = tarr[1] + 0

            if (!(suite in suite_seen)) {
                suite_seen[suite] = 1
                nsuites++
                suite_order[nsuites] = suite
                suite_time[suite] = 0
            }
            suite_time[suite] += t
            binary_total_ms += t
        }
    }
}

END {
    print_binary_summary(prev_label)

    printf "\n%s\n", repeat("=", 70)
    printf "Overall summary\n"
    printf "%s\n", repeat("=", 70)
    printf "  %-30s %8s\n", "BINARY", "TIME"
    printf "  %s\n", repeat("-", 40)
    for (i = 1; i <= nbinaries; i++) {
        printf "  %-30s %6d ms\n", binary_names[i], binary_totals[binary_names[i]]
        grand_total_ms += binary_totals[binary_names[i]]
    }
    printf "  %s\n", repeat("-", 40)
    printf "  %-30s %6d ms\n", "GRAND TOTAL", grand_total_ms
}
