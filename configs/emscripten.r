REBOL [
    File: %emscripten.r

    Description: {
        Emscripten is the name for a tool suite that makes it easier to build
        C/C++ programs that will run in JavaScript/WebAssembly environments.

        The LLVM Toolchain itself covers generating Wasm code from C or C++.
        So where Emscripten fits in is to provide shims that code written to
        call library functions to "just work" in the browser.  This applies
        to high level graphics APIs down to low level functions like malloc()
        and free(), which have no standard implementation in Wasm.

        Also notable is the support for embedding JavaScript code inline
        into C code, via the EM_ASM and EM_JS instructions.  This is used to
        implement the "JavaScript extension", which adds the JS-NATIVE and
        JS-EVAL functions to the Ren-C runtime in order to make it possible
        to smoothly integrate JavaScript and Ren-C.
    }

    Notes: {
      * EMCC Command-Line Options List
        https://emscripten.org/docs/tools_reference/emcc.html

      * Variables present when JS Compiler runs, set with `-sOPTION1=VALUE1`
        https://github.com/emscripten-core/emscripten/blob/main/src/settings.js

      * EMCC_DEBUG is an environment variable for diagnostic output

      * It's possible to use Emscripten to build Wasm code for a runtime that
        does not have any JavaScript.  This is the STANDALONE_WEBASSEMBLY
        option, but it lacks several features that are accomplished better
        using a different compiler than emcc; instead use clang with the
        wasi-sdk.  See %configs/wasi.r for that configuration.

      * There is special binding glue which implements the libRebol API in
        Emscripten, so that functions like reb.Value(...) are available to
        JavaScript in the build.  See %prep-libr3-js.reb for that somewhat
        intricate glue.
    }
]

host: #web  ; #web, or #node (not recently tested)

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
; Emscripten offers some hacks that implement it.  There is a webassembly
; standard for exceptions that is emerging as a better replacement.
;
; So by default we use the `try {...} catch {...}` variant of the exception
; implementation, to exercise it in the web build.
;
; https://forum.rebol.info/t/555
;
; options are: [#uses-try-catch #uses-longjmp #just-aborts]
;
abrupt-failure-model: #uses-try-catch  ; works in Firefox and Chrome

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

; Not all functions are supported by all runtimes.  Consult the roadmap to
; see which engines support what:
;
; https://webassembly.org/roadmap/
;
extensions: make map! compose [
    Clipboard -
    Crypt -
    Console +
    Debugger -
    DNS -
    Filesystem -
    JavaScript +
    Locale -
    Network -
    ODBC -
    Process -
    Stdio -
    TCC -
    Time -
    UUID -
    UTF -
    View -
]


cflags: compose [
    (spread switch abrupt-failure-model [
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
    ])

    (if debug-javascript-extension [spread [
        {-DDEBUG_JAVASCRIPT_EXTENSION=1}

        {-DDEBUG_HAS_PROBE=1}
        {-DDEBUG_FANCY_PANIC=1}
        {-DDEBUG_COUNT_TICKS=1}
        {-DDEBUG_PRINTF_FAIL_LOCATIONS=1}

        {-DDEBUG_COLLECT_STATS=1}  ; !!! maybe temporary, has cost but good info
    ]])

    (if use-asyncify [spread [
        {-DUSE_ASYNCIFY}  ; affects rebPromise() methodology
    ]])
]

ldflags: compose [
    ; We no longer test any configurations with asm.js (wasm is supported by
    ; all browsers of interest now).  But you'd set this to 0 for that.
    ;
    {-s WASM=1}

    {-s DEMANGLE_SUPPORT=0}  ; C++ build does all exports as C, not needed

    (spread switch abrupt-failure-model [
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
    ])

    (unspaced ["-O" optimize])

    ; Emscripten tries to do a lot of automatic linking "magic" for you, and
    ; seeing what it's doing might be helpful...if you can understand it.
    ; https://groups.google.com/forum/#!msg/emscripten-discuss/FqrgANu7ZLs/EFfNoYvMEQAJ
    ;
    ; ({-s VERBOSE=1})

    (switch host [
        #web [
            if use-pthreads [
                ; https://github.com/emscripten-core/emscripten/issues/8102
                {-s ENVIRONMENT='web,worker'}
            ] else [
                {-s ENVIRONMENT='web'}
            ]
        ]
        #node [
            {-s ENVIRONMENT='node'}
        ]
        fail "Javascript HOST must be [#web #node] in %emscripten.r"
    ])

    (if host = #node [
        ;
        ; !!! Complains about missing $SOCKFS symbol otherwise
        ;
        {-s ERROR_ON_UNDEFINED_SYMBOLS=0}
    ])

    ; Generated by %make-reb-lib.r, see notes there.  Pertains to this:
    ; https://github.com/emscripten-core/emscripten/issues/4240
    ;
    (if host = #node [
        {--pre-js prep/include/node-preload.js}
    ])

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

    (if debug-javascript-extension [
        {-s ASSERTIONS=1}
    ] else [
        {-s ASSERTIONS=0}
    ])

    ; Prior to Ren-C becoming stackless, it was necessary to use a fairly
    ; large value for the asyncify stack.  If Asyncify is to be used again, it
    ; would probably not need a very large stack.
    ;
    (if use-asyncify [
        {-s ASYNCIFY_STACK_SIZE=64000}
    ])

    (if false [spread [
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
    ]] else [ spread[
        {--closure 0}
    ]])

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

    ; The EXPORTED_"RUNTIME"_METHODS are referring to functions built into
    ; Emscripten which you can use.  Several seem to link by default, like:
    ;
    ; * stackAlloc()
    ; * stackRestore()
    ;
    ; (Once this included `ccall`, `cwrap`, etc. but those aren't runtime
    ; but "library" now.  See DEFAULT_LIBRARY_FUNCS_TO_INCLUDE for those...)
    ;
    ; Also, the list has been thinning over time.  Once writeArrayToMemory()
    ; and lengthBytesUTF8() and stringToUTF8() were included, but now explicit:
    ;
    ; https://groups.google.com/g/emscripten-discuss/c/GqyrjgRmcEw/
    ;
    ; The documentation claims a `--pre-js` or `--post-js` script that uses
    ; internal methods will auto-export them since the linker "sees" it.  But
    ; that doesn't seem to be the case (and wouldn't be the case for anything
    ; called from EM_ASM() in the C anyway).  So list them explicitly here.
    ;
    ; ENV is an object which represents the initial environment for getenv()
    ; calls.  This is used to set up configuration options for the C code that
    ; come into effect before rebStartup() is finished.  The snapshot happens
    ; after the module's preRun() method, and to assign ENV with MODULARIZE=1
    ; one must export it (or ENV is a function stub that raises an error).
    ;
    {-s EXPORTED_RUNTIME_METHODS=ENV,writeArrayToMemory,lengthBytesUTF8,stringToUTF8}

    ; These are functions in Emscripten that make it easier to call C code.
    ; You don't need them to call C functions with integer arguments.  But
    ; you'll probably want them if you're going to do things like transform
    ; from JavaScript strings into an allocated UTF-8 string on the heap.
    ;
    ; https://emscripten.org/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.html
    ;
    ; !!! Prior to Ren-C being "stackless", there were some issues regarding
    ; calling wrapped functions during emscripten_sleep()--where conservative
    ; Emscripten asserts were blocking forms of reentrancy that should have
    ; been allowed.  But it did not heed ASYNCIFY_BLACKLIST:
    ;
    ; https://github.com/emscripten-core/emscripten/issues/9412
    ;
    ; Consequently, Ren-C had a manually-copied version of ccall and cwrap.
    ; Stackless is here to stay--so the copies aren't necessary--but the code
    ; is kept as it is to show what flexibilities in wrapping there are in
    ; case future questions come up.
    ;
    ;{-s "DEFAULT_LIBRARY_FUNCS_TO_INCLUDE=['ccall', 'cwrap']"}

    ; SAFE_HEAP=1 once didn't work with WASM; does now, but may not be useful:
    ; https://github.com/kripken/emscripten/issues/4474
    ;
    ;{-s SAFE_HEAP=1}

    ; This allows memory growth but disables asm.js optimizations (little to
    ; no effect on WASM).  Disable until it becomes an issue.
    ;
    ;{-s ALLOW_MEMORY_GROWTH=0}

    (if use-asyncify [spread [
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
    ]])

    (if use-pthreads [spread [
        {-s USE_PTHREADS=1}  ; must be in both cflags and ldflags if used

        ; If you don't specify a thread pool size as a linker flag, the first
        ; call to `pthread_create()` won't start running the thread, it will
        ; have to yield first.  See "Special Considerations":
        ; https://emscripten.org/docs/porting/pthreads.html
        ;
        {-s PTHREAD_POOL_SIZE=1}
    ]])

    ; Asking to turn on profiling runs slower, but makes the build process
    ; *A LOT* slower.
    ;
    ;{--profiling-funcs}  ; more minimal than `--profiling`, just the names
]
