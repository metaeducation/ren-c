//
//  File: %c-typechecker.c
//  Summary: "Function generator for an optimized typechecker"
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
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
//     >> /integer?: lambda [v [any-value?]] [integer! = type of :v]
//
//     >> integer? 10
//     == ~true~  ; anti
//
//     >> integer? <foo>
//     == ~false~  ; anti
//
// But given that it is done so often, it's more efficient to have these done
// via optimized internal functions...created at boot time.
//

#include "sys-core.h"


//
//  Decider_Intrinsic_Dispatcher: C
//
// Typecheckers may be dispatched as intrinsics, which is to say they may
// not have their own Level and frame variables.
//
// See LEVEL_FLAG_DISPATCHING_INTRINSIC for more information.
//
Bounce Decider_Intrinsic_Dispatcher(Level* level_)
{
    DECLARE_VALUE (v);
    Option(Bounce) bounce = Trap_Bounce_Decay_Value_Intrinsic(v, LEVEL);
    if (bounce)
        return unwrap bounce;

    if (Get_Level_Flag(level_, DISPATCHING_INTRINSIC)) {
        Details* details = Phase_Details(cast(Phase*, VAL_ACTION(SCRATCH)));
        Value* index = Details_At(details, IDX_TYPECHECKER_DECIDER_INDEX);
        Decider* decider = g_instance_deciders[VAL_UINT8(index)];
        return LOGIC(decider(v));
    }

    bool check_datatype = Cell_Logic(Level_Arg(level_, 3));
    if (check_datatype and not Is_Type_Block(v))
        return nullptr;

    Details* details = Phase_Details(PHASE);
    assert(Array_Len(details) == IDX_TYPECHECKER_MAX);

    Value* index = Details_At(details, IDX_TYPECHECKER_DECIDER_INDEX);
    Decider* decider = check_datatype
        ? g_datatype_deciders[VAL_UINT8(index)]
        : g_instance_deciders[VAL_UINT8(index)];

    return LOGIC(decider(v));
}


//
//  Make_Decider_Intrinsic: C
//
// The typecheckers for things like INTEGER? and ANY-SERIES? are all created
// at boot time.
//
// Each typechecker is implemented by one of up to 256 "Decider" functions
// in a table.  This means that if a parameter recognizes that some of
// the type checking funcions are typecheckers, it can extract which decider
// function is implied via that byte... and then just have the table of bytes
// to blast through the deciders when type checking the arguments.
//
// 1. We need a spec for our typecheckers, which is really just `value` with
//    no type restrictions as the argument.  !!! REVIEW: add help strings?
//
Phase* Make_Decider_Intrinsic(Offset decider_index) {
    DECLARE_ELEMENT (spec);  // simple spec [1]
    Source* spec_array = Make_Source_Managed(2);
    Set_Flex_Len(spec_array, 2);
    Init_Word(Array_At(spec_array, 0), CANON(VALUE));
    Init_Get_Word(Array_At(spec_array, 1), CANON(TYPE));
    Init_Block(spec, spec_array);

    VarList* meta;
    Flags flags = MKF_RETURN;
    Array* paramlist = Make_Paramlist_Managed_May_Fail(
        &meta,
        spec,
        &flags  // native return types checked only if RUNTIME_CHECKS
    );
    Assert_Flex_Term_If_Needed(paramlist);

    Phase* typechecker = Make_Action(
        paramlist,
        nullptr,  // no partials
        &Decider_Intrinsic_Dispatcher,
        IDX_TYPECHECKER_MAX  // details array capacity
    );
    Set_Action_Flag(typechecker, CAN_DISPATCH_AS_INTRINSIC);

    Details* details = Phase_Details(typechecker);

    Init_Integer(
        Details_At(details, IDX_TYPECHECKER_DECIDER_INDEX),
        decider_index
    );

    return typechecker;
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

    const Atom* pack = SPARE;

    assert(Is_Quasi_Block(types));  // could relax this to any list
    assert(Is_Pack(pack));  // could relax this also to any list

    const Element* pack_tail;
    const Element* pack_at = Cell_List_At(&pack_tail, pack);

    const Element* types_tail;
    const Element* types_at = Cell_List_At(&types_tail, types);

    if ((pack_tail - pack_at) != (types_tail - types_at))  // not same length
        return false;

    if (pack_at == pack_tail)  // pack is empty (so both are empty)
        return true;

    bool result = true;

    assert(TOP_INDEX == L->baseline.stack_base);
    Move_Atom(cast(Atom*, PUSH()), SPARE);  // need somewhere to save spare [1]
    ++L->baseline.stack_base;  // typecheck functions should not see that push
    assert(TOP_INDEX == L->baseline.stack_base);

    Context* types_binding = Cell_List_Binding(types);

    for (; types_at != types_tail; ++types_at, ++pack_at) {
        Copy_Cell(SPARE, pack_at);
        Meta_Unquotify_Undecayed(SPARE);
        if (not Typecheck_Atom_In_Spare_Uses_Scratch(
            L, types_at, types_binding
        )){
            result = false;
            goto return_result;
        }
    }

  return_result:

    assert(TOP_INDEX == L->baseline.stack_base);
    --L->baseline.stack_base;
    Move_Drop_Top_Stack_Value(SPARE);  // restore pack to the SPARE [1]

  #if RUNTIME_CHECKS
    Init_Unreadable(SCRATCH);
  #endif

    return result;
}


//
//  Typecheck_Atom_In_Spare_Uses_Scratch: C
//
// Ren-C has eliminated the concept of TYPESET!, instead gaining behaviors
// for TYPE-BLOCK! and TYPE-GROUP!.
//
// 1. For the moment, we prohibit typechecking parameter antiforms.  If you
//    write something like (match [antiform?] frame.unspecialized-arg) then
//    trying to fill the argument to MATCH with an antiform parameter just
//    should not be possible.
//
bool Typecheck_Atom_In_Spare_Uses_Scratch(
    Level* const L,
    const Value* tests,  // PARAMETER!, TYPE-BLOCK!, GROUP!, TYPE-GROUP!...
    Context* tests_binding
){
    USE_LEVEL_SHORTHANDS (L);

    assert(tests != SCRATCH);

    const Atom* v = SPARE;
    assert(not (Is_Antiform(v) and HEART_BYTE(v) == REB_PARAMETER));  // [1]

    bool result;

    const Element* tail;
    const Element* item;
    Context* derived;
    bool match_all;

    if (Cell_Heart(tests) == REB_PARAMETER) {  // usually antiform
        const Array* array = maybe Cell_Parameter_Spec(tests);
        if (array == nullptr)
            return true;  // implicitly all is permitted
        item = Array_Head(array);
        tail = Array_Tail(array);
        derived = SPECIFIED;
        match_all = false;
    }
    else switch (VAL_TYPE(tests)) {
      case REB_TYPE_BLOCK:
        return Is_Stable(v) and (VAL_TYPE(v) == VAL_TYPE_KIND(tests));

      case REB_BLOCK:
        item = Cell_List_At(&tail, tests);
        derived = Derive_Binding(tests_binding, tests);
        match_all = false;
        break;

      case REB_GROUP:
      case REB_TYPE_GROUP:
        item = Cell_List_At(&tail, tests);
        derived = Derive_Binding(tests_binding, tests);
        match_all = true;
        break;

      case REB_QUASIFORM:
      case REB_QUOTED:
      case REB_TYPE_WORD:
      case REB_WORD:
        item = c_cast(Element*, tests);
        tail = c_cast(Element*, tests) + 1;
        derived = tests_binding;
        match_all = true;
        break;

      default:
        assert(false);
        fail ("Bad test passed to Typecheck_Value");
    }

    for (; item != tail; ++item) {
        Assert_Cell_Readable(item);

        Option(const Symbol*) label = nullptr;  // so goto doesn't cross

        if (Is_Quasiform(item)) {  // quasiforms e.g. [~null~] mean antiform
            if (Cell_Heart(item) == REB_BLOCK) {  // typecheck pack
                if (not Is_Pack(v))
                    goto test_failed;
                if (Typecheck_Pack_In_Spare_Uses_Scratch(L, item))
                    goto test_succeeded;
                goto test_failed;
            }

            assert(Is_Stable_Antiform_Heart(Cell_Heart(item)));

            if (Not_Antiform(v) or Cell_Heart(item) != Cell_Heart(v))
                goto test_failed;

            bool strict = false;  // !!! Is being case-insensitive good?
            if (0 == Compare_Cells_Ignore_Quotes(
                item,
                cast(const Value*, v),  // stable antiform if we got here
                strict
            )){
                goto test_succeeded;
            }
            goto test_failed;
        }

        if (Is_Quoted(item)) {  // quoted items e.g. ['off 'on] mean literal
            if (Is_Antiform(v))
                goto test_failed;

            if (QUOTE_BYTE(item) - Quote_Shift(1) != QUOTE_BYTE(v))
                goto test_failed;

            bool strict = false;  // !!! Is being case-insensitive good?
            if (0 == Compare_Cells_Ignore_Quotes(
                item,
                cast(const Element*, v),
                strict
            )){
                goto test_succeeded;
            }
            goto test_failed;
        }

        Kind kind;
        const Value* test;
        if (
            VAL_TYPE_UNCHECKED(item) == REB_WORD
            or VAL_TYPE_UNCHECKED(item) == REB_TYPE_WORD
        ){
            label = Cell_Word_Symbol(item);
            Option(Error*) error = Trap_Lookup_Word(&test, item, derived);
            if (error)
                fail (unwrap error);
            kind = VAL_TYPE(test);  // e.g. TYPE-BLOCK! <> BLOCK!
        }
        else {
            test = item;
            switch (VAL_TYPE_UNCHECKED(test)) {
              case REB_BLOCK:
                kind = REB_TYPE_BLOCK;
                break;

              case REB_GROUP:
                kind = REB_TYPE_GROUP;
                break;

              default:
                kind = VAL_TYPE_UNCHECKED(test);
                break;
            }
        }

        if (Is_Action(test))
            goto run_action;

        switch (kind) {
          run_action: {
            Action* action = VAL_ACTION(test);

            if (Get_Action_Flag(action, CAN_DISPATCH_AS_INTRINSIC)) {
                assert(Is_Stub_Details(action));
                Dispatcher* dispatcher = ACT_DISPATCHER(cast(Phase*, action));

                Copy_Cell(SCRATCH, test);  // intrinsic may need action

              #if DEBUG_CELL_READ_WRITE
                assert(Not_Cell_Flag(SPARE, PROTECTED));
                Set_Cell_Flag(SPARE, PROTECTED);
              #endif

                assert(Not_Level_Flag(L, DISPATCHING_INTRINSIC));
                Set_Level_Flag(L, DISPATCHING_INTRINSIC);
                Bounce bounce = (*dispatcher)(L);
                Clear_Level_Flag(L, DISPATCHING_INTRINSIC);

              #if DEBUG_CELL_READ_WRITE
                Clear_Cell_Flag(SPARE, PROTECTED);
              #endif

                if (bounce == nullptr or bounce == BOUNCE_BAD_INTRINSIC_ARG)
                    goto test_failed;
                if (bounce == BOUNCE_OKAY)
                    goto test_succeeded;

                if (bounce == BOUNCE_FAIL)
                    fail (Error_No_Catch_For_Throw(TOP_LEVEL));
                assert(bounce == L->out);  // no BOUNCE_CONTINUE, API vals, etc
                if (Is_Raised(L->out))
                    fail (Cell_Error(L->out));
                fail (Error_No_Logic_Typecheck(label));
            }

            Flags flags = 0;
            Level* sub = Make_End_Level(
                &Action_Executor,
                FLAG_STATE_BYTE(ST_ACTION_TYPECHECKING) | flags
            );
            Push_Level_Erase_Out_If_State_0(SCRATCH, sub);  // write sub's output to L->scratch
            Push_Action(sub, VAL_ACTION(test), Cell_Coupling(test));
            Begin_Action(sub, VAL_FRAME_LABEL(test), PREFIX_0);

            const Key* key = sub->u.action.key;
            const Param* param = sub->u.action.param;
            Atom* arg = sub->u.action.arg;
            for (; key != sub->u.action.key_tail; ++key, ++param, ++arg) {
                Erase_Cell(arg);  // uninitialized in release, poison in debug
                Copy_Cell(arg, param);
            }

            arg = First_Unspecialized_Arg(&param, sub);
            if (not arg)
                fail (Error_No_Arg_Typecheck(label));  // must take argument

            Copy_Cell(arg, v);  // do not decay [4]

            if (Cell_ParamClass(param) == PARAMCLASS_META)
                Meta_Quotify(arg);

            if (not Typecheck_Coerce_Arg_Uses_Spare_And_Scratch(
                sub, param, arg, false
            )) {
                Drop_Action(sub);
                Drop_Level(sub);
                goto test_failed;
            }

            if (Trampoline_With_Top_As_Root_Throws())
                fail (Error_No_Catch_For_Throw(sub));

            Drop_Level(sub);

            if (not Is_Logic(SCRATCH))  // sub wasn't limited to intrinsics
                fail (Error_No_Logic_Typecheck(label));

            if (not Cell_Logic(SCRATCH))
                goto test_failed;
            break; }

          case REB_TYPE_WORD:
            if (not Typecheck_Atom_In_Spare_Uses_Scratch(
                L, test, tests_binding
            )){
                goto test_failed;
            }
            break;

          case REB_TYPE_GROUP: {
            Context* sub_binding = Derive_Binding(tests_binding, test);
            if (not Typecheck_Atom_In_Spare_Uses_Scratch(
                L, test, sub_binding
            )){
                goto test_failed;
            }
            break; }

          case REB_QUOTED:
          case REB_QUASIFORM: {
            fail ("QUOTED? and QUASI? not supported in TYPE-XXX!"); }

          case REB_PARAMETER: {
            if (not Typecheck_Atom_In_Spare_Uses_Scratch(
                L, test, SPECIFIED
            )){
                goto test_failed;
            }
            break; }

          case REB_TYPE_BLOCK: {
            Kind k;
            if (Is_Antiform(v) and Is_Antiform_Unstable(v))
                k = REB_ANTIFORM;
            else
                k = VAL_TYPE(v);
            if (VAL_TYPE_KIND(test) != k)
                goto test_failed;
            break; }

          default:
            fail ("Invalid element in TYPE-GROUP!");
        }
        goto test_succeeded;

      test_succeeded:
        if (not match_all) {
            result = true;
            goto return_result;
        }
        continue;

      test_failed:
        if (match_all) {
            result = false;
            goto return_result;
        }
        continue;
    }

    if (match_all)
        result = true;
    else
        result = false;
    goto return_result;

  return_result:

  #if RUNTIME_CHECKS
    Init_Unreadable(SCRATCH);
  #endif

    return result;
}


//
//  Typecheck_Coerce_Arg_Uses_Spare_And_Scratch: C
//
// This does extra typechecking pertinent to function parameters, compared to
// the basic type checking.
//
// 1. SPARE and SCRATCH are GC-safe cells in a Level that are usually free
//    for whatever purposes an Executor wants.  But when a Level is being
//    multiplexed with an intrinsic (see LEVEL_FLAG_CAN_DISPATCH_AS_INTRINSIC)
//    it has to give up those cells for the duration of that call.  Type
//    checking uses intrinsics a vast majority of the time, so this function
//    ensures you don't rely on SCRATCH or SPARE not being modified (it
//    marks them unreadable at the end).
//
// 2. !!! Should explicit mutability override, so people can say things
//    like (/foo: func [...] mutable [...]) ?  This seems bad, because the
//    contract of the function hasn't been "tweaked" with reskinning.
//
bool Typecheck_Coerce_Arg_Uses_Spare_And_Scratch(
    Level* const L,
    const Param* param,
    Atom* arg,  // need mutability for coercion
    bool is_return
){
    USE_LEVEL_SHORTHANDS (L);

    assert(arg != SCRATCH and arg != SPARE);

    if (Get_Parameter_Flag(param, NOOP_IF_VOID))
        assert(not Is_Stable(arg) or not Is_Void(arg));  // should've bypassed

    if (Get_Parameter_Flag(param, CONST))
        Set_Cell_Flag(arg, CONST);  // mutability override? [2]

    bool result;

    bool coerced = false;

    // We do an adjustment of the argument to accommodate meta parameters,
    // which check the unquoted type.
    //
    bool unquoted = false;

    if (Cell_ParamClass(param) == PARAMCLASS_META) {
        if (Is_Nulled(arg))
            return Get_Parameter_Flag(param, ENDABLE);

        if (not Is_Quasiform(arg) and not Is_Quoted(arg))
            return false;

        Meta_Unquotify_Undecayed(arg);  // temporary adjustment (easiest option)
        unquoted = true;
    }
    else if (is_return) {
        unquoted = false;
    }
    else {
        unquoted = false;

        if (Not_Stable(arg))
            goto do_coercion;
    }

  typecheck_again:

    if (Is_Antiform(arg)) {
        if (Get_Parameter_Flag(param, NULL_DEFINITELY_OK) and Is_Nulled(arg))
            goto return_true;

        if (Get_Parameter_Flag(param, VOID_DEFINITELY_OK) and Is_Void(arg))
            goto return_true;

        if (Get_Parameter_Flag(param, NIHIL_DEFINITELY_OK) and Is_Nihil(arg))
            goto return_true;

        if (
            Get_Parameter_Flag(param, NOTHING_DEFINITELY_OK)
            and Is_Nothing(arg)
        ){
            goto return_true;
        }
    }

    if (Get_Parameter_Flag(param, ANY_VALUE_OK) and Is_Stable(arg))
        goto return_true;

    if (Get_Parameter_Flag(param, ANY_ATOM_OK))
        goto return_true;

    if (Is_Parameter_Unconstrained(param)) {
        if (Get_Parameter_Flag(param, REFINEMENT)) {  // no-arg refinement
            if (Is_Okay(arg))
                goto return_true;  // nulls handled by NULL_DEFINITELY_OK
            goto return_false;
        }
        goto return_true;  // other parameters
    }

  blockscope {
    const Array* spec = maybe Cell_Parameter_Spec(param);
    const Byte* optimized = spec->misc.any.at_least_4;
    const Byte* optimized_tail = optimized + sizeof(spec->misc.any.at_least_4);

    if (Is_Stable(arg)) {
        for (; optimized != optimized_tail; ++optimized) {
            if (*optimized == 0)
                break;

            Decider* decider = g_instance_deciders[*optimized];
            if (decider(Stable_Unchecked(arg)))
                goto return_true;
        }
    }

    if (Get_Parameter_Flag(param, INCOMPLETE_OPTIMIZATION)) {
        Copy_Cell(SPARE, arg);
        if (Typecheck_Atom_In_Spare_Uses_Scratch(L, param, SPECIFIED))
            goto return_true;
    }
  }

    if (not coerced) {

      do_coercion:

        if (Is_Action(arg)) {
            QUOTE_BYTE(arg) = NOQUOTE_1;
            coerced = true;
            goto typecheck_again;
        }

        if (Is_Raised(arg))
            goto return_false;

        if (Is_Pack(arg) and Is_Pack_Undecayable(arg))
            goto return_false;  // nihil or unstable isotope in first slot

        if (Is_Barrier(arg))
            goto return_false;  // comma antiforms

        if (Is_Antiform(arg) and Is_Antiform_Unstable(arg)) {
            Decay_If_Unstable(arg);
            coerced = true;
            goto typecheck_again;
        }
    }

  return_false:

    result = false;
    goto return_result;

  return_true:

    result = true;
    goto return_result;

  return_result:

    if (unquoted)
        Meta_Quotify(arg);

    if ((result == true) and Not_Stable(arg))
        assert(is_return);

  #if RUNTIME_CHECKS  // always corrupt to emphasize that we *could* have [1]
    Init_Unreadable(SPARE);
    Init_Unreadable(SCRATCH);
  #endif

    return result;
}


//
//  Init_Typechecker: C
//
// Give back an action antiform which can act as a checkerfor a datatype.
//
Value* Init_Typechecker(Init(Value) out, const Element* types) {
    if (Is_Type_Block(types)) {
        Kind kind = VAL_TYPE_KIND(types);
        Offset n = cast(Offset, kind);

        SymId constraint_sym = cast(SymId, REB_MAX + ((n - 1) * 2));
        return Copy_Cell(out, Lib_Var(constraint_sym));
    }

    assert(Is_Type_Word(types));

    return Get_Var_May_Fail(out, types, SPECIFIED);
}


//
//  /typechecker: native [
//
//  "Make a function for checking types (generated function gives LOGIC!)"
//
//      return: [action?]
//      types [type-word! type-block!]
//  ]
//
DECLARE_NATIVE(typechecker)
//
// Compare with MATCHER:
//
//    >> t: typechecker [null? integer!]
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

    Element* types = cast(Element*, ARG(types));
    return Init_Typechecker(OUT, types);
}


//
//  /match: native [
//
//  "Check value using the same typechecking that functions use for parameters"
//
//      return: "Input if it matched, NULL if it did not"
//          [any-value?]
//      test "Type specification, can use NULL instead of [null?]"  ; [1]
//          [~null~ block! type-word! type-group! type-block! parameter!]
//      value "If not /META, NULL values illegal, and VOID returns NULL"  ; [2]
//          [any-value?]
//      :meta "Return the ^^META result (allows checks on NULL and VOID)"
//  ]
//
DECLARE_NATIVE(match)
//
// Note: Ambitious ideas for the "MATCH dialect" are on hold, and this function
// just does some fairly simple matching:
//
//   https://forum.rebol.info/t/time-to-meet-your-match-dialect/1009/5
//
// 1. Passing in NULL for test is taken as a synonym for [null?], which isn't
//    usually very useful for MATCH, but it's useful for things built on it
//    (like ENSURE and NON):
//
//        >> result: null
//
//        >> ensure null result
//        == null
//
//        >> non null result
//        ** Error: NON argument cannot be [null?]
//
// 2. Passing in NULL for *value* creates a problem, because it conflates the
//    "didn't match" signal with the "did match" signal.  To solve this problem
//    requires MATCH:META
//
//        >> match:meta [~null~ integer!] 10
//        == '10
//
//        >> match:meta [~null~ integer!] null
//        == ~null~
//
//        >> match:meta [~null~ integer!] <some-tag>
//        == ~null~  ; anti
{
    INCLUDE_PARAMS_OF_MATCH;

    Value* v = ARG(value);
    Value* test = ARG(test);

    if (not REF(meta)) {
        if (Is_Nulled(v))
            return FAIL(Error_Need_Non_Null_Raw());  // [1]
    }

    if (Is_Nulled(test)) {
        if (not REF(meta))
            return FAIL(
                "Can't give coherent answer for NULL matching without /META"
            );

        if (Is_Nulled(v))
            return Init_Meta_Of_Null(OUT);

        return Init_Nulled(OUT);
    }

    switch (VAL_TYPE(test)) {
      case REB_PARAMETER:
      case REB_BLOCK:
      case REB_TYPE_WORD:
      case REB_TYPE_GROUP:
      case REB_TYPE_BLOCK:
        Copy_Cell(SPARE, v);
        if (not Typecheck_Atom_In_Spare_Uses_Scratch(LEVEL, test, SPECIFIED))
            return nullptr;
        break;

      default:
        assert(false);  // all test types should be accounted for in switch
        return FAIL(PARAM(test));
    }

    //=//// IF IT GOT THIS FAR WITHOUT RETURNING, THE TEST MATCHED /////////=//

    if (Is_Void(v) and not REF(meta))  // not a good case of void-in-null-out
        return FAIL("~void~ antiform needs MATCH:META if in set being tested");

    Copy_Cell(OUT, v);

    if (REF(meta))
        Meta_Quotify(OUT);

    return OUT;
}


//
//  /matcher: native [
//
//  "Make a specialization of the MATCH function for a fixed type argument"
//
//      return: [action?]
//      test "Type specification, can use NULL instead of [null?]"  ; [1]
//          [~null~ block! type-word! type-group! type-block! parameter!]
//  ]
//
DECLARE_NATIVE(matcher)
//
// This is a bit faster at making a specialization than usermode code.
//
// Compare with TYPECHECKER
//
//    >> m: matcher [null? integer!]
//
//    >> m 1020
//    == 1020
//
//    >> m <abc>
//    == ~null~  ; anti
//
//    >> m null
//    ** Script Error: non-NULL value required
//
//    >> m:meta null
//    == ~null~
//
// 1. See MATCH for comments.
{
    INCLUDE_PARAMS_OF_MATCHER;

    Value* test = ARG(test);

    Source* a = Make_Source_Managed(2);
    Set_Flex_Len(a, 2);
    Init_Set_Word(Array_At(a, 0), CANON(TEST));
    Copy_Meta_Cell(Array_At(a, 1), test);
    Init_Block(SPARE, a);

    if (Specialize_Action_Throws(OUT, LIB(MATCH), stable_SPARE, STACK_BASE))
        return THROWN;

    return OUT;
}
