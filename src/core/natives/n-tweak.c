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


// Although TUPLE! uses quotes to indicate a desire to unbind the target, the
// STEPS pushed to stack uses quotes and quasiforms to be literal lifted
// states.  So once a step is produced, the unbind instruction has to be
// encoded another way.
//
// That encoding hasn't been worked out yet to persist, so it's not persistent
// at the moment--it's just a cell flag that is processed for one TWEAK but
// isn't popped into the steps.  Review.
//
#define CELL_FLAG_STEP_NOTE_WANTS_UNBIND  CELL_FLAG_NOTE


// We want to allow (append.series) to give you back a PARAMETER!, this may
// be applicable to other antiforms also (SPLICE!, maybe?)  But probably too
// risky to let you do it with FAILURE!, and misleading to do it with PACK!.
//
static Option(Error*) Trap_Adjust_Lifted_Antiform_For_Tweak(Value* spare)
{
    assert(Is_Lifted_Antiform(spare));
    if (Heart_Of(spare) == TYPE_FRAME) {  // e.g. (append.series)
        LIFT_BYTE_RAW(spare) = ONEQUOTE_NONQUASI_5;
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
    Level* parent,
    Level* const sub,  // sublevel will Push_Level() if not already pushed
    StackIndex picker_index,
    bool dont_indirect
){
    Option(Heart) adjusted = none;
    Flags must_be_final_bit;

    Element* location_arg;
    Stable* picker_arg;
    Element* dual_arg;

  adjust_antiform_pick_if_needed: {

    Dual* dual_spare = As_Dual(Level_Spare(parent));

    must_be_final_bit = (
        dual_spare->header.bits & CELL_FLAG_BINDING_MUST_BE_FINAL
    );

    if (Is_Lifted_Antiform(dual_spare)) {
        adjusted = Heart_Of_Unsigiled_Isotopic(dual_spare);
        Option(Error*) e = Trap_Adjust_Lifted_Antiform_For_Tweak(dual_spare);
        if (e)
            return e;  // don't panic, caller will handle
    }

    require (
      Push_Action(sub, LIB(TWEAK_P), PREFIX_0)
    );

  proxy_arguments_to_frame_dont_panic_in_this_scope: {

  // We carefully lay things out so the old SPARE gets moved into the frame,
  // to free it up to be used for the output.  But this is delicate, as we
  // cannot panic() while an allocated-but-not-pushed Level is extant.
  // So everything in this section must succeed.

    USE_LEVEL_SHORTHANDS (sub);
    INCLUDE_PARAMS_OF_TWEAK_P;

    assert(Is_Possibly_Unstable_Value_Quoted(dual_spare));
    location_arg = Copy_Cell(
        Erase_ARG(LOCATION),
        dual_spare
    );
    Unquote_Cell(location_arg);

    picker_arg = Copy_Cell(
        Erase_ARG(PICKER),
        Data_Stack_At(Stable, picker_index)
    );

    dual_arg = Init_Null_Signifying_Tweak_Is_Pick(Erase_ARG(DUAL));
    USED(dual_arg);

}} erase_parent_spare_now_that_we_are_done_extracting_it: {

    assert(sub->out == Level_Spare(parent));
    Erase_Cell(sub->out);

} adjust_frame_arguments_now_that_its_safe_to_panic: {

    if (Any_Lifted(picker_arg)) {  // literal x.'y or x.('y) => 'y
        Known_Stable_Unlift_Cell(picker_arg);

        if (Is_Logic(picker_arg)) {
            Drop_Action(sub);
            return Error_User(
                "PICK with logic picker never allowed"
            );
        }
    }
    else {
        Element* pick_instruction = As_Element(picker_arg);
        if (Sigil_Of(pick_instruction)) {
            Drop_Action(sub);
            return Error_User(
                "PICK instruction cannot have sigil for variable access"
            );
        }
    }

} call_tweak: {

  // We actually call TWEAK*, the lower-level function that uses the dual
  // protocol--instead of PICK.

    if (SPORADICALLY(32)) {
        LEVEL_STATE_BYTE(sub) = ST_ACTION_TYPECHECKING;
    } else {
        Mark_Typechecked(u_cast(Param*, location_arg));
        Mark_Typechecked(u_cast(Param*, picker_arg));
        Mark_Typechecked(u_cast(Param*, dual_arg));
        Set_Executor_Flag(ACTION, sub, IN_DISPATCH);
    }

    bool threw = Trampoline_With_Top_As_Root_Throws();
    if (threw)  // don't want to return casual error you can TRY from
        return Error_No_Catch_For_Throw(sub);

    assert(sub == TOP_LEVEL);
    unnecessary(Drop_Action(sub));  // !! action is dropped, should it be?

    if (Is_Null_Signifying_Slot_Unavailable(Level_Spare(parent)))
        goto return_without_unbinding;

    Dual* dual_spare = As_Dual(Level_Spare(parent));

    if (Is_Dualized_Bedrock(dual_spare)) {
        if (dont_indirect)
            goto return_without_unbinding;

        goto handle_indirect_pick;
    }

    goto possibly_unbind_spare_and_return;

  handle_indirect_pick: { ////////////////////////////////////////////////////

  // 1. The drain could PANIC regardless of access via VAR or ^VAR, (as it is
  //    "dishonest" to give anything back, when whatever was last assigned was
  //    discarded).  But it's probably more useful if ^VAR is willing to give
  //    a FAILURE! so you can say (try ^var) and get NULL.

    if (Is_Bedrock_Dual_An_Accessor(dual_spare)) {  // FRAME!
        Api(Value*) result = rebLift(dual_spare);
        Copy_Cell(dual_spare, As_Dual(result));  // result of running FRAME!
        rebRelease(result);
        goto possibly_unbind_spare_and_return;
    }

    if (Is_Bedrock_Dual_An_Alias(dual_spare)) {  // ^WORD!, ^TUPLE!
        Quote_Cell(dual_spare);
        Api(Value*) result = rebLift(CANON(GET), dual_spare);
        Copy_Cell(dual_spare, As_Dual(result));  // lifted result of GET
        rebRelease(result);
        goto possibly_unbind_spare_and_return;
    }

    if (Is_Bedrock_Dual_A_Drain(dual_spare)) {  // SPACE
        Quasify_Isotopic_Fundamental(  // signify lifted FAILURE! [1]
            Init_Error_Cell(dual_spare, Error_Cant_Get_Drain_Raw())
        );
        goto return_without_unbinding;
    }

    if (Is_Bedrock_Dual_A_Hole(dual_spare)) {  // unspecialized cell
        if (adjusted == TYPE_FRAME) { // picking parameter from an ACTION!
            LIFT_BYTE(dual_spare) = ONEQUOTE_NONQUASI_5;  // plain lifted
        } else {  // make it look like a NULL
            Init_Lifted_Null_Signifying_Unspecialized(dual_spare);
        }
        goto return_without_unbinding;
    }

    return Error_User("TWEAK* returned unknown dualized bedrock element");

} possibly_unbind_spare_and_return: { ///////////////////////////////////////

    if (must_be_final_bit) {
        if (Not_Cell_Flag(dual_spare, FINAL))
            return Error_Pure_Non_Final_Raw(
                Data_Stack_At(Element, picker_index)
            );
    }
    Clear_Cell_Flag(dual_spare, FINAL);  // picks never final

    if (Get_Cell_Flag(
        Data_Stack_At(Element, picker_index), STEP_NOTE_WANTS_UNBIND
    )){
        Unbind_Cell_If_Bindable_Core(dual_spare);  // unbind after reading
    }

}} return_without_unbinding: { ////////////////////////////////////////////////

    return SUCCESS;
}}


Option(Error*) Trap_Tweak_Spare_Is_Dual_Writeback_Dual_In_Scratch_To_Spare(
    Level* parent,
    Level* const sub,
    StackIndex picker_index
){
    Dual* dual_writeback_scratch = As_Dual(Level_Scratch(parent));

    if (Is_Lifted_Antiform(Level_Spare(parent)))
        return Error_User("TWEAK* cannot be used on antiforms");

    if (Get_Cell_Flag(Level_Spare(parent), BINDING_MUST_BE_FINAL))
        return Error_Pure_Non_Final_Raw(  // if we're poking, that's bad
            Data_Stack_At(Element, picker_index)
        );

    require (
      Push_Action(sub, LIB(TWEAK_P), PREFIX_0)
    );

    Element* location_arg;
    Stable* picker_arg;
    Value* dual_value_arg;

  proxy_arguments_to_frame_dont_panic_in_this_scope: {

    Dual* dual_location_spare = As_Dual(Level_Spare(parent));  // incoming

  // We can't panic while there's an extant level that's not pushed.
  //
  // (See notes in Trap_Call_Pick_Refresh_Dual_In_Spare() for more details.)
  //
  // 1. GET:STEPS returns @var for steps of var.  But is (get @var) same as
  //    (get $var) ?

    USE_LEVEL_SHORTHANDS (sub);
    INCLUDE_PARAMS_OF_TWEAK_P;

    assert(Is_Possibly_Unstable_Value_Quoted(dual_location_spare));
    location_arg = Copy_Cell(
        Erase_ARG(LOCATION),
        dual_location_spare
    );
    Unquote_Cell(location_arg);

    picker_arg = Copy_Cell(
        Erase_ARG(PICKER),
        Data_Stack_At(Element, picker_index)
    );

    dual_value_arg = u_cast(Value*, Erase_ARG(DUAL));

} erase_parent_spare_now_that_we_are_done_extracting_it: {

    assert(sub->out == Level_Spare(parent));
    Erase_Cell(sub->out);

} adjust_frame_arguments_now_that_its_safe_to_panic: {

    if (Is_Lifted_Action(dual_writeback_scratch)) {  // !!! must generalize
        if (Is_Word(picker_arg)) {
            Update_Frame_Cell_Label(  // !!! is this a good idea?
                dual_writeback_scratch, Word_Symbol(picker_arg)
            );
        }
    }

    if (Get_Cell_Flag(
        Data_Stack_At(Element, picker_index), STEP_NOTE_WANTS_UNBIND
    )){
        Unbind_Cell_If_Bindable_Core(dual_writeback_scratch);  // before write
    }

    if (Any_Lifted(picker_arg))
        Known_Stable_Unlift_Cell(picker_arg);
    else
        Clear_Cell_Sigil(As_Element(picker_arg));

    Copy_Cell(dual_value_arg, dual_writeback_scratch);

} call_updater: {

  // 1. We return success--in the sense of a non-`fail()` Error* result--even
  //    if the slot was not available.  If we're on the last pick of a tuple,
  //    the pick may not panic.  (Note this is distinct from if the picker was
  //    objectively bad, like using an OBJECT! picker in a BLOCK!)

    if (SPORADICALLY(32)) {
        LEVEL_STATE_BYTE(sub) = ST_ACTION_TYPECHECKING;
    } else {
        Mark_Typechecked(u_cast(Param*, location_arg));
        Mark_Typechecked(u_cast(Param*, picker_arg));
        Mark_Typechecked(u_cast(Param*, dual_value_arg));
        Set_Executor_Flag(ACTION, sub, IN_DISPATCH);
    }

    bool threw = Trampoline_With_Top_As_Root_Throws();

    if (threw)  // don't want to return casual error you can TRY from
        panic (Error_No_Catch_For_Throw(TOP_LEVEL));

    Stable* stable_spare = As_Stable(Level_Spare(parent));
    if (Is_Logic(stable_spare)) {  // "success" even if unavailable [1]
        possibly(Is_Okay_Signifying_No_Writeback(stable_spare));
        possibly(Is_Null_Signifying_Slot_Unavailable(stable_spare));
        goto return_success;
    }

    Dual* dual_spare = As_Dual(Level_Spare(parent));

    if (not Is_Dualized_Bedrock(dual_spare))
        goto return_success;

  handle_indirect_poke: {

  // This means TWEAK* returned OUT_UNLIFTED_DUAL_INDIRECT_POKE, and whatever
  // that output was written to the SPARE of level_.  It means that it didn't
  // write the value, but told us where it wants the value to be written
  // (such as to an aliased variable, or through a SETTER function).
  //
  // We handle it here, and indicate there is no writeback needed.
  //
  // (If someone wanted a writeback *and* to run something like a SETTER from
  // a Bedrock slot, they'd have to handle the SETTER themselves and then
  // return what they wanted to write back.  We provide this general mechanism
  // as a convenience, expecting most POKE targets to want writeback -OR-
  // an indirect poke--but not both at once.)

    if (Is_Bedrock_Dual_An_Accessor(dual_spare)) {  // FRAME!
        Element* quoted = Copy_Cell(Level_Spare(sub), TOP_ELEMENT);
        rebElide(dual_spare, quoted);  // quote suppresses eval
    }
    else if (Is_Bedrock_Dual_An_Alias(dual_spare)) {  // ^WORD!, ^TUPLE!
        Element* quoted = Copy_Lifted_Cell(Level_Spare(sub), TOP_ELEMENT);
        Quote_Cell(dual_spare);
        rebElide(CANON(TWEAK), dual_spare, quoted);  // quote suppresses eval
    }
    else if (Is_Bedrock_Dual_A_Drain(dual_spare)) {  // SPACE
        // do nothing, you wrote to a drain...
    }
    else return Error_User(
        "TWEAK* returned bad dualized bedrock element for writeback"
    );

    Init_Okay_Signifying_No_Writeback(stable_spare);
    goto return_success;

}} return_success: { //////////////////////////////////////////////////////////

    Corrupt_Cell_If_Needful(dual_writeback_scratch);  // done with it
    return SUCCESS;
}}


//
//  Try_Push_Steps_To_Stack_For_Word: C
//
// This doesn't generate an ERROR! because some clients (e.g. infix detection)
// don't want to pay a heavy cost if a word doesn't have a binding.
//
bool Try_Push_Steps_To_Stack_For_Word(
    const Element* wordlike,
    Context* binding
){
    assert(Is_Word(wordlike) or Is_Meta_Form_Of(WORD, wordlike));

    if (not Try_Get_Binding_Of(PUSH(), wordlike, binding)) {
        DROP();
        return false;
    }

    Lift_Cell(TOP_ELEMENT);  // dual protocol, lift (?)

    Copy_Cell(PUSH(), wordlike);  // variable is what we're picking with
    switch (opt Cell_Underlying_Sigil(TOP_ELEMENT)) {
      case SIGIL_0:
        break;

      case SIGIL_META:  // we remember the sigil from scratch_var
        TOP->header.bits &= (~ CELL_MASK_SIGIL);
        break;

      case SIGIL_PIN:
      case SIGIL_TIE:
        panic ("PICK instruction only understands ^META sigil, for now...");
    }
    unnecessary(Lift_Cell(TOP_STABLE));  // !!! unlifted picker is ok--why?

    return true;
}


//
//  Trap_Push_Steps_To_Stack: C
//
Option(Error*) Trap_Push_Steps_To_Stack(
    const Element* var,
    bool groups_ok
){
    Level* level_ = TOP_LEVEL;

    assert(TOP_INDEX == STACK_BASE);
    const StackIndex base = TOP_INDEX;

    Option(Error*) error = SUCCESS;

    if (Is_Word(var) or Is_Meta_Form_Of(WORD, var)) {
        if (Try_Push_Steps_To_Stack_For_Word(var, SPECIFIED))
            goto return_success;
        error = Error_No_Binding_Raw(var);
        goto return_error;
    }

    if (Is_Tuple(var) or Is_Meta_Form_Of(TUPLE, var))
        goto handle_var_as_sequence;

    if (Is_Tied_Form_Of(BLOCK, var))
        goto handle_var_as_tied_steps_block;

    error = Error_Bad_Value(var);
    goto return_error;

  handle_var_as_sequence: {

    // If we have a sequence, then GROUP!s must be evaluated.  (If we're given
    // a steps array as input, then a GROUP! is literally meant as a
    // GROUP! by value).  These evaluations should only be allowed if the
    // caller has asked us to return steps.

    if (not Sequence_Has_Pointer(var)) {  // compressed byte form
        error = Error_Bad_Value(var);
        goto return_error;
    }

    const Base* payload1 = CELL_PAYLOAD_1(var);
    if (Is_Base_A_Cell(payload1)) {  // pair optimization
        // pairings considered "Listlike", handled by List_At()
    }
    else switch (Stub_Flavor(cast(Flex*, payload1))) {
      case FLAVOR_SYMBOL: {
        if (Get_Cell_Flag(var, LEADING_BLANK)) {  // `/a` or `.a`
            if (Heart_Of(var) != TYPE_TUPLE) {
                error = Error_User("GET leading space only allowed on TUPLE!");
                goto return_error;
            }
            Element* dot1 = Init_Word(SPARE, CANON(DOT_1));
            if (not Try_Get_Binding_Of(PUSH(), dot1, Cell_Binding(var))) {
                DROP();
                error = Error_No_Binding_Raw(As_Element(SPARE));
                goto return_error;
            }
            Lift_Cell(TOP_STABLE);
            Lift_Cell(Init_Word(PUSH(), CANON(DOT_1)));
            Lift_Cell(Init_Word(PUSH(), u_cast(const Symbol*, payload1)));
            goto return_success;
        }

        // `a/` or `a.`
        //
        // !!! If this is a PATH!, it should error if it's not an action...
        // and if it's a TUPLE! it should error if it is an action.  Review.
        //
        if (Try_Push_Steps_To_Stack_For_Word(var, SPECIFIED))
            goto return_success;
        error = Error_No_Binding_Raw(var);
        goto return_error; }

      case FLAVOR_SOURCE:
        break;  // fall through

      default:
        crash (var);
    }

    const Element* tail;
    const Element* head = List_At(&tail, var);
    const Element* at;
    Context* at_binding = Cell_Binding(var);

    if (Any_Word(head)) {  // add binding at head
        if (not Try_Get_Binding_Of(
            PUSH(), head, at_binding
        )){
            error = Error_No_Binding_Raw(As_Element(SPARE));
            goto return_error;
        }

        Lift_Cell(TOP_STABLE);  // dual protocol, lift (?)
    }

    for (at = head; at != tail; ++at) {
        bool unbind;
        switch (LIFT_BYTE(at)) {
          case NOQUOTE_3:
            unbind = false;
            break;

          case ONEQUOTE_NONQUASI_5:
            unbind = true;
            break;

          default:
            panic ("TUPLE! dialect allows single quote 'unbind on items");
        }

        if (Heart_Of(at) == TYPE_GROUP) {
            if (not groups_ok) {
                error = Error_Bad_Get_Group_Raw(var);
                goto return_error;
            }

            if (Eval_Any_List_At_Throws(SPARE, at, at_binding)) {
                error = Error_No_Catch_For_Throw(TOP_LEVEL);
                goto return_error;
            }

            Stable* spare_picker = (
                Decay_If_Unstable(SPARE)
            ) except (Error* e) {
                error = e;
                goto return_error;
            }

            possibly(Is_Antiform(spare_picker));  // e.g. PICK MAP DATATYPE!
            Copy_Lifted_Cell(PUSH(), spare_picker);  // lift is literal pick
        }
        else {
            Copy_Cell_May_Bind(PUSH(), at, at_binding);
            if (unbind)
                LIFT_BYTE(TOP) = NOQUOTE_3;
        }

        // !!! Here we could validate or rule out items in the TUPLE! dialect,
        // however the work would be repeated in the steps pushing; it is
        // likely better to just let the step processing do it.

        if (unbind)
            Set_Cell_Flag(TOP, STEP_NOTE_WANTS_UNBIND);
    }

    goto return_success;

} handle_var_as_tied_steps_block: {

    const Element* tail;
    const Element* head = List_At(&tail, var);
    const Element* at;
    Context* at_binding = Cell_Binding(var);
    for (at = head; at != tail; ++at)
        Copy_Cell_May_Bind(PUSH(), at, at_binding);

    goto return_success;

} return_success: { //////////////////////////////////////////////////////////

    Corrupt_Cell_If_Needful(SPARE);
    return SUCCESS;

} return_error: { ////////////////////////////////////////////////////////////

    Drop_Data_Stack_To(base);
    return error;
}}


//
//  Tweak_Stack_Steps_With_Dual_Scratch_To_Dual_Spare: C
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
Option(Error*) Tweak_Stack_Steps_With_Dual_Scratch_To_Dual_Spare(void)
{
    Level* level_ = TOP_LEVEL;
    const StackIndex base = STACK_BASE;

    assert(
        STATE == ST_TWEAK_TWEAKING
        or STATE == ST_TWEAK_GETTING
        or STATE == ST_TWEAK_SETTING
    );
    const TweakMode mode = u_cast(TweakMode, STATE);

    assert(OUT != SCRATCH and OUT != SPARE);

  #if NEEDFUL_DOES_CORRUPTIONS  // confirm caller pre-corrupted spare [1]
    assert(Not_Cell_Readable(SPARE));
  #endif

    assert(Is_Cell_Stable(SCRATCH));
    bool tweak_is_pick = Is_Null_Signifying_Tweak_Is_Pick(SCRATCH);

    Sink(Stable) spare_location_dual = SPARE;

    StackIndex stackindex_top;

    Option(Error*) error = SUCCESS;  // for common exit path on error

    Shield_Cell_If_Debug(OUT);

    // We always poke from the top of the stack, not from OUT.  This is
    // because we may have to decay it, and we don't want to modify OUT.
    // It also simplifies the bookkeeping because we don't have to remember
    // if we're looking to poke from the stack or not.

    stackindex_top = TOP_INDEX;  // capture "top of stack" before push

    require (
      Level* sub = Make_End_Level(
        &Action_Executor,
        LEVEL_FLAG_DEBUG_STATE_0_OUT_NOT_ERASED_OK
      )
    );
    dont(Erase_Cell(SPARE));  // spare will be used, then erased before call
    Push_Level(SPARE, sub);

  poke_again: { //////////////////////////////////////////////////////////////

    StackIndex stackindex = base + 1;

  do_stack_thing: {

    OnStack(Element*) at = Data_Stack_At(Element, stackindex);

    if (not Any_Lifted(at)) {
        panic ("First Element in STEPS must be lifted");
    }

    Copy_Cell_Core(
        spare_location_dual,  // dual protocol, leave lifted
        at,
        CELL_MASK_COPY | CELL_FLAG_BINDING_MUST_BE_FINAL
    );

    ++stackindex;

} calculate_pick_stack_limit: {

    StackIndex limit = stackindex_top;
    if (tweak_is_pick)
        limit = stackindex_top + 1;

    if (stackindex == limit)
        goto check_for_updater;

  keep_picking_until_last_step: {

    for (; stackindex != limit; ++stackindex, Restart_Action_Level(sub)) {
        bool dont_indirect = (
            (mode == ST_TWEAK_TWEAKING) and (stackindex == limit - 1)
        );
        error = Trap_Call_Pick_Refresh_Dual_In_Spare(
            level_, sub, stackindex, dont_indirect
        );
        if (error) {
            Drop_Level(sub);  // level has no action, error report would assert
            panic (unwrap error);
        }

        if (Is_Null_Signifying_Slot_Unavailable(As_Stable(SPARE))) {
          treat_like_pick_absent_signal:
            Copy_Cell(SPARE, Data_Stack_At(Element, stackindex));
            error = Error_Bad_Pick_Raw(As_Element(SPARE));
            if (
                stackindex == limit - 1
                and not Is_Metaform(Data_Stack_At(Element, stackindex))
            ){
                goto return_error;  // last step can be tolerant, see [A]
            }
            Drop_Level(sub);  // level has no action, error report would assert
            panic (unwrap error);
        }

        if (dont_indirect) {
            possibly(LIFT_BYTE(SPARE) == NOQUOTE_3);  // bedrock introspect
            continue;
        }

        assert(Any_Lifted(SPARE));  // successful pick

        continue;  // if not last pick in tuple, pick again from this product
    }

}} check_for_updater: {

    // 1. SPARE was picked via dual protocol.  At the moment of the PICK,
    //    the picker may have been ^META, in which case we wouldn't want to
    //    decay... but otherwise we would.  But that decay was already done
    //    (it just re-lifted it) so the undecayed won't make an unstable
    //    value here if the picker wasn't ^META.

    if (tweak_is_pick) {
        definitely(spare_location_dual);  // returning this
        goto return_success;
    }

    // This may be the first time we do an update, or it may be a writeback
    // as we go back through the list of steps to update any bits that are
    // required to update in the referencing cells.

    error = Trap_Tweak_Spare_Is_Dual_Writeback_Dual_In_Scratch_To_Spare(
        level_,
        sub,
        stackindex  // picker_index
    );

    if (error)
        goto return_error;

    Stable* spare_writeback_dual_or_logic = As_Stable(SPARE);

    // Subsequent updates become pokes, regardless of initial updater function

    if (Is_Okay_Signifying_No_Writeback(spare_writeback_dual_or_logic))
        goto return_success;

    if (Is_Null_Signifying_Slot_Unavailable(spare_writeback_dual_or_logic)) {
        error = Error_Bad_Pick_Raw(Data_Stack_At(Element, stackindex));
        goto return_error;
    }

    if (stackindex_top == base + 1) {
        panic (
            "Last TWEAK* step in POKE gave non-okay writeback instruction"
        );
    }

    Move_Cell(SCRATCH, spare_writeback_dual_or_logic);  // save for next poke

    --stackindex_top;

    Restart_Action_Level(sub);
    goto poke_again;

}} return_error: { ///////////////////////////////////////////////////////////

    assert(error);

    if (tweak_is_pick)
        Corrupt_Cell_If_Needful(SPARE);  // so you don't think it picked null

    Drop_Level(sub);
    goto finalize_and_return;

} return_success: { //////////////////////////////////////////////////////////

    possibly(Is_Failure(OUT));  // success may be FAILURE! antiform, see [A]

    assert(not error);

    Drop_Level(sub);

    goto finalize_and_return;

} finalize_and_return: { /////////////////////////////////////////////////////

    Corrupt_Cell_If_Needful(SCRATCH);

    Unshield_Cell_If_Debug(OUT);

    return error;
}}


//
//  Tweak_Var_With_Dual_Scratch_To_Spare_Use_Toplevel: C
//
// SPARE may be given back as FAILURE! antiform, see [A]
//
Option(Error*) Tweak_Var_With_Dual_Scratch_To_Spare_Use_Toplevel(
    const Element* var,
    Option(Element*) steps_out  // no GROUP!s if nulled
){
    Level* level_ = TOP_LEVEL;

    possibly(SCRATCH == steps_out);
    assert(SPARE != steps_out);

    const StackIndex base = STACK_BASE;
    assert(TOP_INDEX == STACK_BASE);

    bool groups_ok = (steps_out != nullptr);
    Option(Error*) e = Trap_Push_Steps_To_Stack(var, groups_ok);
    if (e)
        return e;

    e = Tweak_Stack_Steps_With_Dual_Scratch_To_Dual_Spare();
    if (e) {
        Drop_Data_Stack_To(base);
        return e;
    }

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

    Add_Cell_Sigil(unwrap steps_out, SIGIL_TIE);  // $[bl o ck] or $word steps

    return SUCCESS;
}


//
//  /tweak: native [
//
//  "Low-level variable setter, that can assign within the dual band"
//
//      return: [
//          quasiform! quoted!
//          frame! word! ^word! ^tuple! space? parameter!
//          failure!  "Passthru even if it skips the assign"
//      ]
//      target "Word or tuple, or calculated sequence steps (from GET)"
//          [
//              <opt>
//              word! tuple!
//              ^word! ^tuple!
//              $block!
//          ]
//      dual "Ordinary GET or SET with lifted value (unlifts), else dual"
//          [
//              <opt>  "act as a raw GET of the dual state"
//              frame!  "store a GETTER/SETTER function in dual band"
//              word!  "special instructions (e.g. PROTECT, UNPROTECT)"
//              ^word! ^tuple!  "store an alias to another variable"
//              space?  "store a 'drain', which erases all assignments"
//              quasiform! quoted!  "store unlifted values as a normal SET"
//          ]
//      :groups "Allow GROUP! Evaluations"
//      :steps "Return evaluation steps for reproducible access"
//      {dual-refinement-placeholder}  ; GET and SET have :DUAL refinement
//  ]
//
DECLARE_NATIVE(TWEAK)
{
    INCLUDE_PARAMS_OF_TWEAK;
    UNUSED(ARG(STEPS));  // TBD

    if (not ARG(TARGET))
        return COPY_TO_OUT(LOCAL(DUAL));   // same for SET as [10 = (void): 10]

    Element* target = unwrap Element_ARG(TARGET);

    Option(Element*) steps;
    if (ARG(GROUPS))
        steps = GROUPS_OK;
    else
        steps = NO_STEPS;  // no GROUP! evals

    if (STATE == STATE_0)
        STATE = ST_TWEAK_TWEAKING;  // we'll set out to something not erased

    heeded (Corrupt_Cell_If_Needful(SPARE));
    heeded (Copy_Cell(SCRATCH, LOCAL(DUAL)));

    Option(Error*) e = Tweak_Var_With_Dual_Scratch_To_Spare_Use_Toplevel(
        target,
        steps
    );
    if (e)
        return fail (unwrap e);

  return_value_even_if_we_dont_assign: {

  // We want parity between (set $x expression) and (x: expression).  It's
  // very useful that you can write (e: rescue [x: expression]) and in the case
  // of an error, have the assignment skipped and the error trapped.
  //
  // Note that (set $ '^x fail "hi") will assign the failure! to X, but will
  // still pass through the failure as the overall expression result.

    if (ARG(DUAL))
        return COPY_TO_OUT(LOCAL(DUAL));

    return COPY_TO_OUT(SPARE);
}}
