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
//  Make_Native: C
//
// Reused function in Startup_Natives() as well as extensions loading natives,
// which can be parameterized with a different context in which to look up
// bindings by deafault in the API when that native is on the stack.
//
// Entries look like:
//
//    /some-name: native [spec content]
//
// It is optional to put INFIX between the assignment and NATIVE.
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
    Element* spec,
    NativeType native_type,
    Dispatcher* dispatcher,
    VarList* module
){
    // There are implicit parameters to both NATIVE:COMBINATOR and usermode
    // COMBINATOR.  The native needs the full spec.
    //
    // !!! Note: This will manage the combinator's array.  Changing this would
    // need a version of Make_Paramlist_Managed() which took an array + index
    //
    DECLARE_ELEMENT (expanded_spec);
    if (native_type == NATIVE_COMBINATOR) {
        Init_Block(expanded_spec, Expanded_Combinator_Spec(spec));
        BINDING(expanded_spec) = g_lib_context;
        spec = expanded_spec;
    }

    // With the components extracted, generate the native and add it to
    // the Natives table.  The associated C function is provided by a
    // table built in the bootstrap scripts, `g_core_native_dispatchers`.

    VarList* meta;
    Flags flags = MKF_RETURN;
    Array* paramlist = Make_Paramlist_Managed_May_Fail(
        &meta,
        spec,
        &flags  // native return types checked only if RUNTIME_CHECKS
    );
    Assert_Flex_Term_If_Needed(paramlist);

    Phase* native = Make_Phase(
        paramlist,
        nullptr,  // no partials
        dispatcher,  // dispatcher is unique to this native
        IDX_NATIVE_MAX  // details array capacity
    );

    Details* details = Phase_Details(native);

    Copy_Cell(
        Details_At(details, IDX_NATIVE_CONTEXT),
        Varlist_Archetype(module)
    );

    Set_Phase_Flag(native, IS_NATIVE);

    // NATIVE-COMBINATORs actually aren't *quite* their own dispatchers, they
    // all share a common hook to help with tracing and doing things like
    // calculating the furthest amount of progress in the parse.  So we call
    // that the actual "native" in that case.
    //
    if (native_type == NATIVE_COMBINATOR) {
        Phase* native_combinator = native;
        native = Make_Phase(
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
    Tweak_Action_Adjunct(native, meta);

    // Some features are not supported by intrinsics on their first argument,
    // because it would make them too complicated.
    //
    if (native_type == NATIVE_INTRINSIC) {
        const Param* param = ACT_PARAM(native, 2);
        assert(Not_Parameter_Flag(param, REFINEMENT));
        assert(Not_Parameter_Flag(param, ENDABLE));
        UNUSED(param);

        Set_Phase_Flag(native, CAN_DISPATCH_AS_INTRINSIC);
    }

    return native;
}


//
//  /native: native [
//
//  "(Internal Function) Create a native, using compiled C code"
//
//      return: [antiform!]  ; [action?] needs NATIVE to define it!
//      spec [block!]
//      :combinator "This native is an implementation of a PARSE keyword"
//      :intrinsic "This native can be called without building a frame"
//      :generic "This native delegates to type-specific code"
//  ]
//
DECLARE_NATIVE(native)
{
    INCLUDE_PARAMS_OF_NATIVE;

    UNUSED(ARG(generic));  // commentary only, at this time

    if (not g_native_dispatcher_pos)
        return FAIL(
            "NATIVE is for internal use during boot and extension loading"
        );

    Element* spec = cast(Element*, ARG(spec));

    if (REF(combinator) and REF(intrinsic))
        return FAIL(Error_Bad_Refines_Raw());

    NativeType native_type = REF(combinator) ? NATIVE_COMBINATOR
        : REF(intrinsic) ? NATIVE_INTRINSIC
        : NATIVE_NORMAL;

    Dispatcher* dispatcher = *g_native_dispatcher_pos;
    ++g_native_dispatcher_pos;

    Phase* native = Make_Native(
        spec,
        native_type,
        dispatcher,
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
    VarList* adjunct = Alloc_Varlist_Core(NODE_FLAG_MANAGED, REB_OBJECT, 2);
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
// 1. See Startup_Lib() for how all the declarations in LIB for the natives
//    are made in a pre-pass (no need to walk and look for set-words etc.)
//
Source* Startup_Natives(const Element* boot_natives)
{
    Context* lib = g_lib_context;  // native variables already exist [1]

    assert(VAL_INDEX(boot_natives) == 0);  // should be at head, sanity check
    assert(BINDING(boot_natives) == UNBOUND);

    Source* catalog = Make_Source(g_num_core_natives);

    // Must be called before first use of Make_Paramlist_Managed_May_Fail()
    //
    Init_Action_Adjunct_Shim();

    const Element* tail;
    Element* at = Cell_List_At_Known_Mutable(&tail, boot_natives);

    // !!! We could avoid this by making NATIVE a specialization of a NATIVE*
    // function which carries those arguments, which would be cleaner.  The
    // C function could be passed as a HANDLE!.
    //
    assert(g_native_dispatcher_pos == nullptr);
    g_native_dispatcher_pos = g_core_native_dispatchers;
    assert(PG_Currently_Loading_Module == nullptr);
    PG_Currently_Loading_Module = g_lib_context;

    // Due to the bootstrapping of `/native: native [...]`, we can't actually
    // create NATIVE itself that way.  So the prep process should have moved
    // it to be the first native in the list, and we make it manually.
    //
    assert(
        Symbol_Id(unwrap Try_Get_Settable_Word_Symbol(nullptr, at))
        == SYM_NATIVE
    );
    ++at;
    assert(Is_Word(at) and Cell_Word_Id(at) == SYM_NATIVE);
    ++at;
    assert(Is_Block(at));
    DECLARE_ELEMENT (spec);
    Derelativize(spec, at, lib);
    ++at;

    Phase* the_native_action = Make_Native(
        spec,
        NATIVE_NORMAL,  // not a combinator or intrinsic
        *g_native_dispatcher_pos,
        PG_Currently_Loading_Module
    );
    ++g_native_dispatcher_pos;

    Init_Action(
        Sink_Lib_Var(SYM_NATIVE),
        the_native_action,
        CANON(NATIVE),  // label
        UNBOUND  // coupling
    );

    assert(VAL_ACTION(LIB(NATIVE)) == the_native_action);

    DECLARE_ATOM (skipped);
    Init_Any_List_At(skipped, REB_BLOCK, Cell_Array(boot_natives), 3);

    DECLARE_ATOM (discarded);
    if (Eval_Any_List_At_Throws(discarded, skipped, lib))
        panic (Error_No_Catch_For_Throw(TOP_LEVEL));
    if (not Is_Quasi_Word_With_Id(Decay_If_Unstable(discarded), SYM_END))
        panic (discarded);

    assert(
        g_native_dispatcher_pos
        == g_core_native_dispatchers + g_num_core_natives
    );

    g_native_dispatcher_pos = nullptr;
    PG_Currently_Loading_Module = nullptr;

  #if RUNTIME_CHECKS  // ensure a couple of functions can be looked up by ID
    if (not Is_Action(LIB(FOR_EACH)))
        panic (LIB(FOR_EACH));

    if (not Is_Action(LIB(PARSE_REJECT)))
        panic (LIB(PARSE_REJECT));

    Count num_append_args = ACT_NUM_PARAMS(VAL_ACTION(LIB(APPEND)));
    assert(num_append_args == ACT_NUM_PARAMS(VAL_ACTION(LIB(INSERT))));
    assert(num_append_args == ACT_NUM_PARAMS(VAL_ACTION(LIB(CHANGE))));

    Count num_find_args = ACT_NUM_PARAMS(VAL_ACTION(LIB(FIND)));
    assert(num_find_args == ACT_NUM_PARAMS(VAL_ACTION(LIB(SELECT))));
  #endif

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
