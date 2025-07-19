## R3BOOT: Bootstrap Ren-C Branch: **...THAT YOU SHOULD PROBABLY IGNORE**

*There's almost no reason you should be looking at this.*  See main branch:

  https://github.com/metaeducation/ren-c

This is a stripped down mess of an ancient version of the executable.  Its
sole use is to do some code generation in the build process for building the
modern executables.

One key reason why such an old snapshot is used mostly comes down to *speed*.
At time of writing, the main branch of Ren-C is much slower...due to its
focus on getting design points correct over premature optimization.  (Also
some things that make it "slower", e.g. being stackless, makes it viable
in places like the web browser--where it would require intrusive code
generation that would slow it down otherwise.)

This R3BOOT codebase is also simpler.  Using it as a "trusty baseline" while
experimenting on the main branch means it's (theoretically) less likely to
lead to tripping on its problems, while trying to implement new behaviors.

Major features in the main branch will *NEVER* be ported back to this one.
As a partial list just to give the idea:

**UTF-8 Everywhere**

  https://forum.rebol.info/t/realistically-migrating-rebol-to-utf8-everywhere/374

**Stacklessness**

  https://forum.rebol.info/t/switching-to-stackless-why-this-why-now/1247

**Isotopes**

  https://forum.rebol.info/t/a-justification-of-generalized-isotopes/1918

**UPARSE**

  https://forum.rebol.info/t/introducing-the-hackable-usermode-parse-uparse/1529

**Pure Virtual Binding**

  https://forum.rebol.info/t/rebol-and-scopes-well-why-not/1751

Given how hopelessly behind this snapshot of the interpreter is, features in
it are mercilessly culled to lower the maintenance footprint when patches
need to be done.  It is tweaked in various ways to make it more compatible
with a modern version...but it also has to be able to be built with the
previous bootstrap executable snapshot.  So it's a balancing act.

Expect the files to be filled with outdated comments, bad code, and outright
contradictions.  This is a means to an end, and of no interest in itself.

## If Using Address Sanitizer, Disable UseAfterReturn Check

GCC 13 and Clang 15 turn on the UseAfterReturn stack check by default.  This
makes C stack frames allocate at arbitrary addresses, interfering with the
method of detecting stack overflows used by the bootstrap executable:

  https://github.com/google/sanitizers/wiki/AddressSanitizerUseAfterReturn

You must turn it off, e.g. with an environment variable:

    $ export ASAN_OPTIONS=detect_stack_use_after_return=0

(Modern Ren-C is "stackless", and does not use techniques beyond the C standard
to try and guess about what addresses are getting near a stack overflow.)

## License

License is now completely muddy, because this project copies wholesale from a
LGPL codebase into an Apache2 one.  Don't copy things out of this code anyway,
what are you, crazy?  :-)
