## src/include/*

This directory contains include files for code that is written to the
"internal API", which you get with `#include "sys-core.h"`.

Code written to the internal API has to deal with many complex details related
to memory and garbage collection.  It has access to the data stack and can do
anything that a native function could do.  This means functions like Array_At(),
Push_Lifeguard(), Pop_Source_From_Stack(). etc are available.  The result is
efficiency at the cost of needing to worry about details, as well as being
more likely to need to change the code if the internals change.

Code written to the external API in Ren-C operates on RebolValue pointers only,
and has no API for extracting things like Flex* or VarList*.  Values created
by this API cannot live on the data stack, and the nodes they reference will be
protected from garbage collection until they are rebRelease()'d

## Include Guards Are Not Used

There is a weak form of layering enforcement done by having the include files
go in a specific order.  It does make some syntax highlighting confused,
since the include files can only be understood in the context of how the
%sys-core.h file aggregates them.  But it has been a sort of helpful guide in
seeing where architectural boundaries should be drawn.
