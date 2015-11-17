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
    RXX_32,     // logic
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

    switch (RXT_Eval_Class[Reb_To_RXT[VAL_TYPE(val)]]) {
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
    VAL_SET(val, RXT_To_Reb[type]);
    switch (RXT_Eval_Class[type]) {
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
        VAL_WORD_SYM(val) = arg.i2.int32a;
        VAL_WORD_FRAME(val) = 0;
        VAL_WORD_INDEX(val) = 0;
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
    REBSER *blk;
    REBVAL *val;
    REBCNT len;

    blk = Make_Array(len = RXA_COUNT(frm));
    for (n = 1; n <= len; n++) {
        val = Alloc_Tail_Array(blk);
        RXI_To_Value(val, frm->args[n], RXA_TYPE(frm, n));
    }
    Val_Init_Block(out, blk);
}


/***********************************************************************
**
x*/ REBRXT Do_Callback(REBSER *obj, u32 name, RXIARG *rxis, RXIARG *result)
/*
**      Given an object and a word id, call a REBOL function.
**      The arguments are converted from extension format directly
**      to the data stack. The result is passed back in ext format,
**      with the datatype returned or zero if there was a problem.
**
***********************************************************************/
{
    REBVAL *val;
    struct Reb_Call *call;
    REBCNT len;
    REBCNT n;
    REBVAL label;
    REBVAL out;

    // Find word in object, verify it is a function.
    if (!(val = Find_Word_Value(obj, name))) {
        SET_EXT_ERROR(result, RXE_NO_WORD);
        return 0;
    }
    if (!ANY_FUNC(val)) {
        SET_EXT_ERROR(result, RXE_NOT_FUNC);
        return 0;
    }

    // Create stack frame (use prior stack frame for location info):
    SET_TRASH_SAFE(&out); // OUT slot for function eval result
    Val_Init_Word_Unbound(&label, REB_WORD, name);
    call = Make_Call(
        &out,
        VAL_SERIES(DSF_WHERE(PRIOR_DSF(DSF))),
        VAL_INDEX(DSF_WHERE(PRIOR_DSF(DSF))),
        &label,
        val
    );
    obj = VAL_FUNC_PARAMLIST(val);  // func words
    len = SERIES_TAIL(obj)-1;   // number of args (may include locals)

    // Push args. Too short or too long arg frames are handled W/O error.
    // Note that refinements args can be set to anything.
    for (n = 1; n <= len; n++) {
        REBVAL *arg = DSF_ARG(call, n);

        if (n <= RXI_COUNT(rxis))
            RXI_To_Value(arg, rxis[n], RXI_TYPE(rxis, n));
        else
            SET_NONE(arg);

        // Check type for word at the given offset:
        if (!TYPE_CHECK(BLK_SKIP(obj, n), VAL_TYPE(arg))) {
            result->i2.int32b = n;
            SET_EXT_ERROR(result, RXE_BAD_ARGS);
            Free_Call(call);
            return 0;
        }
    }

    // Evaluate the function:
    if (Dispatch_Call_Throws(call)) {
        // !!! Does this need handling such that there is a way for the thrown
        // value to "bubble up" out of the callback, or is an error sufficient?
        fail (Error_No_Catch_For_Throw(DSF_OUT(call)));
    }

    // Return resulting value from output
    *result = Value_To_RXI(&out);
    return Reb_To_RXT[VAL_TYPE(&out)];
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

    // Sanity checks:
    if (VAL_EVENT_TYPE(event) != EVT_CALLBACK)
        return R_NONE;
    if (!(cbi = cast(RXICBI*, VAL_EVENT_SER(event))))
        return R_NONE;

    n = Do_Callback(cbi->obj, cbi->word, cbi->args, &(cbi->result));

    SET_FLAG(cbi->flags, RXC_DONE);

    if (!n) {
        REBVAL temp;
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
    REBCHR *name;
    void *dll;
    REBCNT error;
    REBYTE *code;
    CFUNC *info; // INFO_FUNC
    REBSER *obj;
    REBVAL *val = D_ARG(1);
    REBEXT *ext;
    CFUNC *call; // RXICAL
    REBSER *src;
    int Remove_after_first_run;
    //Check_Security(SYM_EXTENSION, POL_EXEC, val);

    if (!D_REF(2)) { // No /dispatch, use the DLL file:

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
    obj = VAL_OBJ_FRAME(Get_System(SYS_STANDARD, STD_EXTENSION));
    obj = Copy_Array_Shallow(obj);

    // Shallow copy means we reuse STD_EXTENSION's word list, which is
    // already managed.  We manage our copy to match.
    MANAGE_SERIES(obj);
    Val_Init_Object(D_OUT, obj);

    // Set extension fields needed:
    val = FRM_VALUE(obj, STD_EXTENSION_LIB_BASE);
    VAL_SET(val, REB_HANDLE);
    VAL_I32(val) = ext->index;
    if (!D_REF(2)) *FRM_VALUE(obj, STD_EXTENSION_LIB_FILE) = *D_ARG(1);
    Val_Init_Binary(FRM_VALUE(obj, STD_EXTENSION_LIB_BOOT), src);

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
void Make_Command(REBVAL *out, const REBVAL *spec, const REBVAL *extension, const REBVAL *command_num)
{
    if (!IS_MODULE(extension) && !IS_OBJECT(extension)) goto bad_func_def;

    // Check that handle and extension are somewhat valid (not used)
    {
        REBEXT *rebext;
        REBVAL *handle = VAL_OBJ_VALUE(extension, 1);
        if (!IS_HANDLE(handle)) goto bad_func_def;
        rebext = &Ext_List[VAL_I32(handle)];
        if (!rebext || !rebext->call) goto bad_func_def;
    }

    if (!IS_INTEGER(command_num) || VAL_INT64(command_num) > 0xffff)
        goto bad_func_def;

    if (!IS_BLOCK(spec)) goto bad_func_def;

    // See notes in `Make_Function()` about why a copy is *required*.
    VAL_FUNC_SPEC(out) =
        Copy_Array_At_Deep_Managed(VAL_SERIES(spec), VAL_INDEX(spec));

    VAL_FUNC_PARAMLIST(out) = Check_Func_Spec(VAL_FUNC_SPEC(spec));

    // Make sure the command doesn't use any types for which an "RXT" parallel
    // datatype (to a REB_XXX type) has not been published:
    {
        REBVAL *args = BLK_HEAD(VAL_FUNC_PARAMLIST(out)) + 1; // skip SELF
        for (; NOT_END(args); args++) {
            if (
                (3 != ~VAL_TYPESET_BITS(args)) // not END and UNSET (no args)
                && (VAL_TYPESET_BITS(args) & ~RXT_ALLOWED_TYPES)
            ) {
                fail (Error(RE_BAD_FUNC_ARG, args));
            }
        }
    }

    // There is no "body", but we want to save `extension` and `command_num`
    // and the only place there is to put it is in the place where a function
    // body series would go.  So make a 2 element series to store them and
    // copy the values into it.
    //
    VAL_FUNC_BODY(out) = Make_Array(2);
    Append_Value(VAL_FUNC_BODY(out), extension);
    Append_Value(VAL_FUNC_BODY(out), command_num);
    MANAGE_SERIES(VAL_FUNC_BODY(out));

    VAL_SET(out, REB_COMMAND); // clears exts and opts in header...

    // Put the command REBVAL in slot 0 so that REB_COMMAND, like other
    // function types, can find the function value from the paramlist.

    *BLK_HEAD(VAL_FUNC_PARAMLIST(out)) = *out;

    return;

bad_func_def:
    {
        // emulate error before refactoring (improve if it's relevant...)
        REBVAL def;
        REBSER *series = Make_Array(3);
        Append_Value(series, spec);
        Append_Value(series, extension);
        Append_Value(series, command_num);
        Val_Init_Block(&def, series);

        fail (Error(RE_BAD_FUNC_DEF, &def));
    }
}


//
//  Do_Command_Throws: C
// 
// Evaluates the arguments for a command function and creates
// a resulting stack frame (struct or object) for command processing.
// 
// A command value consists of:
//     args - same as other funcs
//     spec - same as other funcs
//     body - [ext-obj func-index]
//
REBFLG Do_Command_Throws(const REBVAL *value)
{
    // All of these were checked above on definition:
    REBVAL *val = BLK_HEAD(VAL_FUNC_BODY(value));
    REBEXT *ext = &Ext_List[VAL_I32(VAL_OBJ_VALUE(val, 1))]; // Handler
    REBCNT cmd = cast(REBCNT, Int32(val + 1));
    REBCNT argc = SERIES_TAIL(VAL_FUNC_PARAMLIST(value)) - 1; // not self

    REBCNT n;
    RXIFRM frm; // args stored here

    // Copy args to command frame (array of args):
    RXA_COUNT(&frm) = argc;
    if (argc > 7) fail (Error(RE_BAD_COMMAND));
    val = DSF_ARG(DSF, 1);
    for (n = 1; n <= argc; n++, val++) {
        RXA_TYPE(&frm, n) = Reb_To_RXT[VAL_TYPE(val)];
        frm.args[n] = Value_To_RXI(val);
    }

    // Call the command:
    n = ext->call(cmd, &frm, 0);
    val = DSF_OUT(DSF);

    assert(!THROWN(val));

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

    case RXR_BAD_ARGS:
        fail (Error(RE_BAD_CMD_ARGS));

    case RXR_NO_COMMAND:
        fail (Error(RE_NO_CMD));

    case RXR_ERROR:
        fail (Error(RE_COMMAND_FAIL));

    default:
        SET_UNSET(val);
    }

    return FALSE; // There is currently no interface for commands to "throw"
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
void Do_Commands(REBVAL *out, REBSER *cmds, void *context)
{
    REBVAL *blk;
    REBCNT index = 0;
    REBVAL *set_word = 0;
    REBVAL *cmd_word;
    REBSER *words;
    REBVAL *args;
    REBVAL *val;
    const REBVAL *func; // !!! Why is this called 'func'?  What is this?
    RXIFRM frm; // args stored here
    REBCNT n;
    REBEXT *ext;
    REBCEC *ctx = cast(REBCEC*, context);
    REBVAL save;

    if (ctx) ctx->block = cmds;
    blk = BLK_HEAD(cmds);

    while (NOT_END(blk)) {

        // var: command result
        if IS_SET_WORD(blk) {
            set_word = blk++;
            index++;
        };

        // get command function
        if (IS_WORD(cmd_word = blk)) {
            // Optimized var fetch:
            n = VAL_WORD_INDEX(blk);
            if (n > 0) func = FRM_VALUES(VAL_WORD_FRAME(blk)) + n;
            else func = GET_VAR(blk); // fallback
        } else func = blk;

        if (!IS_COMMAND(func)) {
            REBVAL commandx_word;
            Val_Init_Word_Unbound(
                &commandx_word, REB_WORD, SYM_FROM_KIND(REB_COMMAND)
            );
            fail (Error(RE_EXPECT_VAL, &commandx_word, blk));
        }

        // Advance to next value
        blk++;
        if (ctx) ctx->index = index; // position of function
        index++;

        // get command arguments and body
        words = VAL_FUNC_PARAMLIST(func);
        RXA_COUNT(&frm) = SERIES_TAIL(VAL_FUNC_PARAMLIST(func)) - 1; // no self

        // collect each argument (arg list already validated on MAKE)
        n = 0;
        for (args = BLK_SKIP(words, 1); NOT_END(args); args++) {

            //Debug_Type(args);
            val = blk++;
            index++;
            if (IS_END(val)) fail (Error_No_Arg(cmd_word, args));
            //Debug_Type(val);

            // actual arg is a word, lookup?
            if (VAL_TYPE(val) >= REB_WORD) {
                if (IS_WORD(val)) {
                    // !!! The "mutable" is probably not necessary here
                    // However, this code is not written for val to be const
                    if (IS_WORD(args)) val = GET_MUTABLE_VAR(val);
                }
                else if (IS_PATH(val)) {
                    const REBVAL *path = val;
                    if (IS_WORD(args)) {
                        if (Do_Path(&save, &path, 0)) {
                            // !!! comment said "found a function"
                        } else {
                            val = &save;
                        }
                    }
                }
                else if (IS_PAREN(val)) {
                    if (DO_ARRAY_THROWS(&save, val)) {
                        // !!! Should this paren evaluation be able to "bubble
                        // up" so that returns and throws can be caught up
                        // the stack, or is raising an error here sufficient?

                        fail (Error_No_Catch_For_Throw(&save));
                    }
                    val = &save;
                }
                // all others fall through
            }

            // check datatype
            if (!TYPE_CHECK(args, VAL_TYPE(val))) {
                REBVAL arg_word;
                Val_Init_Word_Unbound(
                    &arg_word, REB_WORD, VAL_TYPESET_SYM(args)
                );
                fail (Error(RE_EXPECT_ARG, cmd_word, &arg_word, Type_Of(val)));
            }

            // put arg into command frame
            n++;
            RXA_TYPE(&frm, n) = Reb_To_RXT[VAL_TYPE(val)];
            frm.args[n] = Value_To_RXI(val);
        }

        // Call the command (also supports different extension modules):
        func  = BLK_HEAD(VAL_FUNC_BODY(func));
        n = (REBCNT)VAL_INT64(func + 1);
        ext = &Ext_List[VAL_I32(VAL_OBJ_VALUE(func, 1))]; // Handler
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
            Set_Var(set_word, val);
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
    ctx.block = VAL_SERIES(D_ARG(1));
    ctx.index = 0;
    Do_Commands(D_OUT, ctx.block, &ctx);

    return R_OUT;
}
