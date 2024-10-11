## UTF-16/UTF-32 HANDLING EXTENSION

R3-Alpha had an incomplete model for doing codecs, that required C coding
to implement...even though the input and output types to DO-CODEC were
Rebol values.  Under Ren-C these are done as plain ACTION!s, which can
be coded in either C as natives or Rebol.

A few incomplete text codecs were included in R3-Alpha, and have been
kept around for testing.  They were converted here into groups of native
functions.  UTF-8 is the focus of Ren-C (handling of which is not
optional, the scanner depends on it).  UTF-16 and UTF-32 have very little
modern relevance...so related routines were moved here in order to make
the core build smaller.
