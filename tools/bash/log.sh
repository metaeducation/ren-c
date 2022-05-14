#!/usr/bin/env bash
# (See this directory's README.md for remarks on bash methodology)
[[ -n "$_log_sh" ]] && return || readonly _log_sh=1

#
# log.sh
#
# Out-of-band logging mechanism that prevents the mixing of status information
# with the stdout intended to be used for information exchange between scripts
# and functions:
#
# https://stackoverflow.com/a/18581814
#
# !!! While it would be nice to have more logging options, we're actually not
# intending to have all that many bash scripts.
#

exec 3>&1  # open new file descriptor that redirects to stdout

# !!! Initially it was attempted to define a variable here so you could use
# the output location in other routines that weren't shaped like log().
#
#     to_log=3  # wanted to redirect output here, e.g. `ls 1> $to_log`
#
# Sadly this cannot work; you can't abstract redirection across file names
# and file handles...
#
# https://stackoverflow.com/a/35556881
#
# Instead, trust the domain knowledge for this and put a comment to see %log.sh
# whenever using `1>&3` to stream output to the log.

log () {
    echo "-- $1" 1>&3  # shouldn't get mixed up with function outputs
}
