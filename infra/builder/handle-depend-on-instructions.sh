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

# input: $1 - project name
#        $2 - fetch_cmd
# output: N/A
# descr: go to the project repo and perform git fetch
fetch_ref_spec() {
    local project_name=$1
    local fetch_cmd=$2
    project_repo="${WORKSPACE}/$project_name"

    if [[ "$project_name" == "dnvme" || "$project_name" == "tnvme" ]]; then
        project_repo="${WORKSPACE}/nvmeCompl/$project_name"
    fi

    echo "------------- start fetch_ref_spec ----------------"
    echo "FETCH CMD: $fetch_cmd"
    echo "PROJECT REPO: $project_repo"
    pushd "$project_repo"
    echo "PWD: $(pwd)"
    echo "BRANCH PRIOR TO FETCH CMD: $(git branch -a)"
    echo "REPO STATUS PRIOR TO FETCH CMD: $(git status)"

    if eval "$fetch_cmd"; then
        echo "SUCCESS - Fetched and local repo was updated"
    else
        echo "FAILED to fetch due to a conflict/s or a commit that is already in the current history"
        echo "REPO STATUS AFTER THE FETCH: $(git status)"
        echo "Resetting the local repo"
        git reset --hard
        exit 1
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

# Get commit message if triggered from Gerrit
if [[ "$BUILD_SOURCE" == "gerrit" ]]; then
    echo "Fetching commit message from Gerrit REST API..."
    # Remove the )]}' prefix that Gerrit adds to prevent XSSI attacks
    commit_message=$(curl -s "https://${GERRIT_HOST}/changes/${GERRIT_USER}%2F${PROJECT}~${CHANGE_ID}/revisions/current/commit" \
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

    url_no_c_no_plus=$(echo "$url" | sed -e 's-\/c--g' -e 's-\/+--g')
    change_num=$(echo "$url_no_c_no_plus" | grep -o '[0-9]*$')
    gerrit_host=$(echo $url_no_c_no_plus | cut -d "/" -f 3)
    gerrit_user=$(echo $url_no_c_no_plus | cut -d "/" -f 4)
    project_name=$(echo $url_no_c_no_plus | cut -d "/" -f 5)
    gerrit_port="29418"

    echo ""
    echo "------------------------------------------------------------"
    echo "DEPENDENCY URL: $url"
    echo "PROJECT: $project_name"
    echo "CHANGE: $change_num"
    echo "GERRIT HOST: $gerrit_host"
    echo "GERRIT USER: $gerrit_user"
    echo "------------------------------------------------------------"

    echo "NOTE: this line works only with a public key added to the $gerrit_host"
    gerrit_query=$(ssh -p $gerrit_port $gerrit_user@$gerrit_host gerrit query --current-patch-set --format=JSON change:$change_num | jq)
    echo "GERRIT QUERY: $gerrit_query"

    refs_changes=$(echo "$gerrit_query" | jq -r '.currentPatchSet.ref' | head -1)

    fetch_cmd="git fetch https://$gerrit_host/$gerrit_user/$project_name $refs_changes && git cherry-pick FETCH_HEAD"
    echo "FETCH CMD: $fetch_cmd"

    if [[ " ${projectArr[*]} " =~ " $project_name " ]]; then
        fetch_ref_spec "$project_name" "$fetch_cmd"
    else
        echo "$ERROR_INVALID_PROJECT_NAME_OR_URL"
        exit 1
    fi
done

echo "******************** end handle-depend-on-instructions.sh *************************"
