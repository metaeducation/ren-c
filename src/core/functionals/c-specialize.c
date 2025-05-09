//
//  file: %c-specialize.c
//  summary: "Routines for Creating Function Variations with Fixed Parameters"
//  section: datatypes
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2015-2020 Ren-C Open Source Contributors
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
// A specialization is an Action which has some of its parameters fixed.
// e.g. (/ap10: specialize append/ [value: 5 + 5]) makes ap10 have all the same
// refinements available as APPEND, but otherwise just takes one series arg,
// as it will always be appending 10.
//
// Specialization is done by means of making a new "exemplar" frame for the
// action.  Slots in that frame that would have held PARAMETER! antiforms to
// indicate they should gather arguments ("Holes") are replaced by the fixed
// value, which is type checked.
//
// Partial specialization uses a different mechanism.  FILE-TO-LOCAL:PASS
// fulfills a frame slot value since :PASS has no arguments, but APPEND:PART
// does not.  Distinctions of (get $append:dup:part) and (get $append:part:dup)
// require ordering information that has to be tracked outside of the
// exemplar frame.
//

#include "sys-core.h"


//
//  Make_Varlist_For_Action_Push_Partials: C
//
// For partial refinement specializations in the action, this will push the
// refinement to the stack.  In this way it retains the ordering information
// implicit in the partial refinements of an action's existing specialization.
//
// It is able to take in more specialized refinements on the stack.  These
// will be ordered *after* partial specializations in the function already.
// The caller passes in the stack pointer of the lowest priority refinement,
// which goes up to TOP_INDEX for the highest of those added specializations.
//
// Since this is walking the parameters to make the frame already--and since
// we don't want to bind to anything specialized out (including the ad-hoc
// refinements added on the stack) we go ahead and collect bindings from the
// frame if needed.
//
ParamList* Make_Varlist_For_Action_Push_Partials(
    const Value* action,  // need ->binding, so can't just be a Phase*
    StackIndex lowest_stackindex,  // caller can add refinements
    Option(Binder*) binder,
    Option(const Value*) placeholder
){
    StackIndex highest_stackindex = TOP_INDEX;

    Phase* phase = Cell_Frame_Phase(action);

    REBLEN num_slots = Phase_Num_Params(phase) + 1;  // +1 for rootvar
    Array* a = Make_Array_Core(FLEX_MASK_VARLIST, num_slots);
    Set_Flex_Len(a, num_slots);

    Tweak_Bonus_Keylist_Shared(a, Phase_Keylist(phase));

    assert(Is_Action(action) or Is_Frame(action));  // tolerate either?
    Value* rootvar = Flex_Head_Dynamic(Element, a);
    Copy_Cell(rootvar, action);
    QUOTE_BYTE(rootvar) = NOQUOTE_1;  // make sure it's a plain FRAME!
    Protect_Rootvar_If_Debug(rootvar);

    const Key* tail;
    const Key* key = Phase_Keys(&tail, phase);
    const Param* param = Phase_Params_Head(phase);

    Value* arg = Flex_At(Value, a, 1);

    REBLEN index = 1;  // used to bind REFINEMENT? values to parameter slots

    for (; key != tail; ++key, ++param, ++arg, ++index) {
        if (Is_Specialized(param)) {  // includes locals
            Blit_Param_Keep_Mark(arg, param);

          continue_specialized:

            continue;
        }

        const Symbol* symbol = Key_Symbol(key);  // added to binding
        if (Not_Parameter_Flag(param, REFINEMENT)) {  // nothing to push

          continue_unspecialized:

            Erase_Cell(arg);
            if (placeholder) {
                if (Get_Parameter_Flag(param, REFINEMENT))
                    Init_Nulled(arg);
                else
                    Copy_Cell(arg, unwrap placeholder);
            }
            else
                Copy_Cell(arg, param);

            if (binder)
                Add_Binder_Index(unwrap binder, symbol, index);

            continue;
        }

        Erase_Cell(arg);

        // Unspecialized refinement slot.  It may be partially specialized,
        // e.g. we may have pushed to the stack from the PARTIALS for it.
        //
        // Check the passed-in refinements on the stack for usage.
        //
        StackIndex stackindex = highest_stackindex;
        for (; stackindex != lowest_stackindex; --stackindex) {
            OnStack(Element*) ordered = Data_Stack_At(Element, stackindex);
            if (Cell_Word_Symbol(ordered) != symbol)
                continue;  // just continuing this loop

            assert(Cell_Binding(ordered) == UNBOUND);  // we bind only one
            Tweak_Cell_Word_Index(ordered, index);
            Tweak_Cell_Binding(ordered, phase);  // !!! Review

            if (not Is_Parameter_Unconstrained(param))  // needs argument
                goto continue_unspecialized;

            // If refinement named on stack takes no arguments, then it can't
            // be partially specialized...only fully, and won't be bound:
            //
            //     >> specialize skip:unbounded/ [unbounded: ok]
            //     ** Error: unbounded not bound
            //
            Init_Okay(arg);
            goto continue_specialized;
        }

        goto continue_unspecialized;
    }

    Tweak_Misc_Varlist_Adjunct(a, nullptr);
    Tweak_Link_Inherit_Bind(a, nullptr);

    return cast(ParamList*, a);
}


//
//  Make_Varlist_For_Action: C
//
// This creates a FRAME! context with parameter antiforms in all unspecialized
// slots.
//
// !!! The ultimate concept is that it would be possible for a FRAME! to
// preserve ordering information such that an ACTION! could be made from it.
// Right now the information is the stack ordering numbers of the refinements
// which to make it usable should be relative to the lowest ordered StackIndex
// and not absolute.
//
ParamList* Make_Varlist_For_Action(
    const Value* action, // need ->binding, so can't just be a Phase*
    StackIndex lowest_stackindex,
    Option(Binder*) binder,
    Option(const Value*) placeholder
){
    ParamList* exemplar = Make_Varlist_For_Action_Push_Partials(
        action,
        lowest_stackindex,
        binder,
        placeholder
    );

    Manage_Flex(exemplar);  // !!! was needed before, review
    Drop_Data_Stack_To(lowest_stackindex);
    return exemplar;
}


//
//  Specialize_Action_Throws: C
//
// Create a new ACTION! value that uses the same implementation as another,
// but just takes fewer arguments or refinements.  It does this by storing a
// heap-based "exemplar" FRAME! in the specialized action; this stores the
// values to preload in the stack frame cells when it is invoked.
//
// The caller may provide information on the order in which refinements are
// to be specialized, using the data stack.  These refinements should be
// pushed in the *reverse* order of their invocation, so APPEND:DUP:PART
// has :DUP at TOP, and :PART under it.  List stops at lowest_stackindex.
//
bool Specialize_Action_Throws(
    Sink(Value) out,
    const Value* specializee,
    Option(Element*) def,  // !!! REVIEW: binding modified directly, not copied
    StackIndex lowest_stackindex
){
    assert(out != specializee);

    Option(const Symbol*) label = Cell_Frame_Label(specializee);
    Option(VarList*) coupling = Cell_Frame_Coupling(specializee);

    DECLARE_BINDER (binder);
    if (def)
        Construct_Binder_Core(binder);  // conditional, must use _Core()

    Phase* unspecialized = Cell_Frame_Phase(specializee);

    // This produces a context where partially specialized refinement slots
    // will be on the stack (including any we are adding "virtually", from
    // the current TOP_INDEX down to the lowest_stackindex).
    //
    ParamList* exemplar = Make_Varlist_For_Action_Push_Partials(
        specializee,
        lowest_stackindex,
        def ? binder : nullptr,
        nullptr  // no placeholder, leave parameter! antiforms
    );
    Manage_Flex(exemplar);  // destined to be managed, guarded

    if (def) { // code that fills the frame...fully or partially
        //
        // Bind all the SET-WORD! in the body that match params in the frame
        // into the frame.  This means `value: value` can very likely have
        // `value:` bound for assignments into the frame while `value` refers
        // to whatever value was in the context the specialization is running
        // in, but this is likely the more useful behavior.
        //
        Use* use = Alloc_Use_Inherits_Core(
            USE_FLAG_SET_WORDS_ONLY,
            Cell_List_Binding(unwrap def)
        );
        Init_Frame(Stub_Cell(use), exemplar, label, coupling);

        Tweak_Cell_Binding(unwrap def, use);
        Remember_Cell_Is_Lifeguard(Stub_Cell(use));  // protects exemplar

        // !!! Only one binder can be in effect, and we're calling arbitrary
        // code.  Must clean up now vs. in loop we do at the end.  :-(
        //
        Destruct_Binder_Core(binder);

        bool threw = Eval_Any_List_At_Throws(out, unwrap def, SPECIFIED);

        if (threw) {
            Drop_Data_Stack_To(lowest_stackindex);
            return true;
        }

        Erase_Cell(out);  // ignore result of specialization code
    }

    const Key* tail;
    const Key* key = Phase_Keys(&tail, unspecialized);
    const Param* param = Phase_Params_Head(unspecialized);

    Value* arg = Varlist_Slots_Head(exemplar);

    StackIndex ordered_stackindex = lowest_stackindex;

    // If you specialize out the first argument of an infixed function, then
    // it ceases being infix.
    //
    // !!! Needs handling for interaction with REORDER.
    //
    bool first_param = true;
    Option(InfixMode) infix_mode = Cell_Frame_Infix_Mode(specializee);

    for (; key != tail; ++key, ++param, ++arg) {
        if (Is_Specialized(param)) {  // was specialized in underlying phase
            assert(not Is_Parameter(arg));  // user couldn't have changed it!
            continue;
        }

        if (Is_Parameter(arg)) {
          #if DEBUG_POISON_UNINITIALIZED_CELLS
            Poison_Cell(arg);
          #endif
            Blit_Param_Unmarked(arg, param);
            if (first_param)
                first_param = false;  // leave infix as is
            continue;
        }

        // !!! If argument was previously specialized, should have been type
        // checked already... don't type check again (?)
        //
        if (Get_Parameter_Flag(param, VARIADIC))
            panic ("Cannot currently SPECIALIZE variadic arguments.");

        if (not Typecheck_Coerce_Uses_Spare_And_Scratch(
            TOP_LEVEL, param, arg, false
        )){
            panic (Error_Arg_Type(label, key, param, arg));
        }

        Mark_Typechecked(arg);

        if (first_param) {
            first_param = false;
            infix_mode = PREFIX_0;  // specialized out the first parameter
        }
    }

    // Everything should have balanced out for a valid specialization.
    // Turn partial refinements into an array of things to push.
    //
    Array* partials;
    if (ordered_stackindex == TOP_INDEX)
        partials = nullptr;
    else {
        // The list of ordered refinements may contain some cases like /ONLY
        // which aren't considered partial because they have no argument.
        // If that's the only kind of partial we hvae, we'll free this array.
        //
        // !!! This array will be allocated too big in cases like /dup/only,
        // review how to pick the exact size efficiently.  There's also the
        // case that duplicate refinements or non-existent ones create waste,
        // but since we error and throw those arrays away it doesn't matter.
        //
        partials = Make_Source(
            TOP_INDEX - ordered_stackindex  // maximum partial count possible
        );

        while (ordered_stackindex != TOP_INDEX) {
            ordered_stackindex += 1;
            OnStack(Element*) ordered = Data_Stack_At(
                Element, ordered_stackindex
            );
            if (not Cell_Binding(ordered)) {  // specialize print:asdf/
                Refinify_Pushed_Refinement(ordered);
                panic (Error_Bad_Parameter_Raw(ordered));
            }

            Value* slot = Varlist_Slot(exemplar, VAL_WORD_INDEX(ordered));
            if (not Is_Specialized(cast(Param*, slot))) {
                //
                // It's still partial...
                //
                assert(VAL_WORD_INDEX(ordered) != 0);
                Init_Pushable_Refinement_Bound(
                    Alloc_Tail_Array(partials),
                    Key_Symbol(Varlist_Key(exemplar, VAL_WORD_INDEX(ordered))),
                    exemplar,
                    VAL_WORD_INDEX(ordered)
                );
            }
        }
        Drop_Data_Stack_To(lowest_stackindex);

        if (Array_Len(partials) == 0) {
            Free_Unmanaged_Flex(partials);
            partials = nullptr;
        }
        else {
            Manage_Flex(partials);
            panic ("Refinement Promotion is being rethought");
        }
    }

    Init_Frame(out, exemplar, label, coupling);
    Actionify(out);

    Tweak_Cell_Frame_Infix_Mode(out, infix_mode);

    return false;  // code block did not throw
}


//
//  specialize: native [
//
//  "Create a new action through partial or full specialization of another"
//
//      return: [action!]
//      original "Frame whose parameters will be set to fixed values"
//          [<unrun> frame!]
//      def "Definition code setting fields for args and refinements"
//          [block!]
//  ]
//
DECLARE_NATIVE(SPECIALIZE)
//
// 1. Refinement specializations via path are pushed to the stack, giving
//    order information that can't be meaningfully gleaned from an arbitrary
//    code block (specialize append/ [dup: x | if y [part: z]]), we shouldn't
//    think that intends any ordering of :dup:part or :part:dup)
{
    INCLUDE_PARAMS_OF_SPECIALIZE;

    Element* specializee = Element_ARG(ORIGINAL);
    Element* def = Element_ARG(DEF);

    if (Specialize_Action_Throws(
        OUT,
        specializee,
        def,
        STACK_BASE  // lowest ordered stackindex [1]
    )){
        return THROWN;  // e.g. (specialize append:dup/ [value: throw 10])
    }

    return OUT;
}


//
//  First_Unspecialized_Param: C
//
// Note that refinement promotion can make this a bit strange:
//
//     >> foo: func [:a [block!] :b [block!] :c [block!] :d [block!]] [...]
//     >> foo-d: foo:d/
//
// This means that the last parameter (D) is actually the first of FOO-D.
//
const Param* First_Unspecialized_Param(Sink(const Key*) key_out, Phase* phase)
{
    const Key* key_tail;
    const Key* key = Phase_Keys(&key_tail, phase);
    Param* param = Phase_Params_Head(phase);
    for (; key != key_tail; ++key, ++param) {
        if (Is_Specialized(param))
            continue;
        if (Get_Parameter_Flag(param, REFINEMENT))
            continue;
        if (key_out)
            *key_out = key;
        return param;
    }
    return nullptr;
}


//
//  Get_First_Param_Literal_Class: C
//
// !!! This is very inefficient, and the parameter class should be cached
// in the frame somehow.
//
Option(ParamClass) Get_First_Param_Literal_Class(Phase* phase) {
    ParamList* paramlist = Phase_Paramlist(phase);
    if (Not_Flavor_Flag(VARLIST, paramlist, PARAMLIST_LITERAL_FIRST))
        return PARAMCLASS_0;

    ParamClass pclass = Cell_Parameter_Class(
        First_Unspecialized_Param(nullptr, phase)
    );
    assert(  // !!! said it quoted its first parameter!
        pclass == PARAMCLASS_JUST
        or pclass == PARAMCLASS_THE
        or pclass == PARAMCLASS_SOFT
    );
    return pclass;
}


//
//  Last_Unspecialized_Param: C
//
// See notes on First_Unspecialized_Param() regarding complexity
//
const Param* Last_Unspecialized_Param(Sink(const Key*) key_out, Phase* act)
{
    const Key* key;
    const Key* key_head = Phase_Keys(&key, act);
    Param* param = Phase_Params_Head(act) + (key - key_head);

    while (key != key_head) {
        --key;
        --param;
        if (Is_Specialized(param))
            continue;
        if (Get_Parameter_Flag(param, REFINEMENT))
            continue;
        if (key_out)
            *key_out = key;
        return param;
    }
    return nullptr;
}

//
//  First_Unspecialized_Arg: C
//
// Helper built on First_Unspecialized_Param(), can also give you the param.
//
Value* First_Unspecialized_Arg(Option(const Param* *) param_out, Level* L)
{
    Phase* phase = Level_Phase(L);
    const Param* param = First_Unspecialized_Param(nullptr, phase);
    if (param_out)
        *(unwrap param_out) = param;

    if (param == nullptr)
        return nullptr;

    REBLEN index = param - Phase_Params_Head(phase);
    return Level_Args_Head(L) + index;
}
