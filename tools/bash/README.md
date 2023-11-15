# Stylized Bash Helper Scripts

Long term, a goal is to use the Ren-C interpreter itself to perform most
functions in building and testing.  This would include things like downloading
files off the Internet (instead of using wget or curl).

But getting an executable in the first place requires using some kind of
download tool (!)  So there's always going to be some amount of bash scripting
to kick off the process.  And realistically, there are many configuration
tasks in the build that are just easier to do in bash (...for now...)

This directory contains utility scripts for running in bash.


## Files Containing Functions

For better maintainability and reuse, the scripts are broken down into
individual files of functions.  Since there is a `local` keyword for declaring
local variables, this is one of the few tools complex bash scripts have for
cleanly isolating parts from one another.

To import these functions into a script, one uses `source`.  Then you call
the functions, and you can capture their output into variables:

    source ${repo_dir}bash/fetch-prebuilt.sh
    r3_make=$(fetch_prebuilt)  # returns the prebuilt r3 executable path


## REPO_DIR should be defined

It's expected that the Ren-C repository path is set in the $repo_dir variable
when you source the utility scripts.

**Note that the Ren-C convention for directories is to include the slash.**
This does look a bit more awkward in bash, but at least it tends to make
mistakes more obvious...while doubling up slashes is often glossed over in
a sloppy way.


## Include Guards

The scripts can be included multiple times, with only the first time counting.
So each can be explicit about its dependencies.  This is done with an "include
guard" at the top of each script:

https://coderwall.com/p/it3b-q/bash-include-guard


## Status Messages Separated From Stdout

Like bash scripts, bash functions can only return small integer codes.  The
main currency of exchange is through whatever gets written to stdout.  But
this becomes contentious with wanting to write status information.  In order
to avoid mixing the two, logging from %log.sh uses this method:

https://stackoverflow.com/a/18581814/
