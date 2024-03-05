## Bootstrap Branch Of Ren-C: **...THAT YOU SHOULD PROBABLY IGNORE**

*There's almost no reason you should be looking at this.*

What it is, is a stripped down mess of an ancient version of the executable.
Its sole use is to do some code generation in the build process for making
modern executables.

The reasons why such an old snapshot is used mostly comes down to *speed*.
At time of writing, the main branch of Ren-C is much slower...due to its
focus on getting design points correct over premature optimization.  (Also
some things that make it "slower", e.g. being stackless, makes it viable
in places like the web browser--where it would require intrusive code
generation that would slow it down otherwise.)

Major features in the main branch will never be ported back to this one.
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
