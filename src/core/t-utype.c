/***********************************************************************
**
**  This file should be GC'd during the next make-make disruption
**  (changing the files makes it more troublesome to switch around
**  branches, and is best done in a batch when there's an important
**  reason to do so.)
**
**  Also in that pending list: move linux's dev-serial.c to posix,
**  as it appears to not have anything linux-specific about it.
**
***********************************************************************/

// empty translation units are forbidden, must have something
static int Remove_Utype;
