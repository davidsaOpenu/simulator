#!/bin/bash
set -e

WORKSPACE=$1 # WORKSPACE env variable of Jenkins
commit_message=$2 #GERRIT_CHANGE_SUBJECT env variable of GerritTrigger

echo "$commit_message"
echo "$WORKSPACE"


# input: $1 - project name
#        $2 - fetch_cmd
# output: N/A
# descr: go to the project repo and perform git fetch
fetch_ref_spec() {
    local project_name=$1
    local fetch_cmd=$2
    project_repo="${WORKSPACE}/$project_name"
    if [[ "$project_name " == " dnvme " || " $project_name " == " dnvme " ]]; then
        project_repo="${WORKSPACE}/nvmeCompl/$project_name"
    fi
    pushd "$project_repo"
    echo "*********************************************************"
    echo "PROJECT_REPO: $project_repo"
    echo "FETCH COMMAND: $fetch_cmd"
    echo "REPO STATUS PRIOR TO FETCH: $(git status)"
    if eval "$fetch_cmd"; then
        echo "SUCCESS - Fetched and local repo was updated"
    else
        echo "FAILED to fetch due to a conflict/s or a commit that is already in the current history"
	echo "REPO STATUS AFTER THE FETCH: $(git status)"
	echo "resseting the local repo"
	git reset --hard
	exit 1
    fi
    popd
}


commit_message="Title

line #1
line #2

depends--on: https://review.gerrithub.io/c/davidsaOpenu/qemu/+/496901
depends-on: https://review.gerrithub.io/c/davidsaOpenu/simulator/+/413565
Depends-On:https://review.gerrithub.io/c/davidsaOpenu/dnvme/+/520344

line #X
line #X+1
"

#WORKSPACE="/home/davidsa/eVSSIM"

commit_message=$GERRIT_CHANGE_SUBJECT

echo "$commit_message"
echo "$WORKSPACE"

declare -a projectArr=("3.16.2" "dnvme" "nvme-cli" "qemu" "simulator" "tnvme")

ERROR_INVALID_PROJECT_NAME_OR_URL="
Invalid url was specified following the 'depends-on:' in the commit message.
The name of the project should be one of the following: 
$(printf '%s\n' ${projectArr[@]})
"

depends_on_lines=$(echo "$commit_message" | grep -i 'depends-on' | grep -o 'http.*')

for url in $depends_on_lines; do
    url_no_c_no_plus=$(echo "$url" | sed -e 's-\/c--g' -e 's-\/+--g' )
    change_num=$(echo "$url_no_c_no_plus" | grep -o '[0-9]*$')
    gerrit_host=$(echo $url_no_c_no_plus | cut -d "/" -f 3)
    gerrit_user=$(echo $url_no_c_no_plus | cut -d "/" -f 4)
    project_name=$(echo $url_no_c_no_plus | cut -d "/" -f 5)
    gerrit_port="29418"

    echo "NOTE: this line works only with a public key added to the $gerrit_host"
    gerrit_query=$(ssh -p $gerrit_port $gerrit_user@$gerrit_host gerrit query --current-patch-set --format=JSON change:$change_num | jq)
    echo "$gerrit_query"
    
    refs_changes=$(echo "$gerrit_query" | jq -r '.currentPatchSet.ref' | head -1)
    
    fetch_cmd="git fetch https://$gerrit_host/$gerrit_user/$project_name $refs_changes && git cherry-pick FETCH_HEAD"
    echo "$fetch_cmd"

    if [[ " ${projectArr[*]} " =~ " $project_name " ]]; then
	fetch_ref_spec "$project_name" "$fetch_cmd" 
    else
        echo "$ERROR_INVALID_PROJECT_NAME_OR_URL"
        exit 1
    fi 
done


