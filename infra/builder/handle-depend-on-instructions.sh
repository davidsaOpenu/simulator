#!/bin/bash
set -e

# Apply each cross-repository dependency to its Gerrit target branch.  Later
# build steps may explicitly check out that branch (for example, QEMU 11.0),
# so applying a dependency to the current/default branch can silently lose it.
# Likewise, a fetched commit object is not an applied dependency unless it is
# reachable from the branch being built.

show_help() {
    cat << EOF

Usage: $(basename "$0") [workspace] [change id] [Gerrit User Name] [build source: gerrit | timer]

Description: This script fetches and applies all the commits your commit depends on.

Examples:
  From Gerrit change:
    $(basename "$0") workspace 123454 davidsaOpenu gerrit

  From timer:
    $(basename "$0") workspace unused davidsaOpenu timer

Optional: set EVSSIM_DEPENDENCY_RECORD to an output JSON path for S2 input
resolution. Required dependency failures return nonzero with or without it.

EOF
    exit 0
}

if [ $# -ne 4 ]; then
    show_help
fi

WORKSPACE=$(realpath "$1")
CHANGE_ID="$2"
GERRIT_USER="$3"
BUILD_SOURCE="$4"
GERRIT_HOST="review.gerrithub.io"
PROJECT="simulator"

# input: $1 - text to clean
# output: N/A
# descr: remove non ASCII chars and "CR"
clean_text() {
    local text="$1"

    # Remove all non-ASCII characters
    cleaned_text=$(echo "$text" | tr -cd '\0-\177')

    # Remove carriage returns
    cleaned_text=$(echo "$cleaned_text" | tr -d '\r')

    echo "$cleaned_text"
}

# Function to print summary - moved to a separate function for reusability
print_dependency_summary() {
    echo ""
    echo "=========================================="
    echo "DEPENDENCY PROCESSING SUMMARY"
    echo "=========================================="
    if [ ${#dependency_results[@]} -eq 0 ]; then
        echo "No dependencies found in commit message."
    else
        for result in "${dependency_results[@]}"; do
            echo "$result"
        done
    fi
    echo "=========================================="
}


# The record is opt-in until the workflow uses S2's resolved-input path.
# A partial scan must never look like a successful no-dependency selection.
write_dependency_record() {
    [[ -n "${EVSSIM_DEPENDENCY_RECORD:-}" ]] || return 0
    local exit_status=$1
    local temporary
    temporary=$(mktemp "${EVSSIM_DEPENDENCY_RECORD}.XXXXXX") || return 1
    if ! printf '%s\n' "${dependency_records[@]}" | jq -s \
        --argjson complete "$dependency_scan_complete" --argjson exit_status "$exit_status" \
        '{schema_version: 1,
          status: (if $complete == 1 and $exit_status == 0 and
                      all(.[]; .disposition == "applied" or .disposition == "already_present")
                   then "complete" else "failed" end), dependencies: .}' > "$temporary"; then
        rm -f -- "$temporary"
        return 1
    fi
    mv -- "$temporary" "$EVSSIM_DEPENDENCY_RECORD"
}

record_dependency() {
    local disposition=$1
    local error=${2:-}
    local applied_revision=""
    local patch_set=0
    local repo="${WORKSPACE}/$project_name"
    if [[ "$project_name" == "dnvme" || "$project_name" == "tnvme" ]]; then
        repo="${WORKSPACE}/nvmeCompl/$project_name"
    fi
    if [[ "$disposition" != failed && "$change_status" != NEW && "$change_status" != MERGED ]]; then
        disposition=failed
        error="Unsupported Gerrit change status"
    fi
    if [[ "$refs_changes" =~ ^refs/changes/[0-9]{2}/[0-9]+/([1-9][0-9]*)$ ]]; then
        patch_set=${BASH_REMATCH[1]}
    elif [[ "$disposition" != failed ]]; then
        disposition=failed
        error="Exact Gerrit patch-set ref was not obtained"
    fi
    # The command's result must not depend on whether JSON export is enabled.
    if [[ "$disposition" == failed ]]; then
        dependency_failed=1
    fi
    [[ -n "${EVSSIM_DEPENDENCY_RECORD:-}" ]] || return 0
    if [[ "$disposition" == applied || "$disposition" == already_present ]]; then
        if [[ "$disposition" == already_present ]] && \
                git -C "$repo" merge-base --is-ancestor "$commit_hash" HEAD 2>/dev/null; then
            applied_revision=$commit_hash
        else
            # A cherry-pick has a new commit ID; retain the actual applied HEAD.
            applied_revision=$(git -C "$repo" rev-parse HEAD) || return 1
        fi
    fi
    local entry
    entry=$(jq -nc --arg project "$project_name" --arg url "$url" \
        --argjson patch_set "$patch_set" --arg revision "$commit_hash" --arg ref "$refs_changes" \
        --arg branch "$target_branch" --arg applied "$applied_revision" \
        --arg disposition "$disposition" --arg error "$error" \
        '{project: $project, url: $url, patch_set: $patch_set, revision: $revision, ref: $ref,
          target_branch: $branch, applied_revision: $applied, disposition: $disposition, error: $error}') || return 1
    dependency_records+=("$entry")
}


# Configure Git identity if not already set
configure_git_identity() {
    # Check if user.name and user.email are configured
    if ! git config --global user.name >/dev/null 2>&1; then
        echo "Configuring Git user.name..."
        git config --global user.name "Jenkins Build System" || return 1
    fi

    if ! git config --global user.email >/dev/null 2>&1; then
        echo "Configuring Git user.email..."
        git config --global user.email "jenkins@build.system" || return 1
    fi
}


# input: $1 - project name
#        $2 - fetch URL
#        $3 - commit hash
#        $4 - Gerrit target branch
#        $5 - exact patch-set ref
# output: N/A
# descr: go to the project repo and perform git fetch
fetch_ref_spec() {
    local project_name=$1
    local fetch_url=$2
    local commit_hash=$3
    local target_branch=$4
    local patch_ref=$5
    local branch_key="${project_name}:${target_branch}"
    local project_repo="${WORKSPACE}/$project_name"
    application_error="Dependency application failed"

    if [[ "$project_name" == "dnvme" || "$project_name" == "tnvme" ]]; then
        project_repo="${WORKSPACE}/nvmeCompl/$project_name"
    fi

    echo "------------- start fetch_ref_spec ----------------"
    echo "FETCH URL: $fetch_url ($patch_ref)"
    echo "PROJECT REPO: $project_repo"
    echo "COMMIT HASH: $commit_hash"
    echo "TARGET BRANCH: $target_branch"

    pushd "$project_repo" || return 1
    echo "PWD: $(pwd)"
    echo "BRANCH PRIOR TO FETCH CMD: $(git branch -a)"
    echo "REPO STATUS PRIOR TO FETCH CMD: $(git status)"

    if ! configure_git_identity; then
        popd
        return 1
    fi

    if ! git check-ref-format --branch "$target_branch" >/dev/null 2>&1; then
        echo "ERROR: Invalid Gerrit target branch: $target_branch"
        popd
        return 1
    fi

    # Start each dependency project/branch from its remote target branch once.
    # Further dependencies for the same branch are then stacked on that result.
    if [[ -z "${prepared_dependency_branches[$branch_key]:-}" ]]; then
        echo "Preparing dependency target branch origin/$target_branch"
        if ! git fetch origin "$target_branch"; then
            echo "ERROR: Could not fetch origin/$target_branch"
            popd
            return 1
        fi
        if ! git checkout -B "$target_branch" "origin/$target_branch"; then
            echo "ERROR: Could not check out origin/$target_branch"
            popd
            return 1
        fi
        if ! git branch --set-upstream-to="origin/$target_branch" "$target_branch"; then
            echo "ERROR: Could not set upstream for $target_branch"
            popd
            return 1
        fi
        prepared_dependency_branches[$branch_key]=1
    elif ! git checkout "$target_branch"; then
        echo "ERROR: Could not return to dependency branch $target_branch"
        popd
        return 1
    fi

    # Object presence alone is not sufficient: the dependency must be in the
    # history of the branch that the build will use.
    if git merge-base --is-ancestor "$commit_hash" HEAD 2>/dev/null; then
        echo "SUCCESS - Commit $commit_hash is already an ancestor of HEAD"
        echo "   Status: ALREADY_IN_REPO - Commit is in history"
        popd
        echo "PWD: $(pwd)"
        echo "------------- end fetch_ref_spec ----------------"
        return 2  # Special return code for "already in repo"
    fi

    # Fetch and verify before applying: a moving/mismatched ref must not be
    # applied merely because recording the result is disabled.
    echo "Attempting to fetch and cherry-pick..."
    if ! git fetch "$fetch_url" "$patch_ref"; then
        popd
        return 1
    fi
    local fetched_revision
    if ! fetched_revision=$(git rev-parse FETCH_HEAD); then
        popd
        return 1
    fi
    if [[ "$fetched_revision" != "$commit_hash" ]]; then
        application_error="Fetched revision differs from Gerrit metadata"
        echo "ERROR: $application_error"
        popd
        return 1
    fi
    if git cherry-pick FETCH_HEAD; then
        echo "SUCCESS - Fetched and applied dependency successfully"
        echo "   Status: NEWLY_APPLIED - Cherry-pick completed"
    else
        echo "Cherry-pick failed"
        echo "REPO STATUS AFTER THE FETCH: $(git status)"

        # Check if we're in a cherry-pick state
        if git rev-parse --verify CHERRY_PICK_HEAD >/dev/null 2>&1; then
            echo "Cherry-pick in progress. Checking if it's an empty commit..."

            # Check if the cherry-pick resulted in no changes (empty commit)
            if git diff --cached --quiet && git diff --quiet; then
                echo "Cherry-pick resulted in no changes (commit already applied). Skipping..."
                if ! git cherry-pick --skip; then
                    popd
                    return 1
                fi
                echo "   Status: ALREADY_IN_REPO - Empty cherry-pick skipped"
                popd
                echo "PWD: $(pwd)"
                echo "------------- end fetch_ref_spec ----------------"
                return 2  # Special return code for "already in repo"
            else
                echo "Cherry-pick has conflicts or other issues"
                echo "   Status: FAILED - Conflicts detected"
                echo "Aborting cherry-pick"
                if ! git cherry-pick --abort; then
                    popd
                    return 1
                fi
                popd
                return 1  # Failed
            fi
        else
            echo "FAILED to fetch due to other reasons"
            echo "   Status: FAILED - Unknown error"
            popd
            return 1  # Failed
        fi
    fi

    echo "BRANCH AFTER FETCH CMD: $(git branch -a)"
    echo "REPO STATUS AFTER FETCH CMD: $(git status)"

    popd
    echo "PWD: $(pwd)"
    echo "------------- end fetch_ref_spec ----------------"
}

echo "******************** start handle-depend-on-instructions.sh *************************"
echo "WORKSPACE: $WORKSPACE"
echo "CHANGE_ID: $CHANGE_ID"
echo "GERRIT_USER: $GERRIT_USER"
echo "BUILD SOURCE: $BUILD_SOURCE"

# Initialize dependency results array
declare -a dependency_results=()
declare -A prepared_dependency_branches=()
declare -a dependency_records=()
dependency_scan_complete=0
dependency_failed=0
if [[ -n "${EVSSIM_DEPENDENCY_RECORD:-}" ]]; then
    command -v jq >/dev/null || { echo "ERROR: jq is required for the dependency record" >&2; exit 1; }
    trap 'dependency_record_exit=$?; write_dependency_record "$dependency_record_exit" || exit 1' EXIT
    write_dependency_record 1
fi

# Get commit message if triggered from Gerrit
if [[ "$BUILD_SOURCE" == "gerrit" ]]; then
    echo "Fetching commit message from Gerrit REST API..."
    # Check transport/HTTP success separately from JSON parsing so a failed
    # request cannot supply a commit message, even if it returned valid JSON.
    commit_message=""
    if commit_response=$(curl --noproxy "*" -f -s "https://${GERRIT_HOST}/changes/${GERRIT_USER}%2F${PROJECT}~${CHANGE_ID}/revisions/current/commit"); then
        # Remove Gerrit's XSSI prefix and require one nonblank string message.
        commit_message=$(printf '%s\n' "$commit_response" | sed "1s/^)]}'*//" | jq -ers '
            if length == 1 then
                .[0].message |
                if type == "string" then select(test("\\S"))
                else error("Missing Gerrit commit message") end
            else error("Expected exactly one Gerrit commit") end' 2>/dev/null) || commit_message=""
    fi

    if [[ -z "$commit_message" || "$commit_message" == "null" ]]; then
        echo "ERROR: Failed to retrieve commit message from Gerrit"
        echo "Trying alternative method using SSH..."
        # SSH emits one JSON record per change plus a stats record. Select
        # the change before decoding its multiline message; head on raw text
        # would discard the Depends-on trailers. Reject failed SSH commands
        # even if they emitted a partial response before failing.
        commit_message=""
        if ssh_response=$(ssh -p 29418 "${GERRIT_USER}@${GERRIT_HOST}" \
                gerrit query --current-patch-set --commit-message --format=JSON "change:${CHANGE_ID}"); then
            commit_message=$(printf '%s\n' "$ssh_response" | jq -ers '
                map(select(.type != "stats")) |
                if length == 1 then
                    .[0].commitMessage |
                    if type == "string" then select(test("\\S"))
                    else error("Missing Gerrit commit message") end
                else error("Expected exactly one Gerrit change") end') || commit_message=""
        fi

        if [[ -z "$commit_message" || "$commit_message" == "null" ]]; then
            echo "ERROR: Failed to retrieve commit message via SSH as well"
            print_dependency_summary
            exit 1
        fi
    fi
else
    commit_message="Triggered by timer; no commit message to parse"
fi

echo "COMMIT MESSAGE:"
echo "$commit_message"

cleaned_commit_message=$(clean_text "$commit_message")
echo "CLEANED COMMIT MESSAGE:"
echo "$cleaned_commit_message"

# Exit early if triggered by timer
if [[ "$BUILD_SOURCE" == "timer" ]]; then
    echo "Timer triggered — skipping depends-on parsing."
    print_dependency_summary
    echo "******************** end handle-depend-on-instructions.sh *************************"
    dependency_scan_complete=1
    exit 0
fi

declare -a projectArr=("kernel" "dnvme" "nvme-cli" "qemu" "simulator" "tnvme" "open-osd")

ERROR_INVALID_PROJECT_NAME_OR_URL="
Invalid url was specified following the 'depends-on:' in the commit message.
The name of the project should be one of the following:
$(printf '%s\n' ${projectArr[@]})
"

# Parse each required trailer, including malformed ones, so none disappear
# from the summary or turn a failed scan into a successful no-dependency run.
mapfile -t depends_on_lines < <(printf '%s\n' "$cleaned_commit_message" | grep -i '^[[:space:]]*depends-on:')

echo "PARSING DEPENDS-ON LINES"

for dependency_line in "${depends_on_lines[@]}"; do
    url="${dependency_line#*:}"
    url="${url#"${url%%[![:space:]]*}"}"
    url="${url%"${url##*[![:space:]]}"}"
    change_status=""
    commit_hash=""
    refs_changes=""
    target_branch=""
    project_name=""
    # Remove trailing slash if it exists
    url=${url%/}

    # Parse Gerrit URL properly
    # URL format: https://review.gerrithub.io/c/davidsaOpenu/qemu/+/1220521
    if [[ "$url" =~ ^https://([A-Za-z0-9.-]+)/c/([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+)/\+/([1-9][0-9]*)$ ]]; then
        gerrit_host=${BASH_REMATCH[1]}
        gerrit_user=${BASH_REMATCH[2]}
        project_name=${BASH_REMATCH[3]}
        change_num=${BASH_REMATCH[4]}
    else
        dependency_results+=("Malformed Depends-on trailer: $dependency_line")
        record_dependency failed "Invalid dependency URL"
        continue
    fi
    gerrit_port="29418"

    echo ""
    echo "------------------------------------------------------------"
    echo "DEPENDENCY URL: $url"
    echo "PROJECT: $project_name"
    echo "CHANGE: $change_num"
    echo "GERRIT HOST: $gerrit_host"
    echo "GERRIT USER: $gerrit_user"
    echo "------------------------------------------------------------"

    if [[ ! " ${projectArr[*]} " =~ " $project_name " ]]; then
        echo "$ERROR_INVALID_PROJECT_NAME_OR_URL"
        dependency_results+=("Change $change_num - was not applied because of invalid project name")
        record_dependency failed "Invalid project name"
        continue
    fi

    # Try REST API first
    echo "Trying REST API..."
    api_response=$(curl --noproxy "*" -s "https://${gerrit_host}/changes/${gerrit_user}%2F${project_name}~${change_num}/detail?o=CURRENT_REVISION" 2>/dev/null) || api_response=""

    if [[ -n "$api_response" && "$api_response" != *"Not Found"* ]]; then
        echo "REST API successful"
        echo "Debug: API response length: ${#api_response}"

        # Remove the )]}' prefix that Gerrit adds to prevent XSSI attacks
        clean_response=$(echo "$api_response" | sed "1s/^)]}'*//")
        echo "Debug: After cleaning, length: ${#clean_response}"

        # Check if jq is available and working
        if ! command -v jq &> /dev/null; then
            echo "ERROR: jq command not found"
            change_status="UNKNOWN"
            commit_hash=""
        else
            echo "Debug: jq is available, parsing JSON..."

            # Try to parse the JSON with error handling
            change_status=$(echo "$clean_response" | jq -r '.status' 2>/dev/null || echo "PARSE_ERROR")
            echo "Debug: Parsed status: '$change_status'"

            target_branch=$(echo "$clean_response" | jq -r '.branch // empty' 2>/dev/null || echo "")
            echo "Debug: Parsed target branch: '$target_branch'"

            # Get the revision hash
            current_revision=$(echo "$clean_response" | jq -r '.current_revision' 2>/dev/null || echo "null")
            echo "Debug: current_revision from API: '$current_revision'"

            echo "Debug: Final current_revision: '$current_revision'"

            if [[ -n "$current_revision" && "$current_revision" != "null" && "$current_revision" != "PARSE_ERROR" ]]; then
                commit_hash="$current_revision"
                echo "Debug: Using commit_hash: '$commit_hash'"

                # Get the ref from the revision details
                refs_changes=$(echo "$clean_response" | jq -r --arg revision "$current_revision" '.revisions[$revision].ref // empty' 2>/dev/null || echo "")
                echo "Debug: refs from API: '$refs_changes'"
            else
                echo "Debug: Could not get commit hash"
                commit_hash=""
            fi
        fi
    else
        echo "REST API failed or returned Not Found"
        change_status=""
        commit_hash=""
    fi

    echo "Debug: Final values - status: '$change_status', hash: '$commit_hash'"

    # Missing or invalid REST metadata requires an authoritative SSH response;
    # do not guess a revision or assume patch set 1.
    if [[ "$change_status" != NEW && "$change_status" != MERGED && "$change_status" != ABANDONED ]] || \
            [[ ! "$commit_hash" =~ ^[0-9a-f]{40}$ || ! "$refs_changes" =~ ^refs/changes/[0-9]{2}/[0-9]+/[1-9][0-9]*$ || -z "$target_branch" || "$target_branch" == null ]]; then
        echo "REST API failed, trying SSH..."
        if ssh_result=$(ssh -p $gerrit_port $gerrit_user@$gerrit_host gerrit query --current-patch-set --format=JSON change:$change_num 2>/dev/null); then
            if [[ -n "$ssh_result" ]]; then
                echo "SSH successful"
                change_status=$(echo "$ssh_result" | jq -r '.status // empty' | head -1) || change_status=""
                commit_hash=$(echo "$ssh_result" | jq -r '.currentPatchSet.revision // empty' | head -1) || commit_hash=""
                refs_changes=$(echo "$ssh_result" | jq -r '.currentPatchSet.ref // empty' | head -1) || refs_changes=""
                target_branch=$(echo "$ssh_result" | jq -r '.branch // empty' | head -1) || target_branch=""
            fi
        else
            ssh_status=$?
            echo "ERROR: SSH metadata retrieval failed for $change_num (exit $ssh_status)"
            dependency_results+=("Change $change_num - was not applied because SSH metadata retrieval failed")
            record_dependency failed "SSH metadata retrieval failed (exit $ssh_status)"
            continue
        fi
    fi

    # Check if we got the info
    if [[ -z "$change_status" ]]; then
        echo "ERROR: Could not retrieve change status for $change_num"
        dependency_results+=("Change $change_num - was not applied because API failed")
        record_dependency failed "Change metadata was not obtained"
        continue
    fi

    # An unverified revision cannot satisfy a required dependency.
    if [[ ! "$commit_hash" =~ ^[0-9a-f]{40}$ ]]; then
        echo "WARNING: Could not get commit hash, but change status is $change_status"
        if [[ "$change_status" == "MERGED" ]]; then
            echo "INFO: Since change is MERGED, it might already be in the repository"
            dependency_results+=("Change $change_num - was not applied because commit hash could not be determined (but change is merged)")
        else
            dependency_results+=("Change $change_num - was not applied because commit hash could not be determined")
        fi
        record_dependency failed "Revision was not obtained"
        continue
    fi

    if [[ ! "$refs_changes" =~ ^refs/changes/[0-9]{2}/[0-9]+/[1-9][0-9]*$ ]]; then
        dependency_results+=("Change $change_num - was not applied because its exact patch-set ref was not obtained")
        record_dependency failed "Exact Gerrit patch-set ref was not obtained"
        continue
    fi

    echo "CHANGE STATUS: $change_status"
    echo "COMMIT HASH: $commit_hash"
    echo "TARGET BRANCH: $target_branch"

    if [[ -z "$target_branch" || "$target_branch" == "null" ]]; then
        echo "ERROR: Could not determine target branch for $change_num"
        dependency_results+=("Change $change_num - was not applied because its target branch could not be determined")
        record_dependency failed "Target branch was not obtained"
        continue
    fi

    # Dependency status
    if [[ "$change_status" == "MERGED" ]]; then
        echo "INFO: Change $change_num is MERGED. Checking if already applied..."
    elif [[ "$change_status" == "ABANDONED" ]]; then
        echo "WARNING: Change $change_num is ABANDONED. Skipping..."
        dependency_results+=("Change $change_num - was not applied because it was abandoned")
        record_dependency failed "Change is abandoned"
        continue
    elif [[ "$change_status" == NEW ]]; then
        echo "INFO: Change $change_num is not merged (status: $change_status). Will apply if possible..."
    else
        dependency_results+=("Change $change_num - was not applied because of unsupported status: $change_status")
        record_dependency failed "Unsupported Gerrit change status"
        continue
    fi

    # Capture the special already-present result without toggling errexit.
    echo "Attempting to apply dependency..."
    fetch_result=0
    fetch_ref_spec "$project_name" "https://$gerrit_host/$gerrit_user/$project_name" \
        "$commit_hash" "$target_branch" "$refs_changes" || fetch_result=$?

    if [ $fetch_result -eq 0 ]; then
        dependency_results+=("Change $change_num - was applied")
        record_dependency applied
    elif [ $fetch_result -eq 2 ]; then
        dependency_results+=("Change $change_num - was not applied because it is already in the repo")
        record_dependency already_present
    else
        dependency_results+=("Change $change_num - was not applied because of error: $application_error")
        record_dependency failed "$application_error"
    fi
done
dependency_scan_complete=1

# Always print summary at the end
print_dependency_summary

echo "******************** end handle-depend-on-instructions.sh *************************"
exit "$dependency_failed"
