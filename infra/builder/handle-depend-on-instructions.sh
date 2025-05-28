#!/bin/bash
set -e


show_help() {
    cat << EOF

Usage: $(basename "$0") [working directory] [change id] [Gerrit User Name]

Description: This script fetches all the commits your commit depends on.

Example: $(basename "$0") ~/Documents/OSW 123456 MyGerritUserName

for https://review.gerrithub.io/c/davidsaOpenu/simulator/+/1234 - the change id is 1234

EOF
    exit 0
}

if [ $# -eq 0 ]; then
    show_help
fi


fetch_command_parsed=$(curl -s "https://review.gerrithub.io/changes/davidsaOpenu%2Fsimulator~$2/revisions/current/commit" | sed -n 's/.*"message":"\([^"]*\)".*/\1/p' | sed 's/\\n/\n/g')

working_dir=$(realpath "$1")

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
#        $3 - gerrit username
# output: N/A
# descr: go to the project repo and perform git fetch
fetch_ref_spec() {
    local project_name=$1
    local fetch_cmd=$2
    project_repo="${WORKSPACE}/$project_name"

    # For compatibility with pipeline commands
    if [ -n "$3" ]; then
        fetch_cmd=$fetch_command_parsed
    fi
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

WORKSPACE=$1 # WORKSPACE env variable of Jenkins

# Pipeline commands compatibility
if [ -n "$3" ]; then
    commit_message=$(echo "$fetch_command_parsed") # GERRIT_CHANGE_SUBJECT env variable of GerritTrigger
else
    commit_message=$(echo "$2" | base64 --decode)
fi
echo "COMMIT MESSAGE: $commit_message"
cleaned_commit_message=$(clean_text "$commit_message")

echo "******************** start handle-depend-on-instructions.sh *************************"
echo "CLEANED COMMIT MESSAGE: $cleaned_commit_message"
echo "WORKSPACE: $WORKSPACE"

#cleaned_commit_message="Title

#line #1
#line #2

#depends--on: https://review.gerrithub.io/c/davidsaOpenu/qemu/+/496901
#depends-on: https://review.gerrithub.io/c/davidsaOpenu/simulator/+/413565
#Depends-On:https://review.gerrithub.io/c/davidsaOpenu/dnvme/+/520344

#line #X
#line #X+1
#"

#WORKSPACE="/home/davidsa/eVSSIM"

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
    if [ -n "$3" ]; then
        gerrit_user=$(echo $3 | cut -d "/" -f 4)
    else
        gerrit_user=$(echo $url_no_c_no_plus | cut -d "/" -f 4)
    fi
    project_name=$(echo $url_no_c_no_plus | cut -d "/" -f 5)
    gerrit_port="29418"

    echo "NOTE: this line works only with a public key added to the $gerrit_host"
    gerrit_query=$(ssh -p $gerrit_port $gerrit_user@$gerrit_host gerrit query --current-patch-set --format=JSON change:$change_num | jq)
    echo "GERRIT QUERY: $gerrit_query"

    refs_changes=$(echo "$gerrit_query" | jq -r '.currentPatchSet.ref' | head -1)

    # Pipeline compatibility
    if [ -n "$3" ]; then
        fetch_cmd="git fetch https://$gerrit_host/davidsaOpenu/$project_name $refs_changes && git cherry-pick FETCH_HEAD"
    else
        fetch_cmd="git fetch https://$gerrit_host/$gerrit_user/$project_name $refs_changes && git cherry-pick FETCH_HEAD"
    fi
    echo "FETCH CMD: $fetch_cmd"

    if [[ " ${projectArr[*]} " =~ " $project_name " ]]; then
        fetch_ref_spec "$project_name" "$fetch_cmd"
    else
        echo "$ERROR_INVALID_PROJECT_NAME_OR_URL"
        exit 1
    fi
done

echo "******************** end handle-depend-on-instructions.sh *************************"

