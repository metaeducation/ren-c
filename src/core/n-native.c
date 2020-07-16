//
//  File: %n-native.c
//  Summary: {Implementation of "user natives" using an embedded C compiler}
//  Section: natives
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016-2018 Rebol Open Source Contributors
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
// A user native is an ACTION! whose body is not a Rebol block, but a textual
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

// for the ACT_DETAILS() array of a user native

#define IDX_TCC_NATIVE_LINKNAME \
    IDX_NATIVE_MAX // generated if the native doesn't specify

#define IDX_TCC_NATIVE_STATE \
    IDX_TCC_NATIVE_LINKNAME + 1 // will be a BLANK! until COMPILE happens

#define IDX_TCC_NATIVE_MAX \
    (IDX_TCC_NATIVE_STATE + 1)


// COMPILE replaces &Pending_Native_Dispatcher that user natives start with,
// so the dispatcher alone can't be usd to detect them.  ACTION_FLAG_XXX are
// in too short of a supply to give them their own flag.  Other natives put
// their source in ACT_DETAILS [0] and their context in ACT_DETAILS [1], so
// for the moment just assume if the source is text it's a user native.
//
bool Is_User_Native(const RELVAL *action) {
    if (NOT_VAL_FLAG(action, ACTION_FLAG_NATIVE))
        return false;

    REBARR *details = ACT_DETAILS(VAL_ACTION(action));
    assert(ARR_LEN(details) >= 2); // ACTION_FLAG_NATIVE needs source+context
    return IS_TEXT(ARR_AT(details, IDX_NATIVE_BODY));
}


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

struct rebol_sym_cfunc_t {
    const char *name;
    CFUNC *cfunc;
};

struct rebol_sym_data_t {
    const char *name;
    void *data;
};

extern const struct rebol_sym_cfunc_t rebol_sym_cfuncs[];
extern const struct rebol_sym_data_t rebol_sym_data[];
extern
#ifdef __cplusplus
"C"
#endif
const void *r3_libtcc1_symbols[];


static void tcc_error_report(void *opaque, const char *msg_utf8)
{
    // When `tcc_set_error_func()` is called, you can pass it a value that
    // it will pass back.  We pass EMPTY_BLOCK to test it (and explain it).
    // Note that since the compilation can be delayed after MAKE-NATIVE exits,
    // pointers to local variables should not be used here.
    //
    assert(cast(REBVAL*, opaque) == EMPTY_BLOCK);
    UNUSED(opaque);

    DECLARE_LOCAL (msg);
    Init_Text(msg, Make_String_UTF8(msg_utf8));
    fail (Error_Tcc_Error_Warn_Raw(msg));
}


static int do_add_path(
    TCCState *state,
    const RELVAL *path,
    int (*add)(TCCState *, const char *)
){
    // Converts FILE! to a local path, or assumes TEXT! is already local
    //
    char *local_utf8 = rebSpell(
        "file-to-local/pass/full ensure [file! text!]", KNOWN(path),
        rebEND
    );
    int ret = add(state, local_utf8);
    rebFree(local_utf8);
    return ret;
}


static void do_set_path(
    TCCState *state,
    const RELVAL *path,
    void (*set)(TCCState *, const char *)
){
    // Converts FILE! to a local path, or assumes TEXT! is already local
    //
    char *local_utf8 = rebSpell(
        "file-to-local/pass/full ensure [file! text!]", KNOWN(path),
        rebEND
    );
    set(state, local_utf8);
    rebFree(local_utf8);
}


static REBCTX* add_path(
    TCCState *state,
    const RELVAL *path,
    int (*add)(TCCState *, const char *),
    REBSYM err_id_sym
) {
    if (path) {
        if (IS_FILE(path) or IS_TEXT(path)) {
            if (do_add_path(state, path, add) < 0)
                return Error(SYM_TCC, err_id_sym, path);
        }
        else {
            assert(IS_BLOCK(path));

            RELVAL *item;
            for (item = VAL_ARRAY_AT(path); NOT_END(item); ++item) {
                if (not IS_FILE(item) and not IS_TEXT(item))
                    return Error(SYM_TCC, err_id_sym, item);

                if (do_add_path(state, item, add) < 0)
                    return Error(SYM_TCC, err_id_sym, item);
            }
        }
    }

    return nullptr;
}


static void cleanup(const REBVAL *val)
{
    TCCState *state = VAL_HANDLE_POINTER(TCCState, val);
    assert(state != nullptr);
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
    REBACT *phase = FRM_PHASE(f);
    assert(ACT_DISPATCHER(phase) == &Pending_Native_Dispatcher);

    REBVAL *action = ACT_ARCHETYPE(phase); // this action's value

    // !!! With this as an extension and with binding advancements, this
    // should be able to use the string "compile" and trust it to bind to the
    // extension module's COMPILE.
    //
    // Today's COMPILE doesn't return a result on success (just fails on
    // errors), but if it changes to return one consider what to do with it.
    //
    rebElide(rebEval(NAT_VALUE(compile)), "[", action, "]", rebEND);

    // Now that it's compiled, it should have replaced the dispatcher with a
    // function pointer that lives in the TCC_State.  Use REDO, and don't
    // bother re-checking the argument types.
    //
    assert(ACT_DISPATCHER(phase) != &Pending_Native_Dispatcher);
    return R_REDO_UNCHECKED;
}

#endif


//
//  make-native: native [
//
//  {Create an ACTION! which is compiled from a C source STRING!}
//
//      return: [action!]
//          "Function value, will be compiled on demand or by COMPILE"
//      spec [block!]
//          "The spec of the native"
//      source [text!]
//          "C source of the native implementation"
//      /linkname
//          "Provide a specific linker name"
//      name [text!]
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

    REBACT *native = Make_Action(
        Make_Paramlist_Managed_May_Fail(ARG(spec), MKF_MASK_NONE),
        &Pending_Native_Dispatcher, // will be replaced e.g. by COMPILE
        nullptr, // no facade (use paramlist)
        nullptr, // no specialization exemplar (or inherited exemplar)
        IDX_TCC_NATIVE_MAX // details len [source module linkname tcc_state]
    );

    REBARR *details = ACT_DETAILS(native);

    if (Is_Series_Frozen(VAL_SERIES(source)))
        Move_Value(ARR_AT(details, IDX_NATIVE_BODY), source); // no copy
    else {
        Init_Text(
            ARR_AT(details, IDX_NATIVE_BODY),
            Copy_String_At_Len(source, -1) // might change before COMPILE call
        );
    }

    // !!! Natives on the stack can specify where APIs like rebValue() should
    // look for bindings.  For the moment, set user natives to use the user
    // context...it could be a parameter of some kind (?)
    //
    Move_Value(
        ARR_AT(details, IDX_NATIVE_CONTEXT),
        Get_System(SYS_CONTEXTS, CTX_USER)
    );

    if (REF(linkname)) {
        REBVAL *name = ARG(name);

        if (Is_Series_Frozen(VAL_SERIES(name)))
            Move_Value(ARR_AT(details, IDX_TCC_NATIVE_LINKNAME), name);
        else {
            Init_Text(
                ARR_AT(details, IDX_TCC_NATIVE_LINKNAME),
                Copy_String_At_Len(name, -1)
            );
        }
    }
    else {
        // Auto-generate a linker name based on the numeric value of the
        // paramlist pointer.  Just "N_" followed by the hexadecimal value.
        // So 2 chars per byte, plus 2 for "N_", and account for the
        // terminator (even though it's set implicitly).
        //
        // Note: This repeats some work in ENBASE.

        unsigned int len = 2 + (sizeof(REBACT*) * 2);
        REBSER *ser = Make_Unicode(len);
        REBARR *paramlist = ACT_PARAMLIST(native); // unique for this action!
        const char *src = cast(const char*, &paramlist);
        REBUNI *dest = UNI_HEAD(ser);

        *dest ='N';
        ++dest;
        *dest = '_';
        ++dest;

        unsigned int n = 0;
        while (n < sizeof(REBACT*)) {
            Form_Hex2_Uni(dest, *src); // terminates each time
            ++src;
            dest += 2;
            ++n;
        }
        TERM_UNI_LEN(ser, len);

        Init_Text(ARR_AT(details, IDX_TCC_NATIVE_LINKNAME), ser);
    }

    Init_Blank(ARR_AT(details, IDX_TCC_NATIVE_STATE)); // no TCC_State, yet...

    SET_VAL_FLAGS(ACT_ARCHETYPE(native), ACTION_FLAG_NATIVE);
    return Init_Action_Unbound(D_OUT, native);
  #endif
}


//
//  compile: native [
//
//  {Compiles one or more native functions at the same time, with options.}
//
//      return: [<opt> text!]
//          {No return value, unless /INSPECT is used to get the processed C}
//      natives [block!]
//          {Functions from MAKE-NATIVE or STRING!s of code.}
//      /options
//      flags [block!]
//      {
//          The block supports the following dialect:
//          include [block! path!]
//              "include path"
//          debug
//              "Add debugging information to the generated code?"
//          options [any-string!]
//          runtime-path [file! text!]
//          library-path [block! file! text!]
//          library [block! file! text!]
//      }
//      /inspect {Return the C source code as text, but don't compile it}
//  ]
//
REBNATIVE(compile)
{
    INCLUDE_PARAMS_OF_COMPILE;

  #if !defined(WITH_TCC)
    UNUSED(ARG(natives));
    UNUSED(REF(options));
    UNUSED(ARG(flags));
    UNUSED(ARG(inspect));

    fail (Error_Not_Tcc_Build_Raw());
  #else
    REBVAL *natives = ARG(natives);

    bool debug = false; // !!! not implemented yet

    if (VAL_LEN_AT(ARG(natives)) == 0)
        fail (Error_Tcc_Empty_Spec_Raw());

    RELVAL *inc = nullptr;
    RELVAL *lib = nullptr;
    RELVAL *libdir = nullptr;
    RELVAL *options = nullptr;
    RELVAL *rundir = nullptr;

    REBSPC *specifier = VAL_SPECIFIER(ARG(flags));

    if (REF(options)) {
        RELVAL *val = VAL_ARRAY_AT(ARG(flags));

        for (; NOT_END(val); ++val) {
            if (not IS_WORD(val)) {
                DECLARE_LOCAL (non_word);
                Derelativize(non_word, val, specifier);
                fail (Error_Tcc_Expect_Word_Raw(non_word));
            }

            switch (VAL_WORD_SYM(val)) {
            case SYM_INCLUDE:
                ++val;
                if (not (IS_BLOCK(val) or IS_FILE(val) or ANY_STRING(val))) {
                    DECLARE_LOCAL (include);
                    Derelativize(include, val, specifier);
                    fail (Error_Tcc_Invalid_Include_Raw(include));
                }
                inc = val;
                break;

            case SYM_DEBUG:
                debug = true;
                break;

            case SYM_OPTIONS:
                ++val;
                if (not IS_TEXT(val)) {
                    DECLARE_LOCAL (option);
                    Derelativize(option, val, specifier);
                    fail (Error_Tcc_Invalid_Options_Raw(option));
                }
                options = val;
                break;

            case SYM_RUNTIME_PATH:
                ++val;
                if (not (IS_FILE(val) or IS_TEXT(val))) {
                    DECLARE_LOCAL (path);
                    Derelativize(path, val, specifier);
                    fail (Error_Tcc_Invalid_Library_Path_Raw(path));
                }
                rundir = val;
                break;

            case SYM_LIBRARY_PATH:
                ++val;
                if (not (IS_BLOCK(val) or IS_FILE(val) or ANY_STRING(val))) {
                    DECLARE_LOCAL (path);
                    Derelativize(path, val, specifier);
                    fail (Error_Tcc_Invalid_Library_Path_Raw(path));
                }
                libdir = val;
                break;

            case SYM_LIBRARY:
                ++val;
                if (not (IS_BLOCK(val) or IS_FILE(val) or ANY_STRING(val))) {
                    DECLARE_LOCAL (library);
                    Derelativize(library, val, specifier);
                    fail (Error_Tcc_Invalid_Library_Raw(library));
                }
                lib = val;
                break;

            default: {
                DECLARE_LOCAL (bad);
                Derelativize(bad, val, specifier);
                fail (Error_Tcc_Not_Supported_Opt_Raw(bad)); }
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
        if (IS_WORD(item) or IS_GET_WORD(item)) {
            var = Get_Opt_Var_May_Fail(item, VAL_SPECIFIER(natives));
            if (IS_NULLED(var))
                fail (Error_No_Value_Core(item, VAL_SPECIFIER(natives)));
        }

        if (IS_ACTION(var)) {
            assert(Is_User_Native(var));

            // Remember this function, because we're going to need to come
            // back and fill in its dispatcher and TCC_State after the
            // compilation...
            //
            DS_PUSH(KNOWN(var));

            REBARR *details = VAL_ACT_DETAILS(var);
            RELVAL *source = ARR_AT(details, IDX_NATIVE_BODY);
            RELVAL *linkname = ARR_AT(details, IDX_TCC_NATIVE_LINKNAME);

            Append_Unencoded(mo->series, "const REBVAL *");
            Append_Utf8_String(mo->series, linkname, VAL_LEN_AT(linkname));
            Append_Unencoded(mo->series, "(REBFRM *frame_)\n{\n");

            REBVAL *param = VAL_ACT_PARAMS_HEAD(var);
            unsigned int num = 1;
            for (; NOT_END(param); ++param) {
                REBSTR *spelling = VAL_PARAM_SPELLING(param);

                enum Reb_Param_Class pclass = VAL_PARAM_CLASS(param);
                switch (pclass) {
                case PARAM_CLASS_LOCAL:
                case PARAM_CLASS_RETURN:
                    assert(false); // natives shouldn't generally use these...
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
                    Append_Unencoded(mo->series, STR_HEAD(spelling));
                    Append_Unencoded(mo->series, ");\n");
                    break;

                default:
                    assert(false);
                }
            }
            if (num != 1)
                Append_Unencoded(mo->series, "\n");

            Append_Utf8_String(mo->series, source, VAL_LEN_AT(source));
            Append_Unencoded(mo->series, "\n}\n\n");
        }
        else if (IS_TEXT(var)) {
            //
            // A string is treated as just a fragment of code.  This allows
            // for writing things like C functions or macros that are shared
            // between multiple user natives.
            //
            Append_Utf8_String(mo->series, var, VAL_LEN_AT(var));
            Append_Unencoded(mo->series, "\n");
        }
        else {
            assert(false);
        }
    }

    // To help in debugging, it can be useful to see what is being passed in
    //
    if (REF(inspect)) {
        DS_DROP_TO(dsp_orig); // don't modify the collected user natives
        return Init_Text(D_OUT, Pop_Molded_String(mo));
    }

    REBSER *combined_src = Pop_Molded_UTF8(mo);

    TCCState *state = tcc_new();
    if (not state)
        fail (Error_Tcc_Construction_Raw());

    void* opaque = cast(void*, EMPTY_BLOCK); // can pass data through...
    tcc_set_error_func(state, opaque, tcc_error_report);

    if (options) {
        char *options_utf8 = rebSpell(KNOWN(options), rebEND);
        tcc_set_options(state, options_utf8);
        rebFree(options_utf8);
    }

    REBCTX *err = nullptr;

    if ((err = add_path(state, inc, tcc_add_include_path, SYM_TCC_INCLUDE)))
        fail (err);

    if (tcc_set_output_type(state, TCC_OUTPUT_MEMORY) < 0)
        fail (Error_Tcc_Output_Type_Raw());

    if (tcc_compile_string(state, cs_cast(BIN_HEAD(combined_src))) < 0)
        fail (Error_Tcc_Compile_Raw(natives));

    Free_Unmanaged_Series(combined_src);

    // It is technically possible for ELF binaries to "--export-dynamic" (or
    // -rdynamic in CMake) and make executables embed symbols for functions
    // in them "like a DLL".  However, we would like to make API symbols for
    // Rebol available to the dynamically loaded code on all platforms, so
    // this uses `tcc_add_symbol()` to work the same way on Windows/Linux/OSX
    //
    const struct rebol_sym_data_t *sym_data = &rebol_sym_data[0];
    for (; sym_data->name != nullptr; ++sym_data) {
        if (tcc_add_symbol(state, sym_data->name, sym_data->data) < 0)
            fail (Error_Tcc_Relocate_Raw());
    }

    const struct rebol_sym_cfunc_t *sym_cfunc = &rebol_sym_cfuncs[0];
    for (; sym_cfunc->name != nullptr; ++sym_cfunc) {
        // ISO C++ forbids casting between pointer-to-function and
        // pointer-to-object, use memcpy to circumvent.
        void *ptr;
        assert(sizeof(ptr) == sizeof(sym_cfunc->cfunc));
        memcpy(&ptr, &sym_cfunc->cfunc, sizeof(ptr));
        if (tcc_add_symbol(state, sym_cfunc->name, ptr) < 0)
            fail (Error_Tcc_Relocate_Raw());
    }

    // Add symbols in libtcc1, to avoid bundling with libtcc1.a
    const void **sym = &r3_libtcc1_symbols[0];
    for (; *sym != nullptr; sym += 2) {
        if (tcc_add_symbol(state, cast(const char*, *sym), *(sym + 1)) < 0)
            fail (Error_Tcc_Relocate_Raw());
    }

    if ((err = add_path(
        state, libdir, tcc_add_library_path, SYM_TCC_LIBRARY_PATH
    ))) {
        fail (err);
    }

    if ((err = add_path(state, lib, tcc_add_library, SYM_TCC_LIBRARY)))
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
        assert(IS_ACTION(var) and Is_User_Native(var));

        REBARR *details = VAL_ACT_DETAILS(var);
        REBVAL *linkname = KNOWN(ARR_AT(details, IDX_TCC_NATIVE_LINKNAME));

        char *name_utf8 = rebSpell("ensure text!", linkname, rebEND);
        void *sym = tcc_get_symbol(state, name_utf8);
        rebFree(name_utf8);

        if (not sym)
            fail (Error_Tcc_Sym_Not_Found_Raw(linkname));

        // Circumvent ISO C++ forbidding cast between function/data pointers
        //
        REBNAT c_func;
        assert(sizeof(c_func) == sizeof(void*));
        memcpy(&c_func, &sym, sizeof(c_func));

        ACT_DISPATCHER(VAL_ACTION(var)) = c_func;
        Move_Value(ARR_AT(details, IDX_TCC_NATIVE_STATE), handle);

        DS_DROP;
    }

    return nullptr;
  #endif
}
