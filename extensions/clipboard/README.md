The `clipboard://` port is only implemented for Windows:

[#2029: Extend clipboard:// implementation to all supported platforms](https://github.com/rebol/rebol-issues/issues/2029)

It was extracted into an extension and simplified from its R3-Alpha format.  (It had been framed in the now-deprecated "REBDEV" device model in two parts, so that the clipboard port was written to speak to an abstract "clipboard device".  Now the only involved abstraction layer is the abstraction of PORT!s themselves.)

Since the clipboard is not available on all platforms *(and as an extension, might not be built in even on those platforms where it is available)*, the clipboard-based tests were not guaranteed to succeed.  They are kept here for now, as extension-specific tests should likely live with those extensions in their directory.
