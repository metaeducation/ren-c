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
// functions to call at all.  See %make-natives.r
//

#include "sys-core.h"


//
//  Extract_Intrinsic: C
//
Intrinsic* Extract_Intrinsic(Phase* phase)
{
    assert(ACT_DISPATCHER(phase) == &Intrinsic_Dispatcher);

    Details* details = Phase_Details(phase);
    assert(Array_Len(details) >= IDX_INTRINSIC_MAX);  // typecheck uses more

    Cell* handle = Details_At(details, IDX_INTRINSIC_CFUNC);
    return cast(Intrinsic*, VAL_HANDLE_CFUNC(handle));
}


//
//  Intrinsic_Dispatcher: C
//
// While frames aren't necessary to execute intrinsics, the system is able to
// run intrinsics using this thin wrapper of a dispatcher as if they were
// ordinary natives.
//
Bounce Intrinsic_Dispatcher(Level* const L)
{
    USE_LEVEL_SHORTHANDS (L);

    assert(ACT_HAS_RETURN(PHASE));
    Value(*) arg = Level_Arg(L, 2);  // skip the RETURN

    Intrinsic* intrinsic = Extract_Intrinsic(PHASE);
    (*intrinsic)(OUT, PHASE, arg);  // typechecking done when frame was built

    return OUT;
}


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
Phase* Make_Native(
    REBVAL *spec,
    NativeType native_type,
    CFunction* cfunc,  // may be Dispatcher*, may be Intrinsic*
    Context* module
){
    // There are implicit parameters to both NATIVE/COMBINATOR and usermode
    // COMBINATOR.  The native needs the full spec.
    //
    // !!! Note: This will manage the combinator's array.  Changing this would
    // need a version of Make_Paramlist_Managed() which took an array + index
    //
    DECLARE_STABLE (expanded_spec);
    if (native_type == NATIVE_COMBINATOR) {
        Init_Block(expanded_spec, Expanded_Combinator_Spec(spec));
        spec = expanded_spec;
    }

    // With the components extracted, generate the native and add it to
    // the Natives table.  The associated C function is provided by a
    // table built in the bootstrap scripts, `Native_C_Funcs`.

    Context* meta;
    Flags flags = MKF_RETURN;
    Array* paramlist = Make_Paramlist_Managed_May_Fail(
        &meta,
        spec,
        &flags  // return type checked only in debug build
    );
    Assert_Series_Term_If_Needed(paramlist);

    Phase* native;
    if (native_type == NATIVE_INTRINSIC) {
        native = Make_Action(
            paramlist,
            nullptr,  // no partials
            &Intrinsic_Dispatcher,
            IDX_INTRINSIC_MAX  // details array capacity
        );

        Details* details = Phase_Details(native);
        Init_Handle_Cfunc(Details_At(details, IDX_INTRINSIC_CFUNC), cfunc);
    }
    else {
        native = Make_Action(
            paramlist,
            nullptr,  // no partials
            cast(Dispatcher*, cfunc),  // dispatcher is unique to this native
            IDX_NATIVE_MAX  // details array capacity
        );

        Details* details = Phase_Details(native);

        Init_Blank(Details_At(details, IDX_NATIVE_BODY));
        Copy_Cell(
            Details_At(details, IDX_NATIVE_CONTEXT),
            CTX_ARCHETYPE(module)
        );

        Set_Action_Flag(native, IS_NATIVE);
    }

    // NATIVE-COMBINATORs actually aren't *quite* their own dispatchers, they
    // all share a common hook to help with tracing and doing things like
    // calculating the furthest amount of progress in the parse.  So we call
    // that the actual "native" in that case.
    //
    if (native_type == NATIVE_COMBINATOR) {
        Phase* native_combinator = native;
        native = Make_Action(
            ACT_PARAMLIST(native_combinator),
            nullptr,  // no partials
            &Combinator_Dispatcher,
            2  // IDX_COMBINATOR_MAX  // details array capacity
        );

        Copy_Cell(
            Array_At(Phase_Details(native), 1),  // IDX_COMBINATOR_BODY
            Phase_Archetype(native_combinator)
        );
    }

    // We want the meta information on the wrapped version if it's a
    // NATIVE-COMBINATOR.
    //
    assert(ACT_ADJUNCT(native) == nullptr);
    mutable_ACT_ADJUNCT(native) = meta;

    // Some features are not supported by intrinsics, because it would make
    // them too complicated.
    //
    if (native_type == NATIVE_INTRINSIC) {
        assert(ACT_NUM_PARAMS(native) == 2);  // return + 1 argument
        const Param* param = ACT_PARAM(native, 2);
        assert(Not_Parameter_Flag(param, REFINEMENT));
        assert(Not_Parameter_Flag(param, SKIPPABLE));
        assert(Not_Parameter_Flag(param, ENDABLE));
        UNUSED(param);
    }

    return native;
}


//
//  native: native [
//
//  {(Internal Function) Create a native, using compiled C code}
//
//      return: "Isotopic ACTION!"
//          [antiform!]  ; [action?] needs NATIVE to define it!
//      spec [block!]
//      /combinator "This native is an implementation of a PARSE keyword"
//      /intrinsic "This native can be called without building a frame"
//  ]
//
DECLARE_NATIVE(native)
{
    INCLUDE_PARAMS_OF_NATIVE;

    Value(*) spec = ARG(spec);

    if (REF(combinator) and REF(intrinsic))
        fail (Error_Bad_Refines_Raw());

    NativeType native_type = REF(combinator) ? NATIVE_COMBINATOR
        : REF(intrinsic) ? NATIVE_INTRINSIC
        : NATIVE_NORMAL;

    if (not PG_Next_Native_Cfunc)
        fail ("NATIVE is for internal use during boot and extension loading");

    CFunction* cfunc = *PG_Next_Native_Cfunc;
    ++PG_Next_Native_Cfunc;

    Phase* native = Make_Native(
        spec,
        native_type,
        cfunc,
        PG_Currently_Loading_Module
    );

    return Init_Action(OUT, native, ANONYMOUS, UNBOUND);
}


//
//  Init_Action_Adjunct_Shim: C
//
// Make_Paramlist_Managed_May_Fail() needs the object archetype ACTION-ADJUNCT
// from %sysobj.r, to have the keylist to use in generating the info used
// by HELP for the natives.  However, natives themselves are used in order
// to run the object construction in %sysobj.r
//
// To break this Catch-22, this code builds a field-compatible version of
// ACTION-ADJUNCT.  After %sysobj.r is loaded, an assert checks to make sure
// that this manual construction actually matches the definition in the file.
//
static void Init_Action_Adjunct_Shim(void) {
    SymId field_syms[1] = {
        SYM_DESCRIPTION
    };
    Context* adjunct = Alloc_Context_Core(REB_OBJECT, 2, NODE_FLAG_MANAGED);
    REBLEN i = 1;
    for (; i != 2; ++i)
        Init_Nulled(Append_Context(adjunct, Canon_Symbol(field_syms[i - 1])));

    Root_Action_Adjunct = Init_Object(Alloc_Value(), adjunct);
    Force_Value_Frozen_Deep(Root_Action_Adjunct);
}

static void Shutdown_Action_Adjunct_Shim(void) {
    rebRelease(Root_Action_Adjunct);
}


//
//  Startup_Natives: C
//
// Returns an array of words bound to natives for SYSTEM.CATALOG.NATIVES
//
Array* Startup_Natives(const Element* boot_natives)
{
    Array* catalog = Make_Array(Num_Natives);

    // Must be called before first use of Make_Paramlist_Managed_May_Fail()
    //
    Init_Action_Adjunct_Shim();

    assert(VAL_INDEX(boot_natives) == 0); // should be at head, sanity check
    const Element* tail;
    Element* item = Cell_Array_At_Known_Mutable(&tail, boot_natives);
    Specifier* specifier = Cell_Specifier(boot_natives);

    // !!! We could avoid this by making NATIVE a specialization of a NATIVE*
    // function which carries those arguments, which would be cleaner.  The
    // C function could be passed as a HANDLE!.
    //
    assert(PG_Next_Native_Cfunc == nullptr);
    PG_Next_Native_Cfunc = Native_C_Funcs;
    assert(PG_Currently_Loading_Module == nullptr);
    PG_Currently_Loading_Module = Lib_Context;

    // Due to the recursive nature of `native: native [...]`, we can't actually
    // create NATIVE itself that way.  So the prep process should have moved
    // it to be the first native in the list, and we make it manually.
    //
    assert(Is_Set_Word(item) and Cell_Word_Id(item) == SYM_NATIVE);
    ++item;
    assert(Is_Word(item) and Cell_Word_Id(item) == SYM_NATIVE);
    ++item;
    assert(Is_Block(item));
    DECLARE_STABLE (spec);
    Derelativize(spec, item, specifier);
    ++item;

    Phase* the_native_action = Make_Native(
        spec,
        NATIVE_NORMAL,  // not a combinator or intrinsic
        *PG_Next_Native_Cfunc,
        PG_Currently_Loading_Module
    );
    ++PG_Next_Native_Cfunc;

    Init_Action(
        Append_Context(Lib_Context, Canon(NATIVE)),
        the_native_action,
        Canon(NATIVE),  // label
        UNBOUND
    );

    assert(VAL_ACTION(Lib(NATIVE)) == the_native_action);

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
    Bind_Values_Set_Midstream_Shallow(item, tail, Lib_Context_Value);

    DECLARE_LOCAL (skipped);
    Init_Array_Cell_At(skipped, REB_BLOCK, Cell_Array(boot_natives), 3);

    DECLARE_LOCAL (discarded);
    if (Do_Any_Array_At_Throws(discarded, skipped, specifier))
        panic (Error_No_Catch_For_Throw(TOP_LEVEL));
    if (not Is_Anti_Word_With_Id(discarded, SYM_DONE))
        panic (discarded);

  #if !defined(NDEBUG)
    //
    // Ensure the evaluator called NATIVE as many times as we had natives,
    // and check that a couple of functions can be successfully looked up
    // by their symbol ID numbers.

    assert(PG_Next_Native_Cfunc == Native_C_Funcs + Num_Natives);

    if (not Is_Action(Lib(GENERIC)))
        panic (Lib(GENERIC));

    if (not Is_Action(Lib(PARSE_REJECT)))
        panic (Lib(PARSE_REJECT));
  #endif

    assert(PG_Next_Native_Cfunc == Native_C_Funcs + Num_Natives);

    PG_Next_Native_Cfunc = nullptr;
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
    Shutdown_Action_Adjunct_Shim();
}
