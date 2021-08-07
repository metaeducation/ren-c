This directory contains tests for several parse variants.

Some of these tests were copied or adapted from %parse-test.red

  https://github.com/red/red/blob/master/tests/source/units/parse-test.red

The header for that file said:

    Red [
        Title: "Red PARSE test script"
        Author: "Nenad Rakocevic"
        File: %parse-test.reds
        Tabs: 4
        Rights: "Copyright (C) 2011-2015 Red Foundation. All rights reserved."
        License: "BSD-3 - https://github.com/red/red/blob/origin/BSD-3-License.txt"
    ]

BSD-3 Code May Be Included or extended as Apache 2.0 License, which the
Ren-C tests are still licensed as (though the interpreter and many supporting
files are now LGPL 3.0)

  http://www.apache.org/legal/resolved.html#category-a

(Though Apache 2.0 licenses do not permit taking code to BSD-3, permission is
granted for any of the Ren-C PARSE tests to be taken back as BSD-3 by the
Red project if they wish.)


## Naming of PARSE variants

PARSE3 Refers to the native code for R3-Alpha's PARSE, derived from what was
in R3-Alpha.  The code was very "organic" and had evolved over time...with a
complex set of flags and states.  It was not extensible; the keywords were
baked in and interacted in specific ways that were difficult to modify.  As
Ren-C was developed it was pushed toward being more rigorous code in some
ways--at least in regards to memory safety and GC.  But the lack of a true
"architecture" limited the ability to make clean extensions for it.

UPARSE for (U)sermode PARSE is a new architecture for PARSE based on the
concept of parser combinators.  The goal is to put together a more coherent
and modular design in usermode, which can then be extended or altered by
using a different set of combinators.  Those combinators can be chosen for
new features, or just to get compatibility with Rebol2/R3-Alpha/Red parse.
As it matures, more of it will be written as native code.  It is MUCH more
flexible and extensible than PARSE3, and is ultimately what will take the
name PARSE in Ren-C.

PARSE2 is an emulation of Rebol2/Red parse conventions via UPARSE's mechanics
for extensibility.  This behavior is largely compatible with PARSE3; however
PARSE3 is being gradually migrated to match more of UPARSE's conventions in
order to ease migration (since no significant R3-Alpha codebases exist, the
PARSE2 code should be used by anyone who wants historical behavior).
