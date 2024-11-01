## Structure And Flag Definitions

This folder is meant to contain very little *code* (e.g. few functions or
function-like macros).  It's only the `struct` and #define of flags that are
used in the core.  Most of the content is comments.

All of these files are included *before* the inclusion of the automatically
gathered header file, %tmp-internals.h.  Mechanically this allows anything that
has to have its definition complete to be fully defined to be used by value in
the interface.  That's not usually strictly necessary--because most things are
taken as pointers not by value, and so only need a forward declaration.
But when building as C++ in the checked build, some definitions become "smart
pointers" which need full class definitions to be used in an interface.

Breaking things out in this way also helps to better see the dependencies.
Being before %tmp-internals.h limits the list of what can be done in an inline
function, so that discourages writing a lot of inline functions in the files.
This offers guidance on helping to organize the source, which otherwise would
have no natural cue on when to break things into separate include files.

It may be interesting to have some of the information in these files be
generated from tables.  If the flags were generated in this way, then it would
mean features could be developed like automatically generating bitfield
"pun" structures to mirror the bit layout (that currently has to be done by
hand, so it tends to get outdated).
