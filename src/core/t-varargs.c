//
//  File: %t-varargs.h
//  Summary: "Variadic Argument Type and Services"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The VARARGS! data type implements an abstraction layer over an eval level
// or arbitrary array of values.  All copied instances of a TYPE_VARARGS value
// remain in sync as values are TAKE-d out of them.  Once they report
// reaching a TAIL? they will always report TAIL?...until the call that
// spawned them is off the stack, at which point they will report an error.
//

#include "sys-core.h"


INLINE void Init_For_Vararg_End(Atom* out, enum Reb_Vararg_Op op) {
    if (op == VARARG_OP_TAIL_Q)
        Init_Logic(out, true);
    else
        Init_Barrier(out);
}


// Some VARARGS! are generated from a block with no level, while others
// have a level.  It would be inefficient to force the creation of a level on
// each call for a BLOCK!-based varargs.  So rather than doing so, there's a
// prelude which sees if it can answer the current query just from looking one
// unit ahead.
//
INLINE bool Vararg_Op_If_No_Advance_Handled(
    Atom* out,
    enum Reb_Vararg_Op op,
    Option(const Element*) opt_look, // the first value in the varargs input
    Context* binding,
    ParamClass pclass
){
    if (not opt_look) {
        Init_For_Vararg_End(out, op); // exhausted
        return true;
    }

    const Element* look = unwrap opt_look;

    if (pclass == PARAMCLASS_NORMAL and Is_Comma(look)) {
        Init_For_Vararg_End(out, op);  // non-quoted COMMA!
        return true;
    }

    if (pclass == PARAMCLASS_NORMAL and Is_Word(look)) {
        //
        // When a variadic argument is being TAKE-n, deferred left hand side
        // argument needs to be seen as end of variadic input.  Otherwise,
        // `summation 1 2 3 |> 100` acts as `summation 1 2 (3 |> 100)`.
        // Deferred operators need to act somewhat as an expression barrier.
        //
        // Same rule applies for "tight" arguments, `sum 1 2 3 + 4` with
        // sum being variadic and tight needs to act as `(sum 1 2 3) + 4`
        //
        // Look ahead, and if actively bound see if it's to an infix function
        // and the rules apply.

        const Value* child_gotten = maybe Lookup_Word(look, binding);

        if (child_gotten and Is_Action(child_gotten)) {
            Option(InfixMode) infix_mode = Cell_Frame_Infix_Mode(child_gotten);
            if (infix_mode) {
                if (
                    pclass == PARAMCLASS_NORMAL or
                    infix_mode == INFIX_DEFER
                ){
                    Init_For_Vararg_End(out, op);
                    return true;
                }
            }
        }
    }

    // The odd circumstances which make things simulate END--as well as an
    // actual END--are all taken care of, so we're not "at the TAIL?"
    //
    if (op == VARARG_OP_TAIL_Q) {
        Init_Logic(out, false);
        return true;
    }

    if (op == VARARG_OP_FIRST) {
        if (pclass == PARAMCLASS_JUST)
            Copy_Cell(out, look);
        else if (pclass == PARAMCLASS_THE)
            Derelativize(out, look, binding);
        else
            fail (Error_Varargs_No_Look_Raw()); // hard quote only

        return true; // only a lookahead, no need to advance
    }

    return false; // must advance, may need to create a level to do so
}


//
//  Do_Vararg_Op_Maybe_End_Throws_Core: C
//
// Service routine for working with a VARARGS!.  Supports TAKE-ing or just
// returning whether it's at the end or not.  The TAKE is not actually a
// destructive operation on underlying data--merely a semantic chosen to
// convey feeding forward with no way to go back.
//
// Whether the parameter is quoted or evaluated is determined by the typeset
// information of the `param`.  The typeset in the param is also used to
// check the result, and if an error is delivered it will use the name of
// the parameter symbol in the fail() message.
//
// If op is VARARG_OP_TAIL_Q, then it will return LIB(BLANK) or LIB(NULL),
// and this case cannot return a thrown value.
//
// For other ops, it will return END_NODE if at the end of variadic input,
// or OUT if there is a value.
//
// If an evaluation is involved, then a thrown value is possibly returned.
//
bool Do_Vararg_Op_Maybe_End_Throws_Core(
    Sink(Atom) out,
    enum Reb_Vararg_Op op,
    const Cell* vararg,
    ParamClass pclass  // PARAMCLASS_0 to use vararg's class
){
    const Key* key;
    const Param* param = Param_For_Varargs_Maybe_Null(&key, vararg);
    if (pclass == PARAMCLASS_0)
        pclass = Cell_ParamClass(param);

    Option(Level*) vararg_level;

    Level* L;
    Element* shared;
    if (Is_Block_Style_Varargs(&shared, vararg)) {
        //
        // We are processing an ANY-LIST?-based varargs, which came from
        // either a MAKE VARARGS! on an ANY-LIST? value -or- from a
        // MAKE ANY-LIST? on a varargs (which reified the varargs into an
        // list during that creation, flattening its entire output).

        vararg_level = nullptr;

        if (Vararg_Op_If_No_Advance_Handled(
            out,
            op,
            Is_Cell_Poisoned(shared) ? nullptr : Cell_List_Item_At(shared),
            Is_Cell_Poisoned(shared) ? SPECIFIED : Cell_List_Binding(shared),
            pclass
        )){
            goto type_check_and_return;
        }

        // Note this may be Is_Varargs_Infix(), where the left hand side was
        // synthesized into an array-style varargs with either 0 or 1 item to
        // be taken.
        //
        // !!! Note also that if the argument is evaluative, it will be
        // evaluated when the TAKE occurs...which may be never, if no TAKE of
        // this argument happens.  Review if that should be an error.

        switch (pclass) {
        case PARAMCLASS_META:
            fail ("Variadic literal parameters not yet implemented");

        case PARAMCLASS_NORMAL: {
            Level* L_temp = Make_Level_At(
                &Stepper_Executor,
                shared,
                EVAL_EXECUTOR_FLAG_FULFILLING_ARG
            );
            Push_Level_Erase_Out_If_State_0(out, L_temp);

            // Note: a sublevel is not needed here because this is a single use
            // level, whose state can be overwritten.
            //
            if (Eval_Step_Throws(out, L_temp)) {
                Drop_Level(L_temp);
                return true;
            }

            if (Is_Feed_At_End(L_temp->feed) or Is_Barrier(out))
                Poison_Cell(shared);
            else {
                // The indexor is "prefetched", so though the temp level would
                // be ready to use again we're throwing it away, and need to
                // effectively "undo the prefetch" by taking it down by 1.
                //
                assert(Level_Array_Index(L_temp) > 0);
                VAL_INDEX_UNBOUNDED(shared) = Level_Array_Index(L_temp) - 1;
            }

            Drop_Level(L_temp);
            break; }

        case PARAMCLASS_THE:
            Derelativize(
                out,
                Cell_List_Item_At(shared),
                Cell_List_Binding(shared)
            );
            VAL_INDEX_UNBOUNDED(shared) += 1;
            break;

        case PARAMCLASS_JUST:
            Copy_Cell(out, Cell_List_Item_At(shared));
            VAL_INDEX_UNBOUNDED(shared) += 1;
            break;

        case PARAMCLASS_SOFT:
            if (Is_Soft_Escapable_Group(Cell_List_Item_At(shared))) {
                if (Eval_Any_List_At_Throws(
                    out, Cell_List_Item_At(shared), Cell_List_Binding(shared)
                )){
                    return true;
                }
            }
            else { // not a soft-"exception" case, quote ordinarily
                Derelativize(
                    out,
                    Cell_List_Item_At(shared),
                    Cell_List_Binding(shared)
                );
            }
            VAL_INDEX_UNBOUNDED(shared) += 1;
            break;

        default:
            fail ("Invalid variadic parameter class");
        }

        if (
            not Is_Cell_Poisoned(shared)
            and VAL_INDEX(shared) >= Cell_Series_Len_Head(shared)
        ){
            Poison_Cell(shared);  // signal end to all varargs sharing value
        }
    }
    else if (Is_Level_Style_Varargs_May_Fail(&L, vararg)) {
        //
        // "Ordinary" case... use the original level implied by the VARARGS!
        // (so long as it is still live on the stack)

        // The infixed case always synthesizes an array to hold the evaluated
        // left hand side value.  (See notes on Is_Varargs_Infix().)
        //
        assert(not Is_Varargs_Infix(vararg));

        vararg_level = L;

        Option(const Element*) look = nullptr;
        if (not Is_Level_At_End(L))
            look = At_Level(L);

        if (Vararg_Op_If_No_Advance_Handled(
            out,
            op,
            look,
            Level_Binding(L),
            pclass
        )){
            goto type_check_and_return;
        }

        // Note that evaluative cases here need a sublevel, because a function
        // is running in L and its state can't be overwritten by an arbitrary
        // evaluation.
        //
        switch (pclass) {
        case PARAMCLASS_NORMAL: {
            Flags flags = EVAL_EXECUTOR_FLAG_FULFILLING_ARG;

            Level* sub = Make_Level(&Stepper_Executor, L->feed, flags);
            if (Trampoline_Throws(out, sub))  // !!! Stackful, should yield!
                return true;
            break; }

        case PARAMCLASS_JUST:
            Just_Next_In_Feed(out, L->feed);
            break;

        case PARAMCLASS_THE:
            The_Next_In_Feed(out, L->feed);
            break;

        case PARAMCLASS_SOFT:
            if (Is_Soft_Escapable_Group(At_Level(L))) {
                if (Eval_Any_List_At_Throws(
                    out,
                    At_Level(L),
                    Level_Binding(L)
                )){
                    return true;
                }
                Fetch_Next_In_Feed(L->feed);
            }
            else // not a soft-"exception" case, quote ordinarily
                The_Next_In_Feed(out, L->feed);
            break;

        default:
            fail ("Invalid variadic parameter class");
        }
    }
    else
        panic ("Malformed VARARG cell");

  type_check_and_return:;

    if (Is_Cell_Erased(out))
        return false;

    if (op == VARARG_OP_TAIL_Q) {
        assert(Is_Logic(out));
        return false;
    }

    if (param and not Is_Barrier(out)) {
        if (not Typecheck_Coerce_Uses_Spare_And_Scratch(
            TOP_LEVEL, param, out, false
        )){
            // !!! Array-based varargs only store the parameter list they are
            // stamped with, not the level.  This is because storing non-reified
            // types in payloads is unsafe...only safe to store Level* in a
            // binding.  So that means only one level can be pointed to per
            // vararg.  Revisit the question of how to give better errors.
            //
            if (not vararg_level)
                fail (out);

            fail (Error_Phase_Arg_Type(
                unwrap vararg_level, key, param, cast(const Value*, out))
            );
        }
    }

    // Note: may be at end now, but reflect that at *next* call

    return false; // not thrown
}


IMPLEMENT_GENERIC(MAKE, Is_Varargs)
{
    INCLUDE_PARAMS_OF_MAKE;

    assert(Cell_Datatype_Heart(ARG(TYPE)) == TYPE_VARARGS);
    UNUSED(ARG(TYPE));

    Element* arg = Element_ARG(DEF);

    // With MAKE VARARGS! on an ANY-LIST?, the array is the backing store
    // (shared) that the varargs interface cannot affect, but changes to
    // the array will change the varargs.
    //
    if (Any_List(arg)) {
        //
        // Make a single-element array to hold a reference+index to the
        // incoming ANY-LIST?.  This level of indirection means all
        // VARARGS! copied from this will update their indices together.
        // By protocol, if the array is exhausted then the shared element
        // should be an END marker (not an array at its end)
        //
        Array* array1 = Alloc_Singular(FLEX_MASK_MANAGED_SOURCE);
        if (Cell_Series_Len_At(arg) == 0)
            Poison_Cell(Stub_Cell(array1));
        else
            Copy_Cell(Stub_Cell(array1), arg);

        Reset_Cell_Header_Noquote(TRACK(OUT), CELL_MASK_VARARGS);
        Tweak_Cell_Varargs_Phase(OUT, nullptr);
        UNUSED(CELL_VARARGS_SIGNED_PARAM_INDEX(OUT));  // corrupts in C++11
        Tweak_Cell_Varargs_Origin(OUT, array1);

        return OUT;
    }

    // !!! Permit FRAME! ?

    return FAIL(Error_Bad_Make(TYPE_VARARGS, arg));
}


// !!! It's not clear that TAKE is the best place to put the concept of
// getting the next value of a VARARGS!, though it seems to fit.
//
// 1. Usually TAKE has a series type which it can mirror on the output, e.g.
//    (take:part '{a b c d} 2) => {a b}.  But VARARGS! doesn't have a series
//    type so we just use BLOCK!.  Presumably that's the best answer?
//
IMPLEMENT_GENERIC(TAKE, Is_Varargs)
{
    INCLUDE_PARAMS_OF_TAKE;

    Element* varargs = cast(Element*, ARG(SERIES));

    if (Bool_ARG(DEEP))
        return FAIL(Error_Bad_Refines_Raw());
    if (Bool_ARG(LAST))
        return FAIL(Error_Varargs_Take_Last_Raw());

    if (not Bool_ARG(PART)) {
        if (Do_Vararg_Op_Maybe_End_Throws(
            OUT,
            VARARG_OP_TAKE,
            varargs
        )){
            return THROWN;
        }
        if (Is_Barrier(OUT))
            return RAISE(Error_Nothing_To_Take_Raw());
        return OUT;
    }

    assert(TOP_INDEX == STACK_BASE);

    if (not Is_Integer(ARG(PART)))
        return FAIL(PARAM(PART));

    REBINT limit = VAL_INT32(ARG(PART));
    if (limit < 0)
        limit = 0;

    while (limit-- > 0) {
        if (Do_Vararg_Op_Maybe_End_Throws(
            OUT,
            VARARG_OP_TAKE,
            varargs
        )){
            return THROWN;
        }
        if (Is_Barrier(OUT))
            break;
        Move_Cell(PUSH(), Decay_If_Unstable(OUT));
    }

    return Init_Block(OUT, Pop_Source_From_Stack(STACK_BASE));  // block? [1]
}


IMPLEMENT_GENERIC(PICK, Varargs)
{
    INCLUDE_PARAMS_OF_PICK;

    const Element* varargs = Element_ARG(LOCATION);
    const Element* picker = Element_ARG(PICKER);

    if (not Is_Integer(picker))
        return FAIL(picker);

    if (VAL_INT32(picker) != 1)
        return FAIL(Error_Varargs_No_Look_Raw());

    if (Do_Vararg_Op_Maybe_End_Throws(
        OUT,
        VARARG_OP_FIRST,
        varargs
    )){
        assert(false); // VARARG_OP_FIRST can't throw
        return THROWN;
    }
    if (Is_Barrier(OUT))
        Init_Nulled(OUT);

    return OUT; }



//
//  CT_Varargs: C
//
// Simple comparison function stub (required for every type--rules TBD for
// levels of "exactness" in equality checking, or sort-stable comparison.)
//
REBINT CT_Varargs(const Cell* a, const Cell* b, bool strict)
{
    UNUSED(strict);

    // !!! For the moment, say varargs are the same if they have the same
    // source feed from which the data comes.  (This check will pass even
    // expired varargs, because the expired stub should be kept alive as
    // long as its identity is needed).
    //
    if (Cell_Varargs_Origin(a) == Cell_Varargs_Origin(b))
        return 0;
    return Cell_Varargs_Origin(a) > Cell_Varargs_Origin(b) ? 1 : -1;
}


IMPLEMENT_GENERIC(TAIL_Q, Is_Varargs)
{
    INCLUDE_PARAMS_OF_TAIL_Q;

    Element* vararg = Element_ARG(ELEMENT);

    if (Do_Vararg_Op_Maybe_End_Throws(
        OUT,
        VARARG_OP_TAIL_Q,
        vararg
    )){
        assert(false);
        return THROWN;
    }
    assert(Is_Logic(OUT));
    return OUT;
}


IMPLEMENT_GENERIC(EQUAL_Q, Is_Varargs)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;

    return LOGIC(CT_Varargs(ARG(VALUE1), ARG(VALUE2), Bool_ARG(STRICT)) == 0);
}


// The molding of a VARARGS! does not necessarily have complete information,
// because it doesn't want to perform evaluations...or advance any frame it
// is tied to.  However, a few things are knowable; such as if the varargs
// has reached its end, or if the frame the varargs is attached to is no
// longer on the stack.
//
IMPLEMENT_GENERIC(MOLDIFY, Is_Varargs)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(ELEMENT);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = Bool_ARG(FORM);

    UNUSED(form);

    Begin_Non_Lexical_Mold(mo, v);  // #[varargs! or make varargs!

    Append_Codepoint(mo->string, '[');

    ParamClass pclass;
    const Key* key;
    const Param* param = Param_For_Varargs_Maybe_Null(&key, v);
    if (param == NULL) {
        pclass = PARAMCLASS_JUST;
        Append_Ascii(mo->string, "???"); // never bound to an argument
    }
    else {
        DECLARE_ELEMENT (param_word);
        switch ((pclass = Cell_ParamClass(param))) {
          case PARAMCLASS_NORMAL:
            Init_Word(param_word, Key_Symbol(key));
            break;

          case PARAMCLASS_JUST:
            Quotify(Init_Word(param_word, Key_Symbol(key)));
            break;

          case PARAMCLASS_THE:
            Init_Any_Word(param_word, TYPE_THE_WORD, Key_Symbol(key));
            break;

          case PARAMCLASS_SOFT:
            Quotify(Getify(Init_Word(param_word, Key_Symbol(key))));
            break;

          default:
            panic (NULL);
        };
        Mold_Element(mo, param_word);
    }

    Append_Ascii(mo->string, " => ");

    Level* L;
    Element* shared;
    if (Is_Block_Style_Varargs(&shared, v)) {
        if (Is_Cell_Poisoned(shared))
            Append_Ascii(mo->string, "[]");
        else if (pclass == PARAMCLASS_JUST or pclass == PARAMCLASS_THE)
            Mold_Element(mo, shared); // full feed can be shown if hard quoted
        else
            Append_Ascii(mo->string, "[...]"); // can't look ahead
    }
    else if (Is_Level_Style_Varargs_Maybe_Null(&L, v)) {
        if (L == NULL)
            Append_Ascii(mo->string, "!!!");
        else if (Is_Feed_At_End(L->feed)) {
            Append_Ascii(mo->string, "[]");
        }
        else if (pclass == PARAMCLASS_JUST or pclass == PARAMCLASS_THE) {
            Append_Ascii(mo->string, "[");
            Mold_Element(mo, At_Feed(L->feed));  // 1 value shown if hard quote
            Append_Ascii(mo->string, " ...]");
        }
        else
            Append_Ascii(mo->string, "[...]");
    }
    else
        assert(false);

    Append_Codepoint(mo->string, ']');

    End_Non_Lexical_Mold(mo);

    return NOTHING;
}


//
//  variadic?: native [
//
//  "Returns TRUE if a frame may take a variable number of arguments"
//
//      return: [logic?]
//      frame [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(VARIADIC_Q)
{
    INCLUDE_PARAMS_OF_VARIADIC_Q;

    Phase* phase = Cell_Frame_Phase(ARG(FRAME));

    const Key* key_tail;
    const Key* key = Phase_Keys(&key_tail, phase);
    const Value* param = Phase_Params_Head(phase);
    for (; key != key_tail; ++param, ++key) {
        if (Get_Parameter_Flag(param, VARIADIC))
            return Init_Logic(OUT, true);
    }

    return Init_Logic(OUT, false);
}
