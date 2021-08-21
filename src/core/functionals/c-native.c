//
//  File: %c-native.c
//  Summary: "Function that executes implementation as native code"
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2020 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the GNU Lesser General Public License (LGPL), Version 3.0.
// You may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.en.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A native is unique from other function types, because instead of there
// being a "Native_Dispatcher()", each native has a C function that acts as
// its dispatcher.
//
// Also unique about natives is that the native function constructor must be
// built "by hand", since it is required to get the ball rolling on having
// functions to call at all.
//
// If there *were* a REBNATIVE(native) this would be its spec:
//
//  native: native [
//      spec [block!]
//  ]
//

#include "sys-core.h"


//
//  Make_Native: C
//
// Reused function in Startup_Natives() as well as extensions loading natives,
// which can be parameterized with a different context in which to look up
// bindings by deafault in the API when that native is on the stack.
//
// Entries look like:
//
//    some-name: native [spec content]
//
// It is optional to put ENFIX between the SET-WORD! and the spec.
//
// If refinements are added, this will have to get more sophisticated.
//
// Though the manual building of this table is not as "nice" as running the
// evaluator, the evaluator makes comparisons against native values.  Having
// all natives loaded fully before ever running Eval_Core() helps with
// stability and invariants...also there's "state" in keeping track of which
// native index is being loaded, which is non-obvious.  But these issues
// could be addressed (e.g. by passing the native index number / DLL in).
//
REBACT *Make_Native(
    REBVAL *spec,
    bool is_combinator,
    REBNAT dispatcher,
    REBCTX *module
){
    // There are implicit parameters to both NATIVE/COMBINATOR and usermode
    // COMBINATOR.  The native needs the full spec.
    //
    // !!! Note: This will manage the combinator's array.  Changing this would
    // need a version of Make_Paramlist_Managed() which took an array + index
    //
    DECLARE_LOCAL (expanded_spec);
    if (is_combinator) {
        Init_Block(expanded_spec, Expanded_Combinator_Spec(spec));
        spec = expanded_spec;
    }

    // With the components extracted, generate the native and add it to
    // the Natives table.  The associated C function is provided by a
    // table built in the bootstrap scripts, `Native_C_Funcs`.

    REBCTX *meta;
    REBFLGS flags = MKF_KEYWORDS | MKF_RETURN;
    REBARR *paramlist = Make_Paramlist_Managed_May_Fail(
        &meta,
        spec,
        &flags  // return type checked only in debug build
    );
    ASSERT_SERIES_TERM_IF_NEEDED(paramlist);

    // Natives are their own dispatchers; there is no wrapper added for cases
    // like `return: <void>` or `return: <none>`.  They must return a value
    // consistent with the response.  Make sure the typesets are right for the
    // debug build to check it.
    //
  #if !defined(NDEBUG)
    if (flags & MKF_IS_ELIDER) {
        assert(GET_PARAM_FLAG(cast(REBPAR*, ARR_AT(paramlist, 1)), ENDABLE));
        assert(Is_Typeset_Empty(cast(REBPAR*, ARR_AT(paramlist, 1))));
    }
    if (flags & MKF_HAS_OPAQUE_RETURN) {
        assert(NOT_PARAM_FLAG(cast(REBPAR*, ARR_AT(paramlist, 1)), ENDABLE));
        assert(Is_Typeset_Empty(cast(REBPAR*, ARR_AT(paramlist, 1))));
    }
  #endif

    REBACT *native = Make_Action(
        paramlist,
        dispatcher,  // "dispatcher" is unique to this "native"
        IDX_NATIVE_MAX  // details array capacity
    );
    SET_ACTION_FLAG(native, IS_NATIVE);

    REBARR *details = ACT_DETAILS(native);
    Init_Blank(ARR_AT(details, IDX_NATIVE_BODY));
    Copy_Cell(ARR_AT(details, IDX_NATIVE_CONTEXT), CTX_ARCHETYPE(module));

    // NATIVE-COMBINATORs actually aren't *quite* their own dispatchers, they
    // all share a common hook to help with tracing and doing things like
    // calculating the furthest amount of progress in the parse.  So we call
    // that the actual "native" in that case.
    //
    if (is_combinator) {
        REBACT *native_combinator = native;
        native = Make_Action(
            ACT_SPECIALTY(native_combinator),
            &Combinator_Dispatcher,
            2  // IDX_COMBINATOR_MAX  // details array capacity
        );

        Copy_Cell(
            ARR_AT(ACT_DETAILS(native), 1),  // IDX_COMBINATOR_BODY
            ACT_ARCHETYPE(native_combinator)
        );
    }

    // We want the meta information on the wrapped version if it's a
    // NATIVE-COMBINATOR.
    //
    assert(ACT_META(native) == nullptr);
    mutable_ACT_META(native) = meta;

    return native;
}


//
//  native: native [
//
//  {(Internal Function) Create a native, using compiled C code}
//
//      return: [action!]
//      spec [block!]
//      /combinator
//  ]
//
REBNATIVE(native)
{
    INCLUDE_PARAMS_OF_NATIVE;

    if (not PG_Next_Native_Dispatcher)
        fail ("NATIVE is for internal use during boot and extension loading");

    REBNAT dispatcher = *PG_Next_Native_Dispatcher;
    ++PG_Next_Native_Dispatcher;

    REBACT *native = Make_Native(
        ARG(spec),
        did REF(combinator),
        dispatcher,
        PG_Currently_Loading_Module
    );

    return Init_Action(D_OUT, native, ANONYMOUS, UNBOUND);
}


//
//  Init_Action_Meta_Shim: C
//
// Make_Paramlist_Managed_May_Fail() needs the object archetype ACTION-META
// from %sysobj.r, to have the keylist to use in generating the info used
// by HELP for the natives.  However, natives themselves are used in order
// to run the object construction in %sysobj.r
//
// To break this Catch-22, this code builds a field-compatible version of
// ACTION-META.  After %sysobj.r is loaded, an assert checks to make sure
// that this manual construction actually matches the definition in the file.
//
static void Init_Action_Meta_Shim(void) {
    SYMID field_syms[3] = {
        SYM_DESCRIPTION, SYM_PARAMETER_TYPES, SYM_PARAMETER_NOTES
    };
    REBCTX *meta = Alloc_Context_Core(REB_OBJECT, 4, NODE_FLAG_MANAGED);
    REBLEN i = 1;
    for (; i != 4; ++i)
        Init_Nulled(Append_Context(meta, nullptr, Canon(field_syms[i - 1])));

    Root_Action_Meta = Init_Object(Alloc_Value(), meta);
    Force_Value_Frozen_Deep(Root_Action_Meta);
}

static void Shutdown_Action_Meta_Shim(void) {
    rebRelease(Root_Action_Meta);
}


//
//  Startup_Natives: C
//
// Returns an array of words bound to natives for SYSTEM.CATALOG.NATIVES
//
REBARR *Startup_Natives(const REBVAL *boot_natives)
{
    REBARR *catalog = Make_Array(Num_Natives);
    REBCTX *lib = VAL_CONTEXT(Lib_Context);

    // Must be called before first use of Make_Paramlist_Managed_May_Fail()
    //
    Init_Action_Meta_Shim();

    assert(VAL_INDEX(boot_natives) == 0); // should be at head, sanity check
    const RELVAL *tail;
    RELVAL *item = VAL_ARRAY_KNOWN_MUTABLE_AT(&tail, boot_natives);
    assert(VAL_SPECIFIER(boot_natives) == SPECIFIED);

    // !!! We could avoid this by making NATIVE a specialization of a NATIVE*
    // function which carries those arguments, which would be cleaner.  The
    // C function could be passed as a HANDLE!.
    //
    assert(PG_Next_Native_Dispatcher == nullptr);
    PG_Next_Native_Dispatcher = Native_C_Funcs;
    assert(PG_Currently_Loading_Module == nullptr);
    PG_Currently_Loading_Module = lib;

    // Due to the recursive nature of `native: native [...]`, we can't actually
    // create NATIVE itself that way.  So the prep process should have moved
    // it to be the first native in the list, and we make it manually.
    //
    assert(IS_SET_WORD(item) and VAL_WORD_ID(item) == SYM_NATIVE);
    ++item;
    assert(IS_WORD(item) and VAL_WORD_ID(item) == SYM_NATIVE);
    ++item;
    assert(IS_BLOCK(item));
    REBVAL *spec = SPECIFIC(item);
    ++item;

    REBACT *the_native_action = Make_Native(
        spec,
        false,  // not a combinator
        *PG_Next_Native_Dispatcher,
        PG_Currently_Loading_Module
    );
    ++PG_Next_Native_Dispatcher;

    Init_Action(
        Append_Context(lib, nullptr, Canon(SYM_NATIVE)),
        the_native_action,
        Canon(SYM_NATIVE),  // label
        UNBOUND
    );

    assert(Native_Act(NATIVE) == the_native_action);

    // The current rule in "Sea of Words" is that all SET-WORD!s that are just
    // "attached" to a context can materialize variables.  It's not as safe
    // as something like JavaScript's strict mode...but rather than institute
    // some new policy we go with the somewhat laissez faire historical rule.
    //
    // *HOWEVER* the rule does not apply to Lib_Context.  You will get an
    // error if you try to assign to something attached to Lib before being
    // explicitly added.  So we have to go over the SET-WORD!s naming natives
    // (first one at time of writing is `api-transient: native [...]`) and
    // BIND/SET them.
    //
    Bind_Values_Set_Midstream_Shallow(item, tail, Lib_Context);

    DECLARE_LOCAL (skipped);
    Init_Any_Array_At(skipped, REB_BLOCK, VAL_ARRAY(boot_natives), 3);

    DECLARE_LOCAL (discarded);
    if (Do_Any_Array_At_Throws(discarded, skipped, SPECIFIED))
        panic (Error_No_Catch_For_Throw(discarded));

  #if !defined(NDEBUG)
    //
    // Ensure the evaluator called NATIVE as many times as we had natives,
    // and check that a couple of functions can be successfully looked up
    // by their symbol ID numbers.

    assert(PG_Next_Native_Dispatcher == Native_C_Funcs + Num_Natives);

    REBVAL *generic = MOD_VAR(lib, Canon(SYM_GENERIC), true);
    if (not IS_ACTION(generic))
        panic (generic);
    assert(Native_Act(GENERIC) == VAL_ACTION(generic));

    REBVAL *parse_reject = MOD_VAR(lib, Canon(SYM_PARSE_REJECT), true);
    if (not IS_ACTION(parse_reject))
        panic (parse_reject);
    assert(Native_Act(PARSE_REJECT) == VAL_ACTION(parse_reject));
  #endif

    assert(PG_Next_Native_Dispatcher == Native_C_Funcs + Num_Natives);

    PG_Next_Native_Dispatcher = nullptr;
    PG_Currently_Loading_Module = nullptr;

    return catalog;
}


//
//  Shutdown_Natives: C
//
// Being able to run Recycle() during the native startup process means being
// able to holistically check the system state.  This relies on initialized
// data in the natives table.  Since the interpreter can be shutdown and
// started back up in the same session, we can't rely on zero initialization
// for startups after the first, unless we manually null them out.
//
void Shutdown_Natives(void) {
    Shutdown_Action_Meta_Shim();
}
