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
//  typechecker-archetype: native:intrinsic [
//
//  "For internal use (builds parameters and return slot)"
//
//      return: [logic?]
//      value "Value to test"
//          [<opt-out> any-stable?]
//      :type "Test a concrete type, (integer?:type integer!) passes"
//      :quoted
//      :quasiform
//      :tied
//      :pinned
//      :metaform
//  ]
//
DECLARE_NATIVE(TYPECHECKER_ARCHETYPE)
//
// !!! Due to bootstrapping issues, we can't simply use the phase of
// LIB(TYPECHECKER_ARCHETYPE) to get the paramlist to use when building
// typecheckers.  It has to be built manually.  This paramlist is just
// for reference, to use as INCLUDE_PARAMS_OF_TYPECHECKER_ARCHETYPE in the
// Typechecker_Dispatcher().
{
    INCLUDE_PARAMS_OF_TYPECHECKER_ARCHETYPE;

    /* UNUSED(ARG(VALUE))); */  // it's an intrinsic, no first param defined
    UNUSED(ARG(TYPE));
    UNUSED(ARG(QUOTED));
    UNUSED(ARG(QUASIFORM));
    UNUSED(ARG(TIED));
    UNUSED(ARG(PINNED));
    UNUSED(ARG(METAFORM));

    panic ("TYPECHECKER-ARCHETYPE called (internal use only)");
}


//
//  Typechecker_Dispatcher: C
//
// Typecheckers may be dispatched as intrinsics, which is to say they may
// not have their own Level and frame variables.
//
// See LEVEL_FLAG_DISPATCHING_INTRINSIC for more information.
//
Bounce Typechecker_Dispatcher(Level* const L)
{
    USE_LEVEL_SHORTHANDS (L);

    const Value* atom = Intrinsic_Typechecker_Atom_ARG(LEVEL);

    if (Is_Void(atom))
        return LOGIC(false);  // opt-out of the typecheck (null fails)

    Details* details = Level_Intrinsic_Details(L);
    assert(Details_Max(details) == MAX_IDX_TYPECHECKER);

    DECLARE_VALUE (temp);  // can't overwrite scratch if error can be raised
    Copy_Cell(temp, atom);
    require (
      Stable* v = Decay_If_Unstable(temp)
    );

    Option(Type) type = Type_Of(v);

    if (Not_Level_Flag(L, DISPATCHING_INTRINSIC)) {
        INCLUDE_PARAMS_OF_TYPECHECKER_ARCHETYPE;

        bool check_datatype = Bool_ARG(TYPE);
        if (check_datatype) {
            if (not Is_Datatype(v))
                return fail (
                    "Datatype check on non-datatype (use TRY for NULL)"
                );

            if (
                Bool_ARG(QUOTED)
                or Bool_ARG(TIED) or Bool_ARG(PINNED) or Bool_ARG(METAFORM)
            ){
                return fail (Error_Bad_Refines_Raw());
            }

            type = Datatype_Type(v);
        }
        else {
            if (Bool_ARG(QUOTED)) {
                if (Is_Antiform(v) or Quotes_Of(cast(Element*, v)) == 0)
                    return LOGIC(false);
                Noquotify(cast(Element*, v));
            }

            type = Type_Of(v);

            if (Bool_ARG(QUASIFORM)) {
                if (type != TYPE_QUASIFORM)
                    return LOGIC(false);

                Unquasify(cast(Element*, v));
                type = Type_Of(v);
            }

            if (Bool_ARG(TIED)) {
                if (Bool_ARG(PINNED) or Bool_ARG(METAFORM))
                    return fail (Error_Bad_Refines_Raw());

                if (type != TYPE_TIED)
                    return LOGIC(false);

                type = Heart_Of(v);
            }
            else if (Bool_ARG(PINNED)) {
                if (Bool_ARG(METAFORM))
                    return fail (Error_Bad_Refines_Raw());

                if (type != TYPE_PINNED)
                    return LOGIC(false);

                type = Heart_Of(v);
            }
            else if (Bool_ARG(METAFORM)) {
                if (type != TYPE_METAFORM)
                    return LOGIC(false);

                type = Heart_Of(v);
            }
        }
    }

    TypesetByte typeset_byte = VAL_UINT8(
        Details_At(details, IDX_TYPECHECKER_TYPESET_BYTE)
    );

    if (Is_Trash(v) and typeset_byte != cast(Byte, TYPE_TRASH))
        panic ("trash! antiforms can't be typechecked");

    if (
        Is_Nulled(v) and (
            typeset_byte != cast(Byte, TYPE_KEYWORD)
            and typeset_byte != cast(Byte, TYPE_TRASH)
        )
    ){
        panic (
            "NULL antiforms have limited typechecking (e.g. KEYWORD?, TRASH?)"
        );
    }

    if (not type)  // not a built-in type, no typechecks apply
        return LOGIC(false);

    return LOGIC(Builtin_Typeset_Check(typeset_byte, unwrap type));
}


//
//  Typechecker_Details_Querier: C
//
bool Typechecker_Details_Querier(
    Sink(Stable) out,
    Details* details,
    SymId property
){
    assert(Details_Dispatcher(details) == &Typechecker_Dispatcher);
    assert(Details_Max(details) == MAX_IDX_TYPECHECKER);
    UNUSED(details);

    switch (property) {
      case SYM_RETURN_OF: {
        const Stable* archetype = LIB(TYPECHECKER_ARCHETYPE);
        Details* archetype_details = Ensure_Frame_Details(archetype);
        return Raw_Native_Details_Querier(
            out, archetype_details, SYM_RETURN_OF
        ); }

      default:
        break;
    }

    return false;
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
// 2. We need a spec for our typecheckers, which is really just `value` with
//    no type restrictions as the argument.  !!! REVIEW: add help strings?
//
// 3. Since the return type is always a LOGIC?, Typechecker_Details_Querier()
//    can fabricate that return without it taking up a cell's worth of space
//    on each typechecker instantiation (that isn't intrinsic).
//
Details* Make_Typechecker(TypesetByte typeset_byte) {  // parameter cache [1]
    DECLARE_ELEMENT (spec);  // simple spec [2]
    Source* spec_array = Make_Source_Managed(7);
    Set_Flex_Len(spec_array, 7);
    Metafy_Cell(Init_Word(Array_At(spec_array, 0), CANON(VALUE)));
    Init_Get_Word(Array_At(spec_array, 1), CANON(TYPE));
    Init_Get_Word(Array_At(spec_array, 2), CANON(QUOTED));
    Init_Get_Word(Array_At(spec_array, 3), CANON(QUASIFORM));
    Init_Get_Word(Array_At(spec_array, 4), CANON(TIED));
    Init_Get_Word(Array_At(spec_array, 5), CANON(PINNED));
    Init_Get_Word(Array_At(spec_array, 6), CANON(METAFORM));
    Init_Block(spec, spec_array);

    StackIndex base = TOP_INDEX;

    assume (
      ParamList* paramlist = Make_Paramlist_Managed(
        spec,
        MKF_DONT_POP_RETURN,  // no RETURN: in spec (always logic)
        SYM_0  // return type for all typecheckers is the same [3]
    ));

    DECLARE_ELEMENT (discard);
    Pop_Unpopped_Return(discard, base);  // no typespec, no description

    Details* details = Make_Dispatch_Details(
        BASE_FLAG_MANAGED | DETAILS_FLAG_CAN_DISPATCH_AS_INTRINSIC,
        Phase_Archetype(paramlist),
        &Typechecker_Dispatcher,
        MAX_IDX_TYPECHECKER  // details array capacity
    );

    Init_Integer(
        Details_At(details, IDX_TYPECHECKER_TYPESET_BYTE),
        typeset_byte
    );

    return details;
}


//
//  Typecheck_Pack_In_Spare_Uses_Scratch: C
//
// It's possible in function type specs to check packs with ~[...]~ notation.
// This routine itemwise checks a pack against one of those type specs.
//
// Note that blocks are legal, as in ~[[integer! word!] object!]~, which would
// mean that the first item in the pack can be either an integer or word.
//
// 1. Due to the way that the intrinsic optimization works, it has to have
//    the argument to the intrinsic in the spare...and it uses the scratch
//    cell for putting the intrinsic action itself value into.  Since we're
//    recursing and want intrinsic optimizations to still work, we have
//    to push the existing SPARE out of the way.  If we used Alloc_Value()
//    we'd be creating an API handle with an unstable antiform in it.
//
bool Typecheck_Pack_In_Spare_Uses_Scratch(
    Level* const L,
    const Element* types
){
    USE_LEVEL_SHORTHANDS (L);

    const Value* pack = SPARE;

    assert(Is_Quasi_Block(types));  // could relax this to any list
    assert(Is_Pack(pack));  // could relax this also to any list

    const Element* pack_tail;
    const Element* pack_at = List_At(&pack_tail, pack);

    const Element* types_tail;
    const Element* types_at = List_At(&types_tail, types);

    if ((pack_tail - pack_at) != (types_tail - types_at))  // not same length
        return false;

    if (pack_at == pack_tail)  // pack is empty (so both are empty)
        return true;

    bool result = true;

    assert(TOP_INDEX == L->baseline.stack_base);
    Move_Value(PUSH(), SPARE);  // need somewhere to save spare [1]
    ++L->baseline.stack_base;  // typecheck functions should not see that push
    assert(TOP_INDEX == L->baseline.stack_base);

    Context* types_binding = List_Binding(types);

    for (; types_at != types_tail; ++types_at, ++pack_at) {
        Copy_Cell(SPARE, pack_at);
        assume (
          Unliftify_Undecayed(SPARE)
        );
        if (not Typecheck_Value_In_Spare_Uses_Scratch(
            L, types_at, types_binding
        )){
            result = false;
            goto return_result;
        }
    }

  return_result:

    assert(TOP_INDEX == L->baseline.stack_base);
    --L->baseline.stack_base;
    Move_Value(SPARE, TOP_ATOM);  // restore pack to the SPARE [1]
    DROP();
    assert(Is_Pack(SPARE));

  #if RUNTIME_CHECKS
    Init_Unreadable(SCRATCH);
  #endif

    return result;
}


//
//  Typecheck_Spare_With_Predicate_Uses_Scratch: C
//
bool Typecheck_Spare_With_Predicate_Uses_Scratch(
    Level* const L,
    const Stable* test,
    Option(const Symbol*) label
){
    USE_LEVEL_SHORTHANDS (L);

    assert(Is_Action(test) or Is_Frame(test));

    bool result;

    assert(test != SCRATCH);
    assert(test != SPARE);

    const Value* v = SPARE;

  try_builtin_typeset_checker_dispatch: {

  // The fastest (and most common case) is when we recognize the dispatcher as
  // being the Typechecker_Dispatcher().  This means it's one of 255 built-in
  // type checks, such as ANY-WORD? or INTEGER? or INTEGER!.

    Details* details = opt Try_Frame_Details(test);
    if (not details)
        goto non_intrinsic_dispatch;

    Dispatcher* dispatcher = Details_Dispatcher(details);
    if (dispatcher == &Typechecker_Dispatcher) {
        TypesetByte typeset_byte = VAL_UINT8(
            Details_At(details, IDX_TYPECHECKER_TYPESET_BYTE)
        );
        Option(Type) type = Type_Of(SPARE);  // ELEMENT? tests ExtraHeart types
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
        Copy_Cell(SCRATCH, test);  // intrinsic may need, but panic() requires
        possibly(Is_Antiform(SCRATCH));  // don't bother canonizing LIFT_BYTE()

        Remember_Cell_Is_Lifeguard(SPARE);
        Remember_Cell_Is_Lifeguard(SCRATCH);

        assert(Not_Level_Flag(L, DISPATCHING_INTRINSIC));
        Set_Level_Flag(L, DISPATCHING_INTRINSIC);
        Set_Level_Flag(L, RUNNING_TYPECHECK);
        Bounce bounce = Apply_Cfunc(dispatcher, L);
        Clear_Level_Flag(L, RUNNING_TYPECHECK);
        Clear_Level_Flag(L, DISPATCHING_INTRINSIC);

        Forget_Cell_Was_Lifeguard(SPARE);
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
        if (Is_Error(L->out))
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
    Push_Level_Erase_Out_If_State_0(SCRATCH, sub);  // sub's out is L->scratch
    require (
      Push_Action(sub, test, PREFIX_0)
    );

    const Key* key = sub->u.action.key;
    const Param* param = sub->u.action.param;
    Value* arg = sub->u.action.arg;
    for (; key != sub->u.action.key_tail; ++key, ++param, ++arg) {
        if (Is_Specialized(param))
            Blit_Param_Drop_Mark(arg, param);
        else {
            Erase_Cell(arg);
            if (Get_Parameter_Flag(param, REFINEMENT))
                Init_Nulled(arg);
            else
                Init_Unspecialized_Ghost(arg);
        }
    }

    arg = First_Unspecialized_Arg(&param, sub);
    if (not arg)
        panic (Error_No_Arg_Typecheck(label));  // must take argument

    Copy_Cell(arg, v);  // do not decay [4]

    heeded (Corrupt_Cell_If_Needful(Level_Spare(sub)));
    heeded (Corrupt_Cell_If_Needful(Level_Scratch(sub)));

    require (
      bool check = Typecheck_Coerce(sub, param, arg, false)
    );
    if (not check) {
        Drop_Action(sub);
        Drop_Level(sub);
        goto test_failed;
    }

    if (Trampoline_With_Top_As_Root_Throws())
        panic (Error_No_Catch_For_Throw(sub));

    Drop_Level(sub);

    if (Is_Error(SCRATCH))
        goto test_failed;  // e.g. see NULL? for its ERROR! on heavy null

    Stable* scratch = Known_Stable(SCRATCH);

    if (not Is_Logic(scratch))  // sub wasn't limited to intrinsics
        panic (Error_No_Logic_Typecheck(label));

    if (Cell_Logic(scratch))
        goto test_succeeded;

    goto test_failed;

} test_failed: { /////////////////////////////////////////////////////////////

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


//
//  Typecheck_Value_In_Spare_Uses_Scratch: C
//
// Ren-C has eliminated the concept of TYPESET!, instead gaining behaviors
// for TYPE-BLOCK! and TYPE-GROUP!.
//
// 1. RETURN can typecheck parameter antiforms, though arguments should not
//    support it, so (match [antiform?] frame.unspecialized-arg) is illegal.
//    Review where a check for prohibiting this might be put.
//
bool Typecheck_Value_In_Spare_Uses_Scratch(
    Level* const L,
    const Stable* tests,  // PARAMETER!, TYPE-BLOCK!, GROUP!, TYPE-GROUP!...
    Context* tests_binding
){
    USE_LEVEL_SHORTHANDS (L);

    assert(tests != SCRATCH);

    const Value* v = SPARE;

    bool result;

    const Element* tail;
    const Element* item;
    Context* derived;
    bool match_all;

    if (Heart_Of(tests) == TYPE_PARAMETER) {  // usually antiform
        const Array* array = opt Parameter_Spec(tests);
        if (array == nullptr)
            return true;  // implicitly all is permitted
        item = Array_Head(array);
        tail = Array_Tail(array);
        derived = SPECIFIED;
        match_all = false;
    }
    else switch (opt Type_Of(tests)) {
      case TYPE_DATATYPE:
        return Is_Cell_Stable(v) and (Type_Of(v) == Datatype_Type(tests));

      case TYPE_BLOCK:
        item = List_At(&tail, tests);
        derived = Derive_Binding(tests_binding, Known_Element(tests));
        match_all = false;
        break;

      case TYPE_GROUP:
        item = List_At(&tail, tests);
        derived = Derive_Binding(tests_binding, Known_Element(tests));
        match_all = true;
        break;

      case TYPE_QUASIFORM:
      case TYPE_QUOTED:
      case TYPE_WORD:
        item = cast(Element*, tests);
        tail = cast(Element*, tests) + 1;
        derived = tests_binding;
        match_all = true;
        break;

      case TYPE_ACTION:
        return Typecheck_Spare_With_Predicate_Uses_Scratch(
            L,
            tests,
            Frame_Label(tests)
        );

      default:
        assert(false);
        panic ("Bad test passed to Typecheck_Value");
    }

    DECLARE_STABLE (test);
    Push_Lifeguard(test);

    if (item == tail)
       goto end_looping_over_tests;  // might mean all match or no match

  check_spare_against_test_in_item: { ////////////////////////////////////////

  // 1. Some elements in the parameter specification array are accounted for
  // in type checking by flags or optimization bytes.  There is no need to
  // check those here, just check things that don't have PARAMSPEC_SPOKEN_FOR.
  //
  // 2. [~word!~] matches QUASI-WORD? and ['integer!] matches QUOTED-INTEGER?
  //    and [''~any-series?~] matches DOUBLE_QUOTED-QUASI-ANY-SERIES?... etc.
  //    Because we don't want to make an infinite number of type constraint
  //    functions to cover every combination.  This pretty much ties our hands
  //    on the meaning of quasiform or quoted WORD!s in the type spec.
  //
  // 3. While TAG! will have PARAMSPEC_SPOKEN_FOR in a PARAMETER!, it does not
  //    in a plain BLOCK! used with TYPECHECK.  TYPECHECK could pay to convert
  //    BLOCK! to PARAMETER! but it's cheaper if we are willing to process a
  //    block on the fly.  This handles <null> and <void> tags but it should
  //    probably have behavior for <opt> and other parameter spec tags, though
  //    it's tricky given that typecheck can't mutate the incoming value.

    Option(Sigil) sigil = SIGIL_0;  // added then removed if non-zero
    LiftByte lift_byte = LIFT_BYTE(SPARE);  // adjusted if test quoted/quasi

    if (Get_Cell_Flag(item, PARAMSPEC_SPOKEN_FOR))
        goto continue_loop;

    if (Heart_Of(item) == TYPE_WORD)  // our hands are tied on the meaning [2]
        goto adjust_quote_level_and_run_type_constraint;

    if (Is_Quasiform(item))
        goto handle_non_word_quasiform;

    if (Is_Quoted(item))
        goto handle_non_word_quoted;

    if (Any_Sigiled_Space(item)) {
        if (
            not Is_Antiform(SPARE)
            and Any_Sigiled_Space(Known_Element(SPARE))
            and Sigil_Of(item) == Sigil_Of(Known_Element(SPARE))
        ){
            goto test_succeeded;
        }
        goto test_failed;
    }

    if (Is_Tag(item)) {  // if BLOCK!, support <null> and <void> [3]
        if (0 == CT_Utf8(item, g_tag_null, true)) {
            if (Is_Cell_Stable(SPARE) and Is_Nulled(cast(Stable*, SPARE)))
                goto test_succeeded;
            goto test_failed;
        }
        if (0 == CT_Utf8(item, g_tag_void, true)) {
            if (Is_Void(SPARE))
                goto test_succeeded;
            goto test_failed;
        }
    }

    panic (item);

  handle_non_word_quasiform: { ///////////////////////////////////////////////

  // 1. ~[integer! word!]~ is a typecheck that matches a 2-element PACK! with
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
  //    Quasiform group of [~(true false)~] actually looks kind of good; it
  //    will match any of the single items in the group literally.

    if (Heart_Of(item) == TYPE_BLOCK) {  // typecheck pack [1]
        if (not Is_Pack(v))
            goto test_failed;
        if (Typecheck_Pack_In_Spare_Uses_Scratch(L, item))
            goto test_succeeded;
        goto test_failed;
    }

    if (Heart_Of(item) == TYPE_FENCE) {  // interpret as datatype [2]
        panic ("Quasiform FENCE! in type spec not supported yet");
    }

    if (Heart_Of(item) == TYPE_GROUP) {  // match any element literally [3]
        if (Is_Antiform(SPARE))
            goto test_failed;  // can't match against antiforms

        const Element* splice_tail;
        const Element* splice_at = List_At(&splice_tail, item);

        for (; splice_at != splice_tail; ++splice_at) {
            bool strict = true;  // system now case-sensitive by default
            require (
              bool equal = Equal_Values(
                Known_Element(SPARE),
                splice_at,
                strict
            ));
            if (equal)
                goto test_succeeded;
        }
        goto test_failed;
    }

    panic (item);

} handle_non_word_quoted: { //////////////////////////////////////////////////

    // It's not clear exactly what non-word quoteds would do.  It could be
    // that '[integer! word!] would match a non-quoted BLOCK! with 2
    // elements in it that were an integer and a word, for instance.  But
    // anything we do would be inconsistent with the WORD! interpretation
    // that it's exactly the same quoting level as the quotes on the test.
    //
    // Review when this gets further.

    panic (item);

} adjust_quote_level_and_run_type_constraint: {

    if (LIFT_BYTE(item) != NOQUOTE_2) {
        if (LIFT_BYTE(item) != LIFT_BYTE(SPARE))
            goto test_failed;  // should be willing to accept subset quotes
        LIFT_BYTE(SPARE) = NOQUOTE_2;
    }

} handle_after_any_quoting_adjustments: {

    sigil = Sigil_Of(item);
    if (sigil) {
        if (Is_Antiform(v) or Sigil_Of(cast(Element*, v)) != sigil) {
            sigil = SIGIL_0;  // don't unsigilize at test_failed
            goto test_failed;
        }

        Clear_Cell_Sigil(Known_Element(SPARE));  // make plain, will re-sigilize
    }

    Option(const Symbol*) label = Word_Symbol(item);

    DECLARE_ELEMENT (temp_item_word);
    Copy_Cell(temp_item_word, item);
    KIND_BYTE(temp_item_word) = TYPE_WORD;
    LIFT_BYTE(temp_item_word) = NOQUOTE_2;  // ~word!~ or 'word! etc.

    require (
      Get_Word(test, temp_item_word, derived)
    );

    if (Is_Action(test)) {
        if (Typecheck_Spare_With_Predicate_Uses_Scratch(L, test, label))
            goto test_succeeded;
        goto test_failed;
    }

    switch (opt Type_Of_Unchecked(test)) {
      case TYPE_PARAMETER: {
        if (Typecheck_Value_In_Spare_Uses_Scratch(L, test, SPECIFIED))
            goto test_succeeded;
        goto test_failed; }

      case TYPE_DATATYPE: {
        Option(Type) t = Type_Of(v);
        if (t) {  // builtin type
            if (Datatype_Type(test) == t)
                goto test_succeeded;
            goto test_failed;
        }
        if (Datatype_Extra_Heart(test) == Cell_Extra_Heart(v))
            goto test_succeeded;
        goto test_failed; }

      default:
        break;
    }

    panic ("Invalid element in TYPE-GROUP!");

} test_succeeded: {

    if (sigil)
        Add_Cell_Sigil(Known_Element(SPARE), unwrap sigil);

    LIFT_BYTE(SPARE) = lift_byte;  // restore quote level

    if (not match_all) {
        result = true;
        goto return_result;
    }
    goto continue_loop;

} test_failed: {

    if (sigil)
        Add_Cell_Sigil(Known_Element(SPARE), unwrap sigil);

    LIFT_BYTE(SPARE) = lift_byte;  // restore quote level

    if (match_all) {
        result = false;
        goto return_result;
    }
    goto continue_loop;

}} continue_loop: {

    ++item;
    if (item != tail)
        goto check_spare_against_test_in_item;

} end_looping_over_tests: {

    if (match_all)
        result = true;
    else
        result = false;

    goto return_result;

} return_result: {

  #if RUNTIME_CHECKS
    Init_Unreadable(SCRATCH);
  #endif

    Drop_Lifeguard(test);

    return result;
}}


//
//  Typecheck_Coerce: C
//
// This does extra typechecking pertinent to function parameters, compared to
// the basic type checking.
//
// 1. SPARE and SCRATCH are GC-safe cells in a Level that are usually free
//    for whatever purposes an Executor wants.  But when a Level is being
//    multiplexed with intrinsics (see DETAILS_FLAG_CAN_DISPATCH_AS_INTRINSIC)
//    it has to give up those cells for the duration of that call.  Type
//    checking uses intrinsics a vast majority of the time, so this function
//    ensures you don't rely on SCRATCH or SPARE not being modified (it also
//    marks them unreadable at the end).
//
// 2. !!! Should explicit mutability override, so people can say things
//    like (foo: func [...] mutable [...]) ?  This seems bad, because the
//    contract of the function hasn't been "tweaked" with reskinning.
//
Result(bool) Typecheck_Coerce(
    Level* const L,
    const Element* param,
    Value* atom,  // need mutability for coercion
    bool is_return
){
    USE_LEVEL_SHORTHANDS (L);

  #if NEEDFUL_DOES_CORRUPTIONS  // we use SCRATCH and SPARE as workspaces [1]
    assert(Not_Cell_Readable(SCRATCH));
    assert(Not_Cell_Readable(SPARE));
  #endif

    assert(atom != SCRATCH and atom != SPARE);

    if (Get_Parameter_Flag(param, OPT_OUT))
        assert(not Is_Void(atom));  // should have bypassed this check

    if (Get_Parameter_Flag(param, CONST))
        Set_Cell_Flag(atom, CONST);  // mutability override? [2]

    bool result;

    bool coerced = false;

    if (Parameter_Class(param) == PARAMCLASS_META) {
        //
        // check as-is, try coercing if it doesn't work
    }
    else if (is_return) {
        //
        // same as PARAMCLASS_META
    }
    else if (Not_Cell_Stable(atom)) {
        if (Is_Void(atom)) {  // non-^META endable parameters can be void
            if (Get_Parameter_Flag(param, ENDABLE))
                goto return_true;
        }
        goto do_coercion;
    }

  typecheck_again: {

    if (Is_Antiform(atom)) {
        if (
            Get_Parameter_Flag(param, NULL_DEFINITELY_OK)
            and Is_Light_Null(atom)
        ){
            goto return_true;
        }

        if (Get_Parameter_Flag(param, VOID_DEFINITELY_OK) and Is_Void(atom))
            goto return_true;

        if (
            Get_Parameter_Flag(param, TRASH_DEFINITELY_OK)
            and Is_Possibly_Unstable_Value_Trash(atom)
        ){
            goto return_true;
        }
    }

    if (Get_Parameter_Flag(param, ANY_STABLE_OK) and Is_Cell_Stable(atom))
        goto return_true;

    if (Get_Parameter_Flag(param, ANY_ATOM_OK))
        goto return_true;

    if (Is_Parameter_Unconstrained(param)) {
        if (Get_Parameter_Flag(param, REFINEMENT)) {  // no-arg refinement
            assert(not Is_Light_Null(atom));  // NULL_DEFINITELY_OK handles
            if (Is_Possibly_Unstable_Value_Okay(atom))
                goto return_true;
            goto return_false;
        }
        goto return_true;  // other parameters
    }

    if (Get_Parameter_Flag(param, SPACE_DEFINITELY_OK))
        if (Is_Cell_Stable(atom) and Is_Space(u_cast(Stable*, atom)))
            goto return_true;

} do_optimized_checks_signaled_by_bytes: {

    const Array* spec = opt Parameter_Spec(param);
    const TypesetByte* optimized = spec->misc.at_least_4;
    const TypesetByte* optimized_tail
        = optimized + sizeof(spec->misc.at_least_4);

    Option(Type) type = Type_Of(atom);  // Option/extension can be ANY-ELEMENT?
    for (; optimized != optimized_tail; ++optimized) {
        if (*optimized == 0)
            break;  // premature end of list

        if (Builtin_Typeset_Check(*optimized, type))
            goto return_true;  // ELEMENT?/FUNDAMENTAL? test TYPE_0 types
    }

    if (Get_Parameter_Flag(param, INCOMPLETE_OPTIMIZATION)) {
        Copy_Cell(SPARE, atom);
        if (Typecheck_Value_In_Spare_Uses_Scratch(L, param, SPECIFIED))
            goto return_true;
    }

} more_stuff: {

    if (not coerced) {

      do_coercion:

        if (
            Is_Possibly_Unstable_Value_Action(atom)
            and Get_Parameter_Flag(param, UNRUN)
        ){
            LIFT_BYTE(atom) = NOQUOTE_2;
            possibly(coerced);  // this may be a coercion after decay...
            coerced = true;
            goto typecheck_again;
        }

        if (Is_Error(atom))
            goto return_false;

        if (Is_Ghost(atom))
            goto return_false;  // comma antiforms

        if (Is_Antiform(atom) and Is_Antiform_Unstable(atom)) {
            trap (
              Decay_If_Unstable(atom)
            );
            assert(not coerced);  // should only decay once...
            coerced = true;

            if (
                Is_Possibly_Unstable_Value_Action(atom)
                and Get_Parameter_Flag(param, UNRUN)
            ){
                LIFT_BYTE(atom) = NOQUOTE_2;
            }

            goto typecheck_again;
        }
    }

} return_false: { ////////////////////////////////////////////////////////////

    result = false;
    goto return_result;

} return_true: { /////////////////////////////////////////////////////////////

    result = true;
    goto return_result;

} return_result: { ///////////////////////////////////////////////////////////

    if ((result == true) and Not_Cell_Stable(atom))
        assert(
            is_return
            or Parameter_Class(param) == PARAMCLASS_META
            or Get_Parameter_Flag(param, ENDABLE)
        );

  #if RUNTIME_CHECKS  // always corrupt to emphasize that we *could* have [1]
    Init_Unreadable(SPARE);
    Init_Unreadable(SCRATCH);
  #endif

    return result;
}}


//
//  Init_Typechecker: C
//
// Give back an action antiform which can act as a checker for a datatype.
//
Result(Stable*) Init_Typechecker(
    Init(Stable) out,
    const Stable* datatype_or_block
){
    possibly(out == datatype_or_block);

    if (Is_Datatype(datatype_or_block)) {
        Option(Type) t = Datatype_Type(datatype_or_block);
        if (not t)
            panic ("TYPECHECKER does not support extension types yet");

        Byte type_byte = u_cast(Byte, unwrap t);
        SymId16 id16 = cast(SymId16, type_byte) + MIN_SYM_TYPESETS - 1;
        assert(id16 == type_byte);  // MIN_SYM_TYPESETS should be 1

        Copy_Cell(out, Lib_Stable(cast(SymId, id16)));
        assert(Ensure_Frame_Details(out));  // need TypesetByte

        return out;
    }

    Source* a = Make_Source_Managed(2);
    Set_Flex_Len(a, 2);
    Init_Set_Word(Array_At(a, 0), CANON(TEST));

    const Element* block = Known_Element(datatype_or_block);
    Element* param = Init_Unconstrained_Parameter(
        Array_At(a, 1), FLAG_PARAMCLASS_BYTE(PARAMCLASS_NORMAL)
    );
    Push_Lifeguard(param);
    Option(Error*) e = rescue (
        Set_Parameter_Spec(param, block, Cell_Binding(block))
    );
    Drop_Lifeguard(param);
    if (e)
        return fail (unwrap e);

    DECLARE_ELEMENT (def);

    Init_Block(def, a);
    Push_Lifeguard(def);

    bool threw = Specialize_Action_Throws(
        out, LIB(MATCH), def, TOP_INDEX  // !!! should be TYPECHECK
    );
    Drop_Lifeguard(def);

    if (threw)
        panic (Error_No_Catch_For_Throw(TOP_LEVEL));

    return out;
}


//
//  typechecker: native [
//
//  "Make a function for checking types (generated function gives LOGIC!)"
//
//      return: [~[action!]~]
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
    return Packify_Action(OUT);
}


//
//  typecheck: native [
//
//  "Same typechecking as function arguments"
//
//      return: [logic? error!]  ; returns error vs. panic [1]
//      test [block! datatype! parameter! action!]
//      ^value [any-value?]
//      :meta "Don't pre-decay argument (match ^META argument mode)"
//  ]
//
DECLARE_NATIVE(TYPECHECK)
//
// 1. The reason a :META typecheck that fails with ERROR! returns the error
//    is to avoid a separate :RELAX switch to say you don't want to panic
//    if an error fails the typecheck.  You just TRY the TYPECHECK.  It may
//    be that :RELAX is a better idea, but trying this idea first...since
//    it's strictly more powerful.
{
    INCLUDE_PARAMS_OF_TYPECHECK;

    Stable* test = ARG(TEST);

    heeded (Copy_Cell(SPARE, Atom_ARG(VALUE)));

    if (not Bool_ARG(META)) {  // decay before typecheck if not ^META
      require(
        Decay_If_Unstable(SPARE)  // or return definitional error to match [1]?
      );
    }

    switch (opt Type_Of(test)) {
      case TYPE_ACTION:
        if (Typecheck_Spare_With_Predicate_Uses_Scratch(
            LEVEL, test, Frame_Label(test)
        )){
            return LOGIC(true);
        }
        break;

        // fall through
      case TYPE_PARAMETER:
      case TYPE_BLOCK:
      case TYPE_DATATYPE:
        if (Typecheck_Value_In_Spare_Uses_Scratch(LEVEL, test, SPECIFIED))
            return LOGIC(true);
        break;

      default:
        assert(false);  // all test types should be accounted for in switch
        panic (PARAM(TEST));
    }

    if (Is_Error(SPARE)) {
        assert(Bool_ARG(META));  // otherwise would have pre-decay'd, PANIC
        return COPY(SPARE);  // panic would require a :RELAX option [1]
    }

    return LOGIC(false);
}


//
//  match: native [
//
//  "Same typechecking as function arguments, but return value on success"
//
//      return: [<null> any-stable?]
//      test [block! datatype! parameter! action!]
//      value "Won't pass thru NULL (use TYPECHECK for a LOGIC? answer)"
//          [<opt-out> any-stable?]
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

    Stable* spare = Copy_Cell(SPARE, ARG(VALUE));
    assert(not Is_Nulled(spare));  // <opt-out> args should prohibit NULL

    switch (opt Type_Of(test)) {
      case TYPE_ACTION:
        if (not Typecheck_Spare_With_Predicate_Uses_Scratch(
            LEVEL, test, Frame_Label(test)
        )){
            return nullptr;
        }
        break;

      case TYPE_PARAMETER:
      case TYPE_BLOCK:
      case TYPE_DATATYPE:
        if (not Typecheck_Value_In_Spare_Uses_Scratch(LEVEL, test, SPECIFIED))
            return nullptr;
        break;

      default:
        assert(false);  // all test types should be accounted for in switch
        panic (PARAM(TEST));
    }

    return COPY(spare);  // test matched, return input value
}


//
//  matcher: native [
//
//  "Make a specialization of the MATCH function for a fixed type argument"
//
//      return: [~[action!]~]
//      test [block! datatype! parameter! action!]
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

    return Packify_Action(OUT);
}
