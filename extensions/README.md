## Rebol Extensions

Extensions are like modules that can implement some of their functionality
using native code.

Currently, extensions are kept in the main repository to make it easier to
keep them up-to-date, when the internal API or build mechanisms change.  Once
those stabilize, they should be their own individual projects with their own
issue trackers and maintainers.

Extensions and the libraries they link to may have their own licenses 
besides Rebol's Apache2, so see the LICENSE.txt file in the extension
subdirectory for information.

### Building

The build process offers three ways to build an extension, as specified in
the "extensions" section of the config (or EXTENSIONS command-line switch).

* `+` means to build the extension directly into the executable or library.
* `*` means to build as a DLLs that can be distributed separately and then
      loaded optionally when needed.
* `-` means do not build the extension at all.

Passing configuration parameters to the per-extension make script is a work
in progress.  In the meantime, environment variables can be a good way to
configure extensions with complex building needs.  Consult the Travis build
file for up-to-date usages of common extensions:

https://github.com/metaeducation/ren-c/blob/master/.travis.yml

### API Choices

These extensions can choose to use either the user-friendly %rebol.h API, or
the complex internal %sys-core.h API.  Using the latter allows the extension
to implement natives whose performance characteristics are identical to
natives which are implemented in the core.  (Though this is not recommended
for most tasks, as the internal API changes frequently--and is easier to get
wrong if one is not fairly well versed in how Rebol is implemented.)
