# SPECS DIRECTORY

This directory is for Rebol-syntax files that declaratively inform the building
of the interpreter.

Some of these files embed directly as Rebol (such as %system.r, which defines
the "system object", available at runtime as SYSTEM or SYS).

Other files are tables that are processed and expanded into C headers and
sources (such as %types.r, which lays out the ordering and categorization
of the fundamental datatypes, producing a large number of #defines and
helper macros/functions for type testing).

Discernment needs to be used to decide how much investment should be made in
abstracting the C sources with Rebol tables and bootstrapping Rebol code
(vs. just simply writing the C out manually).  Not only does the Rebol code
have to be written and maintained, but every change to a file in %specs/
will require re-running the bootstrap preparation step...which can be annoying
if that has to be done too frequently.

But there is a tremendous amount of leverage by using abstraction and table
driven methods for things like %types.r - so it's a powerful tool that should
be used when appropriate.
