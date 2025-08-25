#!/bin/bash
set -e

show_help() {
    cat << EOF

Usage: $(basename "$0") [workspace] [change id] [Gerrit User Name] [build source: gerrit | timer]

Description: This script fetches and applies all the commits your commit depends on.

Examples:
  From Gerrit change:
    $(basename "$0") workspace 123454 davidsaOpenu gerrit

  From timer:
    $(basename "$0") workspace unused davidsaOpenu timer

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

# input: $1 - project name
#        $2 - fetch_cmd
#        $3 - commit hash
# output: N/A
# descr: go to the project repo and perform git fetch
fetch_ref_spec() {
    local project_name=$1
    local fetch_cmd=$2
    local commit_hash=$3
    project_repo="${WORKSPACE}/$project_name"

    if [[ "$project_name" == "dnvme" || "$project_name" == "tnvme" ]]; then
        project_repo="${WORKSPACE}/nvmeCompl/$project_name"
    fi

    echo "------------- start fetch_ref_spec ----------------"
    echo "FETCH CMD: $fetch_cmd"
    echo "PROJECT REPO: $project_repo"
    echo "COMMIT HASH: $commit_hash"

    pushd "$project_repo"
    echo "PWD: $(pwd)"
    echo "BRANCH PRIOR TO FETCH CMD: $(git branch -a)"
    echo "REPO STATUS PRIOR TO FETCH CMD: $(git status)"

    # Check if the commit is already in the current repository
    if git cat-file -e "$commit_hash" 2>/dev/null; then
        echo "SUCCESS - Commit $commit_hash is already present in the repository"
        echo "   Status: ALREADY_IN_REPO - No action needed"
        popd
        echo "PWD: $(pwd)"
        echo "------------- end fetch_ref_spec ----------------"
        return 2  # Special return code for "already in repo"
    fi

    # Check if the commit is reachable from any branch
    if git merge-base --is-ancestor "$commit_hash" HEAD 2>/dev/null; then
        echo "SUCCESS - Commit $commit_hash is already an ancestor of HEAD"
        echo "   Status: ALREADY_IN_REPO - Commit is in history"
        popd
        echo "PWD: $(pwd)"
        echo "------------- end fetch_ref_spec ----------------"
        return 2  # Special return code for "already in repo"
    fi

    # Proceed with fetch and cherry-pick
    echo "Attempting to fetch and cherry-pick..."
    set +e  # Don't exit on error for this section

    eval "$fetch_cmd"
    local exit_code=$?

    set -e  # Re-enable exit on error

    if [ $exit_code -eq 0 ]; then
        echo "SUCCESS - Fetched and applied dependency successfully"
        echo "   Status: NEWLY_APPLIED - Cherry-pick completed"
    else
        echo "Git command failed with exit code: $exit_code"
        echo "REPO STATUS AFTER THE FETCH: $(git status)"

        # Check if we're in a cherry-pick state
        if [[ -f .git/CHERRY_PICK_HEAD ]]; then
            echo "Cherry-pick in progress. Checking if it's an empty commit..."

            # Check if the cherry-pick resulted in no changes (empty commit)
            if git diff --cached --quiet && git diff --quiet; then
                echo "Cherry-pick resulted in no changes (commit already applied). Skipping..."
                echo "   Status: ALREADY_IN_REPO - Empty cherry-pick skipped"
                git cherry-pick --skip
                popd
                echo "PWD: $(pwd)"
                echo "------------- end fetch_ref_spec ----------------"
                return 2  # Special return code for "already in repo"
            else
                echo "Cherry-pick has conflicts or other issues"
                echo "   Status: FAILED - Conflicts detected"
                echo "Aborting cherry-pick and resetting repository"
                git cherry-pick --abort
                git reset --hard
                popd
                return 1  # Failed
            fi
        else
            echo "FAILED to fetch due to other reasons"
            echo "   Status: FAILED - Unknown error"
            echo "Resetting the local repo"
            git reset --hard
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

# Get commit message if triggered from Gerrit
if [[ "$BUILD_SOURCE" == "gerrit" ]]; then
    echo "Fetching commit message from Gerrit REST API..."
    # Remove the )]}' prefix that Gerrit adds to prevent XSSI attacks
    commit_message=$(curl --noproxy "*" -s "https://${GERRIT_HOST}/changes/${GERRIT_USER}%2F${PROJECT}~${CHANGE_ID}/revisions/current/commit" \
        | sed "1s/^)]}'*//" | jq -r '.message' 2>/dev/null)

    if [[ -z "$commit_message" || "$commit_message" == "null" ]]; then
        echo "ERROR: Failed to retrieve commit message from Gerrit"
        echo "Trying alternative method using SSH..."
        # Fallback to SSH method if REST API fails
        commit_message=$(ssh -p 29418 "${GERRIT_USER}@${GERRIT_HOST}" \
            gerrit query --current-patch-set --format=JSON "change:${CHANGE_ID}" \
            | jq -r '.commitMessage' | head -1)

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
    exit 0
fi

declare -a projectArr=("kernel" "dnvme" "nvme-cli" "qemu" "simulator" "tnvme" "open-osd")

ERROR_INVALID_PROJECT_NAME_OR_URL="
Invalid url was specified following the 'depends-on:' in the commit message.
The name of the project should be one of the following:
$(printf '%s\n' ${projectArr[@]})
"

# "|| true" to avoid script fail for commit message that contains no depends-on: http.* lines
depends_on_lines=$(echo "$cleaned_commit_message" | grep -i 'depends-on' | grep -o 'http.*') || true

echo "PARSING DEPENDS-ON LINES"

for url in $depends_on_lines; do
    # Remove trailing slash if it exists
    url=$(echo "$url" | sed 's:/*$::')

    # Parse Gerrit URL properly
    # URL format: https://review.gerrithub.io/c/davidsaOpenu/qemu/+/1220521
    gerrit_host=$(echo "$url" | cut -d "/" -f 3)
    change_num=$(echo "$url" | grep -o '[0-9]*$')

    # Extract user and project from the /c/user/project/ part
    url_path=$(echo "$url" | sed 's|https://[^/]*/c/||' | sed 's|/+/[0-9]*$||')
    gerrit_user=$(echo "$url_path" | cut -d "/" -f 1)
    project_name=$(echo "$url_path" | cut -d "/" -f 2)
    gerrit_port="29418"

    echo ""
    echo "------------------------------------------------------------"
    echo "DEPENDENCY URL: $url"
    echo "PROJECT: $project_name"
    echo "CHANGE: $change_num"
    echo "GERRIT HOST: $gerrit_host"
    echo "GERRIT USER: $gerrit_user"
    echo "------------------------------------------------------------"

    # Try to get change info - first try REST API, then SSH
    change_status=""
    commit_hash=""
    refs_changes=""

    # Try REST API first
    echo "Trying REST API..."
    api_response=$(curl --noproxy "*" -s "https://${gerrit_host}/changes/${gerrit_user}%2F${project_name}~${change_num}/detail" 2>/dev/null)

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

            # Get the revision hash
            current_revision=$(echo "$clean_response" | jq -r '.current_revision' 2>/dev/null || echo "null")
            echo "Debug: current_revision from API: '$current_revision'"

            # If current_revision is null, try to get it from revisions object
            if [[ "$current_revision" == "null" || -z "$current_revision" ]]; then
                echo "Debug: get revision from revisions object..."
                # Keys available in revisions
                revision_keys=$(echo "$clean_response" | jq -r '.revisions | keys[]' 2>/dev/null || echo "")
                echo "Debug: Available revision keys: '$revision_keys'"

                if [[ -n "$revision_keys" ]]; then
                    # Get the first revision key
                    current_revision=$(echo "$revision_keys" | head -1)
                    echo "Debug: Using first revision key: '$current_revision'"
                else
                    echo "Debug: No revisions found, trying alternative approach..."
                    # Try to get it from a different path - sometimes it's in the response differently
                    current_revision=$(echo "$clean_response" | jq -r 'try (.revisions | to_entries[0].key) // empty' 2>/dev/null || echo "")
                fi
            fi

            echo "Debug: Final current_revision: '$current_revision'"

            if [[ -n "$current_revision" && "$current_revision" != "null" && "$current_revision" != "PARSE_ERROR" ]]; then
                commit_hash="$current_revision"
                echo "Debug: Using commit_hash: '$commit_hash'"

                # Get the ref from the revision details
                refs_changes=$(echo "$clean_response" | jq -r ".revisions.\"$current_revision\".ref" 2>/dev/null || echo "null")
                echo "Debug: refs from API: '$refs_changes'"

                # If ref is not available, construct it
                if [[ "$refs_changes" == "null" || -z "$refs_changes" ]]; then
                    change_suffix=$(printf "%02d" $((change_num % 100)))
                    refs_changes="refs/changes/${change_suffix}/${change_num}/1"
                    echo "Debug: Constructed refs: '$refs_changes'"
                fi
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

    # If REST API failed, try SSH
    if [[ -z "$change_status" ]]; then
        echo "REST API failed, trying SSH..."
        ssh_result=$(ssh -p $gerrit_port $gerrit_user@$gerrit_host gerrit query --current-patch-set --format=JSON change:$change_num 2>/dev/null)

        if [[ -n "$ssh_result" ]]; then
            echo "SSH successful"
            change_status=$(echo "$ssh_result" | jq -r '.status' | head -1)
            commit_hash=$(echo "$ssh_result" | jq -r '.currentPatchSet.revision' | head -1)
            refs_changes=$(echo "$ssh_result" | jq -r '.currentPatchSet.ref' | head -1)
        fi
    fi

    # Check if we got the info
    if [[ -z "$change_status" ]]; then
        echo "ERROR: Could not retrieve change status for $change_num"
        dependency_results+=("Change $change_num - was not applied because API failed")
        continue
    fi

    # If we have status but no commit hash, try SSH as fallback
    if [[ -z "$commit_hash" ]]; then
        echo "DEBUG: REST API got status but no commit hash, trying SSH fallback..."
        ssh_result=$(ssh -p $gerrit_port $gerrit_user@$gerrit_host gerrit query --current-patch-set --format=JSON change:$change_num 2>/dev/null)

        if [[ -n "$ssh_result" ]]; then
            echo "SSH fallback successful"
            commit_hash=$(echo "$ssh_result" | jq -r '.currentPatchSet.revision' | head -1)
            refs_changes=$(echo "$ssh_result" | jq -r '.currentPatchSet.ref' | head -1)
            echo "Got from SSH - Hash: $commit_hash, Ref: $refs_changes"
        fi
    fi

    # If still no commit hash, construct what we can
    if [[ -z "$commit_hash" ]]; then
        echo "WARNING: Could not get commit hash, but change status is $change_status"
        if [[ "$change_status" == "MERGED" ]]; then
            echo "INFO: Since change is MERGED, it might already be in the repository"
            dependency_results+=("Change $change_num - was not applied because commit hash could not be determined (but change is merged)")
        else
            dependency_results+=("Change $change_num - was not applied because commit hash could not be determined")
        fi
        continue
    fi

    echo "CHANGE STATUS: $change_status"
    echo "COMMIT HASH: $commit_hash"

    # Dependency status
    if [[ "$change_status" == "MERGED" ]]; then
        echo "INFO: Change $change_num is MERGED. Checking if already applied..."
    elif [[ "$change_status" == "ABANDONED" ]]; then
        echo "WARNING: Change $change_num is ABANDONED. Skipping..."
        dependency_results+=("Change $change_num - was not applied because it was abandoned")
        continue
    else
        echo "INFO: Change $change_num is not merged (status: $change_status). Will apply if possible..."
    fi

    fetch_cmd="git fetch https://$gerrit_host/$gerrit_user/$project_name $refs_changes && git cherry-pick FETCH_HEAD"
    echo "FETCH CMD: $fetch_cmd"

    if [[ " ${projectArr[*]} " =~ " $project_name " ]]; then
        # Call fetch_ref_spec and capture the result
        echo "Attempting to apply dependency..."
        set +e  # Don't exit on error for fetch_ref_spec
        fetch_ref_spec "$project_name" "$fetch_cmd" "$commit_hash"
        fetch_result=$?
        set -e  # Re-enable exit on error

        # Store the result based on return code
        if [ $fetch_result -eq 0 ]; then
            dependency_results+=("Change $change_num - was applied")
        elif [ $fetch_result -eq 2 ]; then
            dependency_results+=("Change $change_num - was not applied because it is already in the repo")
        else
            dependency_results+=("Change $change_num - was not applied because of error")
        fi
    else
        echo "$ERROR_INVALID_PROJECT_NAME_OR_URL"
        dependency_results+=("Change $change_num - was not applied because of invalid project name")
        print_dependency_summary
        exit 1
    fi
done

# Always print summary at the end
print_dependency_summary

echo "******************** end handle-depend-on-instructions.sh *************************"
