This extension implements a process spawning interface known as CALL.  CALL was 
in Rebol2, but was not a feature released in the open source R3-Alpha:

http://www.rebol.com/docs/shell.html

Atronix implemented a version of it which was forced to go through a somewhat
circuitous method of host services.  This extension attempts to simplify the
mechanism by being a bit more like a single native with #ifdefs for the
platforms in question, which cuts down on redundancy and can also make use
of internal APIs that were not available to extensions in R3-Alpha.
