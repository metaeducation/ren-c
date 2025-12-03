//
//  file: %mod-tcc.c
//  summary: -[Implementation of "user natives" using an embedded C compiler]-
//  section: extension
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016 Atronix Engineering
// Copyright 2016-2025 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
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
// string of C code.  It is compiled on the fly by TCC, using the libtcc API.
//
// https://github.com/metaeducation/tcc/blob/mob/libtcc.h
// https://github.com/metaeducation/tcc/blob/mob/tests/libtcc_test.c
//
// See the TCC extension's README.md for an overview of the extension.
//
// This file implements MAKE-NATIVE and a "low level" compile primitive called
// COMPILE*.
//

#include "sys-core.h"
#include "tmp-mod-tcc.h"

#include "libtcc.h"


// tcc_relocate() underwent a breaking API change to add a second parameter in
// September 2012, to provide a custom memory buffer, with the ability to pass
// in a special TCC_RELOCATE_AUTO constant to get the old behavior:
//
// https://repo.or.cz/tinycc.git/commitdiff/ca38792df17fc5c8d2bb6757c512101610420f1e
//
// This was later undone in 2024, going back to a single parameter--based on
// the idea that the TCCState itself contains enough information:
//
// https://repo.or.cz/tinycc.git/commitdiff/b671fc0594625eb5ac147ec83be6d0c1fc1a6ad5
//
// For the sake of supporting multiple libtcc versions, we use the presence of
// the TCC_RELOCATE_AUTO macro to determine which version of the API to call.
//
#if defined(TCC_RELOCATE_AUTO)
    #define tcc_relocate_auto(s) \
        tcc_relocate((s), TCC_RELOCATE_AUTO)

    #define LIBTCC_IS_PROBABLY_FROM_BEFORE_2024  1  // see uses below
#else
    #define tcc_relocate_auto(s) \
        tcc_relocate(s)

    #define LIBTCC_IS_PROBABLY_FROM_BEFORE_2024  0
#endif


// tcc_set_options() was added in February 2013:
//
// https://repo.or.cz/tinycc.git?a=commit;h=05108a3b0a8eff70739b253b8995999b1861f9f2
//
// At one time, the missing TCC_RELOCATE_AUTO macro was used to detect older
// libtcc versions that also lacked tcc_set_options().  But the 2024 change
// removed TCC_RELOCATE_AUTO again :-/ so that won't work.  If for some reason
// you're building with a very old libtcc, define this.
//
#ifdef TCC_EXTENSION_NO_OPTIONS
    void tcc_set_options(TCCState *s, const char *str) {
        UNUSED(s);
        UNUSED(str);

        rebJumps ("panic ["
            "-[You're using OPTIONS in your COMPILE configuration.  But this]-"
            "-[tcc extension was built with an older libtcc that was assumed]-"
            "-[to not have tcc_set_options() (it lacked TCC_RELOCATE_AUTO).]-"
            "-[You'll need to rebuild the tcc extension with a newer lib.]-"
        "]");
    }
#endif


typedef int (*TCC_CSTR_API)(TCCState *, const char *);
int tcc_set_options_i(TCCState *s, const char *str)
  { tcc_set_options(s, str); return 0; }  // make into a TCC_CSTR_API
int tcc_set_lib_path_i(TCCState *s, const char *path)
  { tcc_set_lib_path(s, path); return 0; }  // make into a TCC_CSTR_API



enum {
    IDX_TCC_PRENATIVE_CONTEXT = 1,
    IDX_TCC_PRENATIVE_LINKNAME,  // auto-generated if unspecified
    IDX_TCC_PRENATIVE_SOURCE,  // textual source code
    IDX_TCC_PRENATIVE_STATE,  // will be a SPACE until COMPILE happens
    MAX_IDX_TCC_PRENATIVE = IDX_TCC_PRENATIVE_STATE
};

// While TCC natives use the Api_Function_Dispatcher(), they need to hold a
// reference to the TCCState that was used to compile them so long as the
// CFunction is expected to run.  This is stored in a special extra slot.
//
enum {
    IDX_TCC_NATIVE_STATE = MAX_IDX_API_ACTION + 1,
    MAX_IDX_TCC_NATIVE = IDX_TCC_NATIVE_STATE
};


// This is the function registered to receive error messages during the
// compile.  The current logic just returns one error, but if more than one
// is given they could be batched up.
//
// 1. When `tcc_set_error_func()` is called, you can pass it a value that
//    it will pass back.  We pass g_empty_block to test it (and explain it).
//    Note that since the compilation can be delayed after MAKE-NATIVE exits,
//    pointers to local variables should not be used here.
//
// 2. TCC added a warning for potential missing returns.  But `_Noreturn`
//    support didn't come until after the warning.  And they haven't bumped the
//    version reported by __TINYC__ since 2017...so you can't tell when you
//    can use _Noreturn or not.  Rather than force you to disable all warnings,
//    we filter out this one.
//
static void Error_Reporting_Hook(
    void *opaque,
    const char *msg_utf8
){
    assert(cast(Value*, opaque) == g_empty_block);  // test callback arg [1]
    UNUSED(opaque);

    Value* message = rebText(msg_utf8);

  #if LIBTCC_IS_PROBABLY_FROM_BEFORE_2024  // suppress no value warning [2]
    if (rebDid(
        "find", message, "-[warning: function might return no value]-"
    )){
        rebRelease(message);
        return;
    }
  #endif

    rebJumps ("panic [",
        "-[TCC errors/warnings, '-w' to stop warnings:]-", rebR(message),
    "]");
}


// This calls a TCC API that takes a string on an optional Rebol TEXT! value
// found in the config.
//
// Note the COMPILE usermode front end standardizes FILE! paths into TEXT!
// with FILE-TO-LOCAL, so that on Windows they'll have backslashes, etc.
//
//
static void Process_Text_Helper_Core(
    TCC_CSTR_API some_tcc_api,
    TCCState *state,
    const Value* text,
    const char *label
){
    assert(Is_Text(text));

    char* utf8 = rebSpell(text);
    int status = some_tcc_api(state, utf8);
    rebFree(utf8);

    if (status < 0)  // !!! When is this called vs. Error_Reporting_Hook?
        rebJumps ("panic [",
            "-[TCC]-", rebT(label), "-[rejected:]-", text,
        "]");
}
static void Process_Text_Helper(
    TCC_CSTR_API some_tcc_api,
    TCCState *state,
    const Value* config,
    const char *label
){
    Value* text = rebValue(
        "ensure [<null> text!] select", config, "as word!", rebT(label)
    );

    if (text) {
        Process_Text_Helper_Core(some_tcc_api, state, text, label);
        rebRelease(text);
    }
}


// The COMPILE usermode front end standardizes settings into blocks, if they
// are able to take more than one item in the general case.
// Any FILE! elements are converted with FILE-TO-LOCAL, so that on Windows
// they'll have backslashes, etc.  Factoring this out reduces redundancy.
//
static void Process_Block_Helper(
    TCC_CSTR_API some_tcc_api,
    TCCState *state,
    const Value* config,
    const char *label
){
    Value* block = rebValue(
        "ensure block! select", config, "as word!", rebT(label)
    );

    const Element* tail;
    const Element* text = List_At(&tail, block);
    for (; text != tail; ++text)
        Process_Text_Helper_Core(some_tcc_api, state, text, label);

    rebRelease(block);
}


// libtcc breaks ISO C++ by passing function pointers as void*.  This helper
// uses memcpy to circumvent, assuming they're the same size.
//
static void Add_API_Symbol_Helper(
    TCCState *state,
    const char *symbol,
    CFunction* cfunc_ptr  // see CFunction for why func/data pointers differ
){
    void *void_ptr;
    assert(sizeof(void_ptr) == sizeof(cfunc_ptr));
    memcpy(&void_ptr, &cfunc_ptr, sizeof(cfunc_ptr));

    if (tcc_add_symbol(state, symbol, void_ptr) < 0)
        rebJumps ("panic [",
            "-[tcc_add_symbol() failed for]-", rebT(symbol),
        "]");
}


// When a batch of natives or code are compiled into memory, that memory has
// to stick around as long as you expect a user native to be able to execute.
// So the GC has to keep the generated code alive as long as pointers exist.
// This is tracked by having each user native hold a reference to the memory
// blob via a HANDLE!.  When the last reference to the last native goes away,
// the GC will run this handle cleanup function.
//
static void Tcc_State_Handle_Cleaner(void* p, size_t length)
{
    TCCState *state = cast(TCCState*, p);
    UNUSED(length);

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
Bounce Pending_Native_Dispatcher(Level* L) {
    Details* details = Ensure_Level_Details(L);
    assert(Details_Dispatcher(details) == &Pending_Native_Dispatcher);

    Element* frame = Init_Frame(
        Level_Spare(L), details, Level_Label(L), Level_Coupling(L)
    );

    rebElide("compile [", frame, "]");
    //
    // ^-- !!! Today's COMPILE doesn't return a result on success (just fails
    // on errors), but if it changes to return one consider what to do.

    assert(Details_Dispatcher(details) == &Api_Function_Dispatcher);

    return BOUNCE_REDO_UNCHECKED;
}


//
//  Pending_Native_Details_Querier: C
//
static bool Pending_Native_Details_Querier(
    Sink(Value) out,
    Details* details,
    SymId property
){
    switch (property) {
      case SYM_RETURN_OF: {
        Extract_Paramlist_Returner(out, Phase_Paramlist(details), SYM_RETURN);
        return true; }

      case SYM_BODY_OF: {
        assert(!"Body of not supported by Pending Native yet");
        Init_Space(out);
        return true; }

      default:
        break;
    }

    return false;
}


//
//  export make-native: native [
//
//  "Make ACTION! from C source, compiled on-demand or by COMPILE function"
//
//      return: [~[action!]~]
//      spec "Rebol parameter definitions (similar to FUNCTION's spec)"
//          [block!]
//      source "C source of the native implementation"
//          [text!]
//      :linkname "Provide a specific linker name (default is auto-generated)"
//          [text!]
//  ]
//
DECLARE_NATIVE(MAKE_NATIVE)
{
    INCLUDE_PARAMS_OF_MAKE_NATIVE;

    Element* spec = Element_ARG(SPEC);
    Element* source = Element_ARG(SOURCE);

    require (
      ParamList* paramlist = Make_Paramlist_Managed(
        spec,
        MKF_MASK_NONE,
        SYM_RETURN  // want return
    ));

    Details* details = Make_Dispatch_Details(
        BASE_FLAG_MANAGED | DETAILS_FLAG_OWNS_PARAMLIST,
        Phase_Archetype(paramlist),
        &Pending_Native_Dispatcher,  // will be replaced e.g. by COMPILE
        MAX_IDX_TCC_PRENATIVE  // details len [source module linkname tcc_state]
    );

    // !!! Natives on the stack can specify where APIs like rebValue() should
    // look for bindings.  For the moment, set user natives to use the user
    // context...it could be a parameter of some kind (?)
    //
    Copy_Cell(Details_At(details, IDX_TCC_PRENATIVE_CONTEXT), g_user_module);

    if (Is_Flex_Frozen(Cell_Strand(source)))  // don't have to copy if frozen
        Copy_Cell(Details_At(details, IDX_TCC_PRENATIVE_SOURCE), source);
    else {
        require (
          Strand* copy = Copy_String_At(source)  // might change
        );
        Init_Text(Details_At(details, IDX_TCC_PRENATIVE_SOURCE), copy);
    }

    if (Bool_ARG(LINKNAME)) {
        Value* linkname = ARG(LINKNAME);

        if (Is_Flex_Frozen(Cell_Strand(linkname)))
            Copy_Cell(
                Details_At(details, IDX_TCC_PRENATIVE_LINKNAME),
                linkname
            );
        else {
            require (
              Strand* copy = Copy_String_At(linkname)  // might change
            );
            Init_Text(Details_At(details, IDX_TCC_PRENATIVE_LINKNAME), copy);
        }
    }
    else {
        // Auto-generate a linker name based on the numeric value of the
        // paramlist pointer.  Just "N_" followed by the hexadecimal value.

        intptr_t heapaddr = i_cast(intptr_t, details);
        Value* linkname = rebValue(
            "unspaced [-[N_]- as text! to-hex", rebI(heapaddr), "]"
        );

        Copy_Cell(Details_At(details, IDX_TCC_PRENATIVE_LINKNAME), linkname);
        rebRelease(linkname);
    }

    Init_Space(Details_At(details, IDX_TCC_PRENATIVE_STATE));  // no state, yet

    Init_Action(OUT, details, ANONYMOUS, UNCOUPLED);
    return Packify_Action(OUT);
}


//
//  compile*: native [
//
//  "INTERNAL USE ONLY: Expects arguments to be fully vetted by COMPILE"
//
//      return: [
//          <null>
//          text! "C source text if :INSPECT was requested"
//      ]
//      compilables [block!] "Should be just TEXT! and user native ACTION!s"
//      config [object!] "Vetted and simplified form of :OPTIONS block"
//      :inspect "Return the C source code as text, but don't compile it"
//      :librebol "Connect symbols to running EXE libRebol (rebValue(), etc.)"
//      :files "COMPILABLES is a list of TEXT! specifying local filenames"
//  ]
//
DECLARE_NATIVE(COMPILE_P)
{
    INCLUDE_PARAMS_OF_COMPILE_P;


  //=//// ALLOCATE THE TCC STATE //////////////////////////////////////////=//

    // The state is where the code for the TCC_OUTPUT_MEMORY natives will be
    // living.  It must be kept alive for as long as you expect the user
    // natives to be able to execute, as this is where the Details_Dispatcher()
    // pointers are located.  The GCC manages it via handle (see cleanup())
    //
    TCCState *state = tcc_new();
    if (not state)
        panic ("TCC failed to create a TCC context");

    // We go ahead and put the state into a managed HANDLE!, so that the GC
    // can clean up the memory in the case of a panic().
    //
    // !!! It seems that getting an "invalid object file" error (e.g. by
    // using a Windows libtcc1.a on Linux) causes a leak.  It may be an error
    // in usage of the API, or TCC itself may leak in that case.  Review.
    //
    DECLARE_ELEMENT (handle);
    Init_Handle_Cdata_Managed(
        handle,
        state, // "data" pointer
        1,  // unused length (can't be 0, reserved for CFunction)
        &Tcc_State_Handle_Cleaner  // called upon GC
    );
    Push_Lifeguard(handle);

    void* opaque = cast(void*, g_empty_block); // can parameterize the error...
    tcc_set_error_func(state, opaque, &Error_Reporting_Hook);


  //=//// SET UP OPTIONS FOR THE TCC STATE FROM CONFIG ////////////////////=//

    Value* config = ARG(CONFIG);

    // Sets options (same syntax as the TCC command line, minus commands like
    // displaying the version or showing the TCC tool's help)
    //
    Process_Block_Helper(tcc_set_options_i, state, config, "options");

    // Add include paths (same as `-I` in the options?)
    //
    Process_Block_Helper(tcc_add_include_path, state, config, "include-path");

    // Though it is called `tcc_set_lib_path()`, it says it sets CONFIG_TCCDIR
    // at runtime of the built code, presumably so libtcc1.a can be found.
    //
    // !!! This doesn't seem to help Windows find the libtcc1.a file, so it's
    // not clear what the call does.  The higher-level COMPILE goes ahead and
    // sets the runtime path as an ordinary lib directory on Windows for the
    // moment, since this seems to be a no-op there.  :-/
    //
    Process_Text_Helper(tcc_set_lib_path_i, state, config, "runtime-path");

    // The output_type has to be set *before* you all tcc_output_file() or
    // tcc_relocate(), but has to be set *after* you've configured the
    // options.  (e.g. tcc_set_output_type() creates the debug symbol table,
    // so if you try to set "-g" after you call it it will be too late and
    // the debug symbol generation will crash).
    //
    int output_type = rebUnboxInteger(
        "switch pick", config, "'output-type [",
            "'memory [", rebI(TCC_OUTPUT_MEMORY), "]",  // no tcc_relocate()!
            "'EXE [", rebI(TCC_OUTPUT_EXE), "]",
            "'DLL [", rebI(TCC_OUTPUT_DLL), "]",
            "'OBJ [", rebI(TCC_OUTPUT_OBJ), "]",
            "'preprocess [", rebI(TCC_OUTPUT_PREPROCESS), "]",
            "-1",
        "]"
    );
    if (output_type == -1)
        return rebDelegate("panic [",
            "-[TCC unrecognized output type]- pick", config, "'output-type",
        "]");

    if (tcc_set_output_type(state, output_type) < 0)
        return rebDelegate("panic [",
            "-[TCC failed to set output to]- pick", config, "'output-type",
        "]");


  //=//// SPECIFY USER NATIVES (OR DISK FILES) TO COMPILE /////////////////=//

    Value* compilables = ARG(COMPILABLES);

    assert(TOP_INDEX == STACK_BASE);  // natives are pushed to the stack

    if (Bool_ARG(FILES)) {
        const Element* tail;
        const Element* item = List_At(&tail, compilables);
        for (; item != tail; ++item) {
            if (not Is_Text(item))
                panic (
                    "If COMPILE*:FILES, compilables must be TEXT! paths"
                );

            char *filename_utf8 = rebSpell(item);
            tcc_add_file(state, filename_utf8);
            rebFree(filename_utf8);
        }

        if (Bool_ARG(INSPECT)) {  // nothing to show, besides the file list
            Drop_Lifeguard(handle);
            return rebValue(
                "spaced [--[:INSPECT (files) =>]-- mold", compilables, "]"
            );
        }
    }
    else {
        // The TCC extension creates a new ACTION! type and dispatcher, so has
        // to use the "internal" API.  Since it does, it can take advantage of
        // using the mold buffer.  The buffer is a "hot" memory region that is
        // generally preallocated, and there's no need to say in advance how
        // large the buffer needs to be.  It then can pass the pointer to TCC
        // and discard the data without ever making a TEXT! (as it would need
        // to if it were a client of the "external" libRebol API).
        //
        DECLARE_MOLDER (mo);  // Note: mold buffer is UTF-8
        Push_Mold(mo);

        const Element* tail;
        const Element* item = List_At(&tail, compilables);
        for (; item != tail; ++item) {
            if (Is_Frame(item)) {
                Phase* phase = Frame_Phase(item);
                if (
                    not Is_Stub_Details(phase)
                    or (
                        Details_Dispatcher(cast(Details*, phase))
                        != &Pending_Native_Dispatcher
                    )
                ){
                    panic ("Only user natives can be in COMPILABLES list");
                }

                // Remember this function, because we're going to need to come
                // back and fill in its dispatcher and TCC_State after the
                // compilation...
                //
                Copy_Cell(PUSH(), item);

                Details* details = cast(Details*, phase);
                Value* source = Details_At(details, IDX_TCC_PRENATIVE_SOURCE);
                Value* linkname = Details_At(
                    details, IDX_TCC_PRENATIVE_LINKNAME
                );

                // !!! Review: how to choose LIBREBOL_BINDING_NAME when doing
                // TCC natives?  It includes "rebol.h".

                require (
                  Append_Ascii(mo->strand, "RebolBounce ")
                );
                Append_Any_Utf8(mo->strand, linkname);
                require (
                  Append_Ascii(
                    mo->strand,
                    "(RebolContext* LIBREBOL_BINDING_NAME())\n{"
                ));

                Append_Any_Utf8(mo->strand, source);

                require (
                  Append_Ascii(mo->strand, "}\n\n")
                );
            }
            else if (Is_Text(item)) {
                //
                // A string passed to COMPILE in the list of things-to-compile
                // is treated as just a fragment of code.  This allows writing
                // arbitrary C functions that aren't themselves user natives,
                // but can be called by multiple user natives.  Or defining
                // macros or constants.  The string will appear at the point
                // in the compile where it is given in the list.
                //
                Append_Any_Utf8(mo->strand, item);
                require (
                  Append_Ascii(mo->strand, "\n")
                );
            }
            else {
                // COMPILE should've vetted the list to only TEXT! and ACTION!
                //
                panic (
                    "COMPILE input list must contain TEXT! and ACTION!s"
                );
            }
        }

        // == Mold buffer now contains the combined source ==

        // To help in debugging, it can be useful to see what is compiling
        // this is similar in spirit to the -E option for preprocessing only)
        //
        if (Bool_ARG(INSPECT)) {
            Drop_Lifeguard(handle);
            Drop_Data_Stack_To(STACK_BASE);  // don't modify collected natives
            return Init_Text(OUT, Pop_Molded_Strand(mo));
        }

        if (
            tcc_compile_string(
                state,
                s_cast(Binary_At(mo->strand, mo->base.size))
            ) < 0
        ){
            return rebDelegate("panic [",
                "-[TCC failed to compile the code]-", compilables,
            "]");
        }

        Drop_Mold(mo);  // discard the combined source (no longer needed)
    }

  //=//// LINKING STEPS (Libraries) ///////////////////////////////////////=//

    // TCC compiles the code first, so it knows what symbols it needs...and
    // only then can it narrow down which symbols in a library it needs.  So
    // these steps have to come *after* the compilation.

    // Add library paths (same as using `-L` in the options?)
    //
    Process_Block_Helper(tcc_add_library_path, state, config, "library-path");

    // Add individual library files (same as using -l in the options?  e.g.
    // the actual file is "libxxx.a" but you'd pass just `xxx` here)
    //
    // !!! Does this work for fully specified file paths as well?
    //
    Process_Block_Helper(tcc_add_library, state, config, "library");

    // We could export just one symbol ("g_librebol" for the RebolApiTable) and
    // tell the API to call it as g_librebol->rebXXX(), the way DLLs do it
    // with the LIBREBOL_USES_API_TABLE feature.  But it's more efficient to
    // use direct calls.  There aren't that many entry points for the libRebol
    // API, so just expose their symbols.
    //
    // It is technically possible for ELF binaries to "--export-dynamic" (or
    // -rdynamic in CMake) and make executables embed symbols for functions
    // in them "like a DLL".  However, we would like to make API symbols for
    // Rebol available to the dynamically loaded code on all platforms, so
    // this uses `tcc_add_symbol()` to work the same way on Windows/Linux/OSX
    //
    // !!! Not only is it technically possible to export symbols dynamically,
    // the build configuration for Rebol as a lib seems to force it, at least
    // on linux.  If you add a prototype like:
    //
    //    int Probe_Core_Debug(const Value* v, char* file, int line);
    //
    // ...and then try calling it from your user native, it finds the internal
    // symbol.  Messing with -fvisibility="hidden" and other switches doesn't
    // seem to change this.  (If you define your own Probe_Core_Debug() in the
    // user native C file as a text blob in the compile, that overrides it.)
    //
    // On Windows it doesn't do this, but on the other hand it doesn't seem
    // *able* to do it.  It can only see tcc_add_symbol() exported symbols.
    //
    if (Bool_ARG(LIBREBOL)) {
        //
        // .inc file contains calls for each function in %a-lib.c like:
        //
        //      Add_API_Symbol_Helper(
        //          state,
        //          "API_rebXXX",
        //          f_cast(CFunction*, &API_rebXXX)
        //      );
        //
        #include "tmp-librebol-symbols.inc"
    }

    if (output_type == TCC_OUTPUT_MEMORY) {
        if (tcc_relocate_auto(state) < 0)
            panic ("TCC failed to relocate the code");
    }
    else {
        assert(TOP_INDEX == STACK_BASE);  // no user natives if outputting file

        char *output_file_utf8 = rebSpell(
            "ensure text! pick", config, "'output-file"
        );

        if (tcc_output_file(state, output_file_utf8) < 0)
            panic ("TCC failed to output the file");

        rebFree(output_file_utf8);
    }

    // With compilation complete, find the matching linker names and get
    // their function pointers to substitute in for the dispatcher.
    //
    while (TOP_INDEX != STACK_BASE) {
        Details* details_tcc = Ensure_Frame_Details(TOP);  // stack live
        assert(Details_Dispatcher(details_tcc) == &Pending_Native_Dispatcher);

        Value* linkname = Details_At(details_tcc, IDX_TCC_PRENATIVE_LINKNAME);

        char *name_utf8 = rebSpell("ensure text!", linkname);
        void *sym = tcc_get_symbol(state, name_utf8);
        rebFree(name_utf8);

        if (not sym)
            return rebDelegate("panic [",
                "-[TCC failed to find symbol:]-", linkname,
            "]");

        // Circumvent ISO C++ forbidding cast between function/data pointers
        //
        RebolActionCFunction* cfunc;
        assert(sizeof(cfunc) == sizeof(void*));
        memcpy(&cfunc, &sym, sizeof(cfunc));

        Details* details_api = Make_Dispatch_Details(
            (not BASE_FLAG_MANAGED)  // we swap and free, need unmanaged
                | DETAILS_FLAG_OWNS_PARAMLIST,
            Phase_Archetype(details_tcc),  // reuse paramlist
            &Api_Function_Dispatcher,
            MAX_IDX_TCC_NATIVE
        );

        Init_Handle_Cfunc(
            Details_At(details_api, IDX_API_ACTION_CFUNC),
            f_cast(CFunction*, cfunc)
        );
        Element* block = Init_Block(
            Details_At(details_api, IDX_API_ACTION_BINDING_BLOCK),
            g_empty_array
        );
        Copy_Cell(
            Details_At(details_api, IDX_TCC_NATIVE_STATE),
            handle  // hold reference to TCCState live via HANDLE!
        );
        Tweak_Cell_Binding(block, g_user_context);  // !!! MAKE-NATIVE capture?

        Swap_Stub_Content(details_tcc, details_api);

        Free_Unmanaged_Flex(Details_Array(details_api));  // now not managed

        DROP();
    }

    Drop_Lifeguard(handle);

    return nullptr;
}


//
//  startup*: native [
//
//  "Initialize TCC extension"
//
//      return: []
//  ]
//
DECLARE_NATIVE(STARTUP_P)
{
    INCLUDE_PARAMS_OF_STARTUP_P;

    Register_Dispatcher(
        &Pending_Native_Dispatcher,
        &Pending_Native_Details_Querier
    );
    return "~";
}


//
//  shutdown*: native [
//
//  "Shut down TCC extension"
//
//      return: []
//  ]
//
DECLARE_NATIVE(SHUTDOWN_P)
{
    INCLUDE_PARAMS_OF_SHUTDOWN_P;

    return "~";
}
