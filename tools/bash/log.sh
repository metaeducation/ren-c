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

to_log=3  # redirect output here, e.g. `ls 1> $to_log`

log () {
    # !!! Originally this code said `1>&3`.  It's not possible to put the &
    # inside the definition of $to_log, or you get "ambiguous redirect".
    # Dropping the & doesn't appear to have consequences in this case.
    #
    echo "-- $1" 1>$to_log  # shouldn't get mixed up with function outputs
}

