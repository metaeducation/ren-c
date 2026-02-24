//
//  file: %c-typechecker.c
//  summary: "Function generator for an optimized typechecker"
//  section: datatypes
//  project: "Ren-C Language Interpreter and Run-time Environment"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016-2024 Ren-C Open Source Contributors
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
// Making a typechecker can be easy:
//
//     >> integer?: lambda [v [any-stable?]] [integer! = type of :v]
//
//     >> integer? 10
//     == \~okay~\  ; antiform
//
//     >> integer? <foo>
//     == \~null~\  ; antiform
//
// But given that it is done so often, it's more efficient to have these done
// via optimized "intrinsic" (frameless) functions...created at boot time.
//

#include "sys-core.h"


//
//  /typechecker-archetype: pure native:intrinsic [
//
//  "For internal use (builds parameters and return slot)"
//
//      return: '[logic!]
//      value '[any-stable?]
//      :type "Test a concrete type, (integer?:type integer!) passes"
//      :quoted
//      :quasiform
//      :tied
//      :pinned
//      :metaform
//  ]
//
DECLARE_NATIVE(TYPECHECKER_ARCHETYPE)
{
    INCLUDE_PARAMS_OF_TYPECHECKER_ARCHETYPE;

    Details* details = Level_Intrinsic_Details(LEVEL);
    if (Details_Max(details) != MAX_IDX_TYPECHECKER)
        panic ("TYPECHECKER-ARCHETYPE called (internal use only)");

    Stable* v = ARG(VALUE);

    Option(Type) type = Type_Of(v);

    if (Is_Null(v)) {  // stop casual use of (integer? var) when null
        if (type == TYPE_LOGIC)
            return LOGIC_OUT(true);
        return fail (Error_Type_Test_Null_Raw());  // !!! Review: dumb idea?
    }

    if (Not_Level_Flag(LEVEL, DISPATCHING_INTRINSIC)) {
        bool check_datatype = ARG(TYPE);
        if (check_datatype) {
            if (not Is_Datatype(v))
                return fail (
                    "Datatype check on non-datatype (use TRY for NULL)"
                );

            if (
                ARG(QUOTED)
                or ARG(TIED) or ARG(PINNED) or ARG(METAFORM)
            ){
                return fail (Error_Bad_Refines_Raw());
            }

            type = Datatype_Type(v);
        }
        else {
            if (ARG(QUOTED)) {
                if (Is_Antiform(v) or Quotes_Of(cast(Element*, v)) == 0)
                    return LOGIC_OUT(false);
                Noquotify_Cell(cast(Element*, v));
            }

            type = Type_Of(v);

            if (ARG(QUASIFORM)) {
                if (type != TYPE_QUASIFORM)
                    return LOGIC_OUT(false);

                Unquasify(cast(Element*, v));
                type = Type_Of(v);
            }

            if (ARG(TIED)) {
                if (ARG(PINNED) or ARG(METAFORM))
                    return fail (Error_Bad_Refines_Raw());

                if (type != TYPE_TIED)
                    return LOGIC_OUT(false);

                type = Heart_Of(v);
            }
            else if (ARG(PINNED)) {
                if (ARG(METAFORM))
                    return fail (Error_Bad_Refines_Raw());

                if (type != TYPE_PINNED)
                    return LOGIC_OUT(false);

                type = Heart_Of(v);
            }
            else if (ARG(METAFORM)) {
                if (type != TYPE_METAFORM)
                    return LOGIC_OUT(false);

                type = Heart_Of(v);
            }
        }
    }

    TypesetByte typeset_byte = VAL_UINT8(
        Details_At(details, IDX_TYPECHECKER_TYPESET_BYTE)
    );

    if (not type)  // not a built-in type, no typechecks apply
        return LOGIC_OUT(false);

    return LOGIC_OUT(Builtin_Typeset_Check(typeset_byte, unwrap type));
}


//
//  /unstable-typechecker-archetype: pure native:intrinsic [
//
//  "For internal use (builds parameters and return slot)"
//
//      return: '[logic!]
//      ^value '[any-value?]
//  ]
//
DECLARE_NATIVE(UNSTABLE_TYPECHECKER_ARCHETYPE)
{
    INCLUDE_PARAMS_OF_UNSTABLE_TYPECHECKER_ARCHETYPE;

    Details* details = Level_Intrinsic_Details(LEVEL);
    if (Details_Max(details) != MAX_IDX_TYPECHECKER)
        panic ("UNSTABLE-TYPECHECKER-ARCHETYPE called (internal use only)");

    Value* v = ARG(VALUE);

    if (Is_Cell_Stable(v))  // shortcut, if it's stable, don't match!
        return LOGIC_OUT(false);

    TypesetByte typeset_byte = VAL_UINT8(
        Details_At(details, IDX_TYPECHECKER_TYPESET_BYTE)
    );

    if (typeset_byte == i_cast(TypesetByte, TYPE_FAILURE))
        return LOGIC_OUT(Is_Failure(v));

    require (
      Ensure_No_Failures_Including_In_Packs(v)
    );

    if (typeset_byte == i_cast(TypesetByte, TYPE_VOID)) {
        if (Is_Heavy_Void(v))
            panic ("HEAVY VOID passed to VOID?; use ANY-VOID? to test both");
        return LOGIC_OUT(Is_Void(v));
    }

    Option(Type) type = Type_Of_Possibly_Unstable(v);

    return LOGIC_OUT(Builtin_Typeset_Check(typeset_byte, unwrap type));
}


//
//  Make_Typechecker: C
//
// The typecheckers for things like INTEGER? and ANY-SERIES? are all created
// at boot time.
//
// Each typechecker is implemented by by using up to 255 typeset descriptions
// in g_typesets[].  This means that if a parameter recognizes that some of
// the type checking functions are typecheckers, it can extract the TypesetByte
// encoded in that function... and then just have the table of bytes to blast
// through when type checking the arguments.
//
// 1. While space is limited in the PARAMETER! Cell, it has a packed array of
//    bytes it uses to store cached TypesetByte.  This way it doesn't look
//    them up each time.  0 is reserved to prematurely terminate the array.
//
// 2. We need a spec for our typecheckers, which comes from the built-by-hand
//    native TYPECHECKER-ARCHETYPE.
//
Details* Make_Typechecker(TypesetByte typeset_byte)  // parameter cache [1]
{
    TypesetFlags typeset = g_typesets[typeset_byte];

    const Value* archetype;
    Dispatcher* dispatcher;

    attempt {
        if (not (typeset & TYPESET_FLAG_0_RANGE))  // bits
            break;  // do stable check

        Type start = i_cast(Type, THIRD_BYTE(&typeset));
        Type end = i_cast(Type, FOURTH_BYTE(&typeset));
        if (start != end)  // nontrivial range
            break;

        if (start <= MAX_TYPE_STABLE)  // don't need unstable checks
            break;

        assert(typeset_byte == i_cast(TypesetByte, start));
        continue;
    }
    then {
        archetype = LIB(UNSTABLE_TYPECHECKER_ARCHETYPE);
        dispatcher = NATIVE_CFUNC(UNSTABLE_TYPECHECKER_ARCHETYPE);
    }
    else {
        archetype = LIB(TYPECHECKER_ARCHETYPE);
        dispatcher = NATIVE_CFUNC(TYPECHECKER_ARCHETYPE);
    }

    Details* details = Make_Dispatch_Details(
        BASE_FLAG_MANAGED
            | DETAILS_FLAG_CAN_DISPATCH_AS_INTRINSIC
            | DETAILS_FLAG_RAW_NATIVE,
        archetype,  // use archetype's paramlist [2]
        dispatcher,
        MAX_IDX_TYPECHECKER  // details array capacity
    );
    assert(Get_Details_Flag(details, PURE));  // inherit purity from archetype

    assert(MAX_IDX_RAW_NATIVE == 1);  // RETURN
    assert(MAX_IDX_TYPECHECKER == MAX_IDX_RAW_NATIVE + 1);

    Details* archetype_details = Ensure_Phase_Details(Frame_Phase(archetype));
    Copy_Cell(
        Details_At(details, IDX_RAW_NATIVE_RETURN),
        Details_At(archetype_details, IDX_RAW_NATIVE_RETURN)
    );

    Init_Integer(
        Details_At(details, IDX_TYPECHECKER_TYPESET_BYTE),
        typeset_byte
    );

    return details;
}


//
//  Typecheck_Pack_Use_Toplevel: C
//
// It's possible in function type specs to check packs with ~(...)~ notation.
// This routine itemwise checks a pack against one of those type specs, with
// TEXT! strings ignored as descriptions of the fields.
//
// 1. Due to the way that the intrinsic optimization works, it has to have
//    the argument to the intrinsic in the spare...and it uses the scratch
//    cell for putting the intrinsic action itself value into.
//
// 2. Note that blocks are legal, as in ~([integer! word!] object!)~, which
//    means that the first item in the pack can be either an integer or word.
//    So we don't call Typecheck_Unoptimized_Use_Toplevel() here.
//
bool Typecheck_Pack_Use_Toplevel(  // scratch and spare used [1]
    Level* const L,
    const Value* pack,
    const Element* types
){
    USE_LEVEL_SHORTHANDS (L);

    assert(Is_Quasi_Group(types));  // could relax this to any list
    assert(Is_Pack(pack));  // could relax this also to any list

    const Element* pack_tail;
    const Element* pack_at = List_At(&pack_tail, pack);

    const Element* types_tail;
    const Element* types_at = List_At(&types_tail, types);

    bool result = true;

    Context* types_binding = List_Binding(types);

    while (types_at != types_tail) {
        if (Is_Text(types_at)) {  // allow descriptions inside the pack
            ++types_at;  // advance types block but not pack item
            continue;
        }

        DECLARE_VALUE (unlifted);
        Copy_Cell(unlifted, pack_at);
        assume (
          Unlift_Cell_No_Decay(unlifted)
        );
        if (not Typecheck_Use_Toplevel(  // might be BLOCK!, etc [2]
            L, unlifted, types_at, types_binding
        )){
            result = false;
            goto return_result;
        }
        ++pack_at;
        ++types_at;
    }

  return_result:

  #if RUNTIME_CHECKS
    Init_Unreadable(SCRATCH);
    Init_Unreadable(SPARE);
  #endif

    UNUSED(LEVEL);

    return result;
}


//
//  Predicate_Check_Spare_Uses_Scratch: C
//
bool Predicate_Check_Spare_Uses_Scratch(
    Level* const L,
    const Value* predicate,
    Option(const Symbol*) label
){
    USE_LEVEL_SHORTHANDS (L);

    assert(Is_Action(predicate) or Is_Possibly_Unstable_Value_Frame(predicate));

    bool result;

    assert(predicate != SCRATCH and predicate != SPARE);

  try_builtin_typeset_checker_dispatch: {

  // The fastest (and most common case) is when we recognize the dispatcher as
  // being the Typechecker_Dispatcher().  This means it's one of 255 built-in
  // type checks, such as ANY-WORD? or INTEGER? or INTEGER!.

    Details* details = opt Try_Frame_Details(predicate);
    if (not details)
        goto non_intrinsic_dispatch;

    Dispatcher* dispatcher = Details_Dispatcher(details);
    if (dispatcher == NATIVE_CFUNC(TYPECHECKER_ARCHETYPE)) {
        TypesetByte typeset_byte = VAL_UINT8(
            Details_At(details, IDX_TYPECHECKER_TYPESET_BYTE)
        );
        Option(Type) type = Type_Of_Possibly_Unstable(SPARE);
        if (Builtin_Typeset_Check(typeset_byte, unwrap type))
            goto test_succeeded;
        goto test_failed;
    }

  try_dispatch_as_intrinsic: {

  // Second-fastest are "intrinsic" typechecks.  These functions are designed
  // to be called without a FRAME! in cases where they only take one argument.

  #if (! DEBUG_DISABLE_INTRINSICS)
    if (
        Get_Details_Flag(details, CAN_DISPATCH_AS_INTRINSIC)
        and not SPORADICALLY(100)
    ){
        Param* param = Phase_Params_Head(details);
        if (
            Is_Cell_A_Veto_Hot_Potato(SPARE)
            and Not_Parameter_Flag(param, WANT_VETO)
        ){
            goto test_failed;
        }

        if (Get_Parameter_Flag(param, UNDO_OPT) and Any_Void(SPARE)) {
            Init_Null(SPARE);
        }
        else if (Parameter_Class(param) != PARAMCLASS_META) {
          require (
            Decay_If_Unstable(SPARE)  // decay may eval, do before intrinsic
          );
        }

        Copy_Plain_Cell(SCRATCH, predicate);  // intrinsic, panic() requires
        possibly(Is_Antiform(SCRATCH));  // don't bother canonizing TYPE_BYTE()
        Remember_Cell_Is_Lifeguard(SCRATCH);

        assert(Not_Level_Flag(L, DISPATCHING_INTRINSIC));
        Set_Level_Flag(L, DISPATCHING_INTRINSIC);
        Set_Level_Flag(L, RUNNING_TYPECHECK);
        Bounce bounce = Apply_Cfunc(dispatcher, L);
        Clear_Level_Flag(L, RUNNING_TYPECHECK);
        Clear_Level_Flag(L, DISPATCHING_INTRINSIC);

        Corrupt_Cell_If_Needful(SPARE);  // predicate is allowed to trash it
        Forget_Cell_Was_Lifeguard(SCRATCH);

        if (bounce == nullptr) {
            if (g_failure) {  // was NEEDFUL_RESULT_0 (fail/panic)
                Needful_Test_And_Clear_Failure();  // counts as test failed
            }
            goto test_failed;  // was just `return nullptr`
        }
        if (bounce == BOUNCE_OKAY)
            goto test_succeeded;

        assert(bounce == L->out);  // no BOUNCE_CONTINUE, API vals, etc
        if (Is_Failure(L->out))
            panic (Cell_Error(L->out));
        panic (Error_No_Logic_Typecheck(label));
    }
  #endif

  goto non_intrinsic_dispatch;

}} non_intrinsic_dispatch: { /////////////////////////////////////////////////

    Flags flags = 0;
    require (
      Level* sub = Make_End_Level(
        &Action_Executor,
        FLAG_STATE_BYTE(ST_ACTION_TYPECHECKING) | flags
    ));
    Push_Level(Erase_Cell(SCRATCH), sub);  // sub's out is L->scratch
    require (
      Push_Action(sub, predicate, PREFIX_0)
    );

  copy_exemplar_frame: {

    Mem_Copy(  // note: copies TRACK_FLAG_SHIELD_FROM_WRITES, cleared later
        sub->u.action.arg,
        sub->u.action.param,
        sizeof(Cell) * (sub->u.action.key_tail - sub->u.action.key)
    );

} fill_in_first_argument: {

    const Param* param;
    Arg* arg = First_Unspecialized_Arg(&param, sub);
    if (not arg)
        panic (Error_No_Arg_Typecheck(label));  // must take argument

    Clear_Param_Shield_If_Tracking(arg);
    Copy_Cell(arg, SPARE);  // do not decay [4]

    require (
      bool check = Typecheck_Coerce_Use_Toplevel(
        sub, Known_Unspecialized(param), cast(Value*, arg)
      )
    );
    if (not check) {
        Drop_Action(sub);
        Drop_Level(sub);
        goto test_failed;
    }

} execute_function: {

    if (Trampoline_With_Top_As_Root_Throws())
        panic (Error_No_Catch_For_Throw(sub));

    Drop_Level(sub);

    if (Is_Failure(SCRATCH))
        goto test_failed;  // e.g. see NULL? for its FAILURE! on heavy null

    Stable* scratch = As_Stable(SCRATCH);

    if (not Is_Logic(scratch))  // sub wasn't limited to intrinsics
        panic (Error_No_Logic_Typecheck(label));

    if (Cell_Logic(scratch))
        goto test_succeeded;

    goto test_failed;

}} test_failed: { ////////////////////////////////////////////////////////////

    result = false;
    goto return_result;

} test_succeeded: { //////////////////////////////////////////////////////////

    result = true;
    goto return_result;

} return_result: { ///////////////////////////////////////////////////////////

  #if RUNTIME_CHECKS
    Init_Unreadable(SCRATCH);
  #endif

    return result;
}}


// This mechanically walks through an array of type spec elements, checking
// the more complex forms that can't be optimized into a TypesetByte or
// a flag.
//
static bool Typecheck_Unoptimized_Use_Toplevel(
    Level* const L,
    const Value* v,
    const Element* at,
    const Element* tail,
    Context* binding,
    bool match_all
){
    USE_LEVEL_SHORTHANDS (L);

    bool result;

    DECLARE_VALUE (test);
    Push_Lifeguard(test);

    if (at == tail)
       goto end_looping_over_tests;  // might mean all match or no match

  check_spare_against_test_in_at: { //////////////////////////////////////////

  // 1. Some elements in the parameter spec array are accounted for in type
  //    checking by flags or optimization bytes.  There is no need to check
  //    those here, just check things that don't have TYPE_MARKED_SPOKEN_FOR.
  //
  // 2. [~word!~] matches QUASI-WORD? and ['integer!] matches QUOTED-INTEGER?
  //    and [''~any-series?~] matches DOUBLE_QUOTED-QUASI-ANY-SERIES?... etc.
  //    Because we don't want to make an infinite number of type constraint
  //    functions to cover every combination.  This pretty much ties our hands
  //    on the meaning of quasiform or quoted WORD!s in the type spec, and
  //    we apply that further to sequences [e.g. [word!: /tuple!]]
  //
  // 3. While TAG! will have TYPE_MARKED_SPOKEN_FOR in a PARAMETER!, it does not
  //    in a plain BLOCK! used with TYPECHECK.  TYPECHECK could pay to convert
  //    BLOCK! to PARAMETER! but it's cheaper if we are willing to process a
  //    block on the fly.  This handles <null> and <void> tags but it should
  //    probably have behavior for <opt> and other parameter spec tags, though
  //    it's tricky given that typecheck can't mutate the incoming value.

    if (Get_Cell_Flag(at, TYPE_MARKED_SPOKEN_FOR))  // already checked [1]
        goto continue_loop;

    if (
        Heart_Of(at) == HEART_WORD  // our hands are tied on the meaning [2]
        or Any_Sequence_Type(Heart_Of(at))
    ){
        goto adjust_quote_level_and_run_type_constraint;
    }

    if (Is_Quasiform(at))
        goto handle_non_word_quasiform;

    if (Is_Quoted(at))
        goto handle_non_word_quoted;

    if (Any_Sigiled_Blank(at)) {
        if (
            not Is_Antiform(v)
            and Any_Sigiled_Blank(As_Element(v))
            and Sigil_Of(at) == Sigil_Of(As_Element(v))
        ){
            goto test_succeeded;
        }
        goto test_failed;
    }

    if (Is_Tag(at)) {  // if BLOCK!, support <null> and <void> [3]
        if (0 == CT_Utf8(at, g_tag_null, true)) {
            if (Is_Light_Null(v))
                goto test_succeeded;
            goto test_failed;
        }
        if (0 == CT_Utf8(at, g_tag_void, true)) {
            if (Any_Void(v))
                goto test_succeeded;
            goto test_failed;
        }
    }

    panic (at);

  handle_non_word_quasiform: { ///////////////////////////////////////////////

  // 1. ~(integer! word!)~ is a typecheck that matches a 2-element PACK! with
  //    an integer and a word.  It's recursive, packs can contain packs, etc.
  //
  // 2. Because people might build a type spec block by composing, they might
  //    REIFY an antiform DATATYPE! directly into the spec, something like:
  //
  //        type: word!
  //        compose [return: [integer! (reify type)] ...]
  //
  //    If this happens, they'll get a quasiform FENCE!.  So the friendliest
  //    choice of interpretation of that is as the DATATYPE! it represents.
  //
  // 3. When 'XXX! began matching quoted things of type XXX!, ['true 'false]
  //    stopped being how to match against literal words true and false.
  //    Quasiform group of [~[true false]~] actually looks kind of good; it
  //    will match any of the single items in the group literally.

    if (Heart_Of(at) == HEART_GROUP) {  // typecheck pack [1]
        if (not Is_Pack(v))
            goto test_failed;
        if (Typecheck_Pack_Use_Toplevel(L, v, at))
            goto test_succeeded;
        goto test_failed;
    }

    if (Heart_Of(at) == HEART_FENCE) {  // interpret as datatype [2]
        panic ("Quasiform FENCE! in type spec not supported yet");
    }

    if (Heart_Of(at) == HEART_BLOCK) {  // match any element literally [3]
        if (Is_Antiform(v))
            goto test_failed;  // can't match elements against antiforms

        const Element* splice_tail;
        const Element* splice_at = List_At(&splice_tail, at);

        for (; splice_at != splice_tail; ++splice_at) {
            bool strict = true;  // system now case-sensitive by default
            require (
              bool equal = Equal_Values(
                As_Element(v),
                splice_at,
                strict
            ));
            if (equal)
                goto test_succeeded;
        }
        goto test_failed;
    }

    panic (at);

} handle_non_word_quoted: { //////////////////////////////////////////////////

  // It's not clear exactly what non-word quoteds would do.  It could be that
  // '[integer! word!] would match a non-quoted BLOCK! with 2 elements in it
  // that were an integer and a word, for instance.  But anything we do would
  // be inconsistent with the WORD! interpretation that it's exactly the same
  // quoting level as the quotes on the test.
  //
  // Review when this gets further.

    panic (at);

} adjust_quote_level_and_run_type_constraint: {

    Copy_Cell(SPARE, v);
    Element* scratch = Copy_Cell(SCRATCH, at);

    if (Type_Of_Raw(scratch) > MAX_TYPE_NOQUOTE_NOQUASI) {
        if (not Have_Matching_Lift_Levels(scratch, As_Stable(SPARE)))
            goto test_failed;  // should be willing to accept subset quotes

        Clear_Cell_Quotes_And_Quasi(scratch);
        Clear_Cell_Quotes_And_Quasi(As_Element(SPARE));
    }

    Option(Sigil) scratch_sigil = Cell_Underlying_Sigil(scratch);
    Option(Sigil) v_sigil = Cell_Underlying_Sigil(SPARE);
    if (scratch_sigil) {
        if (scratch_sigil != v_sigil)
            goto test_failed;

        Clear_Cell_Sigil(scratch);
        if (not Is_Antiform(SPARE))
            Clear_Cell_Sigil(As_Element(SPARE));
        scratch_sigil = none;
        v_sigil = none;
    }

    const Symbol* label;

    if (not Any_Sequence_Type(Heart_Of(at))) {
        label = Word_Symbol(at);
        goto handle_after_any_quoting_adjustments;
    }

    if (Type_Of_Raw(SPARE) > MAX_TYPE_NOQUOTE_NOQUASI or v_sigil)
        goto test_failed;  // 'foo: or @foo: won't match word!:

  check_destructured_sequence: {

  // We want to be able to match things like `/[x]:` against a constraint
  // like `[/block!:]`.  Doing so requires going multiple levels deep into the
  // match (first matching against a PATH!, and then matching against a
  // CHAIN! that's inside the PATH!, etc.)
  //
  // This requires not only destructuring the item we're checking (which has
  // been copied into SPARE) but destructuring the typespec item as well...
  // which is being iterated and is const.  So we copy it into SCRATCH.
  //
  // Ultimately this could be generalized to support `integer!/word!:group!`
  // or things that aren't "SingleHeart" sequences.  But for now, this is
  // geared toward just breaking down singleheart sequences to make sure they
  // match at each sequence level, and bottom out in a WORD! that can be used
  // to check the type of the unitary matching element dug into in SPARE.

    do {
        SingleHeart singleheart = opt Try_Get_Sequence_Singleheart(scratch);
        if (not singleheart)
            panic ("Non-Singleheart sequence in typespec dialect unsupported");

        if (Heart_Of_Builtin(SPARE) != Heart_Of_Builtin(scratch))
            goto test_failed;  // must be seq of same type

        if (not Try_Get_Sequence_Singleheart(SPARE))
            goto test_failed;

        if (
            Get_Cell_Flag(SPARE, LEADING_BLANK)  // XXX: doesn't match :XXX
            != Get_Cell_Flag(scratch, LEADING_BLANK)
        ){
            goto test_failed;
        }

        assume (
          Unsingleheart_Sequence(As_Element(SPARE))
        );
        assume (
          Unsingleheart_Sequence(scratch)
        );
    }
    while (Any_Sequence_Type(Heart_Of(scratch)));

    if (not Is_Word(scratch))
        panic ("Non-WORD! in destructured typespec sequence unsupported ATM");

    label = Word_Symbol(scratch);

} handle_after_any_quoting_adjustments: {

    DECLARE_ELEMENT (temp_item_word);
    Init_Word(temp_item_word, label);  // v-- this used Cell_Binding(at) (?)
    Tweak_Cell_Binding(temp_item_word, Cell_Binding(at));
    Bind_Cell_If_Unbound(temp_item_word, binding);
    Add_Cell_Sigil(temp_item_word, SIGIL_META);

    require (
      Get_Word_Or_Tuple(test, temp_item_word)
    );

    if (Is_Action(test)) {
        if (Predicate_Check_Spare_Uses_Scratch(L, test, label))
            goto test_succeeded;
        goto test_failed;
    }

    require (
        Stable* stable_test = Decay_If_Unstable(test)
    );

    switch (opt Type_Of_Unchecked(stable_test)) {
      case TYPE_PARAMETER:  // !! Problem: spare use
        if (Typecheck_Use_Toplevel(L, SPARE, stable_test, SPECIFIED))
            goto test_succeeded;
        goto test_failed;

      case TYPE_DATATYPE: {
        Option(Type) t = Type_Of_Possibly_Unstable(SPARE);
        if (t) {  // builtin type
            if (Datatype_Type(stable_test) == (unwrap t))
                goto test_succeeded;
            goto test_failed;
        }
        if (Datatype_Extra_Heart(stable_test) == Cell_Extra_Heart(SPARE))
            goto test_succeeded;
        goto test_failed; }

      default:
        break;
    }

    panic ("Invalid element in TYPE-GROUP!");

}} test_succeeded: {

    if (not match_all) {
        result = true;
        goto return_result;
    }
    goto continue_loop;

} test_failed: {

    if (match_all) {
        result = false;
        goto return_result;
    }
    goto continue_loop;

}} continue_loop: {

    ++at;
    if (at != tail)
        goto check_spare_against_test_in_at;

} end_looping_over_tests: {

    if (match_all)
        result = true;
    else
        result = false;

    goto return_result;

} return_result: {

    Corrupt_Cell_If_Needful(SCRATCH);
    Corrupt_Cell_If_Needful(SPARE);

    Drop_Lifeguard(test);

    return result;
}}



//
//  Typecheck_Use_Toplevel: C
//
// 1. SPARE and SCRATCH are GC-safe cells in a Level that are usually free
//    for whatever purposes an Executor wants.  But when a Level is being
//    multiplexed with intrinsics (see DETAILS_FLAG_CAN_DISPATCH_AS_INTRINSIC)
//    it has to give up those cells for the duration of that call.  Type
//    checking uses intrinsics a vast majority of the time, so this function
//    ensures you don't rely on SCRATCH or SPARE not being modified (whether
//    it ends up needing to use them in a specific call or not).
//
// 2. Currently PARAMETER! is stored quoted in the RETURN slot of functions
//    as an optimization.  It may be that the typechecked bit could be used
//    to have it not be quoted and yet not gather a local from the callsite,
//    this is under review.
//
bool Typecheck_Use_Toplevel(
    Level* const L,
    const Value* v,
    const Cell* tests,
    Context* tests_binding
){
    possibly(Type_Of_Raw(tests) == BEDROCK_255);  // unspecialized slot

    USE_LEVEL_SHORTHANDS (L);

    Corrupt_Cell_If_Needful(SCRATCH);  // SCRATCH/SPARE used as workspaces [1]
    Corrupt_Cell_If_Needful(SPARE);

    const Element* at;
    const Element* tail;
    Context* derived;
    bool match_all;

    if (Heart_Of(tests) != HEART_PARAMETER)  // note PARAMETER! maybe quoted [2]
        goto handle_non_parameter;

  handle_parameter: {

    const Cell* param = tests;

    const Array* array = opt Parameter_Spec(tests);
    if (array == nullptr) {
        definitely(Is_Parameter_Unconstrained(tests));  // meaning of nullptr

        if (Not_Parameter_Flag(tests, REFINEMENT))
            return true;  // unconstrained non-refinement permits anything

        return (
            Is_Possibly_Unstable_Value_Okay(v)
            or Is_Null_Signifying_Unspecialized(v)
        );
    }

  try_parameter_flag_optimizations: {

    if (Is_Antiform(v)) {
        if (Get_Parameter_Flag(param, NULL_DEFINITELY_OK))
            if (Is_Light_Null(v))  // heavy null becomes light after decay
                return true;

        if (Get_Parameter_Flag(param, VOID_DEFINITELY_OK))
            if (Any_Void(v))
                return true;

        if (Get_Parameter_Flag(param, TRASH_DEFINITELY_OK))
            if (Is_Trash(v))
                return true;
    }

    if (Get_Parameter_Flag(param, ANY_STABLE_OK) and Is_Cell_Stable(v))
        return true;

    if (Get_Parameter_Flag(param, ANY_ATOM_OK))
        return true;

    if (Get_Parameter_Flag(param, SPACE_DEFINITELY_OK))  // !!! worth it?
        if (Is_Cell_Stable(v) and Is_Space(As_Stable(v)))
            return true;

} try_parameter_byte_optimizations: {

    const Array* spec = opt Parameter_Spec(param);
    const TypesetByte* optimized = spec->misc.at_least_4;
    const TypesetByte* optimized_tail
        = optimized + sizeof(spec->misc.at_least_4);

    Option(Type) type = Type_Of_Possibly_Unstable(v);
    for (; optimized != optimized_tail; ++optimized) {
        if (*optimized == 0)
            break;  // premature end of list

        if (Builtin_Typeset_Check(*optimized, type))
            return true;  // ELEMENT?/FUNDAMENTAL? test TYPE_0 types
    }

} no_parameter_optimizations_applied: {

    if (Not_Parameter_Flag(param, INCOMPLETE_OPTIMIZATION))
        return false;  // all tests accounted for by optimizations, so fail

    at = Array_Head(array);
    tail = Array_Tail(array);
    derived = SPECIFIED;
    match_all = false;

    goto call_unoptimized_checker;  // for spec items not TYPE_MARKED_SPOKEN_FOR

}} handle_non_parameter: {

    Type type = opt Type_Of(As_Stable(tests));

    if (type == TYPE_WORD or type == TYPE_QUASIFORM or Is_Quoted_Type(type)) {
        at = cast(Element*, tests);
        tail = cast(Element*, tests) + 1;
        derived = tests_binding;
        match_all = true;
    }
    else switch (type) {
      case TYPE_DATATYPE: {
        Option(Type) t_test = Datatype_Type(As_Stable(tests));
        Option(Type) t = Type_Of_Possibly_Unstable(v);
        if (t_test)
            return t == unwrap t_test;  // builtin type, must match
        if (t)
            return false;  // non-builtin type can't match builtin type
        return Datatype_Extra_Heart(As_Stable(tests)) == Cell_Extra_Heart(v); }

      case TYPE_BLOCK:
        at = List_At(&tail, tests);
        derived = Derive_Binding(tests_binding, As_Element(tests));
        match_all = false;
        break;

      case TYPE_SPLICE: {  // literal match of splice values
        if (Is_Antiform(v))
            return false;  // can't match list elements against antiforms

        at = List_At(&tail, tests);
        for (; at != tail; ++at) {
            bool strict = true;  // system now case-sensitive by default
            require (
              bool equal = Equal_Values(As_Element(v), at, strict)
            );
            if (equal)
                return true;
        }
        return false; }

      case TYPE_GROUP:
        at = List_At(&tail, tests);
        derived = Derive_Binding(tests_binding, As_Element(tests));
        match_all = true;
        break;

      case TYPE_FRAME:
      case TYPE_ACTION:
        Copy_Cell(SPARE, v);
        return Predicate_Check_Spare_Uses_Scratch(
            L,
            As_Value(tests),
            Frame_Label(tests)
        );

      default:
        assert(false);
        panic ("Bad test passed to Typecheck_Value");
    }

    goto call_unoptimized_checker;

} call_unoptimized_checker: {

    return Typecheck_Unoptimized_Use_Toplevel(
        L, v, at, tail, derived, match_all
    );
}}


//
//  Typecheck_Coerce_Use_Toplevel: C
//
// This has some steps that are beyond the basic typechecking, where the
// Parameter_Class() being ^META or not is taken into account for decay and
// coercion.  It also applies the <const> property.
//
Result(bool) Typecheck_Coerce_Use_Toplevel(
    Level* const L,
    const Cell* param,
    Value* v  // not `const Value*` -- coercion needs mutability
){
    possibly(Type_Of_Raw(param) == BEDROCK_255);  // unspecialized slot

    USE_LEVEL_SHORTHANDS (L);

    Corrupt_Cell_If_Needful(SCRATCH);  // always corrupt...
    Corrupt_Cell_If_Needful(SPARE);  // ...since we always might...

    bool result;

    bool coerced = false;

  mandatory_checking: {

  // We could say that applying the CONST bit would be the responsibility of
  // the callee in the case of opted out typechecking, but that doesn't seem
  // to have a lot of value.  It wasn't the const bit test they're seeking
  // to avoid paying for.
  //
  // VETO mechanics are handled at a higher level than typechecking, as
  // typechecking never gets called at all.
  //
  // 1. !!! Should explicit mutability override, so people can say things
  //    like (foo: func [...] mutable [...]) ?  This seems bad, because the
  //    contract of the function hasn't been "tweaked" with reskinning.

    if (Get_Parameter_Flag(param, CONST))
        Set_Cell_Flag(v, CONST);  // mutability override? [1]

    if (Not_Parameter_Flag(param, WANT_VETO))  // veto handled before here
        assert(not Is_Cell_A_Veto_Hot_Potato(v));

    if (Get_Parameter_Flag(param, UNDO_OPT))
        assert(not Any_Void(v));  // shouldn't get here

    if (Not_Cell_Stable(v) and Parameter_Class(param) != PARAMCLASS_META) {
        trap (
            Decay_If_Unstable(v)
        );
    }

} bypass_typecheck_if_typespec_was_quoted: {

    if (Not_Parameter_Checked_Or_Coerced(param))
        return true;  // skip all the non-mandatory checking

    goto call_typecheck;

} call_typecheck: {  /////////////////////////////////////////////////////////

    if (Typecheck_Use_Toplevel(L, v, param, SPECIFIED))
        goto return_true;

    if (coerced)
        goto return_false;  // only coerce once

    if (Is_Cell_Stable(v))
        goto return_false;

    if (Is_Failure(v))
        goto return_false;

    if (Is_Void(v))
        goto return_false;

    if (Is_Trash(v))
        goto return_false;

    if (Is_Action(v)) {
        Deactivate_Action(v);
    }
    else {
        trap (
          Decay_If_Unstable(v)
        );
    }

    coerced = true;
    goto call_typecheck;

} return_false: { ////////////////////////////////////////////////////////////

    result = false;
    goto return_result;

} return_true: { /////////////////////////////////////////////////////////////

    result = true;
    goto return_result;

} return_result: { ///////////////////////////////////////////////////////////

    if (Get_Parameter_Flag(param, UNBIND_ARG))
        Unbind_Cell_If_Bindable_Core(v);

  #if RUNTIME_CHECKS
    if ((result == true) and Not_Cell_Stable(v))
        assert(Parameter_Class(param) == PARAMCLASS_META);
  #endif

  #if NEEDFUL_DOES_CORRUPTIONS
    assert(Not_Cell_Readable(SCRATCH));
    assert(Not_Cell_Readable(SPARE));
  #endif

    UNUSED(LEVEL);

    return result;
}}


//
//  Init_Typechecker: C
//
// Give back an action antiform which can act as a checker for a datatype.
//
Result(Value*) Init_Typechecker(
    Init(Value) out,
    const Stable* datatype_or_block
){
    possibly(u_cast(Value*, out) == datatype_or_block);

    if (Is_Datatype(datatype_or_block)) {
        Option(Type) t = Datatype_Type(datatype_or_block);
        if (not t)
            panic ("TYPECHECKER does not support extension types yet");

        Byte type_byte = i_cast(Byte, unwrap t);
        SymId16 id16 = i_cast(SymId16, type_byte) + MIN_SYM_TYPESETS - 1;
        assert(id16 == type_byte);  // MIN_SYM_TYPESETS should be 1

        Copy_Cell(out, Lib_Value(i_cast(SymId, id16)));
        assert(Ensure_Frame_Details(out));  // need TypesetByte

        return out;
    }

    require (
      Level* const L = Make_End_Level(&Just_Use_Out_Executor, LEVEL_MASK_NONE)
    );
    Push_Level(Erase_Cell(out), L);

    USE_LEVEL_SHORTHANDS (L);

    Init_Set_Word(PUSH(), CANON(TEST));

    const Element* block = As_Element(datatype_or_block);
    Init_Unconstrained_Parameter(
        PUSH(), FLAG_PARAMCLASS_BYTE(PARAMCLASS_NORMAL)
    );

    bool is_returner = false;
    trap (
      Set_Spec_Of_Parameter_In_Top(L, block, Cell_Binding(block), is_returner)
    );

    Element* def = Init_Block(SCRATCH, Pop_Source_From_Stack(STACK_BASE));

    bool threw = Specialize_Action_Throws(
        out, LIB(TYPECHECK), def, TOP_INDEX  // !!! should be TYPECHECK
    );

    if (threw)
        panic (Error_No_Catch_For_Throw(TOP_LEVEL));

    Drop_Level(L);

    return out;
}


//
//  /typechecker: pure native [
//
//  "Make a function for checking types (generated function gives LOGIC!)"
//
//      return: [action!]
//      types [datatype! block!]
//  ]
//
DECLARE_NATIVE(TYPECHECKER)
//
// Compare with MATCHER:
//
//    >> t: typechecker [<null> integer!]
//
//    >> t 1020
//    == ~okay~  ; anti
//
//    >> t <abc>
//    == ~null~  ; anti
//
//    >> t null
//    == ~okay~  ; anti
{
    INCLUDE_PARAMS_OF_TYPECHECKER;

    require (
      Init_Typechecker(OUT, ARG(TYPES))
    );
    return OUT;
}


//
//  /typecheck: native [
//
//  "Same typechecking as function arguments"
//
//      return: [logic! failure!]  ; returns error vs. panic [1]
//      test [block! datatype! splice! parameter! frame!]
//      ^value [any-value?]
//      :meta "Don't pre-decay argument (match ^META argument mode)"
//  ]
//
DECLARE_NATIVE(TYPECHECK)
//
// 1. The reason a :META typecheck that fails with FAILURE! returns the error
//    is to avoid a separate :RELAX switch to say you don't want to panic
//    if an error fails the typecheck.  You just TRY the TYPECHECK.  It may
//    be that :RELAX is a better idea, but trying this idea first...since
//    it's strictly more powerful.
{
    INCLUDE_PARAMS_OF_TYPECHECK;

    Stable* test = ARG(TEST);
    Value* v = ARG(VALUE);

    if (not ARG(META)) {  // decay before typecheck if not ^META
      require(
        Decay_If_Unstable(v)  // or return definitional error to match [1]?
      );
    }

    if (Typecheck_Use_Toplevel(LEVEL, v, test, SPECIFIED))
        return LOGIC_OUT(true);

    if (Is_Failure(v)) {
        assert(ARG(META));  // otherwise would have pre-decay'd, PANIC
        return COPY_TO_OUT(v);  // panic would require a :RELAX option [1]
    }

    return LOGIC_OUT(false);
}


//
//  /match: native [
//
//  "If VALUE passes a type check, return it, else return NULL"
//
//      return: [<null> any-stable?]
//      test [
//          datatype! "simple datatype check"
//          block! "same dialect as type specs for functions"
//          splice! "items to test against literally"
//          parameter! "as used in function specs"
//          frame! "constraint function returning LOGIC!"
//      ]
//      value "Won't pass thru NULL (use TYPECHECK for a LOGIC! answer)"
//          [any-stable?]
//  ]
//
DECLARE_NATIVE(MATCH)
//
// 1. Passing in NULL for value creates a problem, because it conflates the
//    "didn't match" signal with the "did match" signal.  You probably want
//    TYPECHECK if are trying to do such conflations, but if it's what you
//    really want use OPT and void will come back as a a null
{
    INCLUDE_PARAMS_OF_MATCH;

    Stable* test = ARG(TEST);
    Stable* v = ARG(VALUE);

    if (Is_Null(v))  // [1]
        panic ("NULL input to MATCH not currently allowed, use TYPECHECK");

    if (not Typecheck_Use_Toplevel(LEVEL, v, test, SPECIFIED))
        return nullptr;

    return COPY_TO_OUT(v);  // test matched, return input value
}


//
//  /matcher: native [
//
//  "Make a specialization of the MATCH function for a fixed type argument"
//
//      return: [action!]
//      test [block! datatype! parameter! frame!]
//  ]
//
DECLARE_NATIVE(MATCHER)
//
// This is a bit faster at making a specialization than usermode code.
//
// Compare with TYPECHECKER
//
//    >> m: matcher [<null> integer!]
//
//    >> m 1020
//    == 1020
//
//    >> m <abc>
//    == ~null~  ; anti
//
//    >> m null
//    ** Script Error: non-NULL value required
{
    INCLUDE_PARAMS_OF_MATCHER;

    Stable* test = ARG(TEST);

    Source* a = Make_Source_Managed(2);
    Set_Flex_Len(a, 2);
    Init_Set_Word(Array_At(a, 0), CANON(TEST));
    Copy_Lifted_Cell(Array_At(a, 1), test);

    Element* block_in_spare = Init_Block(SPARE, a);

    if (Specialize_Action_Throws(OUT, LIB(MATCH), block_in_spare, STACK_BASE))
        return THROWN;

    return OUT;
}
