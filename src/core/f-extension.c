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
// Copyright 2012-2018 Ren-C Open Source Contributors
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
void cleanup_extension_init_handler(const REBVAL *v)
  { UNUSED(v); } // cleanup CFUNC* just serves as an ID for the HANDLE!


//
//  cleanup_extension_quit_handler: C
//
void cleanup_extension_quit_handler(const REBVAL *v)
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
REBNATIVE(builtin_extensions)
//
// The config file used by %make.r marks extensions to be built into the
// executable (`+`), built as a dynamic library (`*`), or not built at
// all (`-`).  Each of the options marked with + has a C function for
// startup and shutdown.
//
// rebStartup() should not initialize these extensions, because it might not
// be the right ordering.  Command-line processing or other code that uses
// Rebol may need to make decisions on when to initialize them.  So this
// function merely returns the built-in extensions, which can be loaded with
// the LOAD-EXTENSION function.
{
    UNUSED(frame_);

    // Call the generator functions for each builtin extension to get back
    // all the collated information that would be needed to initialize and
    // use the extension (but don't act on the information yet!)

    REBARR *list = Make_Array(NUM_BUILTIN_EXTENSIONS);
    REBLEN i;
    for (i = 0; i != NUM_BUILTIN_EXTENSIONS; ++i) {
        COLLATE_CFUNC *collator = Builtin_Extension_Collators[i];
        REBVAL *details = (*collator)();
        assert(IS_BLOCK(details) and VAL_LEN_AT(details) == IDX_COLLATOR_MAX);
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
//          [file! block!]  ; !!! Should it take a LIBRARY! instead?
//  ]
//
REBNATIVE(load_extension)
//
// An "Extension" is a form of module which has associated native code.  There
// are two ways of getting that native code: one is through a "DLL", and
// another is by means of having it passed in through a HANDLE! of information
// that was "collated" together to build the extension into the executable.
//
// !!! The DLL form has not been tested and maintained, so it needs to be
// hammered back into shape and tested.  However, higher priority is to make
// the extension mechanism work in the WebAssembly build with so-called
// "side modules", so that extra bits of native code functionality can be
// pulled into web sessions that want them.
{
    INCLUDE_PARAMS_OF_LOAD_EXTENSION;

    DECLARE_LOCAL (lib);
    SET_END(lib);
    PUSH_GC_GUARD(lib);

    DECLARE_LOCAL (path);
    SET_END(path);
    PUSH_GC_GUARD(path);

    // See IDX_COLLATOR_MAX for collated block contents, which include init
    // and shutdown functions, as well as native specs and Rebol script
    // source, plus the REBNAT functions for each native.
    //
    const REBARR *details;

    if (IS_BLOCK(ARG(where))) {  // It's one of the BUILTIN-EXTENSIONS
        Init_Blank(lib);
        Init_Blank(path);
        details = VAL_ARRAY(ARG(where)); // already "collated"
    }
    else { // It's a DLL, must locate and call its RX_Collate() function
        assert(IS_FILE(ARG(where)));

        REBVAL *lib_api = rebValue("make library!", ARG(where));

        REBVAL *details_block = rebValue(
            "run-library-collator", lib_api, "{RX_Collate}"
        );

        if (not details_block or not IS_BLOCK(details_block)) {
            rebElide("close", lib_api);
            fail (Error_Bad_Extension_Raw(ARG(where)));
        }

        details = VAL_ARRAY(details_block);
        rebRelease(details_block);

        Copy_Cell(lib, lib_api);
        rebRelease(lib_api);
    }

    assert(ARR_LEN(details) == IDX_COLLATOR_MAX);
    PUSH_GC_GUARD(details);

    // !!! In the initial design, extensions were distinct from modules, and
    // could in fact load several different modules from the same DLL.  But
    // that confused matters in terms of whether there was any requirement
    // for the user to know what an "extension" was.
    //
    // It's not necessarily ideal to have this code written entirely as C,
    // but the way it was broken up into a mix of usermode and native calls
    // in the original extension model was very twisty and was a barrier
    // to enhancement.  So trying a monolithic rewrite for starters.

    const REBVAL *script_compressed
        = DETAILS_AT(details, IDX_COLLATOR_SCRIPT);
    const REBVAL *specs_compressed
        = DETAILS_AT(details, IDX_COLLATOR_SPECS);
    const REBVAL *dispatchers_handle
        = DETAILS_AT(details, IDX_COLLATOR_DISPATCHERS);

    REBLEN num_natives = VAL_HANDLE_LEN(dispatchers_handle);
    REBNAT *dispatchers = VAL_HANDLE_POINTER(REBNAT, dispatchers_handle);

    // !!! used to use STD_EXT_CTX, now this would go in META OF

    REBCTX *module_ctx = Alloc_Context_Core(
        REB_MODULE,
        80,
        NODE_FLAG_MANAGED // !!! Is GC guard unnecessary due to references?
    );
    DECLARE_LOCAL (module);
    Init_Any_Context(module, REB_MODULE, module_ctx);
    PUSH_GC_GUARD(module);

    size_t specs_size;
    REBYTE *specs_utf8 = Decompress_Alloc_Core(
        &specs_size,
        VAL_HANDLE_POINTER(REBYTE, specs_compressed),
        VAL_HANDLE_LEN(specs_compressed),
        -1,  // max
        SYM_GZIP
    );

    // !!! Specs have datatypes in them which are looked up via Get_Var().
    // This is something that raises questions, but necessitates binding to
    // lib to get them.
    //
    // However, the module context inherits from Lib.  So binding specs to the
    // module gives an opportunity to tweak the definitions if necessary so
    // that the specs would see more than what was in lib.  Review.
    //
    REBARR *specs = Scan_UTF8_Managed(
        ANONYMOUS,  // !!! Name of DLL if available?
        specs_utf8,
        specs_size,
        module_ctx  // !!! LOAD-MODULE redundantly interns
    );
    rebFree(specs_utf8);
    PUSH_GC_GUARD(specs);

    // Some of the things being tacked on here (like the DLL info etc.) should
    // reside in the META OF portion, vs. being in-band in the module itself.
    // For the moment, go ahead and bind the code to its own copy of lib.

    REBDSP dsp_orig = DSP; // for accumulating exports

    const RELVAL *tail = ARR_TAIL(specs);
    RELVAL *item = ARR_HEAD(specs);
    REBLEN i;
    for (i = 0; i < num_natives; ++i) {
        //
        // Initial extension mechanism had an /export refinement on native.
        // Change that to be a prefix you can use so it looks more like a
        // normal module export...also Make_Native() doesn't understand it
        //
        bool is_export;
        if (IS_WORD(item) and VAL_WORD_ID(item) == SYM_EXPORT) {
            is_export = true;
            ++item;
        }
        else
            is_export = false;

        RELVAL *name = item;
        if (not IS_SET_WORD(name))
            panic (name);

        // We want to create the native from the spec and naming, and make
        // sure its details know that its a "member" of this module.  That
        // means API calls while the native is on the stack will bind text
        // content into the module...so if you override APPEND locally that
        // will be the APPEND that is used by default.
        //
        REBVAL *native = Make_Native(
            &item, // gets advanced/incremented
            tail,
            SPECIFIED,
            dispatchers[i],
            module
        );
        UNUSED(native);  // added by Make_Native to module

        // !!! Unloading is a feature that was entertained in the original
        // extension model, but support was sketchy.  So unloading is not
        // currently enabled, but mark the native with an "unloadable" flag
        // if it's in a DLL...as a reminder to revisit the issue.
        //
        if (not IS_BLANK(lib)) {
            /*SET_ACTION_FLAG(VAL_ACTION(native), UNLOADABLE_NATIVE);*/
        }

        // !!! The mechanics of exporting is something modules do and have to
        // get right.  We shouldn't recreate that process here, just gather
        // the list of the exports and pass it to the module code.
        //
        if (is_export) {
            //
            // We don't want to necessarily make binding work on stack values.
            // So get to stack by way of the spare cell.
            //
            Init_Word(D_SPARE, VAL_WORD_SYMBOL(name));
            if (0 == Try_Bind_Word(module, D_SPARE))
                panic ("Couldn't bind word just added -- problem");
            Copy_Cell(DS_PUSH(), D_SPARE);
        }
    }

    REBARR *exports_arr = Pop_Stack_Values(dsp_orig);
    DECLARE_LOCAL (exports);
    Init_Block(exports, exports_arr);
    PUSH_GC_GUARD(exports);

    // Now we have an empty context that has natives in it.  Ultimately what
    // we want is to run the init code for a module.
    //
    REBVAL *script_bin = rebValue("gunzip", script_compressed);

    // Module loading mechanics are supposed to be mostly done in usermode,
    // so try and honor that.  This means everything about whether the module
    // gets isolated and such.  It's not sorted out yet, because extensions
    // didn't really run through the full module system...but pretend it does
    // do that here.
    //
    // !!! We currently are pushing all extensions into the lib context so
    // they are seen by every module.  This is an interim step to keep things
    // running, but a better strategy is needed.
    //
    rebElide(
        "sys/import* lib sys/load-module/into/exports",
            rebR(script_bin), module, exports
    );

    // !!! Ideally we would be passing the lib, path,

    DROP_GC_GUARD(exports);
    DROP_GC_GUARD(specs);
    DROP_GC_GUARD(module);
    DROP_GC_GUARD(details);
    DROP_GC_GUARD(path);
    DROP_GC_GUARD(lib);

    // !!! If modules are to be "unloadable", they would need some kind of
    // finalizer to clean up their resources.  There are shutdown actions
    // defined in a couple of extensions, but no protocol by which the
    // system will automatically call them on shutdown (yet)

    return Init_Any_Context(D_OUT, REB_MODULE, module_ctx);
}


//
// Just an ID for the handler
//
static void cleanup_module_handler(const REBVAL *val)
{
    UNUSED(val);
}


//
//  Unloaded_Dispatcher: C
//
// This will be the dispatcher for the natives in an extension after the
// extension is unloaded.
//
static const REBVAL *Unloaded_Dispatcher(REBFRM *f)
{
    UNUSED(f);

    fail (Error_Native_Unloaded_Raw(ACT_ARCHETYPE(FRM_PHASE(f))));
}


//
//  unload-extension: native [
//
//  "Unload an extension"
//
//      return: <none>
//      ext "The extension to be unloaded"
//          [object!]
//      /cleanup "The RX_Quit pointer for the builtin extension"
//          [handle!]
//  ]
//
REBNATIVE(unload_extension)
{
    UNUSED(frame_);
    UNUSED(&Unloaded_Dispatcher);
    UNUSED(&cleanup_module_handler);

    fail ("Unloading extensions is currently not supported");

    // !!! The initial extension model had support for not just loading an
    // extension from a DLL, but also unloading it.  It raises a lot of
    // questions that are somewhat secondary to any known use cases, and the
    // semantics of the system were not pinned down well enough to support it.
    //
    // But one important feature it did achieve was that if an extension
    // initialized something (perhaps e.g. initializing memory) then calling
    // code to free that memory (or release whatever API/resource it was
    // holding) is necessary.
    //
    // HOWEVER: modules that are written entirely in usermode may want some
    // shutdown code too (closing files or network connections, or if using
    // FFI maybe needing to make some FFI close calls.  So a better model of
    // "extension shutdown" would build on a mechanism that would work for
    // any MODULE!...registering its interest with an ACTION! that may be one
    // of its natives, or even just usermode code.
    //
    // Hence the mechanics from the initial extension shutdown (which called
    // CFUNC entry points in the DLL) have been removed.  There's also a lot
    // of other murky areas--like how to disconnect REBNATIVEs from CFUNC
    // dispatchers that have been unloaded...a mechanism was implemented here,
    // but it was elaborate and made it hard to modify and improve the system
    // while still not having clear semantics.  (If an extension is unloaded
    // and reloaded again, should old ACTION! values work again?  If so, how
    // would this deal with a recompiled extension which might have changed
    // the parameters--thus breaking any specializations, etc?)
    //
    // Long story short: the extension model is currently in a simpler state
    // to bring it into alignment with the module system, so that both can
    // be improved together.  The most important feature to add for both is
    // some kind of "finalizer".

    // Note: The mechanical act of unloading a DLL involved these calls.
    /*
        if (not IS_LIBRARY(lib))
            fail (PAR(ext));

        if (IS_LIB_CLOSED(VAL_LIBRARY(lib)))
            fail (Error_Bad_Library_Raw());

        OS_CLOSE_LIBRARY(VAL_LIBRARY_FD(lib));
    */
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
REBVAL *rebCollateExtension_internal(
    const REBYTE script_compressed[], REBLEN script_compressed_len,
    const REBYTE specs_compressed[], REBLEN specs_compressed_len,
    REBNAT dispatchers[], REBLEN dispatchers_len
) {

    REBARR *a = Make_Array(IDX_COLLATOR_MAX); // details
    Init_Handle_Cdata(
        ARR_AT(a, IDX_COLLATOR_SCRIPT),
        m_cast(REBYTE*, script_compressed), // !!! by contract, don't change!
        script_compressed_len
    );
    Init_Handle_Cdata(
        ARR_AT(a, IDX_COLLATOR_SPECS),
        m_cast(REBYTE*, specs_compressed), // !!! by contract, don't change!
        specs_compressed_len
    );
    Init_Handle_Cdata(
        ARR_AT(a, IDX_COLLATOR_DISPATCHERS),
        dispatchers,
        dispatchers_len
    );
    SET_SERIES_LEN(a, IDX_COLLATOR_MAX);

    return Init_Block(Alloc_Value(), a);
}


//
//  Extend_Generics_Someday: C
//
// !!! R3-Alpha's "generics" (like APPEND or TAKE) dispatched to code based on
// the first argument.  So APPEND to a BLOCK! would call the array dispatcher,
// while APPEND to a GOB! would call the gob dispatcher.  The list of legal
// datatypes that could be operated on was fixed as part of the declaration
// in %generics.r (though R3-Alpha called them "actions").
//
// Ren-C attempts to streamline the core so it can be used for more purposes,
// where suppport code for GOB! (or IMAGE!, or VECTOR!) may be redundant or
// otherwise wasteful.  These types are moved to extensions, which may be
// omitted from the build (or optionally loaded as DLLs).   That means that
// when the system is booting, it might not know what a GOB! is...and other
// extensions may wish to add types to the generic after-the-fact as well.
//
// Hence extension types are taken off the generic definitions.  The concept
// is that they would be added dynamically.  How this would be done is not
// known at this time...as an extensible generics system hasn't been made yet.
// What's done instead is the hack of just saying that all generics are
// willing to dispatch to a custom type, and it's the job of the handler to
// raise an error if it doesn't know what the generic means.  The key downside
// of this is that HELP doesn't give you information about what specific
// generics are applicable to extension types.
//
// This function is a placeholder to keep track of the unimplemented feature.
// What was done is that for all the definitions in %generics.r that took an
// extension type previously, a bit of that spec was copied into the extension
// and then passed in to the type registration routine as a block.  In theory
// this *kind* of information could be used to more strategically update the
// type specs and help to reflect the legal operations.
//
// (It would be expected that the ability to extend generics via usermode
// functions would be done through whatever this mechanism for extending them
// with native code would be.)
//
void Extend_Generics_Someday(REBVAL *block) {
    assert(IS_BLOCK(block));
    UNUSED(block);
}
