/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  f-extension.c
**  Summary: support for extensions
**  Section: functional
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"

#include "reb-ext.h" // includes copy of ext-types.h
#include "reb-evtypes.h"

#include "reb-lib.h"

// Extension evaluation categories:
enum {
    RXX_NULL,   // unset
    RXX_PTR,    // any pointer
    RXX_LOGIC,  // logic
    RXX_32,     // char
    RXX_64,     // integer, decimal, etc.
    RXX_SYM,    // word
    RXX_SER,    // string
    RXX_IMAGE,  // image
    RXX_DATE,   // from upper section
    RXX_MAX
};

//(*call)(int cmd, RXIFRM *args);

typedef struct reb_ext {
    RXICAL call;                // Call(function) entry point
    void *dll;                  // DLL library "handle"
    int  index;                 // Index in extension table
    int  object;                // extension object reference
} REBEXT;

#include "tmp-exttypes.h"


// !!!! The list below should not be hardcoded, but until someone
// needs a lot of extensions, it will do fine.
REBEXT Ext_List[64];
REBCNT Ext_Next = 0;


/***********************************************************************
**
**  Local functions
**
***********************************************************************/

/***********************************************************************
**
x*/ RXIARG Value_To_RXI(const REBVAL *val)
/*
***********************************************************************/
{
    RXIARG arg;

    switch (RXT_Eval_Class[Reb_To_RXT[VAL_TYPE_0(val)]]) {
    case RXX_LOGIC:
        //
        // LOGIC! changed to just be a header bit, and there is no VAL_I32
        // in the "payload" any longer.  It must be proxied.
        //
        arg.i2.int32a = (VAL_LOGIC(val) == TRUE) ? 1 : 0;
        break;
    case RXX_64:
        arg.int64 = VAL_INT64(val);
        break;
    case RXX_SER:
        arg.sri.series = VAL_SERIES(val);
        arg.sri.index = VAL_INDEX(val);
        break;
    case RXX_PTR:
        arg.addr = VAL_HANDLE_DATA(val);
        break;
    case RXX_32:
        arg.i2.int32a = VAL_I32(val);
        arg.i2.int32b = 0;
        break;
    case RXX_DATE:
        arg.i2.int32a = VAL_ALL_BITS(val)[2];
        arg.i2.int32b = 0;
        break;
    case RXX_SYM:
        arg.i2.int32a = VAL_WORD_CANON(val);
        arg.i2.int32b = 0;
        break;
    case RXX_IMAGE:
        arg.iwh.image = VAL_SERIES(val);
        arg.iwh.width = VAL_IMAGE_WIDE(val);
        arg.iwh.height = VAL_IMAGE_HIGH(val);
        break;
    case RXX_NULL:
    default:
        arg.int64 = 0;
        break;
    }
    return arg;
}

/***********************************************************************
**
x*/ void RXI_To_Value(REBVAL *val, RXIARG arg, REBCNT type)
/*
***********************************************************************/
{
    // !!! Should use proper Val_Init routines
    //
    VAL_RESET_HEADER(val, RXT_To_Reb[type]);

    switch (RXT_Eval_Class[type]) {
    case RXX_LOGIC:
        //
        // In RXIARG a logic is "in the payload", but it's only a header bit
        // in an actual REBVAL.  Though it's not technically necessary to
        // constrain the accepted values to 0 and 1, it helps with the build
        // that is trying to catch mistakes in REBOOL usage...and also to
        // lock down the RXIARG interface a bit to make it easier to change.
        //
        assert((arg.i2.int32a == 0) || (arg.i2.int32a == 1));
        SET_LOGIC(val, arg.i2.int32a);
        break;
    case RXX_64:
        VAL_INT64(val) = arg.int64;
        break;
    case RXX_SER:
        VAL_SERIES(val) = cast(REBSER*, arg.sri.series);
        VAL_INDEX(val) = arg.sri.index;
        break;
    case RXX_PTR:
        VAL_HANDLE_DATA(val) = arg.addr;
        break;
    case RXX_32:
        VAL_I32(val) = arg.i2.int32a;
        break;
    case RXX_DATE:
        VAL_TIME(val) = NO_TIME;
        VAL_ALL_BITS(val)[2] = arg.i2.int32a;
        break;
    case RXX_SYM:
        Val_Init_Word(val, RXT_To_Reb[type], arg.i2.int32a);
        break;
    case RXX_IMAGE:
        VAL_SERIES(val) = cast(REBSER*, arg.iwh.image);
        VAL_IMAGE_WIDE(val) = arg.iwh.width;
        VAL_IMAGE_HIGH(val) = arg.iwh.height;
        break;
    case RXX_NULL:
        VAL_INT64(val) = 0;
        break;
    default:
        SET_NONE(val);
    }
}

/***********************************************************************
**
x*/ void RXI_To_Block(RXIFRM *frm, REBVAL *out) {
/*
***********************************************************************/
    REBCNT n;
    REBARR *array;
    REBVAL *val;
    REBCNT len;

    array = Make_Array(len = RXA_COUNT(frm));
    for (n = 1; n <= len; n++) {
        val = Alloc_Tail_Array(array);
        RXI_To_Value(val, frm->args[n], RXA_TYPE(frm, n));
    }
    Val_Init_Block(out, array);
}


/***********************************************************************
**
x*/ REBRXT Do_Callback(RXIARG *out, REBFUN *func, REBCNT label_sym, RXIARG *rxis)
/*
**      Given an object and a word id, call a REBOL function.
**      The arguments are converted from extension format directly
**      to the data stack. The result is passed back in ext format,
**      with the datatype returned or zero if there was a problem.
**
**      NOTE: This does not appear to be used by the Ren-C core.  It
**      had to be adapted for the unified prefetch evaluator.  The
**      method it uses is just to build an array to use in the call,
**      see notes on Redo_Func_Throws() for a similar implementation.
**
***********************************************************************/
{
    // Upper bound on the length of the args we might need for an
    // invocation is the total number of parameters (if it had no
    // refinements or locals).
    //
    REBARR *code_array = Make_Array(FUNC_NUM_PARAMS(func));
    REBVAL *code = ARR_HEAD(code_array);

    // The first element of our path will be the function, followed by its
    // refinements.  It has an upper bound on length that is to consider the
    // opposite case where it had only refinements and then the function
    // at the head...
    //
    REBARR *path_array = Make_Array(FUNC_NUM_PARAMS(func) + 1);
    REBVAL *path = ARR_HEAD(path_array);
    REBVAL first;

    // We'll walk through the original functions params, assuming that the
    // desire of the caller is based on position of the refinement in the
    // list (brittle, but that's how R3-Alpha did APPLY!)
    //
    REBVAL *param = FUNC_PARAMS_HEAD(func);
    REBOOL ignoring = FALSE;

    REBCNT n;

    REBIXO indexor;
    REBVAL result;
    VAL_INIT_WRITABLE_DEBUG(&result);

    *path = *FUNC_VALUE(func);
    ++path;

    for (n = 1; n <= RXI_COUNT(rxis); n++, param++) {
        REBVAL arg;
        RXI_To_Value(&arg, rxis[n], RXI_TYPE(rxis, n));

        if (GET_VAL_FLAG(param, TYPESET_FLAG_HIDDEN)) {
             //
             // Pure local... don't add a code arg for it (can't)!
             //
             continue;
        }

        if (GET_VAL_FLAG(param, TYPESET_FLAG_REFINEMENT)) {
            if (IS_CONDITIONAL_FALSE(&arg)) {
                //
                // If the refinement is not in use, do not add it and ignore
                // args until the next refinement.
                //
                ignoring = TRUE;
                continue;
            }

            // In use--and used refinements must be added to the PATH!
            //
            ignoring = FALSE;
            Val_Init_Word(path, REB_WORD, VAL_TYPESET_SYM(param));
            ++path;
            continue;
        }

        // Otherwise it should be a quoted or normal argument.  If ignoring
        // then pass on it, otherwise add the arg to the code as-is.
        //
        if (ignoring) continue;

        *code++ = arg;
    }

    SET_END(code);
    SET_ARRAY_LEN(code_array, code - ARR_HEAD(code_array));
    MANAGE_ARRAY(code_array);

    SET_END(path);
    SET_ARRAY_LEN(path_array, path - ARR_HEAD(path_array));
    Val_Init_Array(&first, REB_PATH, path_array); // manages

    // Invoke DO with the special mode requesting non-evaluation on all
    // args, as they were evaluated the first time around.
    //
    indexor = Do_Array_At_Core(
        &result,
        &first, // path not in array but will be "virtual" first array element
        code_array,
        0, // index
        DO_FLAG_TO_END | DO_FLAG_LOOKAHEAD | DO_FLAG_EVAL_ONLY
    );

    // Used to report a special error on bad args, using the limited checking
    // that was done when this function filled a call frame.  For now, we
    // assume that the ordinary line of failure will suffice.  If not, it
    // could be trapped.
    //
    /* SET_EXT_ERROR(out, RXE_BAD_ARGS); */

    if (indexor != THROWN_FLAG && indexor != END_FLAG) {
        //
        // We may not have stopped the invocation by virtue of the args
        // all not getting consumed, but we can raise an error now that it
        // did not.
        //
        assert(FALSE);
        fail (Error(RE_MISC));
    }

    if (indexor == THROWN_FLAG) {
        //
        // !!! Does this need handling such that there is a way for the thrown
        // value to "bubble up" out of the callback, or is an error sufficient?
        //
        fail (Error_No_Catch_For_Throw(&result));
    }

    // Return resulting value from output
    *out = Value_To_RXI(&result);
    return Reb_To_RXT[VAL_TYPE(&result)];
}


//
//  do-callback: native [
//  
//  "Internal function to process callback events."
//  
//      event [event!] "Callback event"
//  ]
//
REBNATIVE(do_callback)
//
// object word arg1 arg2
{
    RXICBI *cbi;
    REBVAL *event = D_ARG(1);
    REBCNT n;
    REBVAL *val;

    // Sanity checks:
    if (VAL_EVENT_TYPE(event) != EVT_CALLBACK)
        return R_NONE;
    if (!(cbi = cast(RXICBI*, VAL_EVENT_SER(event))))
        return R_NONE;

    // Find word in object, verify it is a function
    //
    val = Find_Word_Value(AS_CONTEXT(cbi->obj), cbi->word);
    if (!val) {
        SET_EXT_ERROR(&cbi->result, RXE_NO_WORD);
        return 0;
    }
    if (!ANY_FUNC(val)) {
        SET_EXT_ERROR(&cbi->result, RXE_NOT_FUNC);
        return 0;
    }

    n = Do_Callback(&cbi->result, VAL_FUNC(val), cbi->word, cbi->args);

    SET_FLAG(cbi->flags, RXC_DONE);

    if (!n) {
        REBVAL temp;
        VAL_INIT_WRITABLE_DEBUG(&temp);
        SET_INTEGER(&temp, GET_EXT_ERROR(&cbi->result));

        fail (Error(RE_INVALID_ARG, &temp));
    }

    RXI_To_Value(D_OUT, cbi->result, n);
    return R_OUT;
}


typedef REBYTE *(INFO_FUNC)(REBINT opts, void *lib);


//
//  load-extension: native [
//  
//  "Low level extension module loader (for DLLs)."
//  
//      name [file! binary!] "DLL file or UTF-8 source"
//      /dispatch {Specify native command dispatch (from hosted extensions)}
//      function [handle!] "Command dispatcher (native)"
//  ]
//
REBNATIVE(load_extension)
//
// Low level extension loader:
// 
// 1. Opens the DLL for the extension
// 2. Calls its Info() command to get its definition header (REBOL)
// 3. Inits an extension structure (dll, Call() function)
// 4. Creates a extension object and returns it
// 5. REBOL code then uses that object to define the extension module
//    including commands, functions, data, exports, etc.
// 
// Each extension is defined as DLL with:
// 
// init() - init anything needed
// quit() - cleanup anything needed
// call() - dispatch a native
{
    PARAM(1, name);
    REFINE(2, dispatch);
    PARAM(3, function);

    REBCHR *name;
    void *dll;
    REBCNT error;
    REBYTE *code;
    CFUNC *info; // INFO_FUNC
    REBCTX *context;
    REBVAL *val = D_ARG(1);
    REBEXT *ext;
    CFUNC *call; // RXICAL
    REBSER *src;
    int Remove_after_first_run;
    //Check_Security(SYM_EXTENSION, POL_EXEC, val);

    if (!REF(dispatch)) { // No /dispatch, use the DLL file:

        if (!IS_FILE(val)) fail (Error_Invalid_Arg(val));

        // !!! By passing NULL we don't get backing series to protect!
        name = Val_Str_To_OS_Managed(NULL, val);

        // Try to load the DLL file:
        if (!(dll = OS_OPEN_LIBRARY(name, &error))) {
            fail (Error(RE_NO_EXTENSION, val));
        }

        // Call its info() function for header and code body:
        if (!(info = OS_FIND_FUNCTION(dll, cs_cast(BOOT_STR(RS_EXTENSION, 0))))){
            OS_CLOSE_LIBRARY(dll);
            fail (Error(RE_BAD_EXTENSION, val));
        }

        // Obtain info string as UTF8:
        if (!(code = cast(INFO_FUNC*, info)(0, Extension_Lib()))) {
            OS_CLOSE_LIBRARY(dll);
            fail (Error(RE_EXTENSION_INIT, val));
        }

        // Import the string into REBOL-land:
        src = Copy_Bytes(code, -1); // Nursery protected
        call = OS_FIND_FUNCTION(dll, cs_cast(BOOT_STR(RS_EXTENSION, 2))); // zero is allowed
    }
    else {
        // Hosted extension:
        src = VAL_SERIES(val);
        call = VAL_HANDLE_CODE(D_ARG(3));
        dll = 0;
    }

    ext = &Ext_List[Ext_Next];
    CLEARS(ext);
    ext->call = cast(RXICAL, call);
    ext->dll = dll;
    ext->index = Ext_Next++;

    // Extension return: dll, info, filename
    context = Copy_Context_Shallow(
        VAL_CONTEXT(Get_System(SYS_STANDARD, STD_EXTENSION))
    );
    Val_Init_Object(D_OUT, context);

    // Set extension fields needed:
    val = CTX_VAR(context, STD_EXTENSION_LIB_BASE);
    VAL_RESET_HEADER(val, REB_HANDLE);
    VAL_I32(val) = ext->index;

    if (!D_REF(2))
        *CTX_VAR(context, STD_EXTENSION_LIB_FILE) = *D_ARG(1);

    Val_Init_Binary(CTX_VAR(context, STD_EXTENSION_LIB_BOOT), src);

    return R_OUT;
}


//
//  Make_Command: C
// 
// A REB_COMMAND is used to connect a Rebol function spec to implementation
// inside of a C DLL.  That implementation uses a set of APIs (RXIARG, etc.)
// which were developed prior to Rebol becoming open source.  It was
// intended that C developers could use an API that was a parallel subset
// of Rebol's internal code, that would be binary stable to survive any
// reorganizations.
// 
// `extension` is an object or module that represents the properties of the
// DLL or shared library (including its DLL handle, load or unload status,
// etc.)  `command-num` is a numbered function inside of that DLL, which
// (one hopes) has a binary interface able to serve the spec which was
// provided.  Though the same spec format is used as for ordinary functions
// in Rebol, the allowed datatypes are more limited...as not all Rebol types
// had a parallel interface under this conception.
// 
// Subsequent to the open-sourcing, the Ren/C initiative is not focusing on
// the REB_COMMAND model--preferring to connect the Rebol core directly as
// a library to bindings.  However, as it was the only extension model
// available under closed-source Rebol3, several pieces of code were built
// to depend upon it for functionality.  This included the cryptography
// extensions needed for secure sockets and a large part of the GUI.
// 
// Being able to quarantine the REB_COMMAND machinery to only builds that
// need it is a working objective.
//
void Make_Command(
    REBVAL *out,
    const REBVAL *spec,
    const REBVAL *extension,
    const REBVAL *command_num
) {
    if (!IS_MODULE(extension) && !IS_OBJECT(extension)) goto bad_func_def;

    // Check that handle and extension are somewhat valid (not used)
    {
        REBEXT *rebext;
        REBVAL *handle = VAL_CONTEXT_VAR(extension, SELFISH(1));
        if (!IS_HANDLE(handle)) goto bad_func_def;
        rebext = &Ext_List[VAL_I32(handle)];
        if (!rebext || !rebext->call) goto bad_func_def;
    }

    if (!IS_INTEGER(command_num) || VAL_INT64(command_num) > 0xffff)
        goto bad_func_def;

    if (!IS_BLOCK(spec)) goto bad_func_def;

    VAL_RESET_HEADER(out, REB_COMMAND); // clears exts and opts in header...

    // See notes in `Make_Function()` about why a copy is *required*.
    VAL_FUNC_SPEC(out) =
        Copy_Array_At_Deep_Managed(VAL_ARRAY(spec), VAL_INDEX(spec));

    out->payload.any_function.func
        = AS_FUNC(Make_Paramlist_Managed(VAL_FUNC_SPEC(spec), SYM_0));

    // There is no "body", but we want to save `extension` and `command_num`
    // and the only place there is to put it is in the place where a function
    // body series would go.  So make a 2 element series to store them and
    // copy the values into it.
    //
    VAL_FUNC_BODY(out) = Make_Array(2);
    Append_Value(VAL_FUNC_BODY(out), extension);
    Append_Value(VAL_FUNC_BODY(out), command_num);
    MANAGE_ARRAY(VAL_FUNC_BODY(out));

    // Put the command REBVAL in slot 0 so that REB_COMMAND, like other
    // function types, can find the function value from the paramlist.

    *FUNC_VALUE(out->payload.any_function.func) = *out;

    // Make sure the command doesn't use any types for which an "RXT" parallel
    // datatype (to a REB_XXX type) has not been published:
    {
        REBVAL *args = VAL_FUNC_PARAMS_HEAD(out);
        for (; NOT_END(args); args++) {
            if (
                (3 != ~VAL_TYPESET_BITS(args)) // not END and UNSET (no args)
                && (VAL_TYPESET_BITS(args) & ~RXT_ALLOWED_TYPES)
            ) {
                fail (Error(RE_BAD_FUNC_ARG, args));
            }
        }
    }

    return;

bad_func_def:
    {
        // emulate error before refactoring (improve if it's relevant...)
        REBVAL def;
        REBARR *array = Make_Array(3);
        Append_Value(array, spec);
        Append_Value(array, extension);
        Append_Value(array, command_num);
        Val_Init_Block(&def, array);

        fail (Error(RE_BAD_FUNC_DEF, &def));
    }
}


//
//  Do_Command_Core: C
// 
// Evaluates the arguments for a command function and creates
// a resulting stack frame (struct or object) for command processing.
// 
// A command value consists of:
//     args - same as other funcs
//     spec - same as other funcs
//     body - [ext-obj func-index]
//
void Do_Command_Core(struct Reb_Call *call_)
{
    // All of these were checked above on definition:
    REBVAL *val = ARR_HEAD(FUNC_BODY(D_FUNC));
    // Handler
    REBEXT *ext = &Ext_List[VAL_I32(VAL_CONTEXT_VAR(val, SELFISH(1)))];
    REBCNT cmd = cast(REBCNT, Int32(val + 1));

    REBCNT n;
    RXIFRM frm; // args stored here

    // Copy args to command frame (array of args):
    RXA_COUNT(&frm) = D_ARGC;
    if (D_ARGC > 7) fail (Error(RE_BAD_COMMAND));
    val = D_ARG(1);
    for (n = 1; n <= D_ARGC; n++, val++) {
        RXA_TYPE(&frm, n) = Reb_To_RXT[VAL_TYPE_0(val)];
        frm.args[n] = Value_To_RXI(val);
    }

    // Call the command:
    n = ext->call(cmd, &frm, 0);

    assert(!THROWN(D_OUT));

    switch (n) {
    case RXR_VALUE:
        RXI_To_Value(D_OUT, frm.args[1], RXA_TYPE(&frm, 1));
        break;
    case RXR_BLOCK:
        RXI_To_Block(&frm, D_OUT);
        break;
    case RXR_UNSET:
        SET_UNSET(D_OUT);
        break;
    case RXR_NONE:
        SET_NONE(D_OUT);
        break;
    case RXR_TRUE:
        SET_TRUE(D_OUT);
        break;
    case RXR_FALSE:
        SET_FALSE(D_OUT);
        break;

    case RXR_BAD_ARGS:
        fail (Error(RE_BAD_CMD_ARGS));

    case RXR_NO_COMMAND:
        fail (Error(RE_NO_CMD));

    case RXR_ERROR:
        fail (Error(RE_COMMAND_FAIL));

    default:
        SET_UNSET(D_OUT);
    }

    // Note: no current interface for Rebol "commands" to throw (to the extent
    // that REB_COMMAND has a future in Ren-C).  If it could throw, then
    // this would set `c->mode = CALL_MODE_THROW_PENDING` in that case.
}


//
//  Do_Commands: C
// 
// Evaluate a block of commands as efficiently as possible.
// The arguments to each command must already be reduced or
// use only variable lookup.
// 
// Returns the last evaluated value, if provided.
//
void Do_Commands(REBVAL *out, REBARR *cmds, void *context)
{
    REBVAL *blk;
    REBCNT index = 0;
    REBVAL *set_word = 0;
    REBCNT cmd_sym = SYM_COMMAND_TYPE; // !!! to avoid uninitialized use, fix!
    REBVAL *args;
    REBVAL *val;
    const REBVAL *func; // !!! Why is this called 'func'?  What is this?
    RXIFRM frm; // args stored here
    REBCNT n;
    REBEXT *ext;
    REBCEC *ctx = cast(REBCEC*, context);

    REBVAL save;
    VAL_INIT_WRITABLE_DEBUG(&save);

    if (ctx) ctx->block = cmds;
    blk = ARR_HEAD(cmds);

    while (NOT_END(blk)) {

        // var: command result
        if (IS_SET_WORD(blk)) {
            set_word = blk++;
            index++;
        };

        // get command function
        if (IS_WORD(blk))
            func = GET_OPT_VAR_MAY_FAIL(blk);
        else
            func = blk;

        if (!IS_COMMAND(func)) {
            REBVAL commandx_word;
            VAL_INIT_WRITABLE_DEBUG(&commandx_word);
            Val_Init_Word(
                &commandx_word, REB_WORD, SYM_FROM_KIND(REB_COMMAND)
            );
            fail (Error(RE_EXPECT_VAL, &commandx_word, blk));
        }

        // Advance to next value
        blk++;
        if (ctx) ctx->index = index; // position of function
        index++;

        // get command arguments and body
        RXA_COUNT(&frm) = VAL_FUNC_NUM_PARAMS(func);

        // collect each argument (arg list already validated on MAKE)
        n = 0;
        for (args = VAL_FUNC_PARAMS_HEAD(func); NOT_END(args); args++) {

            //Debug_Type(args);
            val = blk++;
            index++;
            if (IS_END(val)) fail (Error_No_Arg(cmd_sym, args));
            //Debug_Type(val);

            // actual arg is a word, lookup?
            if (VAL_TYPE(val) >= REB_WORD) {
                if (IS_WORD(val)) {
                    // !!! The "mutable" is probably not necessary here
                    // However, this code is not written for val to be const
                    if (IS_WORD(args)) val = GET_MUTABLE_VAR_MAY_FAIL(val);
                }
                else if (IS_PATH(val)) {
                    if (IS_WORD(args)) {
                        if (Do_Path_Throws(&save, NULL, val, NULL))
                            fail (Error_No_Catch_For_Throw(&save));
                        val = &save;
                    }
                }
                else if (IS_GROUP(val)) {
                    if (DO_ARRAY_THROWS(&save, val)) {
                        // !!! Should this GROUP! evaluation be able to "bubble
                        // up" so that returns and throws can be caught up
                        // the stack, or is raising an error here sufficient?

                        fail (Error_No_Catch_For_Throw(&save));
                    }
                    val = &save;
                }
                // all others fall through
            }

            // check datatype
            if (!TYPE_CHECK(args, VAL_TYPE(val)))
                fail (Error_Arg_Type(cmd_sym, args, Type_Of(val)));

            // put arg into command frame
            n++;
            RXA_TYPE(&frm, n) = Reb_To_RXT[VAL_TYPE_0(val)];
            frm.args[n] = Value_To_RXI(val);
        }

        // Call the command (also supports different extension modules):
        func  = ARR_HEAD(VAL_FUNC_BODY(func));
        n = (REBCNT)VAL_INT64(func + 1);
        ext = &Ext_List[VAL_I32(VAL_CONTEXT_VAR(func, SELFISH(1)))]; // Handler
        n = ext->call(n, &frm, ctx);
        val = out;
        switch (n) {
        case RXR_VALUE:
            RXI_To_Value(val, frm.args[1], RXA_TYPE(&frm, 1));
            break;
        case RXR_BLOCK:
            RXI_To_Block(&frm, val);
            break;
        case RXR_UNSET:
            SET_UNSET(val);
            break;
        case RXR_NONE:
            SET_NONE(val);
            break;
        case RXR_TRUE:
            SET_TRUE(val);
            break;
        case RXR_FALSE:
            SET_FALSE(val);
            break;

        case RXR_ERROR:
            fail (Error(RE_COMMAND_FAIL));

        default:
            SET_UNSET(val);
        }

        if (set_word) {
            *GET_MUTABLE_VAR_MAY_FAIL(set_word) = *val;
            set_word = 0;
        }
    }
}


//
//  do-commands: native [
//  
//  {Evaluate a block of extension module command functions (special evaluation rules.)}
//  
//      commands [block!] "Series of commands and their arguments"
//  ]
//
REBNATIVE(do_commands)
{
    REBCEC ctx;

    ctx.envr = 0;
    ctx.block = VAL_ARRAY(D_ARG(1));
    ctx.index = 0;
    Do_Commands(D_OUT, ctx.block, &ctx);

    return R_OUT;
}
