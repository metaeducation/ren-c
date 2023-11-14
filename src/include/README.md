This directory contains include files for code that is written to either the
internal API, which you get with `#include "sys-core.h"`...or to the external
API, which you get with `#include "reb-host.h"`.

Code written to the internal API has to deal with the specific issues of
series and garbage collection.  It has access to the data stack and can do
anything that a native function could do.  This means functions like ARR_AT(),
PUSH_GUARD_SERIES(), Pop_Stack_Values(). etc are available.  The result is
efficiency at the cost of needing to worry about details, as well as being
more likely to need to change the code if the internals change.

Code written to the external API in Ren-C operates on REBVAL pointers only,
and has no API for extracting Series(*) or Context(*).  Values created by this API
cannot live on the stack, and they will be garbage collected.

Each of the `reb-xxx.h` files is included by %reb-host.h, and each of the
`sys-xxx.h` files are included by %sys-core.h.  The sub-files don't use
include guards and the order of inclusion is important...so they should not be
#include'd individually.
