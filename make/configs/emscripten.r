REBOL []

os-id: default [0.16.2]

gcc-path: 

toolset: [
    gcc %emcc
    ld %emcc
]

optimize: "z"

extensions: make map! [
    BMP -
    Clipboard -
    Crypt -
    Debugger -
    FFI -
    GIF -
    JavaScript +
    JPG -
    Locale -
    ODBC -
    PNG -
    Process -
    UUID -
    View -
    ZeroMQ -
]


; emcc command-line options:
; https://kripken.github.io/emscripten-site/docs/tools_reference/emcc.html
; https://github.com/kripken/emscripten/blob/incoming/src/settings.js
;
; Note environment variable EMCC_DEBUG for diagnostic output

cflags: reduce [
    {-DDEBUG_STDIO_OK}
    {-DDEBUG_HAS_PROBE}
    {-DDEBUG_COUNT_TICKS}

    {-DTO_JAVASCRIPT} ;-- Guides %a-lib.c to add rebPromise() implementation

    ; {-s USE_PTHREADS=1} ;-- must be on compile -and- link to use pthreads
]

ldflags: compose [
    (unspaced ["-O" optimize])

    ; Originally `-s ENVIRONMENT='worker'` was used, due to a belief that
    ; having Rebol running on a thread separate from the GUI would make it
    ; easier to suspend the stack while requests were made to JavaScript to
    ; do GUI updates.  While this is true, it prohibits emterpreter builds
    ; (which aren't compatible with PTHREAD builds) from ever mixing JS code
    ; that manipulates the DOM with code that calls the libRebol API, which
    ; is nearly disqualifying.  Also, in PTHREAD use of MAIN_THREAD_EM_ASM()
    ; requires that the initial `main()` be started from the GUI thread in
    ; order to do synchronous calls to the GUI.  So we use `web` for both.
    ;
    {-s ENVIRONMENT='web'}

    {-s ASSERTIONS=0}
;    (unspaced [{-s 'ASSERTIONS=} either debug = 'none [0] [1] {'}])

    (if false [[
        ; In theory, using the closure compiler will reduce the amount of
        ; unused support code in %libr3.js, at the cost of slower compilation. 
        ; Level 2 is also available, but is not recommended as it impedes
        ; various optimizations.  See the published limitations:
        ;
        ; https://developers.google.com/closure/compiler/docs/limitations
        ;
        ; !!! A closure compile has not been successful yet.  See notes here:
        ; https://github.com/kripken/emscripten/issues/7288
        ; If you get past that issue, the problem looks a lot like:
        ; https://github.com/kripken/emscripten/issues/6828
        ; The suggested workaround for adding --externals involves using
        ; EMCC_CLOSURE_ARGS, which is an environment variable...not a param
        ; to emcc, e.g.
        ;     export EMCC_CLOSURE_ARGS="--externs closure-externs.json"
        ;
        ;{-s IGNORE_CLOSURE_COMPILER_ERRORS=1} ;-- maybe useful
        {-g1} ;-- Note: this level can be used with closure compiler
        {--closure 1}
    ]] else [[
        {--closure 0}
    ]])

    ; Minification usually tied to optimization, but can be set separately.
    ;
    ;{--minify 0}

    ; %reb-lib.js is produced by %make-reb-lib.js - It contains the wrapper
    ; code that proxies JavaScript calls to `rebElide(...)` etc. into calls
    ; to the functions that take a `va_list` pointer, e.g. `_RL_rebElide()`.
    ;
    {--post-js prep/include/reb-lib.js}

    ; While over the long term it may be the case that C++ builds support the
    ; exception mechanism, the JavaScript build is going to be based on
    ; embracing the JS exception model.  So disable C++ exceptions.
    ; https://forum.rebol.info/t//555
    ;
    {-s DISABLE_EXCEPTION_CATCHING=1}
    {-s DEMANGLE_SUPPORT=0} ;-- C++ build does all exports as C, not needed

    ; Currently the exported functions come from EMTERPRETER_KEEP_ALIVE
    ; annotations, but it would be preferable if a JSON file were produced
    ; and used, as %emscripten.h should not (in general) be included by
    ; the %rebol.h file--it has to have a fake #define at the moment.
    ;
    ;{-s EXPORTED_FUNCTIONS="['_something']"}

    ; Documentation claims a `--pre-js` or `--post-js` script that uses
    ; internal methods will auto-export them since the linker "sees" it.  But
    ; that doesn't seem to be the case for %reb-lib.js or things called from
    ; EM_ASM() in the C...so do it explicitly.
    ;
    {-s 'EXTRA_EXPORTED_RUNTIME_METHODS=["ccall", "cwrap", "allocateUTF8"]'}

    ; WASM does not have source maps, so disabling it can aid in debugging
    ; But emcc WASM=0 does not work in VirtualBox shared folders by default
    ; https://github.com/kripken/emscripten/issues/6813
    ;
    ; SAFE_HEAP=1 does not work with WASM
    ; https://github.com/kripken/emscripten/issues/4474
    ;
    {-s WASM=1 -s SAFE_HEAP=0}

    ; This allows memory growth but disables asm.js optimizations (little to
    ; no effect on WASM).  Disable until it becomes an issue.
    ;
    ;{-s ALLOW_MEMORY_GROWTH=0}

    ; The inability to communicate synchronously between the worker and GUI
    ; in JavaScript means that being deep in a C-based interpreter stack on
    ; the worker cannot receive data from the GUI.  Some methods to get past
    ; this appear to be on the horizon with SharedArrayBuffer, which has
    ; spotty support in browsers and was disabled in all of them in 2018 due
    ; to potential security flaws.
    ;
    (if true [[
        {-s EMTERPRETIFY=1}
        {-s EMTERPRETIFY_ASYNC=1}
        {-s EMTERPRETIFY_FILE="libr3.bytecode"}

        ; !!! Is this yet another file, different from the .bytecode?
        ;
        ;{--memory-init-file 1}

        ; "There's always a blacklist.  The whitelist starts empty.  If there
        ; is a non-empty whitelist then everything not in it gets added to the
        ; blacklist.  Everything not in the blacklist gets emterpreted."
        ; https://github.com/kripken/emscripten/issues/7239
        ;
        ; For efficiency, it's best if all functions that can be blacklisted
        ; are.  This is ideally done with a JSON file that is generated via
        ; analysis of the code to see which routines cannot call the
        ; emscripten_sleep_with_yield() function.  Currently anything that
        ; runs the evaluator can, but low-level routines like Make_Series()
        ; could be on the list.
        ;
        ;{-s EMTERPRETIFY_BLACKLIST="['_malloc']"}
        ;{-s EMTERPRETIFY_WHITELIST=@emterpreter_whitelist.json}
    ]] else [[
        {-s USE_PTHREADS=1} ;-- must be on compile and link
    ]])

    ; When debugging in the emterpreter, stack frames all appear to have the
    ; function name `emterpret`.  Asking to turn on profiling will inject an
    ; extra stack frame with the name of the function being called.  It runs
    ; slower, but makes the build process *A LOT* slower.
    ;
    ;{--profiling-funcs} ;-- more minimal than `--profiling`, just the names
]
