REBOL [
    File: %wasi.r

    Description: {
        The WebAssembly System Interface bridges Wasm code with the host (such
        as Linux, Windows, or Mac) to offer features like filesystem access
        through system calls.  It can also run in a web browser, making it
        something of a "competitor" to emscripten (see %configs/emscripten.r)

        Features offered by Wasi engines are tracked in this table:

          https://webassembly.org/roadmap/
    }

    Usage: {
        Building a webassembly project with Clang can target Wasi by targeting
        the wasm32 instruction set, and using a cross-compiling --sysroot that
        is pointed to the libc-wasi sysroot:

            $ ~/wasi-sdk-16.0/bin/clang --sysroot ~/wasi-sysroot test.c

        The wasi-sdk files are available via binary download from GitHub:

          https://github.com/WebAssembly/wasi-sdk/releases
    }

    Notes: {
      * The feature roadmap of Wasi has been moving along in coverage, but it
        is not sufficient to supply the needed threading and network primitives
        required by libuv (which Ren-C's Filesystem and Networking extensions
        are set up to use).  A less-featured file or networking layer would
        likely work, but it's probably better to wait.

      * Before finding Clang with wasi-sdk, it was tried to compile with
        Emscripten in a mode called STANDALONE_WEBASSEMBLY.  But this is too
        limited to be useful in containers like Wasmtime or Wasmedge, as it
        has very basic abilities (like stdio) and can't handle things like
        fopen() calls:

          https://github.com/WasmEdge/WasmEdge/issues/1614
    }
]

os-id: default [0.16.4]

wasi-clang: to file! (get-env 'WASI_CLANG else [
    fail [
        "WASI_CLANG not set, should be to clang executable installed e.g. from"
        https://github.com/WebAssembly/wasi-sdk/releases
    ]
])

wasi-sysroot: to file! (get-env 'WASI_SYSROOT else [
    fail [
        "WASI_SYSROOT not set, should be to path installed e.g. from"
        https://github.com/WebAssembly/wasi-sdk/releases
    ]
])

toolset: compose [
    gcc (wasi-clang)
    ld (wasi-clang)
]


; Historically, debug builds of Emscripten did not include asserts of the
; whole codebase...trusting the desktop builds to test that.  The only part
; that had asserts on was the JavaScript extension, which the desktop builds
; could not test.  Wasi executables aren't being run in the browser and the
; dynamics may be different in terms of memory use.  Review.
;
debug-wasi-extension: false

; Note: anything other than -O0 will strip debug symbols in wasi-sdk:
; https://bugs.llvm.org/show_bug.cgi?id=45602
;
optimize: if debug-wasi-extension [0] else ["s"]


; 1. Filesystem and Networking extensions are based on libuv, which has more
;    complex dependencies than simpler File/Networking would, so headers are
;    not available in wasi-sdk's wasi-libc, but maybe someday:
;
;      https://github.com/nodejs/uvwasi/issues/14#issuecomment-1170660968
;
; 2. The JavaScript extension which provides JS-NATIVE and JS-EVAL uses the
;    Emscripten compiler features heavily.  These tie into infrastructure for
;    emscripten that expects to be running under a web browser or NodeJS,
;    both of which natively have a JS interpreter on hand.  If a WASI build
;    wanted a JS interpreter, it would have to include one, like QuickJS:
;
;      https://bellard.org/quickjs/
;
;    Because containers like WasmEdge demonstrate file I/O and networking via
;    QuickJS, creating a new extension that builds JS-NATIVE and JS-EVAL on
;    these features seems appealing.  However, the bridge that connects
;    WASI to QuickJS is written in Rust:
;
;      https://github.com/second-state/wasmedge-quickjs
;
extensions: make map! compose [
    Clipboard -
    Crypt -
    Console +
    Debugger -
    DNS -
    Event -
    Filesystem -  ; libuv-based, see [1]
    Image -
    JavaScript -  ; emscripten only (embed QuickJS? see [2])
    Library -
    Locale -
    Network -  ; libuv-based, see [1]
    ODBC -
    Process -  ; no pipe2, separate out Call extension?
    Stdio +
    TCC -
    Time -  ; no sys/wait
    UUID -
    UTF -
    View -
]


; emcc command-line options:
; https://kripken.github.io/emscripten-site/docs/tools_reference/emcc.html
; https://github.com/kripken/emscripten/blob/incoming/src/settings.js
;
; Note environment variable EMCC_DEBUG for diagnostic output

cflags: compose [
    {--sysroot ${WASI_SYSROOT}}

    ; "wasm lacks signal support; to enable minimal signal emulation..."
    ; also needs to link with `-lwasi-emulated-signal`
    ;
    ; It's not clear why things like %pstdint.h need <signal.h>, but it seems
    ; the minimal emulation is enough.
    ;
    {-D_WASI_EMULATED_SIGNAL}

    {-DREBOL_FAIL_JUST_ABORTS=1}  ; no exceptions or setjmp()/longjmp()

    (if debug-wasi-extension [spread [
        {-DDEBUG_HAS_PROBE=1}
        {-DDEBUG_FANCY_PANIC=1}
        {-DDEBUG_COUNT_TICKS=1}
        {-DDEBUG_PRINTF_FAIL_LOCATIONS=1}

        {-DDEBUG_COLLECT_STATS=1}  ; !!! maybe temporary, has cost but good info
    ]])
]

ldflags: compose [
    (unspaced ["-O" optimize])

    {-lwasi-emulated-signal}  ; cflags needs {-D_WASI_EMULATED_SIGNAL}

    (if debug-wasi-extension [{-g}])
]
