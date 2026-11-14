## Filesystem Extension

This is a simple extraction of the R3-Alpha Windows and POSIX code for files
and directories.  There was little change in functionality to this code in
Revolt--the major issue was just keeping it running across other major changes
to the system (notably switching to storing UTF-8 Everywhere for strings).

A key the motivation for extracting the code is to make it possible to build
without it (e.g. the Emscripten build).
