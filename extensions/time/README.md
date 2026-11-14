## Time Extension

The C language has standard time functions:

https://en.cppreference.com/w/c/chrono

However, Rebol chose to abstract time as something beyond the standard, into
the "Host Kit".  In Revolt, the host kit is being phased out, so it means
getting the time becomes part of an extension.

This is not necessarily a bad thing.  In the JavaScript build, the C time
functions are made available by Emscripten--but they are emulations.  Hence it
is more direct to call the JS `Date` API to build Rebol values.

One consequence of the core not being able to take time and date access for
granted is that timing the boot process cannot use the same code.  That's
because extensions use Rebol values as their currency, and the system must be
in a sufficiently bootstrapped state to access those functions.

But while factoring the time and date into an extension has complications, it
is helping facilitate the elimination of the OS_XXX APIs, which should
centralize the design concerns so that there can be more focus on improving
the methods by which extensions operate.
