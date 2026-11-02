#!/bin/bash

################################# clean-all.sh #################################
#
# This script cleans and resets the eVSSIM project's git repositories,
# cleaning up all compilation artifacts, resetting local changes, and
# removing untracked files, attempting to recreate a "brand new" state of
# the project. As part of the cleanup, the 'data/', 'dist/', and 'logs/'
# directories are also deleted.
#
# Repositories with local changes require user confirmation before being
# cleaned, which can be turned off with '--assume-yes' and '--assume-no'
# flags. 'data/', 'dist/', 'logs/' deletion can be turned off with
# '--no-delete-*' flags. '--dry-run' flag allows running the script and
# displaying it's prompts and output, watching what would have been cleaned
# without running any destructive commands. '--check' flag shows what needs
# to be cleaned and what is already clean.
#
# Note:
# clean-all.sh is destructive and WILL permanently remove any unsaved local
# changes from the git repositories and generated(data,dist,logs)
# directories. This INCLUDES local changes in the 'simulator' repo!
#
# Run with --help for available options.
#
################################################################################

set -euo pipefail

DEL_DATA=yes
DEL_DIST=yes
DEL_LOGS=yes
# 'yes' for --assume-yes, 'no' for --assume-no, 'ask' otherwise.
ASSUME=ask
DRY_RUN=no
CHECK=no

PROGNAME=$(basename "$0")
EVSSIM_ENV_VARS=${!EVSSIM_*}

usage() {
    cat <<EOF
Usage: $PROGNAME [OPTION]...

Description:
  Cleans the eVSSIM project folders, attempting to reset any compilation,
  build, and environment changes/artifacts left by running parts of the
  project.
  The eVSSIM git repos are cleaned and their working tree restored back to
  master branch's latest local commit.
  The 'data/', 'dist/', and 'logs/' directories are deleted unless one of
  the '--no-delete' options have been passed.
  The 'EVSSIM_' environmental variables are unset, unless the
  '--no-clean-env' option has been passed.

Options:
  -y, --assume-yes      Automatically answer yes to confirmation prompts.
  -n, --assume-no       Automatically answer no to confirmation prompts.
  -d, --dry-run         Show what would happen without executing anything.
  -C, --check           Do not perform any cleaning steps and do not
                        delete anything. Check everything that would have
                        been reset/deleted/discarded and report what needs
                        to be cleaned instead.
  -D, --no-delete       Do not delete 'data/', 'dist/', and 'logs/'
                        directories.
      --no-delete-data  Do not delete 'data/' directory.
      --no-delete-dist  Do not delete 'dist/' directory.
      --no-delete-logs  Do not delete 'logs/' folder.
      --bash-completion Generate bash command for bash completion.
  -h, --help            Display this help message and exit.
EOF
}

# ==================== CLI Options Handling ====================

if ! command -v getopt > /dev/null; then
    echo "$PROGNAME: error: 'getopt' command is required. Install util-linux." >&2
    exit 1
fi

opts=$(getopt -o "yndCDh" -l "assume-yes,assume-no,dry-run,check,no-delete,no-delete-data,no-delete-dist,no-delete-logs,bash-completion,help" --name "$PROGNAME" -- "$@") || (usage && exit 2)
eval set -- "$opts"
while true; do
    case "$1" in
        -y|--assume-yes)   ASSUME=yes; shift ;;
        -n|--assume-no)    ASSUME=no; shift ;;
        -d|--dry-run)      DRY_RUN=yes; shift ;;
        -C|--check)        CHECK=yes; shift ;;
        -D|--no-delete)    DEL_DATA=no DEL_DIST=no DEL_LOGS=no; shift ;;
        --no-delete-data)  DEL_DATA=no; shift ;;
        --no-delete-dist)  DEL_DIST=no; shift ;;
        --no-delete-logs)  DEL_LOGS=no; shift ;;
	--bash-completion) echo complete -W \"-y --assume-yes -n --assume-no -d --dry-run -D --no-delete --no-delete-data --no-delete-dist --no-delete-logs --bash-completion -h --help\" ./$PROGNAME; exit ;;
        -h|--help)         usage; exit ;;
	--)                break ;;
    esac
done

# ==================== Actual Cleaning Code ====================

is_repo_clean() {
    local path=$1
    if [ -n "$(git -C "$path" status --porcelain --ignore-submodules=none)" ]; then
        return 1
    elif [ -n "$(git -C "$path" clean -ndx)" ]; then
        return 1
    fi
    return 0
}

check() {
    echo Checking generated directories:
    for dir in data dist logs; do
        dir_path="$EVSSIM_ROOT_PATH/$dir"
	if [ -d "$dir_path" ]; then
            echo "$dir_path: Present (will be removed)"
	else
            echo "$dir_path: Absent (nothing to remove)"
	fi
    done
    echo

    echo Checking Git repositories:
    for repo in kernel nvme-cli nvmeCompl/dnvme nvmeCompl/tnvme open-osd qemu simulator; do
        repo_path="$EVSSIM_ROOT_PATH/$repo"
        repo_name=$(basename $repo)
        if is_repo_clean "$repo_path"; then
            echo $repo_name: Clean
	else
            echo $repo_name: Dirty
	fi
    done
}

if [ "$CHECK" = "yes" ]; then
    check
    exit
fi

echo Removing generated directories:
if [ "$DEL_DATA" = "yes" ]; then
    echo Deleting "$EVSSIM_ROOT_PATH/data"...
    if [ "$DRY_RUN" = "no" ]; then
        rm -rf "$EVSSIM_ROOT_PATH/data"
    fi
fi
if [ "$DEL_DIST" = "yes" ]; then
    echo Deleting "$EVSSIM_ROOT_PATH/dist"...
    if [ "$DRY_RUN" = "no" ]; then
        rm -rf "$EVSSIM_ROOT_PATH/dist"
    fi
fi
if [ "$DEL_LOGS" = "yes" ]; then
    echo Deleting "$EVSSIM_ROOT_PATH/logs"...
    if [ "$DRY_RUN" = "no" ]; then
        rm -rf "$EVSSIM_ROOT_PATH/logs"
    fi
fi
echo Done
echo

for repo in kernel nvme-cli nvmeCompl/dnvme nvmeCompl/tnvme open-osd qemu simulator; do
    repo_path="$EVSSIM_ROOT_PATH/$repo"
    repo_name=$(basename $repo)
    echo Entering $repo_name:

    if [ -n "$(git -C "$repo_path" status --porcelain --ignore-submodules=none)" ]; then
        proceed=yes
        echo $repo_name has unsaved changes:
        git -C "$repo_path" status --porcelain
        if [ "$ASSUME" = "yes" ]; then
            proceed=yes
            echo "$repo_name: Are you sure you want to reset all changes? [y/N] y"
        elif [ "$ASSUME" = "no" ]; then
            proceed=no
            echo "$repo_name: Are you sure you want to reset all changes? [y/N] n"
        fi
        while [ "$ASSUME" = "ask" ]; do
            read -rp "$repo_name: Are you sure you want to reset all changes? [y/N] " yesno
            case ${yesno,,} in
                y|yes) proceed=yes; break ;;
                n|no|'') proceed=no; break ;;
            esac
        done
        if [ "$proceed" = "no" ]; then
            echo Skipping $repo_name...
	    echo
            continue
        fi
    fi

    echo Resetting and cleaning the repo...
    if [ "$DRY_RUN" = "no" ]; then
        git -C "$repo_path" submodule foreach --recursive 'git reset --hard && git clean -ffdx'
        git -C "$repo_path" reset --hard
        git -C "$repo_path" clean -ffdx
        git -C "$repo_path" switch master
        git -C "$repo_path" submodule update --recursive --force
        git -C "$repo_path" submodule foreach --recursive 'git reset --hard && git clean -ffdx'
        git -C "$repo_path" clean -ffdx
    fi
    echo Done
    echo
done

echo To clean environment variables in your session run:
echo "    unset $EVSSIM_ENV_VARS"
echo
echo Or \'unset '${!EVSSIM_*}'\'.
