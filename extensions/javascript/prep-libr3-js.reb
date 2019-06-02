REBOL [
    Title: "Pre-Build Step for JavaScript Files Passed to EMCC"
    File: %prep-libr3-js.reb  ; used by MAKE-EMITTER

    ; !!! This file can't be `Type: 'Module`, it needs MAKE-EMITTER, CSCAPE,
    ; etc. in the user context and thus isn't isolated.  It is run directly by
    ; a DO from the C libRebol prep script, so that it will have the
    ; structures and helpers set up to process API endpoints.  However, the
    ; ability to do this should be offered as a general build system hook
    ; to extensions (also needed by the TCC extension)

    Version: 0.1.0
    Date: 15-Sep-2018

    Rights: "Copyright (C) 2018-2019 hostilefork.com"

    License: {LGPL 3.0}

    Description: {
        The WASM files produced by Emscripten produce JavaScript functions
        that expect their arguments to be in terms of the SharedArrayBuffer
        HEAP32 that the C code can see.  For common JavaScript types, the
        `cwrap` helper can do most of the work:

        https://emscripten.org/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.html

        However, libRebol makes extensive use of variadic functions, which
        means it needs to do interact with the `va_list()` convention.
        This is beyond the C standard and each compiler can implement it
        differently.  But it was reverse-engineered from emcc/clang build
        output for a simple variadic function.  Since that's the only
        compiler Emscripten works with, we mimic its method of allocation
        with a custom variant of the cwrap helper.
    }
]

e-cwrap: (make-emitter
    "JavaScript C Wrapper functions" output-dir/reb-lib.js
)

=== EMTERPRETER-BLACKLIST TOLERANT CWRAP ===

; Emscripten's `cwrap` is based on a version of ccall which does not allow
; the emterpreter to execute a function while emscripten_sleep_with_yield()
; is in effect.  However, there was a feature added for Ren-C to be able to
; call the WASM function in this case:
;
; https://stackoverflow.com/q/51204703/
;
; This is not accounted for, so we can't use cwrap()/ccall().  For the moment
; we copy the code directly from %preamble.js...minus that assert:
;
; https://github.com/emscripten-core/emscripten/blob/incoming/src/preamble.js
;
; !!! This workaround is only necessary for the emterpreter build.  But also,
; there are few enough routines that a better answer is probably to dodge
; inclusion of `cwrap`/`ccall` altogether and just by-hand wrap the routines.
;
e-cwrap/emit {
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
          writeArrayToMemory(arr, ret);
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

    // This is the part we want to avoid.  Something like reb.Text() is calling
    // the _RL_rebText() function underneath, and that's in EMTERPRETER_BLACKLIST,
    // but the main cwrap/ccall() does not account for it.
    //
    /* <ren-c modification>
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

      ret = convertReturnValue(ret);
      if (stack !== 0) stackRestore(stack);
      return ret;
    }

    function cwrap_tolerant(ident, returnType, argTypes, opts) {
      argTypes = argTypes || [];
      // When the function takes numbers and returns a number, we can just return
      // the original function
      var numericArgs = argTypes.every(function(type){ return type === 'number'});
      var numericRet = returnType !== 'string';
      if (numericRet && numericArgs && !opts) {
        return getCFunc(ident);
      }
      return function() {
        return ccall_tolerant(ident, returnType, argTypes, arguments, opts);
      }
    }
}


=== GENERATE C WRAPPER FUNCTIONS ===

e-cwrap/emit {
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
    var reb  /* local definition only if not using modules */

    /* Could use ENVIRONMENT_IS_NODE here, but really the test should be for
     * if the system supports modules (someone with an understanding of the
     * state of browser modules should look at this).  Note `Module.exports`
     * seems not to be defined, even in the node version.
     */
    if (typeof module !== 'undefined')
        reb = module.exports  /* add to what you get with require('rebol') */
    else
        reb = {}  /* build a new dictionary to use reb.Xxx() if in browser */
}

to-js-type: func [
    return: [<opt> text! tag!]
    s [text!] "C type as string"
][
    case [
        ; APIs dealing with `char *` means UTF-8 bytes.  While C must memory
        ; manage such strings (at the moment), the JavaScript wrapping assumes
        ; input parameters should be JS strings that are turned into temp
        ; UTF-8 on the emscripten heap (freed after the call).  Returned
        ; `char *` should be turned into JS GC'd strings, then freed.
        ;
        ; !!! These APIs can also return nulls.  rebSpell("second [{a}]") is
        ; now null, as a way of doing passthru on failures.
        ;
        (s = "char *") or [s = "const char *"] ["'string'"]

        ; Other pointer types aren't strings.  `unsigned char *` is a byte
        ; array, and should perhaps use ArrayBuffer.  But for now, just assume
        ; anyone working with bytes is okay calling emscripten API functions
        ; directly (e.g. see getValue(), setValue() for peeking and poking).
        ;
        ; !!! It would be nice if REBVAL* could be type safe in the API and
        ; maybe have some kind of .toString() method, so that it would mold
        ; automatically?  Maybe wrap the emscripten number in an object?
        ;
        find s "*" ["'number'"]

        ; !!! There are currently no APIs that deal in arrays directly
        ;
        find s "[" ["'array'"]

        ; !!! JavaScript has a Boolean type...figure out how to use correctly
        ;
        s = "bool" ["'Boolean'"]

        ; !!! JavaScript does not differentiate numeric types, though it does
        ; have a BigInt, which should be considered when bignum is added:
        ;
        ; https://developers.google.com/web/updates/2018/05/bigint
        ;
        find/case [
            "int"
            "unsigned int"
            "double"
            "intptr_t"
            "uintptr_t"
            "int64_t"
            "uint32_t"
            "size_t"
            "REBRXT"
        ] s ["'number'"]

        ; JavaScript has undefined as what `function() {return;}` returns.
        ; The differences between undefined and null are subtle and easy to
        ; get wrong, but a void-returning function should map to undefined.
        ;
        parse s ["void" any space end] ["undefined"]
    ]
]


; Add special API objects only for JavaScript
;
; The `_internal` APIs don't really need reb.XXX entry points (they are called
; directly as _RL_rebXXX()).  But having them in this list makes it easier to
; process them with the other APIs on matters like EMSCRIPTEN_KEEPALIVE and
; EMTERPRETER_BLACKLIST.

append api-objects make object! [
    spec: _  ; e.g. `name: RL_API [...this is the spec, if any...]`
    name: "rebPromise"
    returns: "intptr_t"
    paramlist: []
    proto: "intptr_t rebPromise(unsigned char quotes, void *p, va_list *vaptr)"
    is-variadic: true
]

append api-objects make object! [
    spec: _  ; e.g. `name: RL_API [...this is the spec, if any...]`
    name: "rebSignalResolveNative_internal"  ; !!! see %mod-javascript.c
    returns: "void"
    paramlist: ["intptr_t" frame_id]
    proto: unspaced [
        "void rebSignalResolveNative_internal(intptr_t frame_id)"
    ]
    is-variadic: false
]

append api-objects make object! [
    spec: _  ; e.g. `name: RL_API [...this is the spec, if any...]`
    name: "rebSignalRejectNative_internal"  ; !!! see %mod-javascript.c
    returns: "void"
    paramlist: ["intptr_t" frame_id]
    proto: unspaced [
        "void rebSignalRejectNative_internal(intptr_t frame_id)"
    ]
    is-variadic: false
]

if args/OS_ID = "0.16.2" [  ; APIs for only for pthreads build
    append api-objects make object! [
        spec: _  ; e.g. `name: RL_API [...this is the spec, if any...]`
        name: "rebTakeAwaitLock_internal"  ; !!! see %mod-javascript.c
        returns: "void"
        paramlist: ["intptr_t" native_id]
        proto: unspaced [
            "void rebTakeAwaitLock_internal(void)"
        ]
        is-variadic: false
    ]
] else [  ; APIs only for emterpreter build
    append api-objects make object! [
        spec: _  ; e.g. `name: RL_API [...this is the spec, if any...]`
        name: "rebIdle_internal"  ; !!! see %mod-javascript.c
        returns: "void"
        paramlist: []
        proto: "void rebIdle_internal(void)"
        is-variadic: false
    ]
]

if false [  ; Only used if DEBUG_JAVASCRIPT_SILENT_TRACE (how to know here?)
    append api-objects make object! [
        spec: _  ; e.g. `name: RL_API [...this is the spec, if any...]`
        name: "rebGetSilentTrace_internal"  ; !!! see %mod-javascript.c
        returns: "intptr_t"
        paramlist: []
        proto: unspaced [
            "intptr_t rebGetSilentTrace_internal(void)"
        ]
        is-variadic: false
    ]
]


map-each-api [
    any [
        find name "_internal"  ; called as _RL_rebXXX(), don't need reb.XXX()
        name = "rebStartup"  ; the reb.Startup() is offered by load_r3.js
        name = "rebBytes"  ; JS variant returns array that knows its size
        name = "rebHalt"  ; JS variant augmented to cancel JS promises too
    ]
    then [
        continue
    ]

    no-reb-name: _
    if not parse name ["reb" copy no-reb-name to end] [
        fail ["API name must start with `reb`" name]
    ]

    js-returns: any [
        if find name "Promise" [<promise>]
        to-js-type returns
        fail ["No JavaScript return mapping for type" returns]
    ]

    js-param-types: try collect* [
        for-each [type var] paramlist [
            keep to-js-type type else [
                fail [
                    {No JavaScript argument mapping for type} type
                    {used by} name {with paramlist} mold paramlist
                ]
            ]
        ]
    ]

    if is-variadic [
        if js-param-types [
            print cscape/with
            "!!! WARNING! !!! Skipping mixed variadic function $<Name> !!!"
            api
            continue
        ]

        enter: copy {_RL_rebEnterApi_internal();}
        if false [
            ; It can be useful for debugging to see the API entry points;
            ; using console.error() adds a stack trace to it.
            ;
            append enter unspaced [{^/console.error("Entering } name {");}]
        ]

        return-code: if false [
            ; Similar to debugging on entry, it can be useful on exit to see
            ; when APIs return...code comes *before* the return statement.
            ;
            unspaced [{console.error("Exiting } name {");^/}]
        ] else [
            copy {}
        ]
        append return-code trim/auto copy switch js-returns [
          "'string'" [
            ;
            ; If `char *` is returned, it was rebAlloc'd and needs to be freed
            ; if it is to be converted into a JavaScript string
            {
                var js_str = UTF8ToString(a)
                reb.Free(a)
                return js_str
            }
          ]
          <promise> [
            ;
            ; The promise returns an ID of what to use to write into the table
            ; for the [resolve, reject] pair.  It will run the code that
            ; will call the RL_Resolve later...after a setTimeout, so it is
            ; sure that this table entry has been entered.
            ;
            {
                return new Promise(function(resolve, reject) {
                    reb.RegisterId_internal(a, [resolve, reject])
                })
            }
          ]

          ; !!! Doing return and argument transformation needs more work!
          ; See suggestions: https://forum.rebol.info/t/817

          default [
            {return a}
          ]
        ]

        e-cwrap/emit cscape/with {
            reb.$<No-Reb-Name>_qlevel = function() {
                $<Enter>
                var argc = arguments.length
                var stack = stackSave()
                var va = stackAlloc(4 * (argc + 1 + 1))
                var a, i, l, p
                for (i=0; i < argc; i++) {
                    a = arguments[i]
                    switch (typeof a) {
                      case 'string':
                        l = lengthBytesUTF8(a) + 4
                        l = l & ~3
                        p = stackAlloc(l)
                        stringToUTF8(a, p, l)
                        break
                      case 'number':
                        p = a
                        break
                      default:
                        throw Error("Invalid type!")
                    }
                    HEAP32[(va>>2) + i] = p
                }

                HEAP32[(va>>2) + argc] = reb.END

                /* `va + 4` is where first vararg is, must pass as *address*.
                 * Just put that address on the heap after the reb.END.
                 */
                HEAP32[(va>>2) + (argc + 1)] = va + 4

                a = _RL_$<Name>(this.quotes, HEAP32[va>>2], va + 4 * (argc + 1))

                stackRestore(stack)

                $<Return-Code>
            }

            reb.$<No-Reb-Name> = reb.$<No-Reb-Name>_qlevel.bind({quotes: 0})

            reb.$<No-Reb-Name>Q = reb.$<No-Reb-Name>_qlevel.bind({quotes: 1})
        } api
    ] else [
        e-cwrap/emit cscape/with {
            reb.$<No-Reb-Name> = cwrap_tolerant(  /* vs. Module.cwrap() */
                'RL_$<Name>',
                $<Js-Returns>, [
                    $(Js-Param-Types),
                ]
            )
        } api
    ]
]
e-cwrap/emit {
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
        _RL_rebStartup()

        /* reb.END is a 2-byte sequence that must live at some address
         * it must be initialized before any variadic libRebol API will work
         */
        reb.END = _malloc(2)
        setValue(reb.END, -127, 'i8')  /* 0x80 */
        setValue(reb.END + 1, 0, 'i8')  /* 0x00 */
    }

    reb.Binary = function(array) {  /* how about `reb.Binary([1, 2, 3])` ? */
        let view = null
        if (array instanceof ArrayBuffer)
            view = new Int8Array(array)  /* Int8Array.from() gives 0 length */
        else if (array instanceof Int8Array)
            view = array
        else if (array instanceof Uint8Array)
            view = array
        else
            throw Error("Unknown array type in reb.Binary " + typeof array)

        let binary = _RL_rebUninitializedBinary_internal(view.length)
        let head = _RL_rebBinaryHead_internal(binary)
        writeArrayToMemory(view, head)  /* uses Int8Array.set() on HEAP8 */

        return binary
    }

    /* While there's `writeArrayToMemory()` offered by the API, it doesn't
     * seem like there's a similar function for reading.  Review:
     *
     * https://stackoverflow.com/a/53605865
     */
    reb.Bytes = function(binary) {
        let ptr = _RL_rebBinaryAt_internal(binary)
        let size = _RL_rebBinarySizeAt_internal(binary)

        var view = new Uint8Array(Module.HEAPU8.buffer, ptr, size)

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

    let RL_JS_NATIVES = {}  /* !!! would a Map be more performant? */
    let RL_JS_CANCELABLES = new Set()  /* American spelling has one 'L' */
    const RL_JS_ERROR_HALTED = Error("Halted by Escape, reb.Halt(), or HALT")

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
     * actively cancelable promises canceled by reb.Halt().  TBD.
     */
    reb.Cancelable = (promise) => {
        /*
         * We are going to put this promise into a set, which we call cancel()
         * on in case of a reb.Halt().  This means even if a promise was
         * already cancellable, we have to hook its resolve() and reject()
         * to take it out of the set for normal non-canceled operation.
         *
         * !!! For efficiency we could fold this into reb.Promise(), so that
         * if someone does `await reb.Promise()` they don't have to explicitly
         * make it cancelable, but we'd have to recognize Rebol promises.
         */

        let cancel  /* defined inside promise scope, but added to promise */

        let cancelable = new Promise((resolve, reject) => {
            let wasCanceled = false

            promise.then((val) => {
                if (wasCanceled) {
                    /* it was rejected already, just ignore... */
                }
                else {
                    resolve(val)
                    RL_JS_CANCELABLES.delete(cancelable)
                }
            })
            promise.catch((error) => {
                if (wasCanceled) {
                    /* else it was rejected already, just ignore... */
                }
                else {
                    reject(error)
                    RL_JS_CANCELABLES.delete(cancelable)
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
                reject(RL_JS_ERROR_HALTED)

                /* !!! Supposedly it is safe to iterate and delete at the
                 * same time.  If not, RL_JS_CANCELABLES would need to be
                 * copied by the iteration to allow this deletion:
                 *
                 * https://stackoverflow.com/q/28306756/
                 */
                RL_JS_CANCELABLES.delete(cancelable)
            }
        })
        cancelable.cancel = cancel

        RL_JS_CANCELABLES.add(cancelable)  /* reb.Halt() calls if in set */
        return cancelable
    }

    reb.Halt = function() {
        /*
         * Standard request to the interpreter not to perform any more Rebol
         * evaluations...next evaluator step forces a THROW of a HALT signal.
         */
        _RL_rebHalt()

        /* For JavaScript, we additionally take any JS Promises which were
         * registered via reb.Cancelable() and ask them to cancel.  The
         * cancelability is automatically added to any promises that are used
         * as the return result for a JS-AWAITER, but the user can explicitly
         * request augmentation for promises that they manually `await`.
         */
        RL_JS_CANCELABLES.forEach(promise => {
            promise.cancel()
        })
    }


    reb.RegisterId_internal = function(id, fn) {
        if (id in RL_JS_NATIVES)
            throw Error("Already registered " + id + " in JS_NATIVES table")
        RL_JS_NATIVES[id] = fn
    }

    reb.UnregisterId_internal = function(id) {
        if (!(id in RL_JS_NATIVES))
            throw Error("Can't delete " + id + " in JS_NATIVES table")
        delete RL_JS_NATIVES[id]
    }

    reb.RunNative_internal = function(id, frame_id) {
        if (!(id in RL_JS_NATIVES))
            throw Error("Can't dispatch " + id + " in JS_NATIVES table")

        let resolver = function(res) {
            if (arguments.length > 1)
                throw Error("JS-NATIVE's return/resolve() takes 1 argument")

            /* JS-AWAITER results become Rebol ACTION! returns, and must be
             * received by arbitrary Rebol code.  Hence they can't be any old
             * JavaScript object...they must be a REBVAL*, today a raw heap
             * address (Emscripten uses "number", someday that could be
             * wrapped in a specific JS object type).  Also allow null and
             * undefined...such auto-conversions may expand in scope.
             */

            if (res === undefined)  /* `resolve()`, `resolve(undefined)` */
                {}  /* allow it */
            else if (res === null)  /* explicitly, e.g. `resolve(null)` */
                {}  /* allow it */
            else if (typeof res == "function") {
                /*
                 * If using the emterpreter, the awaiter's resolve() is rather
                 * limited, as it can't call emterpreted functions during the
                 * emscripten_sleep_with_yield().  That makes it hard to make
                 * a REBVAL* to resolve() with.  Workaround is to accept a
                 * JavaScript function, and call it when fetching the result.
                 *
                 * (so allow it)
                 */
            }
            else if (typeof res !== "number") {
                console.log("typeof " + typeof res)
                console.log(res)
                throw Error(
                    "JS-NATIVE return/resolve takes REBVAL*, null, undefined"
                )
            }

            RL_JS_NATIVES[frame_id] = res  /* stow result */
            _RL_rebSignalResolveNative_internal(frame_id)
        }

        let rejecter = function(rej) {
            console.log(rej)

            if (arguments.length > 1)
                throw Error("JS-AWAITER's reject() takes 1 argument")

            /* If a JavaScript throw() happens in the body of a JS-AWAITER's
             * textual JS code, that throw's arg will wind up here.  The
             * likely "bubble up" policy will always make catch arguments a
             * JavaScript Error(), even if it's wrapping a REBVAL* ERROR! as
             * a data member.  It may-or-may-not make sense to prohibit raw
             * Rebol values here.
             */

            if (typeof rej == "number")
                console.log("Suspicious numeric throw() in JS-AWAITER");

            RL_JS_NATIVES[frame_id] = rej  /* stow result */
            _RL_rebSignalRejectNative_internal(frame_id)
        }

        let native = RL_JS_NATIVES[id]
        if (native.is_awaiter) {
            /*
             * There is no built in capability of ES6 promises to cancel, but
             * we make the promise given back cancelable.
             */
            let promise = reb.Cancelable(native())
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
                resolver(native())
            }
            catch(e) {
                rejecter(e)
            }

            /* resolve() or reject() guaranteed to be signaled in this case */
        }
    }

    reb.GetNativeResult_internal = function(frame_id) {
        var result = RL_JS_NATIVES[frame_id]  /* resolution or rejection */
        reb.UnregisterId_internal(frame_id);

        if (typeof result == "function")  /* needed to empower emterpreter */
            result = result()  /* ...had to wait to synthesize REBVAL */

        if (result === null)
            return 0
        if (result === undefined)
            return reb.Void()
        return result
    }

    reb.GetNativeError_internal = function(frame_id) {
        var result = RL_JS_NATIVES[frame_id]  /* resolution or rejection */
        reb.UnregisterId_internal(frame_id)
        if (result == RL_JS_ERROR_HALTED)
            return 0  /* in halt state, can't run more code, will throw! */

        return reb.Value("make error!", reb.T(String(result)))
    }

    reb.ResolvePromise_internal = function(promise_id, rebval) {
        if (!(promise_id in RL_JS_NATIVES))
            throw Error(
                "Can't find promise_id " + promise_id + " in JS_NATIVES"
            )
        RL_JS_NATIVES[promise_id][0](rebval)  /* [0] is resolve() */
        reb.UnregisterId_internal(promise_id);
    }

    reb.RejectPromise_internal = function(promise_id, throw_id) {
        if (!(throw_id in RL_JS_NATIVES))  /* frame_id of throwing awaiter */
            throw Error(
                "Can't find throw_id " + throw_id + " in JS_NATIVES"
            )
        let error = RL_JS_NATIVES[throw_id]  /* typically JS Error() Object */
        reb.UnregisterId_internal(throw_id)

        if (!(promise_id in RL_JS_NATIVES))
            throw Error(
                "Can't find promise_id " + promise_id + " in JS_NATIVES"
            )
        RL_JS_NATIVES[promise_id][1](error)  /* [1] is reject() */
        reb.UnregisterId_internal(promise_id)
    }

    reb.Box = function(js_value) {  /* primordial generic "boxing" routine */
        if (null === js_value)  /* typeof test doesn't work */
            return null  /* See https://stackoverflow.com/a/18808270/ */

        switch (typeof js_value) {
          case 'undefined':
            return reb.Void()

          case 'number':
            return reb.Integer(js_value)

          case 'string':
            return reb.Text(js_value)

          default:  /* used by JS-EVAL* with /VALUE; should it error here? */
            return reb.Void()
        }
    }
}

if false [  ; Only used if DEBUG_JAVASCRIPT_SILENT_TRACE (how to know here?)
    e-cwrap/emit {
        reb.GetSilentTrace_internal = function() {
            return UTF8ToString(_RL_rebGetSilentTrace_internal())
        }
    }
]

e-cwrap/write-emitted


=== GENERATE EMSCRIPTEN KEEPALIVE LIST ===

; It is possible to tell the linker what functions to keep alive via the
; EMSCRIPTEN_KEEPALIVE annotation.  But we don't want %rebol.h to be dependent
; upon the emscripten header.  Since it's possible to use a JSON file to
; specify the list with EXPORTED_FUNCTIONS (via an @), we use that instead.
;
;     EXPORTED_FUNCTIONS=@libr3.exports.json
;

json-collect: function [body [block!]] [
    results: collect compose [
        keep: adapt 'keep [  ; Emscripten prefixes functions w/underscore
            value: unspaced [{"} {_} value {"}]
        ]
        ((body))
    ]
    return cscape/with {
        [
            $(Results),
        ]
    } 'results
]

write output-dir/libr3.exports.json json-collect [
    map-each-api [keep unspaced ["RL_" name]]
]


=== GENERATE EMTERPRETER BLACKLIST/WHITELIST FILES ===

; The emterpreter runs C compiled to bytecode vs. directly running WASM code.
; This gives the ability to suspend the C execution--preserving its stack
; state, yet be able to yield to JavaScript and run code on that same single
; thread.  When the JavaScript is done, it can wake up the suspended
; interpreter and the C can continue.  This gives the illusion of being able
; to pass JavaScript work items to a "worker thread"--and while it is slow,
; it's our only option in browsers without WASM threads and SharedArrayBuffer.
;
; But using the emterpreter doesn't mean *every* function has to be run as
; bytecode.  Only functions that would be on the stack somewhere when an
; `emscripten_sleep_with_yield()` is called.  Hence there is a "blacklist"
; of functions that are compiled as normal WASM, not emterpreter bytecode.
; The more functions that can be found as legal to blacklist, the faster
; the emterpreted version can be.
;
; Beyond speed, an API marked in the blacklist has the special ability of
; still being run while in the yielding state (a feature added to Emscripten
; due to a Ren-C request):
;
; https://stackoverflow.com/q/51204703/
;
; This means that APIs which are able to be blacklisted can be called directly
; from inside a JS-AWAITER.  That means being able to produce `reb.Text()`
; and other values.  But also critically can include reb.Promise() so that
; the final return value of a JS-AWAITER can be returned with it.
;

; for now whitelist is manually crafted
write output-dir/emterpreter.whitelist.json read %emterpreter.whitelist.json

write output-dir/emterpreter.blacklist.json json-collect [
    map-each-api [
        if is-variadic and (name != "rebPromise") [
            ;
            ; Currently, all variadic APIs are variadic because they evaluate.
            ; The exception is rebPromise, which takes its variadic list as
            ; a set of instructions to run later.  Blacklisting that means
            ; it is safe to call from a JS-AWAITER (though you cannot *await*
            ; on it inside a JS-AWAITER, see rebPromise() and this post:
            ;
            ; https://stackoverflow.com/q/55186667/
            ;
            continue
        ]

        any [
            name = "rebRescue"
            name = "rebRescueWith"
        ]
        then [
            ; While not variadic, the rescue functions are API functions
            ; which are called from internal code that needs to be emterpreted
            ; and hence can't be blacklisted from being turned to bytecode.
            ;
            continue
        ]

        if name = "rebIdle_internal" [
            ;
            ; When rebPromise() decides not to run an evaluation (hence why
            ; it can be blacklisted), it queues a setTimeout() to the GUI
            ; thread to come back later and run the evaluation.  What it
            ; queues is the "idle" function...which will do evaluations and
            ; will be on the stack during an `emscripten_sleep_with_yield()`.
            ; Hence it can't be blacklisted from being turned to bytecode.
            ;
            continue
        ]

        keep unspaced ["RL_" name]
    ]
]


=== GENERATE %NODE-PRELOAD.JS ===

; While Node.JS has worker support and SharedArrayBuffer support, Emscripten
; does not currently support ENVIRONMENT=node USE_PTHREADS=1:
;
; https://groups.google.com/d/msg/emscripten-discuss/NxpEjP0XYiA/xLPiXEaTBQAJ
;
; Hence if any simulated synchronousness is to be possible under node, one
; must use the emterpreter (hopefully this is a temporary state of affairs).
; In any case, the emterpreter bytecode file must be loaded, and it seems
; that load has to happen in the `--pre-js` section:
;
; https://github.com/emscripten-core/emscripten/issues/4240
;

e-node-preload: (make-emitter
    "Emterpreter Preload for Node.js" output-dir/node-preload.js
)

e-node-preload/emit {
    var Module = {};
    console.log("Yes we're getting a chance to preload...")
    console.log(__dirname + '/libr3.bytecode')
    var fs = require('fs');

    /* We don't want the direct result, but want the ArrayBuffer
     * Hence the .buffer (?)
     */
    Module.emterpreterFile =
        fs.readFileSync(__dirname + '/libr3.bytecode').buffer

    console.log(Module.emterpreterFile)
}

e-node-preload/write-emitted
