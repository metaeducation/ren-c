//
//  file: %n-tweak.c
//  summary: "Core functionality unifying getting and setting"
//  section: natives
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2024 Ren-C Open Source Contributors
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
// GET and SET tend to share a lot of work, so they unified on a common set
// of infrastructure called TWEAK.
//
// TWEAK speaks only on the level of single variables, so it doesn't know how
// to set things like BLOCK!: those higher-level abilities are in GET and SET.
//

#include "sys-core.h"


// We want to allow (append.series) to give you back a PARAMETER!, this may
// be applicable to other antiforms also (SPLICE!, maybe?)  But probably too
// risky to let you do it with ERROR!, and misleading to do it with PACK!.
//
static Option(Error*) Trap_Adjust_Lifted_Antiform_For_Tweak(Value* spare)
{
    assert(Is_Lifted_Antiform(spare));
    if (Heart_Of(spare) == TYPE_FRAME) {  // e.g. (append.series)
        LIFT_BYTE_RAW(spare) = ONEQUOTE_NONQUASI_4;
        return SUCCESS;
    }

    return Error_User("TWEAK* cannot be used on non-ACTION! antiforms");
}


//
//  Trap_Call_Pick_Refresh_Dual_In_Spare: C
//
// This breaks out the stylized code for calling TWEAK*, in a Level that
// can be reused across multiple TWEAK* calls.
//
// The stylization is to reduce the number of C-stack-based cells that need
// to be protected from GC.  Instead, cells are written directly into the
// locations they need to be, with careful orchestration.  (This also means
// less make-work of copying bits around from one location to another.)
//
// 1. SPARE indicates both the LOCATION used for the TWEAK*, and the output
//    of the TWEAK* call.  It's a "dual" because for normal values it is
//    a lifted representation--but if it's a non-lifted ACTION! then it is
//    a function to call to do the next TWEAK* with.  This prevents explosions
//    in cases like (some-struct.million-ints.10), where you don't want the
//    (some-struct.million-ints) pick to give back a BLOCK! of a million
//    INTEGER!s just so you can pick one of them out of it.
//
Option(Error*) Trap_Call_Pick_Refresh_Dual_In_Spare(  // [1]
    Level* level_,
    Level* sub,  // will Push_Level() if not already pushed
    StackIndex picker_index
){
    if (Is_Lifted_Antiform(SPARE)) {
        Option(Error*) e = Trap_Adjust_Lifted_Antiform_For_Tweak(SPARE);
        if (e)
            return e;  // don't panic, caller will handle
    }

    require (
      Push_Action(sub, LIB(TWEAK_P), PREFIX_0)
    );
    Set_Executor_Flag(ACTION, sub, IN_DISPATCH);

    Element* location_arg;
    Stable* picker_arg;
    Element* dual_arg;

  proxy_arguments_to_frame_dont_panic_in_this_scope: {

  // We carefully lay things out so the old SPARE gets moved into the frame,
  // to free it up to be used for the output.  But this is delicate, as we
  // cannot panic() while an allocated-but-not-pushed Level is extant.
  // So everything in this section must succeed.

    assert(Is_Possibly_Unstable_Value_Quoted(SPARE));
    location_arg = Copy_Cell(
        Force_Erase_Cell(Level_Arg(sub, 1)),
        Known_Element(SPARE)
    );
    Unquotify(location_arg);

    picker_arg = Copy_Cell(
        Force_Erase_Cell(Level_Arg(sub, 2)),
        Data_Stack_At(Stable, picker_index)
    );

    dual_arg = Init_Dual_Nulled_Pick_Signal(
        Force_Erase_Cell(Level_Arg(sub, 3))
    );
    USED(dual_arg);

    if (sub == TOP_LEVEL)
        Erase_Cell(SPARE);
    else
        Push_Level_Erase_Out_If_State_0(SPARE, sub);

} adjust_frame_arguments_now_that_its_safe_to_panic: {

    if (Any_Lifted(picker_arg)) {  // literal x.'y or x.('y) => 'y
        Unliftify_Known_Stable(picker_arg);

        if (Is_Keyword(picker_arg) or Is_Trash(picker_arg))
            return Error_User(
                "PICK with keyword or trash picker never allowed"
            );
    }
    else {
        Element* pick_instruction = Known_Element(picker_arg);
        if (Sigil_Of(pick_instruction))
            return Error_User(
                "PICK instruction cannot have sigil for variable access"
            );
    }

} call_pick_p: {

    // We actually call TWEAK*, the lower-level function that uses the dual
    // protocol--instead of PICK.  That is because if the pick is not the
    // last pick, it may return an out-of-band function value that we need
    // to use to do the next pick.

    bool threw = Trampoline_With_Top_As_Root_Throws();
    if (threw)  // don't want to return casual error you can TRY from
        return Error_No_Catch_For_Throw(sub);

    assert(sub == TOP_LEVEL);
    unnecessary(Drop_Action(sub));  // !! action is dropped, should it be?

    return SUCCESS;
}}


Option(Error*) Trap_Tweak_Spare_Is_Dual_To_Top_Put_Writeback_Dual_In_Spare(
    Level* level_,
    Level* sub,
    StackIndex picker_index
){
    if (Is_Lifted_Antiform(SPARE))
        return Error_User("TWEAK* cannot be used on antiforms");

    Value* spare_location_dual = SPARE;

    require (
      Push_Action(sub, LIB(TWEAK_P), PREFIX_0)
    );
    Set_Executor_Flag(ACTION, sub, IN_DISPATCH);

    Element* location_arg;
    Stable* picker_arg;
    Value* value_arg;

  proxy_arguments_to_frame_dont_panic_in_this_scope: {

  // We can't panic while there's an extant level that's not pushed.
  //
  // (See notes in Trap_Call_Pick_Refresh_Dual_In_Spare() for more details.)
  //
  // 1. GET:STEPS returns @var for steps of var.  But is (get @var) same as
  //    (get $var) ?

    assert(Is_Possibly_Unstable_Value_Quoted(spare_location_dual));
    location_arg = Copy_Cell(
        Force_Erase_Cell(Level_Arg(sub, 1)),
        Known_Element(spare_location_dual)
    );
    Unquotify(location_arg);

    picker_arg = Copy_Cell(
        Force_Erase_Cell(Level_Arg(sub, 2)),
        Data_Stack_At(Element, picker_index)
    );

    value_arg = u_cast(Value*, Force_Erase_Cell(Level_Arg(sub, 3)));

    Push_Level_Erase_Out_If_State_0(SPARE, sub);  // SPARE becomes writeback

} adjust_frame_arguments_now_that_its_safe_to_panic: {

    attempt {  // v-- how to handle cases like ^x.(...) and know it's ^META?
        if (Any_Lifted(picker_arg)) {  // literal x.'y or x.('y) => 'y
            Unliftify_Known_Stable(picker_arg);

            if (Is_Keyword(picker_arg) or Is_Trash(picker_arg))
                return Error_User(
                    "PICK with keyword or trash picker never allowed"
                );

            Copy_Cell(value_arg, TOP_ELEMENT);
            if (Is_Lifted_Ghost_Or_Void(TOP)) // don't know if it was ^META :-(
                break;  // remove signal

            require (
              Unliftify_Undecayed(value_arg)
            );
            Decay_If_Unstable(value_arg) except (Error* e) {
                return e;
            }
            Liftify(value_arg);
            break;
        }

        Element* picker_instruction = Known_Element(picker_arg);
        Option(Sigil) picker_sigil = Sigil_Of(picker_instruction);
        UNUSED(picker_sigil);  // ideas on the table for this...

        if (SIGIL_META == Underlying_Sigil_Of(Known_Element(SCRATCH))) {
            Copy_Cell(value_arg, TOP_ELEMENT);  // don't decay
            continue;
        }

        // if not meta, needs to decay if unstable

        if (not Any_Lifted(TOP)) {
            Copy_Cell(value_arg, TOP);
            continue;  // dual signal, do not lift dual
        }

        if (Is_Lifted_Ghost_Or_Void(TOP)) {  // (x: ~[]~) or (x: ())
            Init_Ghost_For_End(value_arg);  // both act like (^x: ())
            Liftify(value_arg);
            continue;
        }

        Copy_Cell(value_arg, TOP_ELEMENT);
        require (
          Unliftify_Undecayed(value_arg)
        );
        bool was_singular_pack = (
            Is_Pack(value_arg) and Series_Len_At(value_arg) == 1
        );
        Decay_If_Unstable(value_arg) except (Error* e) {
            return e;
        };
        Liftify(value_arg);

        if (Is_Lifted_Action(Known_Stable(value_arg))) {
            //
            // !!! SURPRISING ACTION ASSIGNMENT DETECTION WOULD GO HERE !!!
            // Current concept is that actions-in-a-pack would be how the
            // "unsurprising" bit is encoded.  This is the last gasp of that
            // particular form of safety--if that doesn't work, I give up.
            //
            if (
                not was_singular_pack
                and Not_Cell_Flag(SCRATCH, SCRATCH_VAR_NOTE_ONLY_ACTION)
            ){
                return Error_Surprising_Action_Raw(picker_arg);
            }

            if (Is_Word(picker_arg)) {
                Update_Frame_Cell_Label(  // !!! is this a good idea?
                    Known_Stable(value_arg), Word_Symbol(picker_arg)
                );
            }
        }
        else {
            if (Get_Cell_Flag(SCRATCH, SCRATCH_VAR_NOTE_ONLY_ACTION)) {
                return Error_User(
                    "/word: and /obj.field: assignments need ACTION!"
                );
            }
        }
    }
    then {  // not quoted...
        Plainify(Known_Element(picker_arg));  // drop any sigils
    }

    Clear_Cell_Flag(SCRATCH, SCRATCH_VAR_NOTE_ONLY_ACTION);  // consider *once*

    Corrupt_Cell_If_Needful(TOP);  // shouldn't use past this point

} call_updater: {

    bool threw = Trampoline_With_Top_As_Root_Throws();

    if (threw)  // don't want to return casual error you can TRY from
        return Error_No_Catch_For_Throw(TOP_LEVEL);

    return SUCCESS;
}}


//
//  Trap_Tweak_Var_In_Scratch_With_Dual_Out_Push_Steps: C
//
// This is centralized code for setting or "tweaking" variables.
//
// **Almost all parts of the system should go through this code for assignment,
// even when they know they have just a WORD! in their hand and don't need path
// dispatch.**  Only a few places bypass this code for reasons of optimization,
// but they must do so carefully, because that would skip things like
// accessors (which implement type checking, etc.)
//
// 1. The calling function should do `heeded (Corrupt_Cell_If_Needful(SPARE))`.
//    This helps be sure they're not expecting SPARE to be untouched.  (It's
//    better than trying to work "Corrupts_Spare()" into the already quite-long
//    name of the function.)
//
Option(Error*) Trap_Tweak_Var_In_Scratch_With_Dual_Out_Push_Steps(
    Level* level_,  // OUT may be ERROR! antiform, see [A]
    bool groups_ok
){
    Stable* out = Known_Stable(OUT);

    assert(LEVEL == TOP_LEVEL);
    possibly(Get_Cell_Flag(SCRATCH, SCRATCH_VAR_NOTE_ONLY_ACTION));
    bool only_action = Get_Cell_Flag(
        SCRATCH,
        SCRATCH_VAR_NOTE_ONLY_ACTION
    );
    USED(only_action);

  #if NEEDFUL_DOES_CORRUPTIONS  // confirm caller pre-corrupted spare [1]
    assert(Not_Cell_Readable(SPARE));
  #endif

    Flags flags = LEVEL_MASK_NONE;  // reused, top level, no keepalive needed

    Sink(Value) spare_location_dual = SPARE;

    StackIndex base = TOP_INDEX;
    StackIndex stackindex_top;

    Option(Error*) e = SUCCESS;  // for common exit path on error

    Element* scratch_var = Known_Element(SCRATCH);

  #if RUNTIME_CHECKS
    Protect_Cell(scratch_var);  // (common exit path undoes this protect)
    if (not Is_Dual_Nulled_Pick_Signal(out))
        Protect_Cell(OUT);
  #endif

  dispatch_based_on_scratch_var_type: {

    if (Is_Word(scratch_var) or Is_Meta_Form_Of(WORD, scratch_var))
        goto handle_scratch_var_as_wordlike;

    if (Is_Tuple(scratch_var) or Is_Meta_Form_Of(TUPLE, scratch_var))
        goto handle_scratch_var_as_sequence;

    if (Is_Pinned_Form_Of(BLOCK, scratch_var))
        goto handle_scratch_var_as_pinned_steps_block;

    e = Error_Bad_Value(scratch_var);
    goto return_error;

  handle_scratch_var_as_wordlike: {

    if (not Try_Get_Binding_Of(SPARE, scratch_var)) {
        e = Error_No_Binding_Raw(scratch_var);
        goto return_error;
    }

    Copy_Cell(PUSH(), Known_Element(SPARE));
    Liftify(TOP);  // dual protocol, lift (?)

    Copy_Cell(PUSH(), scratch_var);  // save var for steps + error messages
    switch (unwrap Underlying_Sigil_Of(TOP_ELEMENT)) {
      case SIGIL_0:
        break;

      case SIGIL_META:
        TOP->header.bits &= (~ CELL_MASK_SIGIL);
        break;

      case SIGIL_PIN:
      case SIGIL_TIE:
        e = Error_User(
            "PICK instruction only understands ^META sigil, for now..."
        );
        goto return_error;
    }

    unnecessary(Liftify(TOP));  // if ^x, not literally ^x ... meta-variable

    goto set_from_steps_on_stack;

} handle_scratch_var_as_sequence: {

    // If we have a sequence, then GROUP!s must be evaluated.  (If we're given
    // a steps array as input, then a GROUP! is literally meant as a
    // GROUP! by value).  These evaluations should only be allowed if the
    // caller has asked us to return steps.

    if (not Sequence_Has_Pointer(scratch_var)) {  // compressed byte form
        e = Error_Bad_Value(scratch_var);
        goto return_error;
    }

    const Base* payload1 = CELL_PAYLOAD_1(scratch_var);
    if (Is_Base_A_Cell(payload1)) {  // pair optimization
        // pairings considered "Listlike", handled by List_At()
    }
    else switch (Stub_Flavor(cast(Flex*, payload1))) {
      case FLAVOR_SYMBOL: {
        if (Get_Cell_Flag(scratch_var, LEADING_SPACE)) {  // `/a` or `.a`
            if (Heart_Of(scratch_var) != TYPE_TUPLE) {
                e = Error_User("GET leading space only allowed on TUPLE!");
                goto return_error;
            }
            Init_Word(SPARE, CANON(DOT_1));
            Tweak_Cell_Binding(
                u_cast(Element*, SPARE),
                Cell_Binding(scratch_var)
            );
            if (not Try_Get_Binding_Of(PUSH(), u_cast(Element*, SPARE))) {
                DROP();
                e = Error_No_Binding_Raw(Known_Element(SPARE));
                goto return_error;
            }
            Liftify(TOP);
            Liftify(Init_Word(PUSH(), CANON(DOT_1)));
            Liftify(Init_Word(PUSH(), u_cast(const Symbol*, payload1)));
            goto set_from_steps_on_stack;
        }

        // `a/` or `a.`
        //
        // !!! If this is a PATH!, it should error if it's not an action...
        // and if it's a TUPLE! it should error if it is an action.  Review.
        //
        goto handle_scratch_var_as_wordlike; }

      case FLAVOR_SOURCE:
        break;  // fall through

      default:
        crash (scratch_var);
    }

    const Element* tail;
    const Element* head = List_At(&tail, scratch_var);
    const Element* at;
    Context* at_binding = Cell_Binding(scratch_var);

    if (Any_Word(head)) {  // add binding at head
        if (not Try_Get_Binding_Of(
            PUSH(), Derelativize(SPARE, head, at_binding)
        )){
            DROP();
            e = Error_No_Binding_Raw(Known_Element(SPARE));
            goto return_error;
        }

        Liftify(TOP);  // dual protocol, lift (?)
    }

    for (at = head; at != tail; ++at) {
        if (not Is_Group(at)) {  // must keep WORD!s at head as-is for writeback
            possibly(Is_Quoted(at));  // will be interpreted "literally"
            Derelativize(PUSH(), at, at_binding);
            continue;
        }

        if (not groups_ok) {
            e = Error_Bad_Get_Group_Raw(scratch_var);
            goto return_error;
        }

        if (Eval_Any_List_At_Throws(SPARE, at, at_binding)) {
            Drop_Data_Stack_To(base);
            e = Error_No_Catch_For_Throw(TOP_LEVEL);
            goto finalize_and_return;
        }

        Stable* spare_picker = Decay_If_Unstable(SPARE) except (e) {
            goto return_error;
        }

        possibly(Is_Antiform(spare_picker));  // e.g. PICK DATATYPE! from MAP!
        Liftify(spare_picker);  // signal literal pick
        Move_Cell(PUSH(), spare_picker);
    }

    goto set_from_steps_on_stack;

} handle_scratch_var_as_pinned_steps_block: {

    const Element* tail;
    const Element* head = List_At(&tail, scratch_var);
    const Element* at;
    Context* at_binding = Cell_Binding(scratch_var);
    for (at = head; at != tail; ++at)
        Derelativize(PUSH(), at, at_binding);

    goto set_from_steps_on_stack;

}} set_from_steps_on_stack: { ////////////////////////////////////////////////

    // We always poke from the top of the stack, not from OUT.  This is
    // because we may have to decay it, and we don't want to modify OUT.
    // It also simplifies the bookkeeping because we don't have to remember
    // if we're looking to poke from the stack or not.

    stackindex_top = TOP_INDEX;  // capture "top of stack" before push

    Copy_Cell(PUSH(), Known_Stable(OUT));

  poke_again: { //////////////////////////////////////////////////////////////

    StackIndex stackindex = base + 1;

  do_stack_thing: {

    OnStack(Element*) at = Data_Stack_At(Element, stackindex);
    Copy_Cell(spare_location_dual, at);  // dual protocol, leave lifted
    if (not Any_Lifted(spare_location_dual)) {
        e = Error_User("First Element in STEPS must be lifted");
        goto return_error;
    }

    ++stackindex;

} calculate_pick_stack_limit: {

    StackIndex limit = stackindex_top;
    if (Is_Dual_Nulled_Pick_Signal(out))
        limit = stackindex_top + 1;

    if (stackindex == limit)
        goto check_for_updater;

  keep_picking_until_last_step: {

    require (
      Level* sub = Make_End_Level(&Action_Executor, flags)
    );

    for (; stackindex != limit; ++stackindex, Restart_Action_Level(sub)) {
        e = Trap_Call_Pick_Refresh_Dual_In_Spare(
            level_, sub, stackindex
        );
        if (e) {
            if (sub->varlist)
                Drop_Action(sub);  // drop any varlist, if it exists
            Drop_Level(sub);
            goto return_error;
        }

        if (Any_Lifted(SPARE)) {  // most common answer--successful pick
            if (Is_Metaform(scratch_var))
                continue;  // all meta picks are as-is

            if (Is_Lifted_Unstable_Antiform(SPARE))
                panic ("Unexpected unstable in non-meta pick");

            continue;
        }

        if (Is_Dual_Nulled_Absent_Signal(Known_Stable(SPARE))) {
            Copy_Cell(SPARE, Data_Stack_At(Element, stackindex));
            e = Error_Bad_Pick_Raw(Known_Element(SPARE));
            if (
                stackindex == limit - 1
                and not Is_Metaform(Data_Stack_At(Element, stackindex))
            ){
                Init_Warning(OUT, unwrap e);
                Failify(OUT);  // signal bad pick distinct from panics

                e = SUCCESS;
                Drop_Level(sub);
                goto return_success;  // last step can be tolerant, see [A]
            }
            Drop_Level(sub);
            goto return_error;
        }

        if (Is_Frame(Known_Stable(SPARE))) {
            Api(Stable*) result = rebStable(Known_Stable(SPARE));
            Copy_Cell(SPARE, result);
            Liftify(SPARE);
            rebRelease(result);
            continue;
        }

        e = Error_User("TWEAK* (dual protocol) gave unknown state for PICK");
        Drop_Level(sub);
        goto return_error;
    }

    Drop_Level(sub);

}} check_for_updater: {

    if (
        not Is_Metaform(scratch_var)
        and Is_Lifted_Antiform(spare_location_dual)
        and not Is_Stable_Antiform_Kind_Byte(spare_location_dual)
    ){
        return Error_User("PICK result cannot be unstable unless metaform");
    }

    // 1. SPARE was picked via dual protocol.  At the moment of the PICK,
    //    the picker may have been ^META, in which case we wouldn't want to
    //    decay... but otherwise we would.  But that decay was already done
    //    (it just re-lifted it) so the undecayed won't make an unstable
    //    value here if the picker wasn't ^META.

    if (Is_Dual_Nulled_Pick_Signal(out)) {
        assert(Is_Nulled(TOP));
        Copy_Cell(OUT, spare_location_dual);
        goto return_success;
    }

    // This may be the first time we do an update, or it may be a writeback
    // as we go back through the list of steps to update any bits that are
    // required to update in the referencing cells.

    require (
      Level* sub = Make_End_Level(&Action_Executor, flags)
    );

    e = Trap_Tweak_Spare_Is_Dual_To_Top_Put_Writeback_Dual_In_Spare(
        level_,
        sub,
        stackindex  // picker_index
    );
    if (sub != TOP_LEVEL) {
        assert(e);  // ack, fix!
        Push_Level_Erase_Out_If_State_0(SPARE, sub);
    }
    if (sub->varlist)
        Drop_Action(sub);
    Drop_Level(sub);

    if (e)
        goto return_error;

    Stable* spare_writeback_dual = Known_Stable(SPARE);

    // Subsequent updates become pokes, regardless of initial updater function

    if (Is_Dual_Nulled_No_Writeback_Signal(spare_writeback_dual))
        goto return_success;

    if (stackindex_top == base + 1) {
        e = Error_User(
            "Last TWEAK* step in POKE gave non-null writeback instruction"
        );
        goto return_error;
    }

    Assert_Cell_Stable(spare_writeback_dual);
    Copy_Cell(Data_Stack_At(Value, TOP_INDEX), spare_writeback_dual);

    --stackindex_top;

    goto poke_again;

}}} return_error: { ///////////////////////////////////////////////////////////

    assert(e);
    Drop_Data_Stack_To(base);
    goto finalize_and_return;

} return_success: { //////////////////////////////////////////////////////////

    possibly(Is_Error(OUT));  // success may be ERROR! antiform, see [A]

    assert(not e);

    DROP();  // drop pushed cell for decaying OUT/etc.

    goto finalize_and_return;

} finalize_and_return: { /////////////////////////////////////////////////////

    assert(LEVEL == TOP_LEVEL);

    Corrupt_Cell_If_Needful(SPARE);

  #if RUNTIME_CHECKS
    Unprotect_Cell(scratch_var);
    if (Get_Cell_Flag(OUT, PROTECTED))
        Unprotect_Cell(OUT);
  #endif

    return e;
}}


//
//  Trap_Tweak_Var_In_Scratch_With_Dual_Out: C
//
Option(Error*) Trap_Tweak_Var_In_Scratch_With_Dual_Out(
    Level* level_,  // OUT may be ERROR! antiform, see [A]
    Option(Element*) steps_out  // no GROUP!s if nulled
){
    possibly(SPARE == steps_out or SCRATCH == steps_out);

    assert(STATE != STATE_0);  // trampoline rule: OUT only erased if STATE_0

    dont(assert(TOP_INDEX == STACK_BASE));  // Hmmm, why not?
    StackIndex base = TOP_INDEX;

    Option(Error*) e = Trap_Tweak_Var_In_Scratch_With_Dual_Out_Push_Steps(
        level_,
        steps_out != NO_STEPS
    );
    if (e)
        return e;

    if (not steps_out or steps_out == GROUPS_OK) {
        Drop_Data_Stack_To(base);
        return SUCCESS;
    }

    if (TOP_INDEX == base + 1 and Is_Word(TOP_ELEMENT)) {
        Copy_Cell(unwrap steps_out, TOP_ELEMENT);
        DROP();
    }
    else
        Init_Block(unwrap steps_out, Pop_Source_From_Stack(base));

    Pinify(unwrap steps_out);  // steps are @[bl o ck] or @word

    return SUCCESS;
}


//
//  tweak: native [
//
//  "Low-level variable setter, that can assign within the dual band"
//
//      return: [
//          <null> frame! word! quasiform! quoted!
//          error!      "Passthru even if it skips the assign"
//      ]
//      target "Word or tuple, or calculated sequence steps (from GET)"
//          [
//              <opt-out>
//              word! tuple!
//              ^word! ^tuple!
//              @block!
//          ]
//      dual "Ordinary GET or SET with lifted value (unlifts), else dual"
//          [
//              <opt> "act as a raw GET of the dual state"
//              frame! "set to store a GETTER/SETTER function in dual band"
//              word! "special instructions (e.g. PROTECT, UNPROTECT)"
//              quasiform! quoted! "store unlifted values as a normal SET"
//          ]
//      :groups "Allow GROUP! Evaluations"
//      :steps "Return evaluation steps for reproducible access"
//  ]
//
DECLARE_NATIVE(TWEAK)
{
    INCLUDE_PARAMS_OF_TWEAK;
    UNUSED(ARG(STEPS));  // TBD

    enum {
        ST_TWEAK_INITIAL_ENTRY = STATE_0,
        ST_TWEAK_TWEAKING  // trampoline rule: OUT must be erased if STATE_0
    };

    Stable* dual = ARG(DUAL);

    Copy_Cell(OUT, dual);

    if (Is_Nulled(ARG(TARGET)))
        return OUT;   // same for SET as [10 = (void): 10]

    Element* target = Element_ARG(TARGET);

    Option(Element*) steps;
    if (Bool_ARG(GROUPS))
        steps = GROUPS_OK;
    else
        steps = NO_STEPS;  // no GROUP! evals

    STATE = ST_TWEAK_TWEAKING;  // we'll be setting out to something not erased

    heeded (Copy_Cell(SCRATCH, target));
    heeded (Corrupt_Cell_If_Needful(SPARE));

    Option(Error*) e = Trap_Tweak_Var_In_Scratch_With_Dual_Out(LEVEL, steps);
    if (e)
        panic (unwrap e);

  return_value_even_if_we_dont_assign: {

  // We want parity between (set $x expression) and (x: expression).  It's
  // very useful that you can write (e: rescue [x: expression]) and in the case
  // of an error, have the assignment skipped and the error trapped.
  //
  // Note that (set $ '^x fail "hi") will assign the error! to X, but will
  // still pass through the ERROR! antiform as the overall expression result.

    return OUT;
}}
