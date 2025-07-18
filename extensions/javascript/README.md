## JavaScript Extension

The JavaScript extension is designed to be used with the "Emscripten" build
of Ren-C, which can run in a web browser or with Node.js:

http://kripken.github.io/emscripten-site/

This extension is not required to use the JavaScript version of the libRebol
API, if the only interest is calling Rebol evaluations from JavaScript.  What
it's for is the reverse direction--so that once Rebol code is running, it can
call *back into* JavaScript.

The way this is done is using "JavaScript natives".  A JavaScript native is
an ACTION! whose spec is a Rebol block--much like other functions.  But the
body is a string of JavaScript source.

Key to the extension's usefulness is integration with the idea of "Promises",
which were a popular idiom that was ultimately added to the JavaScript
language itself in the ES6 standard.

### PROMISES AND SIMULATING SYNCHRONOUS BEHAVIORS

Using Emscripten-compiled binaries, it's *relatively* "easy" to make an
ACTION! whose spec is a Rebol block and whose body is a JavaScript text.
This is because calling JS from emscripten C code can be done with the
EM_ASM()/EM_ASM_() directives, for "inline JavaScript" (similar to "inline
assembly" features of other compilers)

https://emscripten.org/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.html

However, many of the cases of interacting with JavaScript wish to see a
synchronous side-effect.  One hits these immediately with things like PRINT
or INPUT...which one would like to interact with the DOM vs. being stuck only
as `alert()` or `console.log()` calls.  A plain JS-NATIVE that runs EM_ASM()
and then continues running more Rebol on the same thread would never yield
that thread to the GUI.

The JS-AWAITER was introduced to deal with this problem, but the nuances are
much more complex.  Still, JS-NATIVE is good for some quick things that do not
need to do any UI interaction.

### PTHREADs vs Emterpreter

Implementing promises means the interpreter state must be able to suspend,
ask for the information, and wait for an answer.  There are essentially only
two ways to do this:

1. Using Emscripten's `<pthread.h>` emulation.  This is based on using a mix
of SharedArrayBuffer and web workers...so that the worker can retain its
state on the stack while asking for work to be done on its behalf.  It can
then use pthread_cond_wait() to wait for a signal from the GUI that the work
is done.

https://emscripten.org/docs/porting/pthreads.html

2. Using the "emterpreter" feature of emscripten, which doesn't run WASM code
directly--rather, it simulates it in a bytecode.  The bytecode interpreter can
be suspended while retaining the state of the stack of the C program it is
implementing.

https://github.com/emscripten-core/emscripten/wiki/Emterpreter

Empirically, approach #2 produces build products that are TWICE THE SIZE and
executes about THIRTY TIMES SLOWER.  Unfortunately, some JavaScript systems
have not yet caught up with pure WASM and shared memory--or require user
intervention to enable it as a "special setting".  So for the moment, the
extension can be built either way.

Pthreads are default, but see %configs/emscripten.r for USING_EMTERPRETER.

### Building

To use this, build Rebol using `config=%configs/emscripten.r`.  Once the code
is built, it must be loaded into a host (a web browser or node.js) which
provides a JavaScript interpreter.  For a sample client that uses the build
products, see ReplPad-JS:

https://github.com/hostilefork/replpad-js/

(Note: At time of writing, integration with Node.JS is untested--and would
have to run the emterpreted build, as emscripten does not yet implement
pthreads on Node.)

### Optimization

How many JS function recursions there are is affected by optimization levels
like -Os or -Oz.  These avoid inlining, which means more JavaScript/WASM stack
calls to do the same amount of work...leading to invisible limit being hit
sooner.  We should always compile %c-eval.c with -O2 to try and avoid too many
recursions, so see #prefer-O2-optimization in %file-base.r.

### License

Though Rebol itself is released under an Apache 2.0 License, the JavaScript
extension represents a significant independent effort, which was designed to
stand separate from the interpreter.  A "stronger" share-alike license was
chosen for this extension: the LGPL v3.  The extension represents significant
work that will likely continue to be extended, and any reasonable fork/clone
shouldn't have a problem sharing their improvements.

Any snippets here that were taken from free sources on the web cite their
original links.  If a small portion of *original* code is of interest, then
permission would almost certainly be granted to borrow it under an MIT license.
Just ask.  Or if you don't feel like asking, use common sense; it's not like
we're Oracle.  9 lines of code doth not a lawsuit make.  *(Unless you ARE
Oracle...in which case, heck yeah we'll sue you!)*

In time, the license might be weakened to something more liberal.  Until such
time as a truly principled significant contributor demands that the project
*not* be relicensed, it's asked that all contributors agree that a more liberal
license could be chosen.  The only rule is that there won't be any "special
treatment" licenses--e.g. a more liberal license for people who pay.  Any
license change will be applicable to everyone.  *(Except, maybe...Oracle.)*

Note: This doesn't preclude someone making a donation in order to ask that
the license be loosened.  It just means that everyone gets the result--not
just the donor.
