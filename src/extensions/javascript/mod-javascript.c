//
//  File: %mod-javascript.c
//  Summary: "Support for calling Javascript from Rebol in Emscripten build"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2018 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This module enables the creation of Rebol ACTION!s whose bodies are strings
// of JavaScript code.  To use it, Rebol must be built with emscripten:
//
// http://kripken.github.io/emscripten-site/
//
// Once built, it must be loaded into a host (e.g. a web browser or node.js)
// which provides a JavaScript interpreter.
//

#include "sys-core.h"
#include "sys-ext.h"

#include "tmp-mod-javascript-first.h"

#include <limits.h> // for UINT_MAX

#include <emscripten.h>

enum {
    IDX_NATIVE_SOURCE = 0, // text string source code of native (for SOURCE)
    IDX_NATIVE_CONTEXT = 1, // rebRun()/etc. bind here (and lib) when running
    IDX_NATIVE_HANDLE = 2, // handle gives hookpoint for GC of table entry
    IDX_NATIVE_MAX
}; // for the ACT_DETAILS() array of a javascript native


//
//  JavaScript_Native_Dispatcher: C
//
// Called when the ACTION! produced by JS-NATIVE is run.
//
const REBVAL *JavaScript_Native_Dispatcher(REBFRM *f)
{
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    UNUSED(details);

    // Getting a value back from JavaScript via EM_ASM_INT can give back an
    // unsigned int.  There are cases in the emscripten code where this is
    // presumed to be good enough to hold any heap address.  Do a sanity
    // check that we aren't truncating.
    //
    uintptr_t id = cast(uintptr_t, ACT_PARAMLIST(FRM_PHASE(f)));
    assert(id < UINT_MAX);

    uintptr_t r = EM_ASM_INT({
        return RL_Dispatch($0);
    }, id);

    return cast(const REBVAL*, r);
}


//
//  JavaScript_Awaiter_Dispatcher: C
//
// Called when the ACTION! produced by JS-AWAITER is run.  The tricky bit is
// that it doesn't actually return to the caller when the body of the JS code
// is done running...it has to wait for either the `resolve` or `reject`
// parameter functions to get called.
//
const REBVAL *JavaScript_Awaiter_Dispatcher(REBFRM *f)
{
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    UNUSED(details);

    // Getting a value back from JavaScript via EM_ASM_INT can give back an
    // unsigned int.  There are cases in the emscripten code where this is
    // presumed to be good enough to hold any heap address.  Do a sanity
    // check that we aren't truncating.
    //
    uintptr_t id = cast(uintptr_t, ACT_PARAMLIST(FRM_PHASE(f)));
    assert(id < UINT_MAX);

    REBYTE volatile atomic = 0;

    // RL_AsyncDispatch() creates two functions that signal back when called
    // to write the atomic with either a 1 or a 2, indicating completion.
    //
    EM_ASM_({
        RL_AsyncDispatch($0, $1)
    }, id, &atomic);

    while (atomic == 0) {
        if (Eval_Signals & SIG_HALT) {
            //
            // !!! TBD: How to handle halts?  We're spinning here, so the
            // GUI should theoretically have a chance to write some cancel.
            // Is it a reject()?
            //
            Eval_Signals &= ~SIG_HALT; // don't preserve state once observed
        }
        emscripten_sleep_with_yield(50); // no resolve(), no reject() calls...
    }

    // Current limitations disallow the calling of EXPORTED_FUNCTIONS during
    // an emscripten_sleep_with_yield().  This might be something that can
    // be worked around with the PTHREAD worker situation...rebPromise()
    // could spawn a thread, which could block and queue off work to the
    // main thread, but then the main thread could still call rebSpell() or
    // other routines that might be necessary to manipulate handles.
    //
    // We work around this limitation in the emterpreter for the moment by
    // forcing all the asynchronous work to speak in terms of JavaScript types
    // and then resolve() with a function that is run -after- the yield,
    // which can do whatever work it needs to with the API to translate the
    // JavaScript types back into Rebol.

    uintptr_t r = EM_ASM_INT({
        return RL_Await($0)
    }, &atomic);

    REBVAL *result = cast(REBVAL*, r);
    return result;
}


//
// cleanup_js_native: C
//
// GC-able HANDLE! avoids leaking in the table mapping integers (pointers) to
// JavaScript functions.
//
void cleanup_js_native(const REBVAL *v) {
    uintptr_t id = cast(uintptr_t, VAL_HANDLE_POINTER(REBARR*, v));
    assert(id < UINT_MAX);

    EM_ASM_({
        RL_Unregister($0);
    }, id);
}


//
//  Make_JavaScript_Action_Common: C
//
// Shared code for creating JS-NATIVE and JS-PROMISE, since there's a fair
// bit in common.
//
REBACT *Make_JavaScript_Action_Common(
    const REBVAL *spec,
    const REBVAL *source,
    bool is_awaiter
){
    // !!! Should it optimize for empty source in the native case with
    // Voider_Dispatcher()?  Note that empty source for a promise will not
    // be fulfilled...

    REBACT *action = Make_Action(
        Make_Paramlist_Managed_May_Fail(spec, MKF_MASK_NONE),
        is_awaiter
            ? &JavaScript_Awaiter_Dispatcher
            : &JavaScript_Native_Dispatcher,
        nullptr, // no facade (use paramlist)
        nullptr, // no specialization exemplar (or inherited exemplar)
        IDX_NATIVE_MAX // details capacity [source module linkname tcc_state]
    );

    // When coding to the internal API, it's easy to make a mistake and call
    // into something that evaluates without having GC protection on array
    // elements when operating by index like this.  Pre-fill them.
    //
    REBARR *details = ACT_DETAILS(action);
    int idx;
    for (idx = 0; idx < IDX_NATIVE_MAX; ++idx)
        Init_Unreadable_Blank(ARR_AT(details, idx));
    TERM_ARRAY_LEN(details, IDX_NATIVE_MAX);

    if (Is_Series_Frozen(VAL_SERIES(source)))
        Move_Value(ARR_AT(details, IDX_NATIVE_SOURCE), source); // no copy
    else {
        Init_Text(
            ARR_AT(details, IDX_NATIVE_SOURCE),
            Copy_String_At_Len(source, -1) // might change
        );
    }

    // The generation of the function called by JavaScript.  It takes no
    // arguments, as giving it arguments would make calling it more complex
    // as well as introduce several issues regarding mapping legal Rebol
    // names to names for JavaScript parameters.  libRebol APIs must be used
    // to access the arguments out of the frame.

    DECLARE_MOLD (mo);
    Push_Mold(mo);

    Append_Unencoded(mo->series, "RL_Register(");

    REBYTE buf[60]; // !!! Why 60?  Copied from MF_Integer()
    REBINT len = Emit_Integer(buf, cast(uintptr_t, ACT_PARAMLIST(action)));
    Append_Unencoded_Len(mo->series, s_cast(buf), len);

    Append_Unencoded(mo->series, ", function() {\n"); // would add ID number
    Append_Unencoded(mo->series, "return function"); // !!!! async function?
    if (is_awaiter)
        Append_Unencoded(mo->series, "(resolve, reject)"); // implicit args
    else
        Append_Unencoded(mo->series, "()"); // only has rebArg(...)
    Append_Unencoded(mo->series, " {");

    // By not using `new function` we are able to make this an async function,
    // as well as avoid escaping of string literals.

    REBSIZ offset;
    REBSIZ size;
    REBSER *temp = Temp_UTF8_At_Managed(
        &offset,
        &size,
        source,
        VAL_LEN_AT(source)
    );
    Append_Utf8_Utf8(mo->series, cs_cast(BIN_AT(temp, offset)), size);

    Append_Unencoded(mo->series, "}\n"); // end `function() {`
    Append_Unencoded(mo->series, "}()"); // invoke dummy function
    Append_Unencoded(mo->series, ");"); // end `RL_Register(`

    TERM_SERIES(mo->series);

    emscripten_run_script(cs_cast(BIN_AT(mo->series, mo->start)));

    Drop_Mold(mo);

    // !!! Natives on the stack can specify where APIs like rebRun() should
    // look for bindings.  For the moment, set user natives to use the user
    // context...it could be a parameter of some kind (?)
    //
    Move_Value(
        ARR_AT(details, IDX_NATIVE_CONTEXT),
        Get_System(SYS_CONTEXTS, CTX_USER)
    );

    Init_Handle_Managed(
        ARR_AT(details, IDX_NATIVE_HANDLE),
        ACT_PARAMLIST(action),
        0,
        &cleanup_js_native
    );

    SET_VAL_FLAGS(ACT_ARCHETYPE(action), ACTION_FLAG_NATIVE);
    return action;
}


//
//  js-native: native/export [
//
//  {Create ACTION! from JavaScript code}
//
//      return: [action!]
//      spec [block!]
//          {Function specification (similar to the one used by FUNC)}
//      source [text!]
//          {JavaScript code as a text string}
//  ]
//
REBNATIVE(js_native)
{
    JAVASCRIPT_INCLUDE_PARAMS_OF_JS_NATIVE;

    REBACT *native = Make_JavaScript_Action_Common(
        ARG(spec),
        ARG(source),
        false // not is_awaiter
    );

    return Init_Action_Unbound(D_OUT, native);
}


//
//  js-awaiter: native/export [
//
//  {Create ACTION! from JavaScript code, won't return until resolve() called}
//
//      return: [action!]
//      spec [block!]
//          {Function specification (similar to the one used by FUNC)}
//      source [text!]
//          {JavaScript code as a text string}
//  ]
//
REBNATIVE(js_awaiter)
{
    JAVASCRIPT_INCLUDE_PARAMS_OF_JS_AWAITER;

    REBACT *awaiter = Make_JavaScript_Action_Common(
        ARG(spec),
        ARG(source),
        true // is_awaiter
    );

    return Init_Action_Unbound(D_OUT, awaiter);
}


#include "tmp-mod-javascript-last.h"
