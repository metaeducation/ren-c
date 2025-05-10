//
//  file: %f-extension.c
//  summary: "support for extensions"
//  section: functional
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Rebol Open Source Contributors
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
// !!! Extensions in Ren-C are a redesign from extensions in R3-Alpha.  They
// are a work in progress (and need documentation and cleanup), but have
// been a proof-of-concept for the core idea to be able to write code that
// looks similar to Rebol natives, but can be loaded from a DLL making calls
// back into the executable...or alternately, built directly into the Rebol
// interpreter itself based on a configuration switch.
//
// See the %extensions/ directory for some current (evolving) examples.
//

#include "sys-core.h"

// Building Rebol as a library may still entail a desire to ship that library
// with built-in extensions (e.g. building libr3.js wants to have JavaScript
// natives as an extension).  So there is no meaning to "built-in extensions"
// for a library otherwise...as every client will be making their own EXE, and
// there's no way to control their build process from Rebol's build process.
//
// Hence, the generated header for boot extensions is included here--to allow
// clients to get access to those extensions through an API.
//
#include "tmp-boot-extensions.inc"

//
//  cleanup_extension_init_handler: C
//
void cleanup_extension_init_handler(const Value* v)
  { UNUSED(v); } // cleanup CFUNC* just serves as an ID for the HANDLE!


//
//  cleanup_extension_quit_handler: C
//
void cleanup_extension_quit_handler(const Value* v)
  { UNUSED(v); } // cleanup CFUNC* just serves as an ID for the HANDLE!


//
//  builtin-extensions: native [
//
//  {Gets the list of builtin extensions for the executable}
//
//      return: "Block of extension specifications ('collations')"
//          [block!]
//  ]
//
DECLARE_NATIVE(BUILTIN_EXTENSIONS)
//
// The config file used by %make.r marks extensions to be built into the
// executable (`+`), or not built at all (`-`).  (built as a dynamic
// library (`*`) is supported in the main branch, but not this old branch)
//
// rebStartup() should not initialize these extensions, because it might not
// be the right ordering.  Command-line processing or other code that uses
// Rebol may need to make decisions on when to initialize them.  So this
// function merely returns the built-in extensions, which can be loaded with
// the LOAD-EXTENSION function.
{
    UNUSED(level_);

    // Call the generator functions for each builtin extension to get back
    // all the collated information that would be needed to initialize and
    // use the extension (but don't act on the information yet!)

    Array* list = Make_Array(NUM_BUILTIN_EXTENSIONS);
    REBLEN i;
    for (i = 0; i != NUM_BUILTIN_EXTENSIONS; ++i) {
        COLLATE_CFUNC *collator = Builtin_Extension_Collators[i];
        Value* details = (*collator)();
        assert(Is_Block(details) and Cell_Series_Len_At(details) == IDX_COLLATOR_MAX);
        Copy_Cell(Alloc_Tail_Array(list), details);
        rebRelease(details);
    }
    return Init_Block(Alloc_Value(), list);
}


//
//  load-extension: native [
//
//  "Extension module loader (for DLLs or built-in extensions)"
//
//      return: [module!]
//      where "Path to extension file or block of builtin extension details"
//          [block!]
//      /no-user "Do not export to the user context"
//      /no-lib "Do not export to the lib context"
//  ]
//
DECLARE_NATIVE(LOAD_EXTENSION)
//
// !!! It is not ideal that this code be all written as C, as it is really
// kind of a variation of LOAD-MODULE and will have to repeat a lot of work.
{
    INCLUDE_PARAMS_OF_LOAD_EXTENSION;

    // See IDX_COLLATOR_MAX for collated block contents, which include init
    // and shutdown functions, as well as native specs and Rebol script
    // source, plus the Dispatcher* functions for each native.
    //
    Array* details = Cell_Array(ARG(WHERE));

    assert(Array_Len(details) == IDX_COLLATOR_MAX);
    Push_GC_Guard(details);

    // !!! In the initial design, extensions were distinct from modules, and
    // could in fact load several different modules from the same DLL.  But
    // that confused matters in terms of whether there was any requirement
    // for the user to know what an "extension" was.
    //
    // It's not necessarily ideal to have this code written entirely as C,
    // but the way it was broken up into a mix of usermode and native calls
    // in the original extension model was very twisty and was a barrier
    // to enhancement.  So trying a monolithic rewrite for starters.

    Value* script_compressed = KNOWN(
        Array_At(details, IDX_COLLATOR_SCRIPT)
    );
    Value* specs_compressed = KNOWN(
        Array_At(details, IDX_COLLATOR_SPECS)
    );
    Value* dispatchers_handle = KNOWN(
        Array_At(details, IDX_COLLATOR_DISPATCHERS)
    );

    REBLEN num_natives = VAL_HANDLE_LEN(dispatchers_handle);
    Dispatcher* *dispatchers = VAL_HANDLE_POINTER(Dispatcher*, dispatchers_handle);

    size_t specs_size;
    Byte *specs_utf8 = Decompress_Alloc_Core(
        &specs_size,
        VAL_HANDLE_POINTER(Byte, specs_compressed),
        VAL_HANDLE_LEN(specs_compressed),
        -1, // max
        CANON(GZIP)
    );

    Option(String*) filename = nullptr;  // !!! Name of DLL if available?
    Array* specs = Scan_UTF8_Managed(
        filename,
        specs_utf8,
        specs_size
    );
    rebFree(specs_utf8);
    Push_GC_Guard(specs);

    // !!! Specs have datatypes in them which are looked up via Get_Var().
    // This is something that raises questions, but go ahead and bind them
    // into lib for the time being (don't add any new words).
    //
    Bind_Values_Deep(Array_Head(specs), Lib_Context);

    // Some of the things being tacked on here (like the DLL info etc.) should
    // reside in the META OF portion, vs. being in-band in the module itself.
    // For the moment, go ahead and bind the code to its own copy of lib.

    // !!! used to use STD_EXT_CTX, now this would go in META OF

    VarList* module_ctx = Alloc_Context_Core(
        TYPE_MODULE,
        80,
        NODE_FLAG_MANAGED // !!! Is GC guard unnecessary due to references?
    );
    DECLARE_VALUE (module);
    Init_Any_Context(module, TYPE_MODULE, module_ctx);
    Push_GC_Guard(module);

    StackIndex base = TOP_INDEX; // for accumulating exports

    Cell* item = Array_Head(specs);
    REBLEN i;
    for (i = 0; i < num_natives; ++i) {
        //
        // Initial extension mechanism had an /export refinement on native.
        // Change that to be a prefix you can use so it looks more like a
        // normal module export...also Make_Native() doesn't understand it
        //
        bool is_export;
        if (Is_Word(item) and Cell_Word_Id(item) == SYM_EXPORT) {
            is_export = true;
            ++item;
        }
        else
            is_export = false;

        Cell* name = item;
        if (not Is_Set_Word(name))
            crash (name);

        // We want to create the native from the spec and naming, and make
        // sure its details know that its a "member" of this module.  That
        // means API calls while the native is on the stack will bind text
        // content into the module...so if you override APPEND locally that
        // will be the APPEND that is used by default.
        //
        Value* native = Make_Native(
            &item, // gets advanced/incremented
            SPECIFIED,
            dispatchers[i],
            module
        );
        UNUSED(native);

        // !!! The mechanics of exporting is something modules do and have to
        // get right.  We shouldn't recreate that process here, just gather
        // the list of the exports and pass it to the module code.
        //
        if (is_export) {
            Init_Word(PUSH(), Cell_Word_Symbol(name));
            if (0 == Try_Bind_Word(module_ctx, TOP))
                crash ("Couldn't bind word just added -- problem");
        }
    }

    Array* exports_arr = Pop_Stack_Values(base);
    DECLARE_VALUE (exports);
    Init_Block(exports, exports_arr);
    Push_GC_Guard(exports);

    // Now we have an empty context that has natives in it.  Ultimately what
    // we want is to run the init code for a module.

    size_t script_size;
    void *script_utf8 = rebGunzipAlloc(
        &script_size,
        VAL_HANDLE_POINTER(Byte, script_compressed),
        VAL_HANDLE_LEN(script_compressed),
        -1 // max
    );
    Value* script_bin = rebRepossess(script_utf8, script_size);

    // Module loading mechanics are supposed to be mostly done in usermode,
    // so try and honor that.  This means everything about whether the module
    // gets isolated and such.  It's not sorted out yet, because extensions
    // didn't really run through the full module system...but pretend it does
    // do that here.
    //
    rebElide(
        "sys/util/load-module/into/exports", rebR(script_bin), module, exports
    );

    // !!! Ideally we would be passing the lib, path,

    // !!! If these were the right refinements that should be tunneled through
    // they'd be tunneled here, but isn't this part of the module's spec?
    //
    UNUSED(Bool_ARG(NO_USER));
    UNUSED(Bool_ARG(NO_LIB));

    Drop_GC_Guard(exports);
    Drop_GC_Guard(module);
    Drop_GC_Guard(specs);
    Drop_GC_Guard(details);

    // !!! If modules are to be "unloadable", they would need some kind of
    // finalizer to clean up their resources.  There are shutdown actions
    // defined in a couple of extensions, but no protocol by which the
    // system will automatically call them on shutdown (yet)

    return Init_Any_Context(OUT, TYPE_MODULE, module_ctx);
}


//
//  rebCollateExtension_internal: C
//
// This routine gathers information which can be called to bring an extension
// to life.  It does not itself decompress any of the data it is given, or run
// any startup code.  This allows extensions which are built into an
// executable to do deferred loading.
//
// !!! For starters, this just returns an array of the values...but this is
// the same array that would be used as the ACT_DETAILS() of an action.  So
// it could return a generator ACTION!.
//
// !!! It may be desirable to separate out the module header and go ahead and
// get that loaded as part of this process, in order to allow queries of the
// dependencies and other information.  That might suggest returning a block
// with an OBJECT! header and an ACTION! to run to do the load?  Or maybe
// a HANDLE! which can be passed as a module body with a spec?
//
// !!! If a DLL gets loaded, it's possible these pointers could be unloaded
// if the information were not used immediately or it otherwise was not run.
// This has to be considered in the unloading mechanics.
//
Value* rebCollateExtension_internal(
    const Byte script_compressed[], REBLEN script_compressed_len,
    const Byte specs_compressed[], REBLEN specs_compressed_len,
    Dispatcher* dispatchers[], REBLEN dispatchers_len
) {

    Array* a = Make_Array(IDX_COLLATOR_MAX); // details
    Init_Handle_Simple(
        Array_At(a, IDX_COLLATOR_SCRIPT),
        m_cast(Byte*, script_compressed), // !!! by contract, don't change!
        script_compressed_len
    );
    Init_Handle_Simple(
        Array_At(a, IDX_COLLATOR_SPECS),
        m_cast(Byte*, specs_compressed), // !!! by contract, don't change!
        specs_compressed_len
    );
    Init_Handle_Simple(
        Array_At(a, IDX_COLLATOR_DISPATCHERS),
        dispatchers,
        dispatchers_len
    );
    Term_Array_Len(a, IDX_COLLATOR_MAX);

    return Init_Block(Alloc_Value(), a);
}


//
//  Hook_Datatype: C
//
// Poor-man's user-defined type hack: this really just gives the ability to
// have the only thing the core knows about a "user-defined-type" be its
// value cell structure and datatype enum number...but have the behaviors
// come from functions that are optionally registered in an extension.
//
// (Actual facets of user-defined types will ultimately be dispatched through
// Rebol-frame-interfaced functions, not raw C structures like this.)
//
void Hook_Datatype(
    enum Reb_Kind kind,
    GENERIC_HOOK gen,
    PATH_HOOK pef,
    COMPARE_HOOK ctf,
    MAKE_HOOK make_func,
    TO_HOOK to_func,
    MOLD_HOOK mold_func
) {
    if (Generic_Hooks[kind] != &T_Unhooked)
        panic ("Generic dispatcher already hooked.");
    if (Path_Hooks[kind] != &PD_Unhooked)
        panic ("Path dispatcher already hooked.");
    if (Compare_Hooks[kind] != &CT_Unhooked)
        panic ("Comparison dispatcher already hooked.");
    if (Make_Hooks[kind] != &MAKE_Unhooked)
        panic ("Make dispatcher already hooked.");
    if (To_Hooks[kind] != &TO_Unhooked)
        panic ("To dispatcher already hooked.");
    if (Mold_Or_Form_Hooks[kind] != &MF_Unhooked)
        panic ("Mold or Form dispatcher already hooked.");

    Generic_Hooks[kind] = gen;
    Path_Hooks[kind] = pef;
    Compare_Hooks[kind] = ctf;
    Make_Hooks[kind] = make_func;
    To_Hooks[kind] = to_func;
    Mold_Or_Form_Hooks[kind] = mold_func;
}


//
//  Unhook_Datatype: C
//
void Unhook_Datatype(enum Reb_Kind kind)
{
    if (Generic_Hooks[kind] == &T_Unhooked)
        panic ("Generic dispatcher is not hooked.");
    if (Path_Hooks[kind] == &PD_Unhooked)
        panic ("Path dispatcher is not hooked.");
    if (Compare_Hooks[kind] == &CT_Unhooked)
        panic ("Comparison dispatcher is not hooked.");
    if (Make_Hooks[kind] == &MAKE_Unhooked)
        panic ("Make dispatcher is not hooked.");
    if (To_Hooks[kind] == &TO_Unhooked)
        panic ("To dispatcher is not hooked.");
    if (Mold_Or_Form_Hooks[kind] == &MF_Unhooked)
        panic ("Mold or Form dispatcher is not hooked.");

    Generic_Hooks[kind] = &T_Unhooked;
    Path_Hooks[kind] = &PD_Unhooked;
    Compare_Hooks[kind] = &CT_Unhooked;
    Make_Hooks[kind] = &MAKE_Unhooked;
    To_Hooks[kind] = &TO_Unhooked;
    Mold_Or_Form_Hooks[kind] = &MF_Unhooked;
}
