#!/usr/bin/env bash
# (See this directory's README.md for remarks on bash methodology)
[[ -n "$_download_sh" ]] && return || readonly _download_sh=1

#
# download.sh
#
# Use either `curl` or `wget` to download data, detecting which is available.
#
# There are two forms, one for loading directly to a variable without creating
# a local file:
#
#     myvar=$(download_echo http://example.com/whatever.txt)
#
# And another form for making a local file:
#
#     download_file http://example.com/remote_name.txt local_name.txt
#

source ${repo_dir}tools/bash/log.sh

which_downloader() {
    which curl > /dev/null  # sets $? to an error signal if curl not in path
    if [ $? -eq 0 ] ; then
        echo "curl"
    else
        which wget > /dev/null
        if [ $? -eq 0 ] ; then
            echo "wget"
        else
            log "Error: you need curl or wget to download binaries."
            exit 1
        fi
    fi
}

download_echo() {  # $1 is source
    local dltool=$(which_downloader)
    if [ $dltool = "wget" ] ; then
        echo $(wget -q $1 -O -)
    else
        echo $(curl -s $1)
    fi
}

download_file() {  # $1 is source, $2 is target
    local dltool=$(which_downloader)
    log "Downloading $1 to $2"
    if [ $dltool = "wget" ] ; then
        wget -O "$2" -nv -o - "$1" $to_log
    else
        curl "$1" > "$2"
    fi
}
