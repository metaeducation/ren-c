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
// Copyright 2012-2021 Ren-C Open Source Contributors
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
// "Extensions" refers to the idea of a module which has associated native
// code--in a DLL or .so file.  The extension machinery allows for the loading
// and unloading of a mixture of natives and usermode functions that can use
// those added natives.
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
//  Startup_Extension_Loader: C
//
void Startup_Extension_Loader(void)
{
    assert(PG_Device_List == nullptr);

    assert(rebDid("empty? system.extensions"));
}


//
// Shutdown_Extension_Loader: C
//
void Shutdown_Extension_Loader(void)
{
    // The UNLOAD-EXTENSION will remove the extension from the list, so we
    // enumerate over a copy of the list.  (See remarks on making this more
    // efficient.)
    //
    rebElide("for-each ext copy system.extensions [unload-extension ext]");

    // !!! The R3-Alpha "Device model" is likely going to go away, but for
    // the sake of cleanliness make sure all "devices" were unregistered.
    //
    assert(PG_Device_List == nullptr);
}


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
// !!! In the initial design, extensions were distinct from modules, and could
// in fact load several different modules from the same DLL.  But that confused
// matters in terms of whether there was any requirement for the user to know
// what an "extension" was.
//
// !!! The DLL form has not been tested and maintained, so it needs to be
// hammered back into shape and tested.  However, higher priority is to make
// the extension mechanism work in the WebAssembly build with so-called
// "side modules", so that extra bits of native code functionality can be
// pulled into web sessions that want them.
{
    INCLUDE_PARAMS_OF_LOAD_EXTENSION;

    // See IDX_COLLATOR_MAX for collated block contents, which include init
    // and shutdown functions, as well as Rebol script source, plus the REBNAT
    // functions for each native.
    //
    const REBARR *details;

    if (IS_BLOCK(ARG(where))) {  // It's one of the BUILTIN-EXTENSIONS
        details = VAL_ARRAY(ARG(where)); // already "collated"
    }
    else {  // It's a DLL, must locate and call its RX_Collate() function
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

        rebRelease(lib_api);  // should we hang onto lib to pass along?
    }

    assert(ARR_LEN(details) == IDX_COLLATOR_MAX);
    PUSH_GC_GUARD(details);

    const REBVAL *script_compressed
        = DETAILS_AT(details, IDX_COLLATOR_SCRIPT);
    REBLEN script_num_codepoints
        = VAL_UINT32(DETAILS_AT(details, IDX_COLLATOR_SCRIPT_NUM_CODEPOINTS));
    const REBVAL *dispatchers_handle
        = DETAILS_AT(details, IDX_COLLATOR_DISPATCHERS);

    REBLEN num_natives = VAL_HANDLE_LEN(dispatchers_handle);
    REBNAT *dispatchers = VAL_HANDLE_POINTER(REBNAT, dispatchers_handle);

    // !!! used to use STD_EXT_CTX, now this would go in META OF

    REBCTX *module_ctx = Alloc_Context_Core(REB_MODULE, 1, NODE_FLAG_MANAGED);

    PG_Next_Native_Dispatcher = dispatchers;
    PG_Currently_Loading_Module = module_ctx;

    DECLARE_LOCAL (module);
    Init_Any_Context(module, REB_MODULE, module_ctx);
    PUSH_GC_GUARD(module);  // !!! Is GC guard unnecessary due to references?

    size_t script_size;
    REBYTE *script_utf8 = Decompress_Alloc_Core(
        &script_size,
        VAL_HANDLE_POINTER(REBYTE, script_compressed),
        VAL_HANDLE_LEN(script_compressed),
        -1,  // max
        SYM_GZIP
    );

    // The decompress routine gives back a pointer which is actually inside of
    // a binary series (e.g. a rebAlloc() product).  Get the series back so
    // we can pass it to import as a string.
    //
    REBVAL *script = rebRepossess(script_utf8, script_size);

    // The rebRepossess() function gives us back a BINARY!.  But we happen to
    // know that the data is actually valid UTF-8.  The scanner does not
    // currently have mechanics to run any faster on already-valid UTF-8, but
    // it could.  Periodically shuffle the data between TEXT! and BINARY!, and
    // binary with the text flag set.
    //
    // !!! Adding at least one feature in the scanner that takes advantage of
    // prevalidated UTF-8 might be a good exploratory task, because until then
    // this *should* make no difference.
    //
    if (SPORADICALLY(2)) {
        REBBIN *bin = VAL_BINARY_ENSURE_MUTABLE(script);
        mutable_SER_FLAVOR(bin) = FLAVOR_STRING;
        TERM_STR_LEN_SIZE(
            cast(REBSTR*, bin),  // legal for tweaking cached data
            script_num_codepoints,
            BIN_LEN(bin)
        );
        mutable_LINK(Bookmarks, m_cast(REBBIN*, bin)) = nullptr;

        if (SPORADICALLY(2))
            Init_Text(RESET(script), STR(bin));
    }

    // !!! We currently are pushing all extensions into the lib context so
    // they are seen by every module.  This is an interim step to keep things
    // running, but a better strategy is needed.
    //
    // !!! sys.import*/into should work, but path mechanics are clunky.
    //
    rebElide("sys/import*/into lib", script, module);

    // !!! Note: This does not get cleaned up in case of an error, needs to
    // have TRAP.
    //
    if (PG_Next_Native_Dispatcher != dispatchers + num_natives)
        panic ("NATIVE calls did not line up with stored dispatch count");
    PG_Next_Native_Dispatcher = nullptr;

    assert(PG_Currently_Loading_Module == module_ctx);
    PG_Currently_Loading_Module = nullptr;

    rebRelease(script);

    DROP_GC_GUARD(module);
    DROP_GC_GUARD(details);

    rebElide("append system.extensions", CTX_ARCHETYPE(module_ctx));

    // !!! If modules are to be "unloadable", they would need some kind of
    // finalizer to clean up their resources.  There are shutdown actions
    // defined in a couple of extensions, but no protocol by which the
    // system will automatically call them on shutdown (yet)

    return Init_Any_Context(D_OUT, REB_MODULE, module_ctx);
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
//      extension "The extension to be unloaded"
//          [module!]
//  ]
//
REBNATIVE(unload_extension)
//
// !!! The initial extension model had support for not just loading extensions
// from a DLL, but also unloading them.  It raises a lot of questions that are
// somewhat secondary to any known use cases, and the semantics of the system
// were not pinned down well enough to support it.
//
// But one important feature it achieved was that if an extension initialized
// something (perhaps e.g. initializing memory) then it could call code to free
// that memory.  For the moment this is done by calling a method named
// `shutdown*` if it exists in the module.
{
    INCLUDE_PARAMS_OF_UNLOAD_EXTENSION;

    REBVAL *extension = ARG(extension);

    REBVAL *pos = rebValue(Lib(FIND), "system.extensions", extension);

    // Remove the extension from the loaded extensions list.
    //
    // !!! This is inefficient in the Shutdown_Extensions() case, because we
    // have to walk a copy of the array.  This likely calls for making the
    // "unload_extension 'all" walk a copy of the array here in the native or
    // some other optimization.  Review.
    //
    if (not pos)
        fail ("Could not find extension in loaded extensions list");
    rebElide(Lib(TAKE), rebR(pos));

    // There is a murky issue about how to disconnect REBNATIVE()s from
    // dispatchers that have been unloaded.  If an extension is unloaded
    // and reloaded again, should old ACTION! values work again?  If so, how
    // would this deal with a recompiled extension which might have changed
    // the parameters--thus breaking any specializations, etc?
    //
    // Idea: Check for a match, and if it is a match wire it up compatibly.
    // If not warn the user, leave a non-running stub in the place of the
    // old function...and they can reboot if they need to or unload and
    // reload the dependent modules.  Note that this would happen more often
    // than one might think for any locals declared as part of the frame, as
    // adding a local changes the "interface"--affecting downstream frames.
    //
    UNUSED(&Unloaded_Dispatcher);

    // Note: The mechanical act of unloading a DLL involved these calls.
    /*
        if (not IS_LIBRARY(lib))
            fail (PAR(ext));

        if (IS_LIB_CLOSED(VAL_LIBRARY(lib)))
            fail (Error_Bad_Library_Raw());

        OS_CLOSE_LIBRARY(VAL_LIBRARY_FD(lib));
    */

   REBVAR *shutdown_action = MOD_VAR(
       VAL_CONTEXT(extension),
       Canon(SHUTDOWN_P),
       true
    );
   if (shutdown_action == nullptr)
        return Init_None(D_OUT);

   rebElide(shutdown_action);

   return Init_None(D_OUT);
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
    const REBYTE script_compressed[],
    REBSIZ script_compressed_size,
    REBLEN script_num_codepoints,
    REBNAT dispatchers[],
    REBLEN dispatchers_len
) {
    REBARR *a = Make_Array(IDX_COLLATOR_MAX); // details
    Init_Handle_Cdata(
        ARR_AT(a, IDX_COLLATOR_SCRIPT),
        m_cast(REBYTE*, script_compressed), // !!! by contract, don't change!
        script_compressed_size
    );
    Init_Integer(
        ARR_AT(a, IDX_COLLATOR_SCRIPT_NUM_CODEPOINTS),
        script_num_codepoints
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
    UNUSED(block);
}
