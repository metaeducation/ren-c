This directory contains tests for several parse variants.

PARSE3 Refers to the native code for the PARSE dialect, derived from what was
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
name PARSE in Ren-C.  Hence the parse-xxx.test.reb files are all tests
for UPARSE.

PARSE2 is an emulation of Rebol2/Red parse conventions via UPARSE's mechanics
for extensibility.  This behavior is largely compatible with PARSE3; however
PARSE3 is being gradually migrated to match more of UPARSE's conventions in
order to ease migration (since no significant R3-Alpha codebases exist, the
PARSE2 code should be used by anyone who wants historical behavior).
