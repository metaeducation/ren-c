//
//  File: %f-extension.c
//  Summary: "support for extensions"
//  Section: functional
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2016 Rebol Open Source Contributors
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
// NOTE: The R3-Alpha extension mechanism and API are deprecated in Ren-C.
//
// See %reb-ext.h for a general overview of R3-Alpha extensions.  Also:
//
// http://www.rebol.com/r3/docs/concepts/extensions-embedded.html
//

#include "sys-core.h"

#include "reb-ext.h" // includes copy of ext-types.h
#include "reb-evtypes.h"

#include "reb-lib.h"


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


void Value_To_RXI(RXIARG *arg, const REBVAL *val)
{
    // Note: This function is imported by %a-lib.c, keep prototype in sync

    switch (VAL_TYPE(val)) {
    case REB_LOGIC:
        arg->i2.int32a = (VAL_LOGIC(val) == TRUE) ? 1 : 0;
        break;

    case REB_INTEGER:
        arg->int64 = VAL_INT64(val);
        break;

    case REB_DECIMAL:
    case REB_PERCENT:
        arg->dec64 = VAL_DECIMAL(val);
        break;

    case REB_PAIR:
        arg->pair.x = VAL_PAIR_X(val);
        arg->pair.y = VAL_PAIR_Y(val);
        break;

    case REB_CHAR:
        arg->i2.int32a = VAL_CHAR(val);
        break;

    case REB_TUPLE:
        //
        // !!! Bad, this is pointing directly at data in a value!  (RXI is
        // not going to be developed further in Ren-C... #wontfix)
        //
        arg->addr = cast(void*, VAL_TUPLE_DATA(m_cast(REBVAL*, val)));
        break;

    case REB_TIME:
        arg->int64 = VAL_TIME(val);
        break;

    case REB_DATE: /* FIXME: avoid type punning */
        arg->i2.int32a = VAL_ALL_BITS(val)[2];
        arg->i2.int32b = 0;
        break;

    case REB_WORD:
    case REB_SET_WORD:
    case REB_GET_WORD:
    case REB_LIT_WORD:
    case REB_REFINEMENT:
    case REB_ISSUE:
        arg->sri.series = VAL_WORD_CANON(val);
        arg->sri.index = 0;
        break;

    case REB_STRING:
    case REB_FILE:
    case REB_EMAIL:
    case REB_URL:
    case REB_TAG:
    case REB_BLOCK:
    case REB_GROUP:
    case REB_PATH:
    case REB_SET_PATH:
    case REB_GET_PATH:
    case REB_LIT_PATH:
    case REB_BINARY:
    case REB_BITSET:
    case REB_VECTOR:
        arg->sri.series = VAL_SERIES(val);
        arg->sri.index = VAL_INDEX(val);
        break;

    case REB_GOB:
        arg->addr = VAL_GOB(val);
        break;

    case REB_HANDLE:
        arg->addr = VAL_HANDLE_DATA(val);
        break;

    case REB_IMAGE:
        arg->iwh.image = VAL_SERIES(val);
        arg->iwh.width = VAL_IMAGE_WIDE(val);
        arg->iwh.height = VAL_IMAGE_HIGH(val);
        break;

    case REB_OBJECT:
    case REB_MODULE:
        arg->addr = VAL_CONTEXT(val);
        break;

    default:
        arg->int64 = 0;
        break;
    }
    return;
}


void RXI_To_Value(REBVAL *val, const RXIARG *arg, REBRXT type)
{
    // Note: This function is imported by %a-lib.c, keep prototype in sync

    switch (type) {
    case RXT_0:
        SET_VOID(val);
        break;

    case RXT_BLANK:
        SET_BLANK(val);
        break;

    case RXT_LOGIC:
        assert((arg->i2.int32a == 0) || (arg->i2.int32a == 1));
        SET_LOGIC(val, LOGICAL(arg->i2.int32a));
        break;

    case RXT_INTEGER:
        SET_INTEGER(val, arg->int64);
        break;

    case RXT_DECIMAL:
        SET_DECIMAL(val, arg->dec64);
        break;

    case RXT_PERCENT:
        SET_PERCENT(val, arg->dec64);
        break;

    case RXT_CHAR:
        SET_CHAR(val, arg->i2.int32a);
        break;

    case RXT_PAIR:
        SET_PAIR(val, arg->pair.x, arg->pair.y);
        break;

    case RXT_TUPLE:
        SET_TUPLE(val, arg->addr);
        break;

    case RXT_TIME:
        SET_TIME(val, arg->int64);
        break;

    case RXT_DATE: // FIXME: see Value_To_RXI
        VAL_RESET_HEADER(val, REB_DATE);
        VAL_ALL_BITS(val)[2] = arg->i2.int32a;
        break;

    case RXT_WORD:
        Val_Init_Word(val, REB_WORD, arg->sri.series);
        break;

    case RXT_SET_WORD:
        Val_Init_Word(val, REB_SET_WORD, arg->sri.series);
        break;

    case RXT_GET_WORD:
        Val_Init_Word(val, REB_GET_WORD, arg->sri.series);
        break;

    case RXT_LIT_WORD:
        Val_Init_Word(val, REB_LIT_WORD, arg->sri.series);
        break;

    case RXT_REFINEMENT:
        Val_Init_Word(val, REB_REFINEMENT, arg->sri.series);
        break;

    case RXT_ISSUE:
        Val_Init_Word(val, REB_ISSUE, arg->sri.series);
        break;

    case RXT_BINARY:
        VAL_RESET_HEADER(val, REB_BINARY);
        goto ser;

    case RXT_STRING:
        VAL_RESET_HEADER(val, REB_STRING);
        goto ser;

    case RXT_FILE:
        VAL_RESET_HEADER(val, REB_FILE);
        goto ser;

    case RXT_EMAIL:
        VAL_RESET_HEADER(val, REB_EMAIL);
        goto ser;

    case RXT_URL:
        VAL_RESET_HEADER(val, REB_URL);
        goto ser;

    case RXT_TAG:
        VAL_RESET_HEADER(val, REB_TAG);
        goto ser;

    case RXT_BLOCK:
        VAL_RESET_HEADER(val, REB_BLOCK);
        goto ser;

    case RXT_GROUP:
        VAL_RESET_HEADER(val, REB_GROUP);
        goto ser;

    case RXT_PATH:
        VAL_RESET_HEADER(val, REB_PATH);
        goto ser;

    case RXT_SET_PATH:
        VAL_RESET_HEADER(val, REB_SET_PATH);
        goto ser;

    case RXT_GET_PATH:
        VAL_RESET_HEADER(val, REB_GET_PATH);
        goto ser;

    case RXT_LIT_PATH:
        VAL_RESET_HEADER(val, REB_LIT_PATH);
        goto ser;

    case RXT_BITSET:
        VAL_RESET_HEADER(val, REB_BITSET);
        goto ser;

    case RXT_VECTOR:
        VAL_RESET_HEADER(val, REB_VECTOR);
        goto ser;

    case RXT_GOB:
        SET_GOB(val, cast(REBGOB*, arg->addr));
        break;

    case RXT_HANDLE:
        SET_HANDLE_DATA(val, arg->addr);
        break;

    case RXT_IMAGE:
        INIT_VAL_SERIES(val, arg->iwh.image);
        VAL_IMAGE_WIDE(val) = arg->iwh.width;
        VAL_IMAGE_HIGH(val) = arg->iwh.height;
        break;

    case RXT_OBJECT:
        Val_Init_Object(val, cast(REBCTX*, arg->addr));
        break;

    default:
        fail(Error(RE_BAD_CMD_ARGS));
    }

    return;

ser:
    INIT_VAL_SERIES(val, arg->sri.series);
    VAL_INDEX(val) = arg->sri.index;
}


void RXI_To_Block(RXIFRM *rxifrm, REBVAL *out)
{
    // Note: This function is imported by %a-lib.c, keep prototype in sync

    REBCNT n;
    REBARR *array;
    REBVAL *val;
    REBCNT len;

    array = Make_Array(len = RXA_COUNT(rxifrm));
    for (n = 1; n <= len; n++) {
        val = Alloc_Tail_Array(array);
        RXI_To_Value(val, &rxifrm->rxiargs[n], RXA_TYPE(rxifrm, n));
    }
    Val_Init_Block(out, array);
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
        info = OS_FIND_FUNCTION(dll, cs_cast(BOOT_STR(RS_EXTENSION, 0)));
        if (!info){
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
        call = OS_FIND_FUNCTION(
            dll, cs_cast(BOOT_STR(RS_EXTENSION, 2))
        ); // zero is allowed
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
    SET_HANDLE_NUMBER(val, ext->index);

    if (!D_REF(2))
        *CTX_VAR(context, STD_EXTENSION_LIB_FILE) = *D_ARG(1);

    Val_Init_Binary(CTX_VAR(context, STD_EXTENSION_LIB_BOOT), src);

    return R_OUT;
}


//
//  Make_Command: C
// 
// `extension` is an object or module that represents the properties of the
// DLL or shared library (including its DLL handle, load or unload status,
// etc.)  `command-num` is a numbered function inside of that DLL, which
// (one hopes) has a binary interface able to serve the spec which was
// provided.  Though the same spec format is used as for ordinary functions
// in Rebol, the allowed datatypes are more limited...as not all Rebol types
// had a parallel interface under this conception.
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
        rebext = &Ext_List[VAL_HANDLE_NUMBER(handle)];
        if (!rebext || !rebext->call) goto bad_func_def;
    }

    if (!IS_INTEGER(command_num) || VAL_INT64(command_num) > 0xffff)
        goto bad_func_def;

    if (!IS_BLOCK(spec)) goto bad_func_def;

    REBFUN *fun; // goto would cross initialization
    fun = Make_Function(
        Make_Paramlist_Managed_May_Fail(spec, MKF_KEYWORDS),
        &Command_Dispatcher,
        NULL // no underlying function, fundamental
    );

    // There is no "code" for a body, but there is information that tells the
    // Command_Dispatcher what to do: `extension` and `command_num`.  This
    // two element array is placed in the FUNC_BODY value.
    //
    REBARR *body_array; // goto would cross initialization
    body_array = Make_Array(2);
    Append_Value(body_array, extension);
    Append_Value(body_array, command_num);

    Val_Init_Block(FUNC_BODY(fun), body_array); // manages

    *out = *FUNC_VALUE(fun);
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
//  make-command: native [
//
//  {Native for creating the FUNCTION! subclass for what was once COMMAND!}
//
//      return: [function!]
//      def [block!]
//  ]
//
REBNATIVE(make_command)
{
    PARAM(1, def);

    REBVAL *def = ARG(def);

    REBVAL spec;
    REBVAL extension;
    REBVAL command_num;

    if (VAL_LEN_AT(def) != 3)
        fail (Error_Invalid_Arg(def));

    COPY_VALUE(&spec, VAL_ARRAY_AT(def), VAL_SPECIFIER(def));
    COPY_VALUE(&extension, VAL_ARRAY_AT(def) + 1, VAL_SPECIFIER(def));
    COPY_VALUE(&command_num, VAL_ARRAY_AT(def) + 2, VAL_SPECIFIER(def));

    // Validity checking on the 3 elements done inside Make_Command, will
    // fail() if the input is not good.
    //
    Make_Command(D_OUT, &spec, &extension, &command_num);

    return R_OUT;
}


//
//  Command_Dispatcher: C
//
// Because it cannot interact with REBVALs directly, a COMMAND! must have
// the Reb_Frame's REBVAL[] array proxied into an array of RXIARGs inside
// of an RXIFRM.  The arguments are indexed starting at 1, and there is a
// a fixed maximum number of arguments (7 in R3-Alpha).  By convention, the
// 1st argument slot in also serves as the slot for a return result.  There
// is also an option by which the argument slots may be reused to effectively
// return a BLOCK! of up to length 7.
//
// No guarantees are made about the lifetime of series inside of RXIARGs,
// and they are not protected from the garbage collector.
//
// !!! The very ad-hoc nature of the R3-Alpha extension API has made it a
// legacy-maintenance-only area.  See notes in %reb-ext.h.
//
REB_R Command_Dispatcher(REBFRM *f)
{
    // For a "body", a command has a data array with [ext-obj func-index]
    // See Make_Command() for an explanation of these two values.
    //
    REBVAL *data = KNOWN(VAL_ARRAY_HEAD(FUNC_BODY(f->func)));
    REBEXT *handler = &Ext_List[
        VAL_HANDLE_NUMBER(VAL_CONTEXT_VAR(data, SELFISH(1)))
    ];
    REBCNT cmd_num = cast(REBCNT, Int32(data + 1));

    REBCNT n;
    RXIFRM rxifrm;

    if (FRM_NUM_ARGS(f) >= RXIFRM_MAX_ARGS) // fixed # of rxiargs in struct :-/
        fail (Error(RE_BAD_COMMAND));

    // Proxy array of REBVAL to array of RXIARG inside of the RXIFRM.
    //
    RXA_COUNT(&rxifrm) = FRM_NUM_ARGS(f); // count is put into rxiargs[0] cell
    f->param = FUNC_PARAMS_HEAD(f->underlying);
    f->arg = f->args_head;
    n = 1; // values start at the rxiargs[1] cell, arbitrary max at rxifrm[7]
    for (; NOT_END(f->param); ++f->param, ++f->arg, ++n) {
        if (IS_VOID(f->arg))
            RXA_TYPE(&rxifrm, n) = 0;
        else {
            RXA_TYPE(&rxifrm, n) = Reb_To_RXT[VAL_TYPE(f->arg)];
            Value_To_RXI(&rxifrm.rxiargs[n], f->arg);
        }
    }

    SET_VOID(f->out); // !!! "commands" seriously predated the END marker trick

    n = handler->call(
        cmd_num, &rxifrm, cast(REBCEC*, TG_Command_Execution_Context)
    );

    assert(!THROWN(f->out));

    switch (n) {
    case RXR_VALUE:
        //
        // !!! By convention, it appears that returning RXR_VALUE means to
        // consider the RXIARG in the [1] slot to be the return value.
        //
        RXI_To_Value(f->out, &rxifrm.rxiargs[1], RXA_TYPE(&rxifrm, 1));
        break;

    case RXR_BLOCK:
        //
        // !!! It seems that one can return a block of up to 7 items, by
        // setting the count of the frame and overwriting the arguments.
        // Very strange interface.  :-/
        //
        RXI_To_Block(&rxifrm, f->out);
        break;

    case RXR_VOID:
        SET_VOID(f->out);
        break;

    case RXR_BLANK:
        SET_BLANK(f->out);
        break;

    case RXR_TRUE:
        SET_TRUE(f->out);
        break;

    case RXR_FALSE:
        SET_FALSE(f->out);
        break;

    case RXR_BAD_ARGS:
        fail (Error(RE_BAD_CMD_ARGS));

    case RXR_NO_COMMAND:
        fail (Error(RE_NO_CMD));

    case RXR_ERROR:
        fail (Error(RE_COMMAND_FAIL));

    default:
        SET_VOID(f->out);
    }

    // Note: no current interface for Rebol "commands" to throw (to the extent
    // that REB_COMMAND has a future in Ren-C).

    return R_OUT;
}
