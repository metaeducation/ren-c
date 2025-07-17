//
//  file: %c-native.c
//  summary: "Function that executes implementation as native code"
//  section: datatypes
//  project: "Ren-C Language Interpreter and Run-time Environment"
//  homepage: https://github.com/metaeducation/ren-c/
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
//    some-name: native [spec content]
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
Result(Details*) Make_Native_Dispatch_Details(
    Element* spec,
    NativeType native_type,
    Dispatcher* dispatcher
){
    // There are implicit parameters to both NATIVE:COMBINATOR and usermode
    // COMBINATOR.  The native needs the full spec.
    //
    // !!! Note: Will manage the combinator's array.  Changing this would need
    // a version of Make_Paramlist_Managed() which took an array + index
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
    require (
      ParamList* paramlist = Make_Paramlist_Managed(
        &adjunct,
        spec,
        MKF_DONT_POP_RETURN,  // we put it in Details, not ParamList
        SYM_RETURN  // native RETURN: types checked only if RUNTIME_CHECKS
    ));

    Assert_Flex_Term_If_Needed(Varlist_Array(paramlist));

    Flags details_flags = (
        BASE_FLAG_MANAGED
            | DETAILS_FLAG_RAW_NATIVE
            | DETAILS_FLAG_API_CONTINUATIONS_OK
            | DETAILS_FLAG_OWNS_PARAMLIST
    );

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
            BASE_FLAG_MANAGED,  // *not* a native, calls one...
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

    // We want the adjunct information on the wrapped version if it's a
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


// The NATIVE native has two entry points: one with no typechecking (used in
// bootstrapping, when TWEAK* is not available to look up the words in the
// type specs) and another that is overwritten into that slot after the
// boot is complete.
//
// (TWEAK* also has this dual nature.)
//
static Bounce Native_Native_Core(Level* level_)
{
    INCLUDE_PARAMS_OF_NATIVE;

    UNUSED(ARG(GENERIC));  // only heeded by %make-natives.r to make tables

    if (not g_native_cfunc_pos)
        panic (
            "NATIVE is for internal use during boot and extension loading"
        );

    Element* spec = Element_ARG(SPEC);

    if (Bool_ARG(COMBINATOR) and Bool_ARG(INTRINSIC))
        panic (Error_Bad_Refines_Raw());

    NativeType native_type = Bool_ARG(COMBINATOR) ? NATIVE_COMBINATOR
        : Bool_ARG(INTRINSIC) ? NATIVE_INTRINSIC
        : NATIVE_NORMAL;

    CFunction* cfunc = *g_native_cfunc_pos;
    ++g_native_cfunc_pos;

    if (g_current_uses_librebol) {
        UNUSED(native_type);  // !!! no :INTRINSIC, but what about :COMBINATOR?
        Value* action = rebFunctionCore(
            cast(RebolContext*, g_currently_loading_module),
            spec,
            f_cast(RebolActionCFunction*, cfunc)
        );
        Copy_Cell(OUT, action);
        rebRelease(action);
    }
    else {
        require (
          Details* details = Make_Native_Dispatch_Details(
            spec,
            native_type,
            f_cast(Dispatcher*, cfunc)
        ));

        Init_Action(OUT, details, ANONYMOUS, NONMETHOD);
    }

    return UNSURPRISING(OUT);
}


//
//  native: native [
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
    return Native_Native_Core(LEVEL);
}


//
//  native-bootstrap: native [
//
//  "(Bootstrap Variation) Version of NATIVE with no typechecking"
//
//      spec
//      :combinator
//      :intrinsic
//      :generic
//  ]
//
DECLARE_NATIVE(NATIVE_BOOTSTRAP)
{
    return Native_Native_Core(LEVEL);
}


//
//  Register_Generics: C
//
// This is called from the extension's startup function, and it registers
// the generics that have IMPLEMENT_GENERIC() from that extension.
//
void Register_Generics(const ExtraGenericTable* generics)
{
    const ExtraGenericTable* entry = generics;
    for (; entry->table != nullptr; ++entry) {
        assert(entry->ext_info->ext_heart == nullptr);
        entry->ext_info->ext_heart = Datatype_Extra_Heart(
            *entry->datatype_ptr
        );

        assert(entry->ext_info->next == nullptr);
        entry->ext_info->next = entry->table->ext_info;
        entry->table->ext_info = entry->ext_info;
        assert(entry->ext_info->next != entry->ext_info);
    }
    assert(entry->ext_info == nullptr);
    assert(entry->datatype_ptr == nullptr);
}


//
//  Unregister_Generics: C
//
// 1. We want to make it possible to cleanly Unregister_Generics() and then
//    call Register_Generics() again.  So we have to return ExtraGenericInfo
//    to its default state that we assert() on... that the ext_heart is null
//    and the ->next is null.
//
void Unregister_Generics(const ExtraGenericTable* generics)
{
    const ExtraGenericTable* entry = generics;
    for (; entry->table != nullptr; ++entry) {
        assert(entry->ext_info->ext_heart == Datatype_Extra_Heart(
            *entry->datatype_ptr
        ));
        assert(Stub_Flavor(entry->ext_info->ext_heart) == FLAVOR_PATCH);
        entry->ext_info->ext_heart = nullptr;  // null out datatype [1]

        ExtraGenericInfo* seek = entry->table->ext_info;
        if (seek == nullptr) {
            assert(false);
            panic ("Unregister_Generics: no ext_info in table");
        }
        if (seek == entry->ext_info)  // have to update list head
            entry->table->ext_info = seek->next;
        else
            while (seek != entry->ext_info) {
                if (seek->next == entry->ext_info) {
                    seek->next = seek->next->next;
                    break;
                }
                seek = seek->next;
                if (seek == nullptr) {
                    assert(false);
                    panic ("Unregister_Generics: ext_info not found");
                }
            }
        entry->ext_info->next = nullptr;  // null out list link [1]
    }
    assert(entry->ext_info == nullptr);
    assert(entry->datatype_ptr == nullptr);
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
    const GenericTable* table,
    const Value* datatype,  // no quoted/quasi/anti [1]
    Level* const L
){
    Option(Heart) heart = Datatype_Heart(datatype);
    if (not heart) {
        ExtraGenericInfo* ext_info = table->ext_info;
        const ExtraHeart* ext_heart = Datatype_Extra_Heart(datatype);
        while (ext_info) {
            if (ext_info->ext_heart == ext_heart) {
                L->u.action.label = Canon_Symbol(symid);  // !!! Level_Verb()
                *bounce = Apply_Cfunc(ext_info->dispatcher, L);
                return true;
            }
            ext_info = ext_info->next;
        }

        UNUSED(ext_heart);  // should check extension generics
        NOOP;  // fall through to default for ANY-ELEMENT?, ANY-FUNDAMENTAL?
    }
    else if (
        heart == TYPE_PORT
        and symid != SYM_OLDGENERIC  // !!! legacy generics for port [2]
    ){
        // !!! skip bad port check for now
        /*if (symid != SYM_MAKE) {
            VarList* ctx = Cell_Varlist(Level_Arg(L, 1));
            if (
                Varlist_Len(ctx) < MAX_STD_PORT
                or not Is_Object(Slot_Hack(Varlist_Slot(ctx, STD_PORT_SPEC)))
            ){
                *bounce = Native_Fail_Result(L, Error_Invalid_Port_Raw());
            }  // "old check" for invalid port
        }*/

        switch (symid) {  // exempt port's IMPLEMENT_GENERIC() cases
          case SYM_MAKE:
          case SYM_EQUAL_Q:
          case SYM_TWEAK_P:
          case SYM_MOLDIFY:
            break;  // fall through to modern dispatch

          default:
            L->u.action.label = Canon_Symbol(symid);  // !!! Level_Verb() hack
            *bounce = GENERIC_CFUNC(OLDGENERIC, Is_Port)(L);
            return true;
        }
    }

    Option(Dispatcher*) dispatcher = Get_Builtin_Generic_Dispatcher(
        table,
        heart  // maybe fallthrough of TYPE_0 for ELEMENT?/FUNDAMENTAL?
    );
    if (not dispatcher)
        return false;  // not handled--some clients want to try more things

    *bounce = Apply_Cfunc(unwrap dispatcher, L);
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
// 1. To speed up slightly, callers are expected to quote (or lift) the
//    cells so they can be passed to the API without rebQ() calls.
//
Bounce Delegate_Operation_With_Part(
    SymId operation,
    SymId delegate,
    // arguments are passed as quoted/lifted [1]
    const Element* lifted_datatype,
    const Element* quoted_element,
    const Element* lifted_part
){
    assert(delegate == SYM_TEXT_X or delegate == SYM_BLOCK_X);

    assert(Any_Lifted(lifted_datatype));  // note: likely only quasiform soon
    assert(Is_Quoted(quoted_element));
    assert(Any_Lifted(lifted_part));

    return rebDelegate(
        CANON(AS), lifted_datatype, Canon_Symbol(operation),
            CANON(COPY), CANON(_S_S), "[",
                CANON(AS), Canon_Symbol(delegate), quoted_element,
                ":part", lifted_part,
            "]"
    );
}


//
//  oldgeneric: native:generic [
//
//  "Generic aggregator for the old-style generic dispatch"
//
//      return: [] "Not actually used"
//  ]
//
DECLARE_NATIVE(OLDGENERIC)
{
    INCLUDE_PARAMS_OF_OLDGENERIC;

    panic ("This should never be called");
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
//  Startup_Action_Adjunct_Shim: C
//
// Make_Paramlist_Managed() needs the object archetype ACTION-ADJUNCT from
// %sysobj.r, to have the keylist to use in generating the info used by HELP
// for the natives.  However, natives themselves are used in order to run the
// object construction in %sysobj.r
//
// To break this Catch-22, this code builds a field-compatible version of
// ACTION-ADJUNCT.  After %sysobj.r is loaded, an assert checks to make sure
// that this manual construction actually matches the definition in the file.
//
void Startup_Action_Adjunct_Shim(void) {
    SymId field_syms[1] = {
        SYM_DESCRIPTION
    };
    VarList* adjunct = Alloc_Varlist_Core(BASE_FLAG_MANAGED, TYPE_OBJECT, 2);
    REBLEN i = 1;
    for (; i != 2; ++i)
        Init_Nulled(Append_Context(adjunct, Canon_Symbol(field_syms[i - 1])));

    Root_Action_Adjunct = Init_Object(Alloc_Value(), adjunct);
    Force_Value_Frozen_Deep(Root_Action_Adjunct);
}

//
//  Shutdown_Action_Adjunct_Shim: C
//
void Shutdown_Action_Adjunct_Shim(void) {
    rebRelease(Root_Action_Adjunct);
}


// Create a native in the library without using the evaluator.
//
// 1. Used with `native:` and `tweak*`
//
static void Make_Native_In_Lib_By_Hand(Level* L, SymId id)
{
    assert(Is_Set_Word(At_Level(L)));  // limited set [1]
    assert(
        Symbol_Id(unwrap Try_Get_Settable_Word_Symbol(nullptr, At_Level(L)))
        == id
    );
    Fetch_Next_In_Feed(L->feed);

    NativeType native_type;
    if (id == SYM_TWEAK_P_BOOTSTRAP) {
        id = SYM_TWEAK_P;  // update the ID we write to
        assert(Is_Word(At_Level(L)));  // native [...]
        assert(Word_Id(At_Level(L)) == SYM_NATIVE);  // native [...]
        native_type = NATIVE_NORMAL;  // genericness only in prep for TWEAK*
    }
    else {
        assert(id == SYM_NATIVE_BOOTSTRAP);
        id = SYM_NATIVE;  // update the ID we write to
        assert(Is_Word(At_Level(L)));
        assert(Word_Id(At_Level(L)) == SYM_NATIVE);  // native [...]
        native_type = NATIVE_NORMAL;
    }

    Fetch_Next_In_Feed(L->feed);

    assert(Is_Block(At_Level(L)));
    DECLARE_ELEMENT (spec);
    Derelativize(spec, At_Level(L), g_lib_context);
    Fetch_Next_In_Feed(L->feed);;

    assume (
      Details* details = Make_Native_Dispatch_Details(
        spec,
        native_type,
        f_cast(Dispatcher*, *g_native_cfunc_pos)
    ));

    ++g_native_cfunc_pos;

    Init_Action(
        Sink_Lib_Var(id),
        details,
        Canon_Symbol(id),  // label
        NONMETHOD  // coupling
    );

    assert(cast(Details*, Frame_Phase(Lib_Var(id))) == details);  // make sure
}


//
//  Startup_Natives: C
//
// This is a special bootstrapping step that makes the native functions.  The
// native for NATIVE itself can clearly not be made by means of calling itself
// e.g. (native: native ["A function that creates natives" ...]), so of course
// that has to be manually constructed.
//
// But further than that, we also need to do a manual construction of the
// TWEAK* native, e.g. (tweak*: native ["A function that tweaks" ...]) would
// have trouble since TWEAK* is used to implement SET-WORD assignments in the
// general case.  Hence we write into the library slots directly just to
// get the ball rolling.
//
// 1. See Startup_Lib() for how all the declarations in LIB for the natives
//    are made in a pre-pass (no need to walk and look for set-words etc.)
//
void Startup_Natives(const Element* boot_natives)
{
    Context* lib = g_lib_context;  // native variables already exist [1]

    assert(Series_Index(boot_natives) == 0);  // should be head, sanity check
    assert(Cell_Binding(boot_natives) == UNBOUND);

    DECLARE_ATOM (dual_step);
    require (
      Level* L = Make_Level_At_Core(
        &Evaluator_Executor, boot_natives, lib, LEVEL_MASK_NONE
    ));
    Init_Void(Evaluator_Primed_Cell(L));
    Push_Level_Erase_Out_If_State_0(dual_step, L);

  setup_native_dispatcher_enumeration: {

    // When we call NATIVE it doesn't take any parameter to say what the
    // Dispatcher* is.  It's assumed there is some global state, which it
    // advances and gets the next Dispatcher* from that global state.
    //
    // This isn't particularly elegant, but something is going to be weird
    // about it one way or another. Fabricating a HANDLE! to pass to NATIVE as
    // a second parameter would be a possibility, but that comes with its own
    // set of problems...like how to inject that HANDLE! into the stream
    // of evaluation when all we have are (foo: native [...]) statements.

    assert(g_native_cfunc_pos == nullptr);
    g_native_cfunc_pos = u_cast(
        CFunction* const*,
        &g_core_native_dispatchers[0]
    );
    assert(g_currently_loading_module == nullptr);
    g_currently_loading_module = g_lib_context;

    g_current_uses_librebol = false;  // raw natives don't use librebol

} make_bedrock_natives_by_hand: {

    // Eval can't run `native: native [...]` or `tweak*: native [...]`
    //
    // TWEAK* is fundamental to interpreter operation for SET and GET
    // operations.  NATIVE is fundamental to making natives themselves.
    //
    // So they're pushed up to the front of the boot block and "made by hand",
    // e.g. not by running evaluation.  (This reordering to put them at the
    // head is done in %make-natives.r)

    Make_Native_In_Lib_By_Hand(L, SYM_NATIVE_BOOTSTRAP);
    Make_Native_In_Lib_By_Hand(L, SYM_TWEAK_P_BOOTSTRAP);

} make_other_natives: {

    // For the rest of the natives, we can actually use the evaluator to
    // execute the NATIVE invocations.  This defers to refinement processing
    // of chains to handle things like NATIVE:COMBINATOR and NATIVE:INTRINSIC.
    // It also means natives could take more arguments if they wanted to,
    // without having to write special code to handle it.

    bool threw = Trampoline_With_Top_As_Root_Throws();
    assert(not threw);
    UNUSED(threw);

    assert(Is_Okay(Known_Stable(L->out)));

} forget_bootstrap_native_and_tweak: {

    // During the make_other_natives evaluation, the NATIVE and TWEAK* lib
    // variables were overwritten with the typechecked versions.  Get rid of
    // the bootstrap versions that were made by hand, so that they don't
    // cause confusion.

    Init_Tripwire(Sink_Lib_Var(SYM_NATIVE_BOOTSTRAP));
    Init_Tripwire(Sink_Lib_Var(SYM_TWEAK_P_BOOTSTRAP));

} finished: {

    Drop_Level(L);

    assert(
        g_native_cfunc_pos
        == (
            u_cast(CFunction* const*, &g_core_native_dispatchers[0])
            + g_num_core_natives
        )
    );

    g_native_cfunc_pos = nullptr;
    g_currently_loading_module = nullptr;

  #if RUNTIME_CHECKS  // ensure a couple of functions can be looked up by ID
    if (not Is_Action(LIB(FOR_EACH)))
        crash (LIB(FOR_EACH));

    if (not Is_Action(LIB(PARSE_REJECT)))
        crash (LIB(PARSE_REJECT));

    Count num_append_args = Phase_Num_Params(Frame_Phase(LIB(APPEND)));
    assert(num_append_args == Phase_Num_Params(Frame_Phase(LIB(INSERT))));
    assert(num_append_args == Phase_Num_Params(Frame_Phase(LIB(CHANGE))));

    Count num_find_args = Phase_Num_Params(Frame_Phase(LIB(FIND)));
    assert(num_find_args == Phase_Num_Params(Frame_Phase(LIB(SELECT))));
  #endif
}}


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
}
