This is an external dependencies directory; e.g. source for projects that are not
part of the Ren/C project or maintained as part of its version history.  Directories
are either Git submodules or instructions will be added to this file for how to
get ahold of the dependencies.

Inspired by Rebol's choice to do targeted subsetting of dependent C libraries via
Rebol scripts and include that source, the goal is not to require a large dependent
build process.  Hence any build process required by these dependencies should be
taken care of by a script.  (See make-zlib for an example.)

