//
//  File: %n-native.c
//  Summary: {Implementation of "user natives" using an embedded C compiler}
//  Section: natives
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016 Rebol Open Source Contributors
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
// A user native is a FUNCTION! whose body is not a Rebol block, but a textual
// string of C code.  It is compiled on the fly by an embedded C compiler
// which is linked in with those Rebol builds supporting user natives:
//
// http://bellard.org/tcc
//
// Once the user native is compiled, it works exactly the same as the built-in
// natives.  However, the user can change the implementations without
// rebuilding the interpreter itself.  This makes it easier to just implement
// part of a Rebol script in C for better performance.
//
// The preprocessed internal header file %sys-core.h will be inserted into
// user source code, which makes all internal functions / macros available.
// However, to use C runtime functions such as memcpy() etc, the library
// libtcc1.a must be included.  This library must be available in addition
// to the interpreter executable.
//
// External libraries can also be used if proper 'library-path' and
// 'library' are specified.
//

#include "sys-core.h"

#if defined(WITH_TCC)
//
// libtcc provides the following functions:
//
// https://github.com/metaeducation/tcc/blob/mob/libtcc.h
//
// For a very simple example of usage of libtcc, see:
//
// https://github.com/metaeducation/tcc/blob/mob/tests/libtcc_test.c
//
#include "libtcc.h"

extern const REBYTE core_header_source[];

struct rebol_sym_func_t {
    const char *name;
    CFUNC *func;
};

struct rebol_sym_data_t {
    const char *name;
    void *data;
};

extern const struct rebol_sym_func_t rebol_sym_funcs[];
extern const struct rebol_sym_data_t rebol_sym_data[];
extern
#ifdef __cplusplus
"C"
#endif
const void *r3_libtcc1_symbols[];

#define CHAR_HEAD(x) cs_cast(BIN_HEAD(x))


static void tcc_error_report(void *ignored, const char *msg)
{
    UNUSED(ignored);

    DECLARE_LOCAL (err);
    REBSER *ser = Make_Binary(strlen(msg) + 2);
    Append_Series(ser, cb_cast(msg), strlen(msg));
    Init_String(err, ser);
    fail (Error_Tcc_Error_Warn_Raw(err));
}


static int do_add_path(
    TCCState *state,
    const RELVAL *path,
    int (*add)(TCCState *, const char *)
) {
    if (!VAL_BYTE_SIZE(path))
        return -1;

    int ret;
    if (IS_FILE(path)) {
        REBSER *lp = Value_To_Local_Path(KNOWN(m_cast(RELVAL*,path)), TRUE);
        REBSER *bin = Make_UTF8_Binary(
            UNI_HEAD(lp), SER_LEN(lp), 2, OPT_ENC_UNISRC
        );
        Free_Series(lp);
        assert(SER_WIDE(bin) == 1);
        ret = add(state, CHAR_HEAD(bin));
        Free_Series(bin);
    }
    else {
        assert(IS_STRING(path));
        ret = add(state, CHAR_HEAD(VAL_SERIES(path)));
    }
    return ret;
}


static void do_set_path(
    TCCState *state,
    const RELVAL *path,
    void (*set)(TCCState *, const char *)
) {
    if (!VAL_BYTE_SIZE(path))
        return;

    if (IS_FILE(path)) {
        REBSER *lp = Value_To_Local_Path(KNOWN(m_cast(RELVAL*, path)), TRUE);
        REBSER *bin = Make_UTF8_Binary(
            UNI_HEAD(lp), SER_LEN(lp), 2, OPT_ENC_UNISRC
        );
        Free_Series(lp);
        assert(SER_WIDE(bin) == 1);
        set(state, CHAR_HEAD(bin));
        Free_Series(bin);
    }
    else {
        assert(IS_STRING(path));
        set(state, CHAR_HEAD(VAL_SERIES(path)));
    }
}


static REBCTX* add_path(
    TCCState *state,
    const RELVAL *path,
    int (*add)(TCCState *, const char *),
    enum REBOL_Errors err_code
) {
    if (path) {
        if (IS_FILE(path) || IS_STRING(path)) {
            if (do_add_path(state, path, add) < 0)
                return Error(err_code, path);
        }
        else {
            assert(IS_BLOCK(path));

            RELVAL *item;
            for (item = VAL_ARRAY_AT(path); NOT_END(item); ++item) {
                if (!IS_FILE(item) && !IS_STRING(item))
                    return Error(err_code, item);

                if (do_add_path(state, item, add) < 0)
                    return Error(err_code, item);
            }
        }
    }

    return NULL;
}


static void cleanup(const REBVAL *val)
{
    TCCState *state = VAL_HANDLE_POINTER(TCCState, val);
    assert(state != NULL);
    tcc_delete(state);
}


//
//  Pending_Native_Dispatcher: C
//
// The MAKE-NATIVE command doesn't actually compile the function directly.
// Instead the source code is held onto, so that several user natives can
// be compiled together by COMPILE.
//
// However, as a convenience, calling a pending user native will trigger a
// simple COMPILE for just that one function, using default options.
//
REB_R Pending_Native_Dispatcher(REBFRM *f) {
    REBARR *array = Make_Array(1);
    Append_Value(array, FUNC_VALUE(f->phase));

    DECLARE_LOCAL (natives);
    Init_Block(natives, array);

    assert(FUNC_DISPATCHER(f->phase) == &Pending_Native_Dispatcher);

    if (Do_Va_Throws(f->out, NAT_VALUE(compile), &natives, END))
        return R_OUT_IS_THROWN;

    // Today's COMPILE doesn't return a result on success (just fails on
    // errors), but if it changes to return one consider what to do with it.
    //
    assert(IS_VOID(f->out));

    // Now that it's compiled, it should have replaced the dispatcher with a
    // function pointer that lives in the TCC_State.  Use REDO, and don't
    // bother re-checking the argument types.
    //
    assert(FUNC_DISPATCHER(f->phase) != &Pending_Native_Dispatcher);
    return R_REDO_UNCHECKED;
}

#endif


//
//  make-native: native [
//
//  {Create a "user native" function compiled from C source}
//
//      return: [function!]
//          "Function value, will be compiled on demand or by COMPILE"
//      spec [block!]
//          "The spec of the native"
//      source [string!]
//          "C source of the native implementation"
//      /linkname
//          "Provide a specific linker name"
//      name [string!]
//          "Legal C identifier (default will be auto-generated)"
//  ]
//
REBNATIVE(make_native)
{
    INCLUDE_PARAMS_OF_MAKE_NATIVE;

#if !defined(WITH_TCC)
    UNUSED(ARG(spec));
    UNUSED(ARG(source));
    UNUSED(REF(linkname));
    UNUSED(ARG(name));

    fail (Error_Not_Tcc_Build_Raw());
#else
    REBVAL *source = ARG(source);

    if (VAL_LEN_AT(source) == 0)
        fail (Error_Tcc_Empty_Source_Raw());

    REBFUN *fun = Make_Function(
        Make_Paramlist_Managed_May_Fail(ARG(spec), MKF_NONE),
        &Pending_Native_Dispatcher, // will be replaced e.g. by COMPILE
        NULL, // no facade (use paramlist)
        NULL // no specialization exemplar (or inherited exemplar)
    );

    REBARR *info = Make_Array(3); // [source name tcc_state]

    if (Is_Series_Frozen(VAL_SERIES(source)))
        Append_Value(info, source); // no need to copy it...
    else {
        // have to copy it (might change before COMPILE is called)
        //
        Init_String(
            Alloc_Tail_Array(info),
            Copy_String_Slimming(
                VAL_SERIES(source),
                VAL_INDEX(source),
                VAL_LEN_AT(source)
            )
        );
    }

    if (REF(linkname)) {
        REBVAL *name = ARG(name);

        if (Is_Series_Frozen(VAL_SERIES(name)))
            Append_Value(info, name);
        else {
            Init_String(
                Alloc_Tail_Array(info),
                Copy_String_Slimming(
                    VAL_SERIES(name),
                    VAL_INDEX(name),
                    VAL_LEN_AT(name)
                )
            );
        }
    }
    else {
        // Auto-generate a linker name based on the numeric value of the
        // function pointer.  Just "N_" followed by the hexadecimal value.
        // So 2 chars per byte, plus 2 for "N_", and account for the
        // terminator (even though it's set implicitly).

        REBCNT len = 2 + sizeof(REBFUN*) * 2;
        REBSER *bin = Make_Binary(len + 1);
        const char *src = cast(const char*, &fun);
        REBYTE *dest = BIN_HEAD(bin);

        *dest ='N';
        ++dest;
        *dest = '_';
        ++dest;

        REBCNT n = 0;
        while (n < sizeof(REBFUN*)) {
            Form_Hex2(dest, *src); // terminates each time
            ++src;
            dest += 2;
            ++n;
        }
        TERM_BIN_LEN(bin, len);

        Init_String(Alloc_Tail_Array(info), bin);
    }

    Init_Blank(Alloc_Tail_Array(info)); // no TCC_State, yet...

    Init_Block(FUNC_BODY(fun), info);

    // We need to remember this is a user native, because we won't over the
    // long run be able to tell it is when the dispatcher is replaced with an
    // arbitrary compiled function pointer!
    //
    SET_VAL_FLAG(FUNC_VALUE(fun), FUNC_FLAG_USER_NATIVE);

    Move_Value(D_OUT, FUNC_VALUE(fun));
    return R_OUT;
#endif
}


//
//  compile: native [
//
//  {Compiles one or more native functions at the same time, with options.}
//
//      return: [<opt>]
//      natives [block!]
//          {Functions from MAKE-NATIVE or STRING!s of code.}
//      /options
//      flags [block!]
//      {
//          The block supports the following dialect:
//          include [block! path!]
//              "include path"
//          define [block!]
//              {define preprocessor symbols as "VAR=VAL" or "VAR"}
//          debug
//              "Add debugging information to the generated code?"
//      }
//  ]
//
REBNATIVE(compile)
{
    INCLUDE_PARAMS_OF_COMPILE;

#if !defined(WITH_TCC)
    UNUSED(ARG(natives));
    UNUSED(REF(options));
    UNUSED(ARG(flags));

    fail (Error_Not_Tcc_Build_Raw());
#else
    REBVAL *natives = ARG(natives);

    REBOOL debug = FALSE; // !!! not implemented yet

    if (VAL_LEN_AT(ARG(natives)) == 0)
        fail (Error_Tcc_Empty_Spec_Raw());

    RELVAL *inc = NULL;
    RELVAL *lib = NULL;
    RELVAL *libdir = NULL;
    RELVAL *options = NULL;
    RELVAL *rundir = NULL;

    if (REF(options)) {
        RELVAL *val = VAL_ARRAY_AT(ARG(flags));

        for (; NOT_END(val); ++val) {
            if (!IS_WORD(val))
                fail (Error_Tcc_Expect_Word_Raw(val));

            switch (VAL_WORD_SYM(val)) {
            case SYM_INCLUDE:
                ++val;
                if (!(IS_BLOCK(val) || IS_FILE(val) || ANY_STRING(val)))
                    fail (Error_Tcc_Invalid_Include_Raw(val));
                inc = val;
                break;

            case SYM_DEBUG:
                debug = TRUE;
                break;

            case SYM_OPTIONS:
                ++val;
                if (!ANY_STRING(val) || !VAL_BYTE_SIZE(val))
                    fail (Error_Tcc_Invalid_Options_Raw(val));
                options = val;
                break;

            case SYM_RUNTIME_PATH:
                ++val;
                if (!(IS_FILE(val) || IS_STRING(val)))
                    fail (Error_Tcc_Invalid_Library_Path_Raw(val));
                rundir = val;
                break;

            case SYM_LIBRARY_PATH:
                ++val;
                if (!(IS_BLOCK(val) || IS_FILE(val) || ANY_STRING(val)))
                    fail (Error_Tcc_Invalid_Library_Path_Raw(val));
                libdir = val;
                break;

            case SYM_LIBRARY:
                ++val;
                if (!(IS_BLOCK(val) || IS_FILE(val) || ANY_STRING(val)))
                    fail (Error_Tcc_Invalid_Library_Raw(val));
                lib = val;
                break;

            default:
                fail (Error_Tcc_Not_Supported_Opt_Raw(val));
            }
        }
    }

    if (debug)
        fail ("Debug builds of user natives are not yet implemented.");

    // Using the "hot" mold buffer allows us to build the combined source in
    // memory that is generally preallocated.  This makes it not necessary
    // to say in advance how large the buffer needs to be.  However, currently
    // the mold buffer is REBUNI wide characters, while TCC expects ASCII.
    // Hence it has to be "popped" as UTF8 into a fresh series.
    //
    // !!! Future plans are to use "UTF-8 Everywhere", which would mean the
    // mold buffer's data could be used directly.
    //
    // !!! Investigate how much UTF-8 support there is in TCC for strings/etc
    //
    DECLARE_MOLD (mo);
    Push_Mold(mo);

    // The core_header_source is %sys-core.h with all include files expanded
    //
    Append_Unencoded(mo->series, cs_cast(core_header_source));

    // This prolog resets the line number count to 0 where the user source
    // starts, in order to give more meaningful line numbers in errors
    //
    Append_Unencoded(mo->series, "\n# 0 \"user-source\" 1\n");

    REBDSP dsp_orig = DSP;

    // The user code is added next
    //
    RELVAL *item;
    for (item = VAL_ARRAY_AT(natives); NOT_END(item); ++item) {
        const RELVAL *var = item;
        if (IS_WORD(item) || IS_GET_WORD(item)) {
            var = Get_Opt_Var_May_Fail(item, VAL_SPECIFIER(natives));
            if (IS_VOID(var))
                fail (Error_No_Value_Core(item, VAL_SPECIFIER(natives)));
        }

        if (IS_FUNCTION(var)) {
            assert(GET_VAL_FLAG(var, FUNC_FLAG_USER_NATIVE));

            // Remember this function, because we're going to need to come
            // back and fill in its dispatcher and TCC_State after the
            // compilation...
            //
            DS_PUSH(const_KNOWN(var));

            RELVAL *info = VAL_FUNC_BODY(var);
            RELVAL *source = VAL_ARRAY_AT_HEAD(info, 0);
            RELVAL *name = VAL_ARRAY_AT_HEAD(info, 1);

            Append_Unencoded(mo->series, "REB_R ");
            Append_String(
                mo->series,
                VAL_SERIES(name),
                VAL_INDEX(name),
                VAL_LEN_AT(name)
            );
            Append_Unencoded(mo->series, "(REBFRM *frame_)\n{\n");

            REBVAL *param = VAL_FUNC_PARAMS_HEAD(var);
            REBCNT num = 1;
            for (; NOT_END(param); ++param) {
                REBSTR *spelling = VAL_PARAM_SPELLING(param);

                enum Reb_Param_Class pclass = VAL_PARAM_CLASS(param);
                switch (pclass) {
                case PARAM_CLASS_LOCAL:
                case PARAM_CLASS_RETURN:
                case PARAM_CLASS_LEAVE:
                    assert(FALSE); // natives shouldn't generally use these...
                    break;

                case PARAM_CLASS_REFINEMENT:
                case PARAM_CLASS_NORMAL:
                case PARAM_CLASS_SOFT_QUOTE:
                case PARAM_CLASS_HARD_QUOTE:
                    Append_Unencoded(mo->series, "    ");
                    if (pclass == PARAM_CLASS_REFINEMENT)
                        Append_Unencoded(mo->series, "REFINE(");
                    else
                        Append_Unencoded(mo->series, "PARAM(");
                    Append_Int(mo->series, num);
                    ++num;
                    Append_Unencoded(mo->series, ", ");
                    Append_Unencoded(mo->series, cs_cast(STR_HEAD(spelling)));
                    Append_Unencoded(mo->series, ");\n");
                    break;

                default:
                    assert(FALSE);
                }
            }
            if (num != 1)
                Append_Unencoded(mo->series, "\n");

            Append_String(
                mo->series,
                VAL_SERIES(source),
                VAL_INDEX(source),
                VAL_LEN_AT(source)
            );
            Append_Unencoded(mo->series, "\n}\n\n");
        }
        else if (IS_STRING(var)) {
            //
            // A string is treated as just a fragment of code.  This allows
            // for writing things like C functions or macros that are shared
            // between multiple user natives.
            //
            Append_String(
                mo->series,
                VAL_SERIES(var),
                VAL_INDEX(var),
                VAL_LEN_AT(var)
            );
            Append_Unencoded(mo->series, "\n");
        }
        else {
            assert(FALSE);
        }
    }

    REBSER *combined_src = Pop_Molded_UTF8(mo);

    TCCState *state = tcc_new();
    if (!state)
        fail (Error_Tcc_Construction_Raw());

    tcc_set_error_func(state, NULL, tcc_error_report);

    if (options) {
        tcc_set_options(state, CHAR_HEAD(VAL_SERIES(options)));
    }

    REBCTX *err = NULL;

    if ((err = add_path(state, inc, tcc_add_include_path, RE_TCC_INCLUDE)))
        fail (err);

    if (tcc_set_output_type(state, TCC_OUTPUT_MEMORY) < 0)
        fail (Error_Tcc_Output_Type_Raw());

    if (tcc_compile_string(state, CHAR_HEAD(combined_src)) < 0)
        fail (Error_Tcc_Compile_Raw(natives));

    Free_Series(combined_src);

    // It is technically possible for ELF binaries to "--export-dynamic" (or
    // -rdynamic in CMake) and make executables embed symbols for functions
    // in them "like a DLL".  However, we would like to make API symbols for
    // Rebol available to the dynamically loaded code on all platforms, so
    // this uses `tcc_add_symbol()` to work the same way on Windows/Linux/OSX
    //
    const struct rebol_sym_data_t *sym_data = &rebol_sym_data[0];
    for (; sym_data->name != NULL; sym_data ++) {
        if (tcc_add_symbol(state, sym_data->name, sym_data->data) < 0)
            fail (Error_Tcc_Relocate_Raw());
    }

    const struct rebol_sym_func_t *sym_func = &rebol_sym_funcs[0];
    for (; sym_func->name != NULL; sym_func ++) {
        // ISO C++ forbids casting between pointer-to-function and
        // pointer-to-object, use memcpy to circumvent.
        void *ptr;
        assert(sizeof(ptr) == sizeof(sym_func->func));
        memcpy(&ptr, &sym_func->func, sizeof(ptr));
        if (tcc_add_symbol(state, sym_func->name, ptr) < 0)
            fail (Error_Tcc_Relocate_Raw());
    }

    // Add symbols in libtcc1, to avoid bundling with libtcc1.a
    const void **sym = &r3_libtcc1_symbols[0];
    for (; *sym != NULL; sym += 2) {
        if (tcc_add_symbol(state, cast(const char*, *sym), *(sym + 1)) < 0)
            fail (Error_Tcc_Relocate_Raw());
    }

    if ((err = add_path(
        state, libdir, tcc_add_library_path, RE_TCC_LIBRARY_PATH
    ))) {
        fail (err);
    }

    if ((err = add_path(state, lib, tcc_add_library, RE_TCC_LIBRARY)))
        fail(err);

    if (rundir)
        do_set_path(state, rundir, tcc_set_lib_path);

    if (tcc_relocate(state, TCC_RELOCATE_AUTO) < 0)
        fail (Error_Tcc_Relocate_Raw());

    DECLARE_LOCAL (handle);
    Init_Handle_Managed(
        handle,
        state, // "data" pointer
        0,
        cleanup // called upon GC
    );

    // With compilation complete, find the matching linker names and get
    // their function pointers to substitute in for the dispatcher.
    //
    while (DSP != dsp_orig) {
        REBVAL *var = DS_TOP;

        assert(IS_FUNCTION(var));
        assert(GET_VAL_FLAG(var, FUNC_FLAG_USER_NATIVE));

        RELVAL *info = VAL_FUNC_BODY(var);
        RELVAL *name = VAL_ARRAY_AT_HEAD(info, 1);
        RELVAL *stored_state = VAL_ARRAY_AT_HEAD(info, 2);

        REBCNT index;
        REBSER *utf8 = Temp_Bin_Str_Managed(name, &index, 0);

        void *sym = tcc_get_symbol(state, cs_cast(BIN_AT(utf8, index)));
        if (sym == NULL)
            fail (Error_Tcc_Sym_Not_Found_Raw(name));

        // ISO C++ forbids casting between pointer-to-function and
        // pointer-to-object, use memcpy to circumvent.
        REBNAT c_func;
        assert(sizeof(c_func) == sizeof(void*));
        memcpy(&c_func, &sym, sizeof(c_func));

        FUNC_DISPATCHER(VAL_FUNC(var)) = c_func;
        Move_Value(stored_state, handle);

        DS_DROP;
    }

    return R_VOID;
#endif
}
