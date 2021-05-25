#!/usr/bin/env bash
# (See this directory's README.md for remarks on bash methodology)
[[ -n "$_fetch_prebuilt_sh" ]] && return || readonly _fetch_prebuilt_sh=1

#
# fetch-prebuilt.sh
#
# Several aspects of the build process require a Ren-C executable to run them
# in what is called the "prep" step.  This step generates a lot of headers
# and compressed data files that are fed to the C compiler.
#
# For platforms that already have a prebuilt Ren-C available, this downloads
# a binary usable for build from the AWS download location--if it's not
# already downloaded.
#
# (Bootstrapping to a new platform is accomplished by performing the prep on
# a system that already has an executable, then zipping the %prep/ directory
# and moving it to the new machine, then building with C and make.)
#
# NOTES:
#
#   * There was unfortunately no particular vetting process for picking the
#     moment of these bootstrap executables.  They represent a somewhat
#     arbitrary past snapshot whose feature set isn't particularly suited to
#     anything besides building the system.  See notes in %bootstrap-shim.r
#     regarding how these older executables are tweaked to make them more in
#     line with current conventions.
#

source ${repo_dir}tools/bash/log.sh
source ${repo_dir}tools/bash/identify-os.sh
source ${repo_dir}tools/bash/download.sh

fetch_prebuilt() {
    local pb_dir="${repo_dir}prebuilt/"

    local localsys=$(identify_os)
    if [[ $localsys = "none" ]]; then
        log "Error: No precompiled R3 binary for your OS."
        exit 1
    else
        log "Searching for a prebuilt binary for $localsys..."

        local prebuilt="$(find ${pb_dir} -name "*$localsys*" | head -n 1)"
        if [[ -n $prebuilt ]]; then
            log "Prebuilt exists: $prebuilt"
        else
            log "Get the list of available prebuilt binaries from S3"
            local s3url=https://r3bootstraps.s3.amazonaws.com/
        
            local xml=$(download_echo $s3url)
        
            # Use tr instead of sed to replace newlines
            # (because sed is not really powerful on Mac/BSD)
            #
            local list=$(
                echo "$xml" | tr "<" "\n" | sed -n -e 's/^Key>\(+*\)/\1/p'
            )
        
            for pb in $list
            do
                # Download only prebuilt binaries for the current OS
                #
                # !!! If there's more than one, this will end up returning
                # the last one downloaded.  What should it do instead?
                #
                if [[ $pb == *"$localsys"* ]]; then
                    local s3pb="$s3url$pb"

                    prebuilt="${pb_dir}$pb"
                    download_file "$s3pb" "$prebuilt"

                    log "Marking with executable bit..."
                    chmod +x "$prebuilt" 1>$to_log
                fi
            done
        fi
    fi

    # return the filename (via echo)
    #
    echo $prebuilt
}
