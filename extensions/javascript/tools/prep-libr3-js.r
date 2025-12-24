Rebol [
    title: "Pre-Build Step for JavaScript Files Passed to EMCC"
    file: %prep-libr3-js.reb  ; used by MAKE-EMITTER

    version: 0.1.0
    date: 15-Sep-2020

    rights: "Copyright (C) 2018-2020 hostilefork.com"

    license: "LGPL 3.0"

    description: --[
        The WASM files produced by Emscripten produce JavaScript functions
        that expect their arguments to be in terms of the SharedArrayBuffer
        HEAP32 that the C code can see.  For common JavaScript types, the
        `cwrap` helper can do most of the work:

        https://emscripten.org/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.html

        However, libRebol makes extensive use of variadic functions, which
        means it needs do custom wrapping code.  Once it used a reverse
        engineered form of `va_list` (which is beyond the C standard, and each
        compiler could implement it differently).  But now it does it with
        an ordinary packed array of C pointers.
    ]--
]

; Note: There are no `import` statements here because this is run by EVAL LOAD
; within the %make-librebol.r script's context.  This is done in order to
; inherit the `api` object, and the `for-each-api` enumerator.  As a result
; it also inherits access to CWRAP and other tools.  Review.


e-cwrap: make-emitter "JavaScript C Wrapper functions" (
    join prep-dir %include/reb-lib.js
)

=== "ASYNCIFY_BLACKLIST TOLERANT CWRAP" ===

; !!! This is a clone of code you would get if you said:
;
;    emcc ... -s "DEFAULT_LIBRARY_FUNCS_TO_INCLUDE=['ccall', 'cwrap']" ...
;
; The reason it was cloned long ago was that  Emscripten's `cwrap` was based on
; a version of ccall which did not allow synchronous function calls in the
; while emscripten_sleep() is in effect.  This is an overly conservative assert
; when the function is on the blacklist and known to not yield:
;
; https://github.com/emscripten-core/emscripten/issues/9412
;
; So the code was copied from %preamble.js...minus that assert:
;
; https://github.com/emscripten-core/emscripten/blob/incoming/src/preamble.js
;
; Years later, Ren-C runs "stacklessly" and can suspend itself--meaning it
; no longer needs Asyncify to do yields.  But while the Ren-C core is able
; to do its own yields, it may become interesting to compile in library code
; from other sources that does not have that ability.  So code that was put
; together to make the Asyncify approach work is not being thrown out in a
; kneejerk fashion...in case it someday becomes useful.
;
; Note: In actuality, there's no preprocessor on this file as on preamble.js
; so this has to either keep or drop *all* of the code under `#if` directives,
; (-or- we'd have to do some preprocessing equivalent based on the build
; settings in the config we are doing `make prep` for.)
;
; !!! There may be few enough routines involved that the better answer is to
; dodge `cwrap`/`ccall` altogether and just by-hand wrap the routines.
;
e-cwrap/emit ---[

  // Returns the C function with a specified identifier (for C++, you need to do manual name mangling)
    function getCFunc(ident) {
        var func = Module['_' + ident]; // closure exported function
   /* #if ASSERTIONS
        assert(func, 'Cannot call unknown function ' + ident + ', make sure it is exported');
      #endif */
        return func;
    }

    function ccall_tolerant(ident, returnType, argTypes, args, opts) {
      // For fast lookup of conversion functions
      var toC = {
        'string': function(str) {
          var ret = 0;
          if (str !== null && str !== undefined && str !== 0) { // null string
            // at most 4 bytes per UTF-8 code point, +1 for the trailing '\0'
            var len = (str.length << 2) + 1;
            ret = stackAlloc(len);
            stringToUTF8(str, ret, len);
          }
          return ret;
        },
        'array': function(arr) {
          var ret = stackAlloc(arr.length);
          Module.writeArrayToMemory(arr, ret);
          return ret;
        }
      };

      function convertReturnValue(ret) {
        if (returnType === 'string') return UTF8ToString(ret);
        if (returnType === 'boolean') return Boolean(ret);
        return ret;
      }

      var func = getCFunc(ident);
      var cArgs = [];
      var stack = 0;

/* <ren-c modification>  // See Note: drop since we never do this
    #if ASSERTIONS
      assert(returnType !== 'array', 'Return type should not be "array".');
    #endif
   </ren-c modification> */

      if (args) {
        for (var i = 0; i < args.length; i++) {
          var converter = toC[argTypes[i]];
          if (converter) {
            if (stack === 0) stack = stackSave();
            cArgs[i] = converter(args[i]);
          } else {
            cArgs[i] = args[i];
          }
        }
      }
      var ret = func.apply(null, cArgs);

/* <ren-c modification>  // See Note: drop since no Emterpreter build
    #if EMTERPRETIFY_ASYNC
      if (typeof EmterpreterAsync === 'object' && EmterpreterAsync.state) {
    #if ASSERTIONS
        assert(opts && opts.async, 'The call to ' + ident + ' is running asynchronously. If this was intended, add the async option to the ccall/cwrap call.');
        assert(!EmterpreterAsync.restartFunc, 'Cannot have multiple async ccalls in flight at once');
    #endif
        return new Promise(function(resolve) {
          EmterpreterAsync.restartFunc = func;
          EmterpreterAsync.asyncFinalizers.push(function(ret) {
            if (stack !== 0) stackRestore(stack);
            resolve(convertReturnValue(ret));
          });
        });
      }
    #endif
   </ren-c modification> */

    // This is the part we need to cut out.  If we are in an asyncify yield
    // situation (e.g. waiting on a JS-AWAITER resolve) we know we can't
    // call something that will potentially wait again.  But APIs like
    // reb.Text() call _API_rebText() function underneath, and that's in
    // ASYNCIFY_BLACKLIST.  But the main cwrap/ccall() does not account for
    // that fact.  Hence we have to patch out the assert.
    //
/* <ren-c modification>  // See Note: couldn't use #if if we wanted to
    #if ASYNCIFY && WASM_BACKEND
      if (typeof Asyncify === 'object' && Asyncify.currData) {
        // The WASM function ran asynchronous and unwound its stack.
        // We need to return a Promise that resolves the return value
        // once the stack is rewound and execution finishes.
    #if ASSERTIONS
        assert(opts && opts.async, 'The call to ' + ident + ' is running asynchronously. If this was intended, add the async option to the ccall/cwrap call.');
        // Once the asyncFinalizers are called, asyncFinalizers gets reset to [].
        // If they are not empty, then another async ccall is in-flight and not finished.
        assert(Asyncify.asyncFinalizers.length === 0, 'Cannot have multiple async ccalls in flight at once');
    #endif
        return new Promise(function(resolve) {
          Asyncify.asyncFinalizers.push(function(ret) {
            if (stack !== 0) stackRestore(stack);
            resolve(convertReturnValue(ret));
          });
        });
      }
    #endif
   </ren-c modification> */

      ret = convertReturnValue(ret);
      if (stack !== 0) stackRestore(stack);

/* <ren-c modification>  // See Note: drop since feature is unused
    #if EMTERPRETIFY_ASYNC || (ASYNCIFY && WASM_BACKEND)
      // If this is an async ccall, ensure we return a promise
      if (opts && opts.async) return Promise.resolve(ret);
    #endif
   </ren-c modification> */

      return ret;
    }

    function cwrap_tolerant(ident, returnType, argTypes, opts) {

/* <ren-c modification>  // See Note: drop since optimization unused
    #if !ASSERTIONS
      argTypes = argTypes || [];
      // When the function takes numbers and returns a number, we can just return
      // the original function
      var numericArgs = argTypes.every(function(type){ return type === 'number'});
      var numericRet = returnType !== 'string';
      if (numericRet && numericArgs && !opts) {
        return getCFunc(ident);
      }
    #endif
   </ren-c modification> */

      return function() {
        return ccall_tolerant(ident, returnType, argTypes, arguments, opts);
      }
    }
]---


=== "GENERATE C WRAPPER FUNCTIONS" ===

e-cwrap/emit ---[
    /* The C API uses names like rebValue().  This is because calls from the
     * core do not go through a struct, but inline directly...also some of
     * the helpers are macros.  However, Node.js does not permit libraries
     * to export "globals" like this... you must say e.g.:
     *
     *     var reb = require('rebol')
     *     let val = reb.Value("1 + 2")
     *
     * Having browser calls match what would be used in Node rather than
     * trying to match C makes the most sense (also provides abbreviation by
     * calling it `r.Run()`, if one wanted).  Additionally, module support
     * in browsers is rolling out, although not fully mainstream yet.
     */

    /* Could use ENVIRONMENT_IS_NODE here, but really the test should be for
     * if the system supports modules (someone with an understanding of the
     * state of browser modules should look at this).
     */
    if (typeof module !== 'undefined')
        reb = module.exports  /* add to what you get with require('rebol') */
    else {
        /* !!! In browser, `reb` is a global (window.reb) set by load-r3.js
         * But it would be better if we "modularized" and let the caller
         * name the module, with `reb` just being a default.  However,
         * letting them name it creates a lot of issues within EM_ASM
         * calls from the C code.  Also, the worker wants to use the same
         * name.  Punt for now.  Note `self` must be used instead of `window`
         * as `window` exists only on the main thread (`self` is a synonym).
         */
         if (typeof window !== 'undefined')
            reb = window.reb  /* main, load-r3.js made it (has reb.m) */
        else {
            reb = self.reb = {}  /* worker, make our own API container */
            reb.m = self  /* module exports are at global scope on worker? */
        }
     }
]---


; 1. While type construction in JavaScript is done with capital type names
;    (e.g. Number(1)), the typeof operator returns lowercase names, and
;    Emscripten uses lowercase names in cwrap/ccall.
;
to-js-type: func [
    return: [null? text! tag!]
    s [text!] "C type as string"
][
    return case [
        ; APIs dealing with `char *` means UTF-8 bytes.  While C must memory
        ; manage such strings (at the moment), the JavaScript wrapping assumes
        ; input parameters should be JS strings that are turned into temp
        ; UTF-8 on the emscripten heap (freed after the call).  Returned
        ; `char *` should be turned into JS GC'd strings, then freed.
        ;
        ; !!! By default, unboxing APIs are not null tolerant. rebOptXxx()
        ; will allow returning nullptr if the input is null, for example
        ; `rebSpellOpt("try second [{a}]")` gives nullptr
        ;
        (s = "char*") or (s = "const char*") ["'string'"]  ; see [1]

        ; A RebolBounce is actually a void* (attempts to make it a struct
        ; holding a void* in some builds fail at runtime at present, but it
        ; can be used as a static compilation check anyway)
        ;
        s = "RebolBounce" ["'number"]  ; see [1]

        ; Other pointer types aren't strings.  `unsigned char *` is a byte
        ; array, and should perhaps use ArrayBuffer.  But for now, just assume
        ; anyone working with bytes is okay calling emscripten API functions
        ; directly (e.g. see getValue(), setValue() for peeking and poking).
        ;
        ; !!! It would be nice if Value* could be type safe in the API and
        ; maybe have some kind of .toString() method, so that it would mold
        ; automatically?  Maybe wrap the emscripten number in an object?
        ;
        find s "*" ["'number'"]  ; see [1]

        ; !!! There are currently no APIs that deal in arrays directly
        ;
        find s "[" ["'array'"]  ; see [1]

        ; !!! JavaScript has a Boolean type...figure out how to use correctly
        ;
        s = "bool" ["'boolean'"]  ; see [1]

        ; For the moment, emscripten is 32-bit, and all of these types map
        ; to JavaScript's simple "Number" type:
        ;
        ; https://developers.google.com/web/updates/2018/05/bigint
        ;
        find:case [
            "int"
            "unsigned int"
            "double"
            "intptr_t"
            "uintptr_t"
            "int32_t"
            "uint32_t"
            "size_t"
        ] s ["'number'"]  ; see [1]

        ; As of Emscripten 4.0.0 (14-Jan-2025) the C int64_t type maps to
        ; JavaScript's BigInt by default:
        ;
        ;  https://github.com/emscripten-core/emscripten/pull/22993
        ;
        ; But BigInt is awkward in JavaScript and not seamlessly compatible
        ; with Number.  Hence this forced the hand of dividing up the API
        ; functions to rebInteger() vs. rebInteger64() etc.
        ;
        find:case [
            "int64_t"
            "uint64_t"
        ] s ["'bigint'"]  ; see [1]

        ; JavaScript has undefined as what `function() {return;}` returns.
        ; The differences between undefined and null are subtle and easy to
        ; get wrong, but a void-returning function should map to undefined.
        ;
        (parse3:match s ["void" opt some space]) [
            "undefined"  ; "undefined" = typeof "undefined"
        ]
    ]
]


; Add special API objects only for JavaScript
;
; The `_internal` APIs don't really need reb.XXX entry points (they are called
; directly as _API_rebXXX()).  But having them in this list makes it easier to
; process them with the other APIs on matters like EMSCRIPTEN_KEEPALIVE and
; ASYNCIFY_BLACKLIST.

append api-objects make object! [
    spec: null  ; e.g. `name: API [...this is the spec, if any...]`
    name: "rebPromise"
    return-type: "intptr_t"
    paramlist: []
    proto: "intptr_t rebPromise(void* p, void* vaptr)"
    is-variadic: okay
]

append api-objects make object! [
    spec: null  ; e.g. `name: API [...this is the spec, if any...]`
    name: "rebResolveNative_internal"  ; !!! see %mod-javascript.c
    return-type: "void"
    paramlist: ["intptr_t" frame_id "intptr_t" value_id]
    proto: unspaced [
        "void rebResolveNative_internal(intptr_t frame_id, intptr_t value_id)"
    ]
    is-variadic: null
]

append api-objects make object! [
    spec: null  ; e.g. `name: API [...this is the spec, if any...]`
    name: "rebRejectNative_internal"  ; !!! see %mod-javascript.c
    return-type: "void"
    paramlist: ["intptr_t" frame_id "intptr_t" error_id]
    proto: unspaced [
        "void rebRejectNative_internal(intptr_t frame_id, intptr_t error_id)"
    ]
    is-variadic: null
]

append api-objects make object! [
    spec: null  ; e.g. `name: API [...this is the spec, if any...]`
    name: "rebIdle_internal"  ; !!! see %mod-javascript.c
    return-type: "void"
    paramlist: []
    proto: "void rebIdle_internal(void)"
    is-variadic: null
]

if null [  ; Only used if DEBUG_JAVASCRIPT_SILENT_TRACE (how to know here?)
    append api-objects make object! [
        spec: null  ; e.g. `name: API [...this is the spec, if any...]`
        name: "rebGetSilentTrace_internal"  ; !!! see %mod-javascript.c
        return-type: "intptr_t"
        paramlist: []
        proto: unspaced [
            "intptr_t rebGetSilentTrace_internal(void)"
        ]
        is-variadic: null
    ]
]


for-each-api [
    any [
        find name "_internal"  ; called as API_rebXXX(), don't need reb.XXX()
        name = "rebStartup"  ; the reb.Startup() is offered by load_r3.js
        name = "rebBytes"  ; JS variant returns array that knows its size
        name = "rebRequestHalt"  ; JS variant adds canceling JS promises too
    ]
    then [
        continue
    ]

    let no-reb-name: null
    parse3:match name ["reb" no-reb-name: across to <end>] else [
        panic ["API name must start with `reb`" name]
    ]

    let js-return-type: any [
        if find name "Promise" [<promise>]
        to-js-type return-type
        panic ["No JavaScript return mapping for type" return-type]
    ]

    let js-param-types: collect* [  ; CSCAPE won't auto-delimit [], use COLLECT*
        for-each [type var] paramlist [
            keep to-js-type type else [
                panic [
                    "No JavaScript argument mapping for type" type
                    "used by" name "with paramlist" mold paramlist
                ]
            ]
        ]
    ]

    if not is-variadic [
        e-cwrap/emit cscape [:api --[
            reb.$<No-Reb-Name> = cwrap_tolerant(  /* vs. R3Module.cwrap() */
                'API_$<Name>',
                $<Js-Return-Type>, [
                    $(Opt Js-Param-Types),
                ]
            )
        ]--]
        continue
    ]

    if js-param-types [
        print cscape [
            :api
            "!!! Note: Skipping mixed variadic for JavaScript: $<Name> !!!"
        ]
        continue
    ]

    let prologue: if null [
        ; It can be useful for debugging to see the API entry points;
        ; using console.error() adds a stack trace to it.
        ;
        unspaced [--[console.error("Entering ]-- name --[");^/]--]
    ] else [
        null
    ]

    let epilogue: if null [
        ; Similar to debugging on entry, it can be useful on exit to see
        ; when APIs return...code comes *before* the return statement.
        ;
        unspaced [--[console.error("Exiting ]-- name --[");^/]--]
    ] else [
        null
    ]

    let code-for-returning: trim:auto copy (switch js-return-type [
        "'string'" [
            ;
            ; If `char *` is returned, it was rebAlloc'd and needs to be freed
            ; if it is to be converted into a JavaScript string
            --[
                if (a == 0)  // null, can come back from e.g. rebSpellOpt()
                    return null
                var js_str = UTF8ToString(a)
                reb.Free(a)
                return js_str
            ]--
        ]
        <promise> [
            ;
            ; The promise returns an ID of what to use to write into the table
            ; for the [resolve, reject] pair.  It will run the code that
            ; will call the rebResolveNative() later...after a setTimeout, so
            ; it is sure that this table entry has been entered.
            --[
                return new Promise(function(resolve, reject) {
                    reb.RegisterId_internal(a, [resolve, reject])
                })
            ]--
        ]
    ] else [
        ; !!! Doing return and argument transformation needs more work!
        ; See suggestions: https://forum.rebol.info/t/817

        --[return a]--
    ])

    e-cwrap/emit cscape [:api --[
        reb.$<No-Reb-Name> = function() {
            $<Opt Prologue>
            let argc = arguments.length
            let stack = stackSave()
            let packed = stackAlloc(4 * (argc + 1))
            for (let i = 0; i < argc; ++i) {
                let arg = arguments[i]
                let p  /* heap address for (maybe) adjusted argument */

                switch (typeof arg) {
                  case 'string': {  /* JS strings act as source code */
                    let len = lengthBytesUTF8(arg) + 4
                    len = len & ~3  /* corrected to align in 32-bits? */
                    p = stackAlloc(len)
                    stringToUTF8(arg, p, len)
                    break }

                  case 'number':  /* heap address, e.g. Cell pointer */
                    p = arg
                    break

                  default:
                    throw Error("Invalid type!")
                }

                HEAP32[(packed>>2) + i] = p
            }

            HEAP32[(packed>>2) + argc] = reb.END

            a = reb.m._API_$<Name>(
                this.getBinding(),  /* "virtual", overridden in shadow */
                packed,
                0   /* null vaptr means `p` is array of `const void*` */
            )

            stackRestore(stack)

            $<Opt Epilogue>
            $<Code-For-Returning>
        }
    ]--]
]

e-cwrap/emit ---[
    /*
     * JavaScript lacks the idea of "virtual fields" which you can mention in
     * methods in a base class, but then shadow in a derived class such that
     * the base methods see the updates.  But if you override a function then
     * that override will effectively be "virtual", such that the methods
     * will call it.
     */
    reb.getBinding = function() { return 0 }  /* null means use user context */

    reb.R = reb.RELEASING
    reb.Q = reb.QUOTING
    reb.U = reb.UNQUOTING

    /* !!! reb.T()/reb.I()/reb.L() could be optimized entry points, but make
     * them compositions for now, to ensure that it's possible for the user to
     * do the same tricks without resorting to editing libRebol's C code.
     */

    reb.T = function(utf8) {
        return reb.R(reb.Text(utf8))  /* might reb.Text() delayload? */
    }

    reb.I = function(int64) {
        return reb.R(reb.Integer(int64))
    }

    reb.L = function(flag) {
        return reb.R(reb.Logic(flag))
    }

    reb.V = function() {  /* https://stackoverflow.com/a/3914600 */
        return reb.R(reb.Value.apply(null, arguments));
    }

    reb.Startup = function() {
        _API_rebStartup()

        /* reb.END is a 2-byte sequence that must live at some address
         * it must be initialized before any variadic libRebol API will work
         */
        reb.END = _malloc(2)
        setValue(reb.END, -9, 'i8')  /* 0xF7, see also `#define rebEND` */
        setValue(reb.END + 1, 0, 'i8')  /* 0x00 */
    }

    reb.Blob = function(array) {  /* how about `reb.Blob([1, 2, 3])` ? */
        let view = null
        if (array instanceof ArrayBuffer)
            view = new Int8Array(array)  /* Int8Array.from() gives 0 length */
        else if (array instanceof Int8Array)
            view = array
        else if (array instanceof Uint8Array)
            view = array
        else
            throw Error("Unknown array type in reb.Blob " + typeof array)

        let binary = reb.m._API_rebUninitializedBlob_internal(view.length)
        let head = reb.m._API_rebBlobHead_internal(binary)
        Module.writeArrayToMemory(view, head)  /* w/Int8Array.set() on HEAP8 */

        return binary
    }

    /* While there's `writeArrayToMemory()` offered by the API, it doesn't
     * seem like there's a similar function for reading.  Review:
     *
     * https://stackoverflow.com/a/53605865
     */
    reb.Bytes = function(binary) {
        let ptr = reb.m._API_rebBlobAt_internal(binary)
        let size = reb.m._API_rebBlobSizeAt_internal(binary)

        var view = new Uint8Array(reb.m.HEAPU8.buffer, ptr, size)

        /* Copy method: https://stackoverflow.com/a/22114687/211160
         */
        var buffer = new ArrayBuffer(size)
        new Uint8Array(buffer).set(view)
        return buffer
    }

    /*
     * JS-NATIVE has a spec which is a Rebol block (like FUNC) but a body that
     * is a TEXT! of JavaScript code.  For efficiency, that text is made into
     * a function one time (as opposed to eval()'d each time).  The function
     * is saved in this map, where the key is the heap pointer that identifies
     * the ACTION! (turned into an integer)
     */

    reb.JS_NATIVES = {}  /* !!! would a Map be more performant? */
    reb.JS_CANCELABLES = new Set()  /* American spelling has one 'L' */
    reb.JS_ERROR_HALTED = Error("Halted by Escape, reb.Halt(), or HALT")

    /* If we just used raw ES6 Promises there would be no way for a signal to
     * cancel them.  Whether it was a setTimeout(), a fetch(), or otherwise...
     * control would not be yielded.  So we wrap returned promises from a
     * JS-AWAITER based on how Promises are made cancelable in React.js, and
     * the C code EM_ASM()-calls the cancel() method on reb.Halt():
     *
     * https://stackoverflow.com/a/37492399
     *
     * !!! The original code returned an object which was not itself a
     * promise, but provided a cancel method.  But we add cancel() to the
     * promise itself, because an async function would not be able to
     * return a "wrapped" promise to provide its own cancellation.
     *
     * !!! This is conceived as a generic API which someone might be able to
     * use in the body of a JS-AWAITER (e.g. with `await`).  But right now,
     * there's no way to find those promises.  All cancelable promises would
     * have to be tracked, and when resolve() or reject() was called the
     * tracking entries in the table would have to be removed...with all
     * actively cancelable promises canceled by reb.RequestHalt().  TBD.
     */
    reb.Cancelable = (promise) => {
        /*
         * We are going to put this promise into a set, which we call cancel()
         * on in case of a reb.RequestHalt().  This means even if a promise was
         * already cancellable, we have to hook its resolve() and reject()
         * to take it out of the set for normal non-canceled operation.
         *
         * !!! For efficiency we could fold this into reb.Promise(), so that
         * if someone does `await reb.Promise()` they don't have to explicitly
         * make it cancelable, but we'd have to recognize Rebol promises.
         */

        let cancel  /* defined in promise scope, but added to promise */

        let cancelable = new Promise((resolve, reject) => {
            let wasCanceled = false

            promise.then((val) => {
                if (wasCanceled) {
                    /* it was rejected already, just ignore... */
                }
                else {
                    resolve(val)
                    reb.JS_CANCELABLES.delete(cancelable)
                }
            })
            promise.catch((error) => {
                if (wasCanceled) {
                    /* else it was rejected already, just ignore... */
                }
                else {
                    reject(error)
                    reb.JS_CANCELABLES.delete(cancelable)
                }
            })

            cancel = function() {
                if (typeof promise.cancel === 'function') {
                    /*
                     * Is there something we can do for interoperability with
                     * a promise that was aleady cancellable (e.g. a BlueBird
                     * cancellable promise)?  If we chain to that cancel, can
                     * we still control what kind of error signal is given?
                     *
                     * http://bluebirdjs.com/docs/api/cancellation.html
                     */
                }

                wasCanceled = true
                reject(reb.JS_ERROR_HALTED)

                /* !!! Supposedly it is safe to iterate and delete at the
                 * same time.  If not, reb.JS_CANCELABLES would need to be
                 * copied by the iteration to allow this deletion:
                 *
                 * https://stackoverflow.com/q/28306756/
                 */
                reb.JS_CANCELABLES.delete(cancelable)
            }
        })
        cancelable.cancel = cancel

        reb.JS_CANCELABLES.add(cancelable)  /* reb.RequestHalt() can call */
        return cancelable
    }

    reb.RequestHalt = function() {
        /*
         * Standard request to the interpreter not to perform any more Rebol
         * evaluations...next evaluator step forces a THROW of a HALT signal.
         */
        _API_rebRequestHalt()

        /* For JavaScript, we additionally take any JS Promises which were
         * registered via reb.Cancelable() and ask them to cancel.  The
         * cancelability is automatically added to any promises that are used
         * as the return result for a JS-AWAITER, but the user can explicitly
         * request augmentation for promises that they manually `await`.
         */
        reb.JS_CANCELABLES.forEach(promise => {
            promise.cancel()
        })
    }

    reb.JavaScriptError = function(e, source_id) {
        let source = source_id ? reb.Value(source_id) : reb.Space()

        return reb.Value("make warning! [",
            "id: 'javascript-error",
            "message:", reb.T(String(e)),
            "arg1:", reb.R(source),
        "]")
    }

    reb.RegisterId_internal = function(id, fn) {
        if (id in reb.JS_NATIVES)
            throw Error("Already registered " + id + " in JS_NATIVES table")
        reb.JS_NATIVES[id] = fn
    }

    reb.UnregisterId_internal = function(id) {
        if (!(id in reb.JS_NATIVES))
            throw Error("Can't delete " + id + " in JS_NATIVES table")
        delete reb.JS_NATIVES[id]
    }

    reb.RunNative_internal = function(native_id, frame_id) {
        if (!(native_id in reb.JS_NATIVES))
            throw Error("Can't dispatch " + native_id + " in JS_NATIVES table")

        let resolver = function(res) {
            if (arguments.length > 1)
                throw Error("JS-NATIVE's return/resolve() takes 1 argument")

            /* JS-AWAITER results become Rebol ACTION! returns, and must be
             * received by arbitrary Rebol code.  Hence they can't be any old
             * JavaScript object...they must be a Value*, today a raw heap
             * address (Emscripten uses "number", someday that could be
             * wrapped in a specific JS object type).  Also allow null and
             * undefined...such auto-conversions may expand in scope.
             */

            let bounce_id
            if (res === undefined)  /* `resolve()`, `resolve(undefined)` */
                bounce_id = reb.Tripwire()  /* allow it */
            else if (res === null)  /* explicitly, e.g. `resolve(null)` */
                bounce_id = 0  /* allow it */
            else if (typeof res == "number") { /* hope it's API heap handle */
                bounce_id = res
            }
            else {
                console.log("typeof " + typeof res)
                console.log(res)
                throw Error(
                    "JS-NATIVE return/resolve takes Value*, null, undefined"
                )
            }

            reb.m._API_rebResolveNative_internal(frame_id, bounce_id)
        }

        let rejecter = function(rej) {
            if (arguments.length > 1)
                throw Error("JS-AWAITER's reject() takes 1 argument")

            /* If a JavaScript throw() happens in the body of a JS-AWAITER's
             * textual JS code, that throw's arg will wind up here.  The
             * likely "bubble up" policy will always make catch arguments a
             * JavaScript Error(), even if it's wrapping a Value* ERROR! as
             * a data member.  It may-or-may-not make sense to prohibit raw
             * Rebol values here.
             */

            if (typeof rej == "number")
                console.log("Suspicious numeric throw() in JS-AWAITER");

            let error_id;
            if (rej == reb.JS_ERROR_HALTED)
                error_id = 0  /* in halt state, can't run more code! */
            else
                error_id = reb.JavaScriptError(rej)

            reb.m._API_rebRejectNative_internal(frame_id, error_id)
        }

        /*
         * The shadowing object is a variant of the `reb` API object which
         * knows what frame it's in.
         */
        let reb_shadow = {
            binding: frame_id,  /* won't be seen by base class reb */
            getBinding: function() { return this.binding },  /* will be seen */
            __proto__: reb
        }

        let native = reb.JS_NATIVES[native_id]
        if (native.is_awaiter) {
            /*
             * There is no built in capability of ES6 promises to cancel, but
             * we make the promise given back cancelable.
             */
            let promise = reb.Cancelable(native(reb_shadow))
            promise.then(resolver).catch(rejecter)  /* cancel causes reject */

            /* resolve() or reject() cannot be signaled yet...JavaScript does
             * not distinguish synchronously fulfilled results:
             *
             *     async function f() { return 1020; }  // auto-promise-ifies
             *     f().then(function() { console.log("prints second"); });
             *     console.log("prints first");  // doesn't care it's resolved
             *
             * Hence the caller must wait for a resolve/reject signal.
             */
        }
        else {
            try {
                resolver(native(reb_shadow))
            }
            catch(e) {
                rejecter(e)
            }

            /* resolve() or reject() guaranteed to be signaled in this case */
        }
    }

    reb.ResolvePromise_internal = function(promise_id, rebval) {
        if (!(promise_id in reb.JS_NATIVES))
            throw Error(
                "Can't find promise_id " + promise_id + " in JS_NATIVES"
            )
        reb.JS_NATIVES[promise_id][0](rebval)  /* [0] is resolve() */
        reb.UnregisterId_internal(promise_id);
    }

    reb.RejectPromise_internal = function(promise_id, throw_id) {
        if (!(throw_id in reb.JS_NATIVES))  /* frame_id of throwing awaiter */
            throw Error(
                "Can't find throw_id " + throw_id + " in JS_NATIVES"
            )
        let error = reb.JS_NATIVES[throw_id]  /* typically a JS Error() obj */
        reb.UnregisterId_internal(throw_id)

        if (!(promise_id in reb.JS_NATIVES))
            throw Error(
                "Can't find promise_id " + promise_id + " in JS_NATIVES"
            )
        reb.JS_NATIVES[promise_id][1](error)  /* [1] is reject() */
        reb.UnregisterId_internal(promise_id)
    }

    reb.Box = function(js_value) {  /* primordial generic "boxing" routine */
        if (null === js_value)  /* typeof test doesn't work */
            return null  /* See https://stackoverflow.com/a/18808270/ */

        switch (typeof js_value) {
          case 'undefined':
            return reb.Tripwire()  /* or `reb.Value("~undefined~") antiform? */

          case 'number':
            return reb.Integer(js_value)

          case 'bigint':
            return reb.Integer64(js_value);

          case 'string':
            return reb.Text(js_value)

          default:  /* used by JS-EVAL* with /VALUE; should it error here? */
            return reb.Tripwire()
        }
    }
]---

if null [  ; Only used if DEBUG_JAVASCRIPT_SILENT_TRACE (how to know here?)
    e-cwrap/emit --[
        reb.GetSilentTrace_internal = function() {
            return UTF8ToString(reb.m._API_rebGetSilentTrace_internal())
        }
    ]--
]

e-cwrap/write-emitted


=== "GENERATE EMSCRIPTEN KEEPALIVE LIST" ===

; It is possible to tell the linker what functions to keep alive via the
; EMSCRIPTEN_KEEPALIVE annotation.  But we don't want %rebol.h to be dependent
; upon the emscripten header.  Since it's possible to use a JSON file to
; specify the list with EXPORTED_FUNCTIONS (via an @), we use that instead.
;
;     EXPORTED_FUNCTIONS=@libr3.exports.json
;

json-collect: func [
    return: [text!]
    body [block!]
][
    let results: collect compose [
        keep: adapt keep/ [  ; Emscripten prefixes functions w/underscore
            if text? value [
                value: unspaced [-["_]- value -["]-]  ; bootstrap semantics
            ]
            else [
                value: quote unspaced [-["_]- unquote value -["]-]  ; ^META
            ]
        ]
        (spread body)
    ]
    return cscape [results --[
        [
            $(Results),
        ]
    ]--]
]

write (join prep-dir %include/libr3.exports.json) json-collect [
    for-each-api [keep unspaced ["API_" name]]
    keep "malloc"  ; !!! Started requiring, did not before (?)
]


=== "GENERATE ASYNCIFY BLACKLIST FILE" ===

; Asyncify has some automatic ability to determine what functions cannot be
; on the stack when a function may yield.  It then does not instrument these
; functions with the additional code allowing it to yield.  However, it makes
; a conservative guess...so it can be helped with additional blacklisted
; functions that one has knowledge should *not* be asyncified:
;
; https://emscripten.org/docs/porting/asyncify.html#optimizing
;
; While the list expected by Emscripten is JSON, that doesn't support comments
; so we transform it from Rebol.
;
; <review> ; IN LIGHT OF ASYNCIFY
; Beyond speed, an API marked in the blacklist has the special ability of
; still being run while in the yielding state (a feature added to Emscripten
; due to a Ren-C request):
;
; https://stackoverflow.com/q/51204703/
;
; This means that APIs which are able to be blacklisted can be called directly
; from within a JS-AWAITER.  That means being able to produce `reb.Text()`
; and other values.  But also critically can include reb.Promise() so that
; the final return value of a JS-AWAITER can be returned with it.
; </review>

write (join prep-dir %include/asyncify-blacklist.json) delimit newline collect [
    keep "["
    for-next 'names load3 %../asyncify-blacklist.r [
        keep unspaced [____ -["]- names.1 -["]- if not last? names [","]]
    ]
    keep "]"
]


=== "GENERATE %NODE-PRELOAD.JS" ===

; !!! Node.js support lapsed for a time, due to no pthreads support:
;
; https://groups.google.com/d/msg/emscripten-discuss/NxpEjP0XYiA/xLPiXEaTBQAJ
;
; When it did work, it required code to be run in the `--pre-js` section.
;
; https://github.com/emscripten-core/emscripten/issues/4240
;
; However, much has changed (e.g. the emterpreter no longer is used due to
; asyncify).  This code is non-working, and needs review.
;

e-node-preload: make-emitter "Emterpreter Preload for Node.js" (
    join prep-dir %include/node-preload.js
)

e-node-preload/emit ---[
    var R3Module = {};
    console.log("Yes we're getting a chance to preload...")
    console.log(__dirname + '/libr3.bytecode')
    var fs = require('fs');

    /* We don't want the direct result, but want the ArrayBuffer
     * Hence the .buffer (?)
     */
    R3Module.emterpreterFile =
        fs.readFileSync(__dirname + '/libr3.bytecode').buffer

    console.log(R3Module.emterpreterFile)
]---

e-node-preload/write-emitted
