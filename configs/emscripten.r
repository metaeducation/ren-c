REBOL [
    File: %emscripten.r
]

javascript-environment: #web  ; or #node (which is not recently tested)

; When browsers run code, it's required that you yield control and return
; from all your stack levels to perform certain operations (like GUI updates
; or network IO).  Ren-C is now "stackless" and can accomplish this without
; special measures:
;
; https://forum.rebol.info/t/stackless-is-here-today-now/1844
;
; But previously it has used Emsterpreter, Pthreads, and Asyncify to work
; around those problems.  The build settings are kept in case there are some
; integration scenarios that may require these methods again:
;
; https://emscripten.org/docs/porting/asyncify.html
;
use-asyncify: false
use-pthreads: false

; Making an actual debug build of the interpreter core is prohibitive for
; emscripten in general usage--even on a developer machine.  This enables a
; smaller set of options for getting better feedback about errors in an
; emscripten build.
;
debug-javascript-extension: true

; Ren-C has build options for supporting C++ exceptions or the use of setjmp()
; and longjmp() (or just to panic, and not handle them at all).  This affects
; how "abrupt" failures that arise in native code is treated--e.g. the calls
; to the `fail()` "pseudo-keyword".
;
; WebAssembly does not have native support for setjmp() and longjmp(), but
; Emscripten offers some hacks that implement it.  Those hacks aren't avaliable
; on other Wasm platforms like WasmEdge...but the C++ exceptions are.
;
; So by default we use the `try {...} catch {...}` variant of the exception
; implementation, to exercise it in the web build.
;
; https://forum.rebol.info/t/555
;
abrupt-failure-model: #uses-try-catch  ; #uses-longjmp #just-aborts

; Want to make libr3.js, not an executable.  This is so that plain `make`
; knows we want that (vs needing to say `make libr3.js`)
;
top: 'library

os-id: default [0.16.1]  ; 0.16.2 was "pthread" version, no longer supported

toolset: [
    gcc %emcc
    ld %emcc
]

; Using the -Os or -Oz size optimizations will drastically improve the size
; of the download...cutting it in as much as half compared to -O2.  But it
; comes at a cost of not inlining, which winds up meaning more than just
; slower in the browser: the intrinsic limit of how many JS/WASM functions it
; lets be on the stack is hit sooner.  We can do per-file optimization choices
; so the #prefer-O2-optimization flag is set on the %c-eval.c file, which
; overrides this "s" optimization.  (It won't override `-Oz` which is supposed
; to be a more extreme size optimization, but seems about the same.)
;
optimize: "s"

extensions: make map! [
    BMP -
    Clipboard -
    Crypt -
    Console +
    Debugger -
    DNS -
    Event -
    Filesystem -
    GIF -
    Gob -
    Image -
    JavaScript +
    JPG -
    Library -
    Locale -
    Network -
    ODBC -
    PNG -
    Process -
    Stdio -
    TCC -
    Time -
    UUID -
    UTF -
    Vector -
    View -
]


; emcc command-line options:
; https://kripken.github.io/emscripten-site/docs/tools_reference/emcc.html
; https://github.com/kripken/emscripten/blob/incoming/src/settings.js
;
; Note environment variable EMCC_DEBUG for diagnostic output

cflags: compose2 [
    ((switch abrupt-failure-model [
        #uses-try-catch [[
            {-DREBOL_FAIL_USES_TRY_CATCH=1}
            {-fwasm-exceptions}  ; needed in cflags *and* ldflags

            ; Note: -fwasm-exceptions is faster than -fexceptions, but newer
            ; https://emscripten.org/docs/porting/exceptions.html#webassembly-exception-handling-proposal

        ]]
        #uses-longjmp [[
            {-DREBOL_FAIL_USES_LONGJMP=1}
            {-s DISABLE_EXCEPTION_CATCHING=1}
        ]]
        #just-aborts [[
            {-DREBOL_FAIL_JUST_ABORTS=1}
            {-s DISABLE_EXCEPTION_CATCHING=1}
        ]]
        fail
    ]))

    ((if debug-javascript-extension [[
        {-DDEBUG_JAVASCRIPT_EXTENSION=1}

        {-DDEBUG_HAS_PROBE=1}
        {-DDEBUG_FANCY_PANIC=1}
        {-DDEBUG_COUNT_TICKS=1}
        {-DDEBUG_PRINTF_FAIL_LOCATIONS=1}

        {-DDEBUG_COLLECT_STATS=1}  ; !!! maybe temporary, has cost but good info
    ]]))

    ((if use-asyncify [[
        {-DUSE_ASYNCIFY}  ; affects rebPromise() methodology
    ]]))
]

ldflags: compose2 [
    ; We no longer test any configurations with asm.js (wasm is supported by
    ; all browsers of interest now).  But you'd set this to 0 for that.
    ;
    {-s WASM=1}

    {-s DEMANGLE_SUPPORT=0}  ; C++ build does all exports as C, not needed

    ((switch abrupt-failure-model [
        #uses-try-catch [[
            {-fwasm-exceptions}  ; needed in cflags *and* ldflags
        ]]
        #uses-longjmp [[
            {-s DISABLE_EXCEPTION_CATCHING=1}
        ]]
        #just-aborts [[
            {-s DISABLE_EXCEPTION_CATCHING=1}
        ]]
        fail
    ]))

    (unspaced ["-O" optimize])

    ; Emscripten tries to do a lot of automatic linking "magic" for you, and
    ; seeing what it's doing might be helpful...if you can understand it.
    ; https://groups.google.com/forum/#!msg/emscripten-discuss/FqrgANu7ZLs/EFfNoYvMEQAJ
    ;
    (comment {-s VERBOSE=1})

    ((switch javascript-environment [
        #web [
            ; https://github.com/emscripten-core/emscripten/issues/8102
            {-s ENVIRONMENT='web,worker'}
        ]
        #node [
            {-s ENVIRONMENT='node'}
        ]
        fail "JAVASCRIPT-ENVIRONMENT must be #web or #node in %emscripten.r"
    ]))

    ((if javascript-environment = #node [
        ;
        ; !!! Complains about missing $SOCKFS symbol otherwise
        ;
        {-s ERROR_ON_UNDEFINED_SYMBOLS=0}
    ]))

    ; Generated by %make-reb-lib.r, see notes there.  Pertains to this:
    ; https://github.com/emscripten-core/emscripten/issues/4240
    ;
    ((if javascript-environment = #node [
        {--pre-js prep/include/node-preload.js}
    ]))

    ; The default build will create an emscripten module named "Module", which
    ; holds various emscripten state (including the memory heap) and service
    ; routines.  If everyone built their projects like this, you would not be
    ; able to load more than one at a time due to the name collision.  So
    ; we use the "Modularize" option to get a callback with a parameter that
    ; is the module object when it is ready.  This also simplifies the loading
    ; process of registering notifications for being loaded and ready.
    ; https://emscripten.org/docs/getting_started/FAQ.html#can-i-use-multiple-emscripten-compiled-programs-on-one-web-page
    ;
    {-s MODULARIZE=1}
    {-s 'EXPORT_NAME="r3_module_promiser"'}

    ((if debug-javascript-extension [
        {-s ASSERTIONS=1}
    ] else [
        {-s ASSERTIONS=0}
    ]))

    ; Prior to Ren-C becoming stackless, it was necessary to use a fairly
    ; large value for the asyncify stack.  If Asyncify is to be used again, it
    ; would probably not need a very large stack.
    ;
    ((if use-asyncify [
        {-s ASYNCIFY_STACK_SIZE=64000}
    ]))

    ((if false [[
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
        ;{-s IGNORE_CLOSURE_COMPILER_ERRORS=1}  ; maybe useful?
        {-g1}  ; Note: debug level 1 can be used with closure compiler
        {--closure 1}
    ]] else [[
        {--closure 0}
    ]]))

    ; Minification usually tied to optimization, but can be set separately.
    ;
    (if debug-javascript-extension [{--minify 0}])

    ; %reb-lib.js is produced by %make-reb-lib.js - It contains the wrapper
    ; code that proxies JavaScript calls to `rebElide(...)` etc. into calls
    ; to the functions that take a `va_list` pointer, e.g. `_RL_rebElide()`.
    ;
    {--post-js prep/include/reb-lib.js}

    ; API exports can appear unused to the compiler.  It's possible to mark a
    ; C function as an export with EMTERPRETER_KEEP_ALIVE, but we prefer to
    ; generate the list so that `rebol.h` doesn't depend on `emscripten.h`
    ;
    {-s EXPORTED_FUNCTIONS=@prep/include/libr3.exports.json}

    ; The EXPORTED_"RUNTIME"_METHODS are referring to JavaScript helper
    ; functions that Emscripten provides that make it easier to call C code.
    ; You don't need them to call C functions with integer arguments.  But
    ; you'll probably want them if you're going to do things like transform
    ; from JavaScript strings into an allocated UTF-8 string on the heap
    ; that is visible to C (allocateUTF8).  See:
    ;
    ; https://emscripten.org/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.html
    ;
    ; The documentation claims a `--pre-js` or `--post-js` script that uses
    ; internal methods will auto-export them since the linker "sees" it.  But
    ; that doesn't seem to be the case (and wouldn't be the case for anything
    ; called from EM_ASM() in the C anyway).  So list them explicitly here.
    ;
    ; !!! For the moment (and possible future) we do not use ccall and cwrap
    ; because they do not heed ASYNCIFY_BLACKLIST to know when it would
    ; be safe to call a wrapped function during emscripten_sleep():
    ;
    ; https://github.com/emscripten-core/emscripten/issues/9412
    ;
    ; ENV is an object which represents the initial environment for getenv()
    ; calls.  This is used to set up configuration options for the C code that
    ; come into effect before rebStartup() is finished.  The snapshot happens
    ; after the module's preRun() method, and to assign ENV with MODULARIZE=1
    ; one must export it (or ENV is a function stub that raises an error).
    ;
    {-s "EXPORTED_RUNTIME_METHODS=['ENV']"}
    ; {-s "EXPORTED_RUNTIME_METHODS=['ccall', 'cwrap', 'allocateUTF8']"}

    ; SAFE_HEAP=1 once didn't work with WASM; does now, but may not be useful:
    ; https://github.com/kripken/emscripten/issues/4474
    ;
    ;{-s SAFE_HEAP=1}

    ; This allows memory growth but disables asm.js optimizations (little to
    ; no effect on WASM).  Disable until it becomes an issue.
    ;
    ;{-s ALLOW_MEMORY_GROWTH=0}

    ((if use-asyncify [[
        {-s ASYNCIFY=1}

        ; Memory initialization file,
        ; used both in asm.js and wasm+pthread
        ; unused in 'pure' wasm':
        ; https://groups.google.com/forum/m/#!topic/emscripten-discuss/_czKmHCbeSY
        ;
        {--memory-init-file 1}

        ; "There's always a blacklist.  The whitelist starts empty.  If there
        ; is a non-empty whitelist then everything not in it gets added to the
        ; blacklist.  Everything not in the blacklist gets [asyncified]."
        ; https://github.com/kripken/emscripten/issues/7239
        ;
        ; Blacklisting functions from being asyncified means they will run
        ; faster, as raw WASM.  But it also means blacklisted APIs can be
        ; called from within a JS-AWAITER, since they don't require use of
        ; the suspended bytecode interpreter.  See additional notes in the
        ; blacklist and whitelist generation code in %prep-libr3-js.reb
        ;
        ; Note: If you build as C++, names will be mangled and so the list
        ; will not work.  Hence to use the asyncify build (with a whitelist)
        ; you have to build with C.
        ;
        ; Note: The *actual* blacklist file is Rebol so it can be commented.
        ; In the JavaScript extension, see %javascript/asyncify-blacklist.r
        ; The `make prep` step converts it.
        ;
        {-s ASYNCIFY_BLACKLIST=@prep/include/asyncify-blacklist.json}

        ; whitelist needs true function names
        ;
        {--profiling-funcs}
    ]]))

    ((if use-pthreads [[
        {-s USE_PTHREADS=1}  ; must be in both cflags and ldflags if used

        ; If you don't specify a thread pool size as a linker flag, the first
        ; call to `pthread_create()` won't start running the thread, it will
        ; have to yield first.  See "Special Considerations":
        ; https://emscripten.org/docs/porting/pthreads.html
        ;
        {-s PTHREAD_POOL_SIZE=1}
    ]]))

    ; Asking to turn on profiling runs slower, but makes the build process
    ; *A LOT* slower.
    ;
    ;{--profiling-funcs}  ; more minimal than `--profiling`, just the names
]
