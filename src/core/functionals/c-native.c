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
// Copyright 2012-2025 Ren-C Open Source Contributors
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
//  Raw_Native_Details_Querier: C
//
// See DETAILS_FLAG_RAW_NATIVE for explanation of why raw natives do not
// have per-Dispatcher Details Queriers.
//
bool Raw_Native_Details_Querier(
    Sink(Value) out,
    Details* details,
    SymId property
){
    switch (property) {
      case SYM_RETURN_OF: {
        Value* param = Details_At(details, IDX_RAW_NATIVE_RETURN);
        assert(Is_Parameter(param));
        Copy_Cell(out, param);
        return true; }

      default:
        break;
    }

    return false;
}


//
//  Make_Native_Dispatch_Details: C
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
Details* Make_Native_Dispatch_Details(
    Element* spec,
    NativeType native_type,
    Dispatcher* dispatcher
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
        Tweak_Cell_Binding(expanded_spec, g_lib_context);
        spec = expanded_spec;
    }

    // With the components extracted, generate the native and add it to
    // the Natives table.  The associated C function is provided by a
    // table built in the bootstrap scripts, `g_core_native_dispatchers`.

    StackIndex base = TOP_INDEX;

    VarList* adjunct;
    ParamList* paramlist = Make_Paramlist_Managed_May_Fail(
        &adjunct,
        spec,
        MKF_DONT_POP_RETURN,  // we put it in Details, not ParamList
        SYM_RETURN  // native RETURN: types checked only if RUNTIME_CHECKS
    );
    Assert_Flex_Term_If_Needed(paramlist);

    Flags details_flags = (
        DETAILS_FLAG_RAW_NATIVE
            | DETAILS_FLAG_API_CONTINUATIONS_OK
            | DETAILS_FLAG_OWNS_PARAMLIST);

    if (native_type == NATIVE_INTRINSIC)
        details_flags |= DETAILS_FLAG_CAN_DISPATCH_AS_INTRINSIC;

    Details* details = Make_Dispatch_Details(
        details_flags,
        Phase_Archetype(paramlist),
        dispatcher,  // dispatcher is unique to this native
        MAX_IDX_RAW_NATIVE  // details array capacity
    );

    Pop_Unpopped_Return(Details_At(details, IDX_RAW_NATIVE_RETURN), base);

    // NATIVE-COMBINATORs actually aren't *quite* their own dispatchers, they
    // all share a common hook to help with tracing and doing things like
    // calculating the furthest amount of progress in the parse.  So we call
    // that the actual "native" in that case.
    //
    if (native_type == NATIVE_COMBINATOR) {
        DECLARE_ELEMENT (native);
        Init_Frame(native, details, ANONYMOUS, NONMETHOD);
        details = Make_Dispatch_Details(
            DETAILS_MASK_NONE,  // *not* a native, calls one...
            native,
            &Combinator_Dispatcher,
            MAX_IDX_COMBINATOR  // details array capacity
        );

        // !!! Not strictly needed, as it's available as Details[0]
        // However, there's a non-native form of combinator as well, which
        // puts a body block in the slot.
        //
        Copy_Cell(Details_At(details, IDX_COMBINATOR_BODY), native);
    }

    // We want the meta information on the wrapped version if it's a
    // NATIVE-COMBINATOR.
    //
    assert(Misc_Phase_Adjunct(details) == nullptr);
    Tweak_Misc_Phase_Adjunct(details, adjunct);

    // Some features are not supported by intrinsics on their first argument,
    // because it would make them too complicated.
    //
    if (native_type == NATIVE_INTRINSIC) {
        const Param* param = Phase_Param(details, 1);
        assert(Not_Parameter_Flag(param, REFINEMENT));
        assert(Not_Parameter_Flag(param, ENDABLE));
        UNUSED(param);
    }

    return details;
}


//
//  /native: native [
//
//  "(Internal Function) Create a native, using compiled C code"
//
//      return: [action!]
//      spec [block!]
//      :combinator "This native is an implementation of a PARSE keyword"
//      :intrinsic "This native can be called without building a frame"
//      :generic "This native delegates to type-specific code"
//  ]
//
DECLARE_NATIVE(NATIVE)
{
    INCLUDE_PARAMS_OF_NATIVE;

    UNUSED(ARG(GENERIC));  // only heeded by %make-natives.r to make tables

    if (not g_native_cfunc_pos)
        return FAIL(
            "NATIVE is for internal use during boot and extension loading"
        );

    Element* spec = Element_ARG(SPEC);

    if (REF(COMBINATOR) and REF(INTRINSIC))
        return FAIL(Error_Bad_Refines_Raw());

    NativeType native_type = REF(COMBINATOR) ? NATIVE_COMBINATOR
        : REF(INTRINSIC) ? NATIVE_INTRINSIC
        : NATIVE_NORMAL;

    CFunction* cfunc = *g_native_cfunc_pos;
    ++g_native_cfunc_pos;

    if (g_current_uses_librebol) {
        UNUSED(native_type);  // !!! no :INTRINSIC, but what about :COMBINATOR?
        Value* action = rebFunctionCore(
            cast(RebolContext*, g_currently_loading_module),
            spec,
            cast(RebolActionCFunction*, cfunc)
        );
        Copy_Cell(OUT, action);
        rebRelease(action);
    }
    else {
        Details* details = Make_Native_Dispatch_Details(
            spec,
            native_type,
            cast(Dispatcher*, cfunc)
        );
        Init_Action(OUT, details, ANONYMOUS, UNBOUND);
    }

    return OUT;
}


//
//  Dispatch_Generic_Core: C
//
// When you define a native as `native:generic`, this means you can register
// hooks for that native based on a type with IMPLEMENT_GENERIC().  In the
// generic native's implementation you choose when to actually dispatch on
// that type, and you can implement a generic for a single type like INTEGER!
// or for builtin typesets like ANY-LIST?.
//
// 1. Generally speaking, generics (and most functions in the system) do
//    not work on antiforms, quasiforms, or quoted datatypes.
//
//    For one thing, this would introduce uncomfortable questions, like:
//    should the NEXT of ''[a b c] be [b c] or ''[b c] ?  This would take the
//    already staggering combinatorics of the system up a notch by forcing
//    "quote propagation" policies to be injected everywhere.
//
//    Yet there's another danger: if quoted/quasi items wind up giving an
//    answer instead of an error for lots of functions, this will lead to
//    carelessness in propagation of the marks...not stripping them off when
//    they aren't needed.  This would lead to an undisciplined hodgepodge of
//    marks that are effectively meaningless.  In addition to being ugly, that
//    limits the potential for using the marks intentionally in a dialect
//    later, if you're beholden to treating leaky quotes and quasis as if
//    they were not there.
//
// 2. R3-Alpha PORT! really baked in the concept of the switch()-based
//    dispatch, and an "actor" model depending on it.  It's going to take
//    a bit longer to break it out of that idea.  Bridge for the meantime
//    to translate new calls into old calls using the passed-in SymId.
//
bool Try_Dispatch_Generic_Core(
    Sink(Bounce) bounce,
    SymId symid,
    GenericTable* table,
    Heart heart,  // no quoted/quasi/anti [1]
    Level* const L
){
    if (heart == TYPE_PORT and symid != SYM_OLDGENERIC) {  // !!! Legacy [2]
        switch (symid) {  // exempt port's IMPLEMENT_GENERIC() cases
          case SYM_MAKE:
          case SYM_EQUAL_Q:
          case SYM_PICK:
          case SYM_POKE:
            break;  // fall through to modern dispatch

          default:
            L->u.action.label = Canon_Symbol(symid);  // !!! Level_Verb() hack
            *bounce = GENERIC_CFUNC(OLDGENERIC, Is_Port)(L);
            return true;
        }
    }

    Dispatcher* dispatcher = maybe Try_Get_Generic_Dispatcher(table, heart);
    if (not dispatcher)
        return false;  // not handled--some clients want to try more things

    *bounce = (*dispatcher)(L);
    return true;  // handled, even if it threw
}


//
//   Delegate_Operation_With_Part: C
//
// There's a common pattern in functions like REVERSE-OF or APPEND-OF which
// is that they're willing to run on immutable types, but delegate to running
// on a copy of the data aliased as a mutable type.
//
// It's easiest to build that pattern on top of functions that exist, as there
// isn't a strong need to write error-prone "efficient" code to do it.
//
// 1. To speed up slightly, callers are expected to quote (or metaquote) the
//    cells so they can be passed to the API without rebQ() calls.
//
Bounce Delegate_Operation_With_Part(
    SymId operation,
    SymId delegate,
    // arguments are passed as quoted/meta [1]
    const Element* meta_datatype,
    const Element* quoted_element,
    const Element* meta_part
){
    assert(delegate == SYM_TEXT_X or delegate == SYM_BLOCK_X);

    assert(Any_Metaform(meta_datatype));  // note: likely only quasiform soon
    assert(Is_Quoted(quoted_element));
    assert(Any_Metaform(meta_part));

    return rebDelegate(
        CANON(AS), meta_datatype, Canon_Symbol(operation),
            CANON(COPY), CANON(_S_S), "[",
                CANON(AS), Canon_Symbol(delegate), quoted_element,
                ":part", meta_part,
            "]"
    );
}


//
//  /oldgeneric: native:generic [
//
//  "Generic aggregator for the old-style generic dispatch"
//
//      return: [~] "Not actually used"
//  ]
//
DECLARE_NATIVE(OLDGENERIC)
{
    INCLUDE_PARAMS_OF_OLDGENERIC;

    return FAIL("This should never be called");
}


//
//   Run_Generic_Dispatch: C
//
// !!! Old concept of generics, based on each type directing to a single
// function with a big switch() statement in it.
//
Bounce Run_Generic_Dispatch(
    const Element* cue,
    Level* L,
    const Symbol* verb
){
    L->u.action.label = verb;  // !!! hack for Level_Verb() for now
    return Dispatch_Generic(OLDGENERIC, cue, L);
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
    VarList* adjunct = Alloc_Varlist_Core(NODE_FLAG_MANAGED, TYPE_OBJECT, 2);
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
    assert(Cell_Binding(boot_natives) == UNBOUND);

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
    assert(g_native_cfunc_pos == nullptr);
    g_native_cfunc_pos = cast(CFunction* const*, g_core_native_dispatchers);
    assert(g_currently_loading_module == nullptr);
    g_currently_loading_module = g_lib_context;

    g_current_uses_librebol = false;  // raw natives don't use librebol

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

    Details* the_native_details = Make_Native_Dispatch_Details(
        spec,
        NATIVE_NORMAL,  // not a combinator or intrinsic
        cast(Dispatcher*, *g_native_cfunc_pos)
    );
    ++g_native_cfunc_pos;

    Init_Action(
        Sink_Lib_Var(SYM_NATIVE),
        the_native_details,
        CANON(NATIVE),  // label
        UNBOUND  // coupling
    );

    assert(Cell_Frame_Phase(LIB(NATIVE)) == the_native_details);

    DECLARE_ATOM (skipped);
    Init_Any_List_At(skipped, TYPE_BLOCK, Cell_Array(boot_natives), 3);

    DECLARE_ATOM (discarded);
    if (Eval_Any_List_At_Throws(discarded, skipped, lib))
        panic (Error_No_Catch_For_Throw(TOP_LEVEL));
    if (not Is_Quasi_Word_With_Id(Decay_If_Unstable(discarded), SYM_END))
        panic (discarded);

    assert(
        g_native_cfunc_pos
        == (
            cast(CFunction* const*, g_core_native_dispatchers)
            + g_num_core_natives
        )
    );

    g_native_cfunc_pos = nullptr;
    g_currently_loading_module = nullptr;

  #if RUNTIME_CHECKS  // ensure a couple of functions can be looked up by ID
    if (not Is_Action(LIB(FOR_EACH)))
        panic (LIB(FOR_EACH));

    if (not Is_Action(LIB(PARSE_REJECT)))
        panic (LIB(PARSE_REJECT));

    Count num_append_args = Phase_Num_Params(Cell_Frame_Phase(LIB(APPEND)));
    assert(num_append_args == Phase_Num_Params(Cell_Frame_Phase(LIB(INSERT))));
    assert(num_append_args == Phase_Num_Params(Cell_Frame_Phase(LIB(CHANGE))));

    Count num_find_args = Phase_Num_Params(Cell_Frame_Phase(LIB(FIND)));
    assert(num_find_args == Phase_Num_Params(Cell_Frame_Phase(LIB(SELECT))));
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
