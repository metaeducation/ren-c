#!/usr/bin/env bash
# (See this directory's README.md for remarks on bash methodology)
[[ -n "$_identify_os_sh" ]] && return || readonly _identify_os_sh=1

#
# identify-os.sh
#
# This uses the `uname` command to determine what kind of system is running.
#

source ${repo_dir}tools/bash/log.sh

identify_os() {
    local usys=$(uname -s | tr '[:upper:]' '[:lower:]')
    log "uname reports your system is: $usys"

    case $usys in
        *cygwin*)  echo "windows";;
        *mingw*)   echo "windows";;
        *msys*)    echo "windows";;
        *windows*) echo "windows";;
        *linux*)
            case $(uname -o | tr '[:upper:]' '[:lower:]') in
                *android*) echo "android";;
                *)         echo "linux";;
            esac
        ;;
        *openbsd*) echo "openbsd";;
        *darwin*)  echo "osx";;
        *)         echo "none";;
    esac
}
