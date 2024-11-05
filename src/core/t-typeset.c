//
//  File: %t-typeset.c
//  Summary: "typeset datatype"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
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

#include "sys-core.h"


//
//  CT_Parameter: C
//
REBINT CT_Parameter(const Cell* a, const Cell* b, bool strict)
{
    UNUSED(strict);

    assert(Cell_Heart(a) == REB_PARAMETER);
    assert(Cell_Heart(b) == REB_PARAMETER);

    if (
        Cell_Parameter_Spec(a) != Cell_Parameter_Spec(b)
    ){
        if (
            maybe Cell_Parameter_Spec(a)
            > maybe Cell_Parameter_Spec(b)
        ){
            return 1;
        }
        return -1;
    }

    if (
        Cell_Parameter_String(a) != Cell_Parameter_String(b)
    ){
        if (
            maybe Cell_Parameter_String(a)
            > maybe Cell_Parameter_String(b)
        ){
            return 1;
        }
        return -1;
    }

    if (Cell_ParamClass(a) != Cell_ParamClass(b))
        return Cell_ParamClass(a) > Cell_ParamClass(b) ? 1 : -1;

    return 0;
}


//
//  Startup_Type_Predicates: C
//
// Functions like ANY-SERIES? are built on top of macros like Any_Series(),
// and are needed for typechecking in natives.  They have to be defined
// before the natives try to form their parameter lists so they can be
// queried for which "Decider" optimizations to cache in the parameter.
//
void Startup_Type_Predicates(void)
{
    REBINT id;
    for (id = SYM_ANY_UNIT_Q; id != SYM_DATATYPES; id += 1) {
        REBINT n = REB_MAX + (id - SYM_ANY_UNIT_Q);  // skip REB_T_RETURN

        Phase* typechecker = Make_Typechecker(n);  // n is decider_index

        Init_Action(
            Sink_Lib_Var_For_Id(cast(SymId, id)),
            typechecker,
            Canon_Symbol(cast(SymId, id)),  // cached symbol for function
            UNBOUND
        );
    }
}


//
//  Shutdown_Typesets: C
//
void Shutdown_Typesets(void)
{
}


//
//  Set_Parameter_Spec: C
//
// This copies the input spec as an array stored in the parameter, while
// setting flags appropriately and making notes for optimizations to help in
// the later typechecking.
//
// 1. As written, the function spec processing code builds the parameter
//    directly into a stack variable.  That means this code can't PUSH()
//    (or call code that does).  It's not impossible to relax this and
//    have the code build the parameter into a non-stack variable then
//    copy it...but try avoiding that.
//
// 2. TAG! parameter modifiers can't be abstracted.  So you can't say:
//
//        modifier: either condition [<end>] [<maybe>]
//        /foo: func [arg [modifier integer!]] [...]
//
// 3. Everything non-TAG! can be abstracted via WORD!.  This can lead to some
//    strange mixtures:
//
//        func compose:deep [x [word! (integer!)]] [ ... ]
//
//    (But then the help will show the types as [word! &integer].  Is it
//    preferable to enforce words for some things?  That's not viable for
//    type predicate actions, like SPLICE?...)
//
// 4. Ren-C disallows unbounds, and validates what the word looks up to
//    at the time of creation.  If it didn't, then optimizations could not
//    be calculated at creation-time.
//
//    (R3-Alpha had a hacky fallback where unbound variables were interpreted
//    as their word.  So if you said `word!: integer!` and used WORD!, you'd
//    get the integer typecheck... but if WORD! is unbound then it would act
//    as a WORD! typecheck.)
//
void Set_Parameter_Spec(
    Cell* param,  // target is usually a stack value [1]
    const Cell* spec,
    Context* spec_binding
){
    ParamClass pclass = Cell_ParamClass(param);
    assert(pclass != PARAMCLASS_0);  // must have class

    uintptr_t* flags = &PARAMETER_FLAGS(param);
    if (*flags & PARAMETER_FLAG_REFINEMENT) {
        assert(*flags & PARAMETER_FLAG_NULL_DEFINITELY_OK);
        assert(pclass != PARAMCLASS_RETURN);
    }
    UNUSED(pclass);

    const Element* tail;
    const Element* item = Cell_List_At(&tail, spec);

    Length len = tail - item;

    Source* copy = cast(Source*, Make_Array_For_Copy(
        FLEX_MASK_MANAGED_SOURCE,
        Cell_Array(spec),
        len
    ));
    Set_Flex_Len(copy, len);
    Cell* dest = Array_Head(copy);

    Byte* optimized = copy->misc.any.at_least_4;
    Byte* optimized_tail = optimized + sizeof(uintptr_t);

    for (; item != tail; ++item, ++dest) {
        Derelativize(dest, item, spec_binding);
        Clear_Cell_Flag(dest, NEWLINE_BEFORE);

        if (Is_Quasiform(item)) {
            if (Cell_Heart(item) == REB_BLANK) {
                *flags |= PARAMETER_FLAG_NOTHING_DEFINITELY_OK;
                continue;
            }
            if (not Is_Stable_Antiform_Heart(Cell_Heart(item))) {
                if (Cell_Heart(item) != REB_BLOCK)  // typecheck packs ok
                    fail (item);
            }

            if (Cell_Heart(item) != REB_WORD) {
                *flags |= PARAMETER_FLAG_INCOMPLETE_OPTIMIZATION;
                continue;
            }

            switch (Cell_Word_Id(item)) {
              case SYM_NULL:
                *flags |= PARAMETER_FLAG_NULL_DEFINITELY_OK;
                break;
              case SYM_VOID:
                *flags |= PARAMETER_FLAG_VOID_DEFINITELY_OK;
                break;
              default:
                *flags |= PARAMETER_FLAG_INCOMPLETE_OPTIMIZATION;
            }
            continue;
        }
        if (Is_Quoted(item)) {  // /foo: func [size ['small 'medium 'large]]...
            *flags |= PARAMETER_FLAG_INCOMPLETE_OPTIMIZATION;
            continue;
        }

        if (Cell_Heart(item) == REB_TAG) {  // literal check of tag [2]
            bool strict = false;

            if (
                0 == CT_String(item, Root_Variadic_Tag, strict)
            ){
                // !!! The actual final notation for variadics is not decided
                // on, so there is compatibility for now with the <...> form
                // from when that was a TAG! vs. a 5-element TUPLE!  While
                // core sources were changed to `<variadic>`, asking users
                // to shuffle should only be done once (when final is known).
                //
                *flags |= PARAMETER_FLAG_VARIADIC;
                Init_Quasi_Word(dest, Canon(VARIADIC_Q)); // !!!
            }
            else if (0 == CT_String(item, Root_End_Tag, strict)) {
                *flags |= PARAMETER_FLAG_ENDABLE;
                Init_Quasi_Word(dest, Canon(NULL));  // !!!
                *flags |= PARAMETER_FLAG_NULL_DEFINITELY_OK;
            }
            else if (0 == CT_String(item, Root_Maybe_Tag, strict)) {
                *flags |= PARAMETER_FLAG_NOOP_IF_VOID;
                Set_Cell_Flag(dest, PARAMSPEC_SPOKEN_FOR);
                Init_Quasi_Word(dest, Canon(VOID));  // !!!
            }
            else if (0 == CT_String(item, Root_Const_Tag, strict)) {
                *flags |= PARAMETER_FLAG_CONST;
                Set_Cell_Flag(dest, PARAMSPEC_SPOKEN_FOR);
                Init_Quasi_Word(dest, Canon(CONST));
            }
            else if (0 == CT_String(item, Root_Unrun_Tag, strict)) {
                // !!! Currently just commentary, degrading happens due
                // to type checking.  Review this.
                //
                Init_Quasi_Word(dest, Canon(UNRUN));
            }
            else {
                fail (item);
            }
            continue;
        }

        const Value* lookup;
        if (Cell_Heart(item) == REB_WORD) {  // allow abstraction [3]
            lookup = maybe Lookup_Word(item, spec_binding);
            if (not lookup)  // not even bound to anything
                fail (item);
            if (Is_Nothing(lookup)) {  // bound but not set
                //
                // !!! This happens on things like LOGIC?, because they are
                // assigned in usermode code.  That misses an optimization
                // opportunity...suggesting strongly those be done sooner.
                //
                *flags |= PARAMETER_FLAG_INCOMPLETE_OPTIMIZATION;
                continue;
            }
            if (Is_Antiform(lookup) and Cell_Heart(lookup) != REB_FRAME)
                fail (item);
            if (Is_Quoted(lookup))
                fail (item);
        }
        else
            lookup = item;

        Heart heart = Cell_Heart(lookup);

        if (heart == REB_TYPE_BLOCK) {
            if (optimized == optimized_tail) {
                *flags |= PARAMETER_FLAG_INCOMPLETE_OPTIMIZATION;
                continue;
            }
            *optimized = VAL_TYPE_KIND(lookup);
            ++optimized;
            Set_Cell_Flag(dest, PARAMSPEC_SPOKEN_FOR);
        }
        else if (
            heart == REB_TYPE_WORD
            or heart == REB_TYPE_PATH or heart == REB_TYPE_TUPLE
        ){
            const Value* slot;
            Option(Error*) error = Trap_Lookup_Word(
                &slot, Ensure_Element(lookup), SPECIFIED
            );
            if (error)
                fail (unwrap error);
            if (not Is_Action(slot))
                fail ("TYPE-WORD! must look up to an action for now");
            heart = REB_FRAME;
            lookup = slot;
            goto handle_predicate;
        }
        else if (heart == REB_FRAME and QUOTE_BYTE(lookup) == ANTIFORM_0) {
          handle_predicate: {
            Phase* phase = ACT_IDENTITY(VAL_ACTION(lookup));
            if (ACT_DISPATCHER(phase) == &Intrinsic_Dispatcher) {
                Intrinsic* intrinsic = Extract_Intrinsic(phase);
                if (intrinsic == &N_any_value_q)
                    *flags |= PARAMETER_FLAG_ANY_VALUE_OK;
                else if (intrinsic == &N_any_atom_q)
                    *flags |= PARAMETER_FLAG_ANY_ATOM_OK;
                else if (intrinsic == &N_nihil_q)
                    *flags |= PARAMETER_FLAG_NIHIL_DEFINITELY_OK;
                else if (intrinsic == &Typechecker_Intrinsic) {
                    if (optimized == optimized_tail) {
                        *flags |= PARAMETER_FLAG_INCOMPLETE_OPTIMIZATION;
                        continue;
                    }

                    Details* details = Phase_Details(phase);
                    assert(Array_Len(details) == IDX_TYPECHECKER_MAX);

                    Value* index = Details_At(
                        details,
                        IDX_TYPECHECKER_DECIDER_INDEX
                    );

                    *optimized = VAL_UINT8(index);
                    ++optimized;
                }
                else
                    *flags |= PARAMETER_FLAG_INCOMPLETE_OPTIMIZATION;
            }
            else
                *flags |= PARAMETER_FLAG_INCOMPLETE_OPTIMIZATION;
          }
        }
        else {
            // By pre-checking we can avoid needing to double check in the
            // actual type-checking phase.

            fail (item);
        }
    }

    if (optimized != optimized_tail)
        *optimized = 0;  // signal termination (else tail is termination)

    Freeze_Source_Shallow(copy);  // !!! copy and freeze should likely be deep
    Tweak_Cell_Parameter_Spec(param, copy);

    assert(Not_Cell_Flag(param, VAR_MARKED_HIDDEN));
}


//
//  /hole?: native:intrinsic [
//
//  "Tells you if argument is parameter antiform, used for unspecialized args"
//
//      return: [logic?]
//      ^value  ; cannot take parameter antiform as normal argument [1]
//  ]
//
DECLARE_INTRINSIC(hole_q)
//
// 1. Although the antiform of PARAMETER! is stable, it is fundamental to the
//    argument gathering process that it represents an unspecialized slot.
//    Hence any function intending to take parameter antiforms must use the
//    ^META argument convention.
{
    UNUSED(phase);

    Init_Logic(out, Is_Meta_Of_Hole(arg));
}


//
//  Makehook_Parameter: C
//
Bounce Makehook_Parameter(Level* level_, Kind kind, Element* arg) {
    assert(kind == REB_PARAMETER);
    UNUSED(kind);
    return RAISE(Error_Bad_Make(REB_PARAMETER, arg));
}


//
//  MF_Parameter: C
//
void MF_Parameter(Molder* mo, const Cell* v, bool form)
{
    if (not form) {
        Begin_Non_Lexical_Mold(mo, v);  // #[parameter! or make parameter!
    }

    DECLARE_ELEMENT(temp);
    Option(const Source*) param_array = Cell_Parameter_Spec(v);
    if (param_array)
        Init_Block(temp, unwrap param_array);
    else
        Init_Block(temp, EMPTY_ARRAY);
    Decorate_According_To_Parameter(temp, v);

    Push_Lifeguard(temp);
    Mold_Or_Form_Element(mo, temp, form);
    Drop_Lifeguard(temp);

    if (not form) {
        End_Non_Lexical_Mold(mo);
    }
}


//
//  Decorate_According_To_Parameter: C
//
// Instead of PARAMETERS OF coming back with an array of decorated arguments,
// you can use a parameter to decorate a word.
//
// So based on the parameter type, this gives you e.g. @(foo) or :foo or 'foo
// if you pass in a WORD!.  But can decorate other things (BLOCK!, etc.)
// so you can decorate a type block, like @([integer! block!])
//
Element* Decorate_According_To_Parameter(
    Need(Element*) e,
    const Cell* param
){
    if (Get_Parameter_Flag(param, REFINEMENT))
        Refinify(e);

    switch (Cell_ParamClass(param)) {
      case PARAMCLASS_RETURN:
      case PARAMCLASS_NORMAL:
        break;

      case PARAMCLASS_META:
        Metafy(e);
        break;

      case PARAMCLASS_SOFT: {
        Source *a = Alloc_Singular(FLEX_MASK_MANAGED_SOURCE);
        Move_Cell(Stub_Cell(a), e);
        Init_Any_List(e, REB_THE_GROUP, a);
        break; }

      case PARAMCLASS_JUST:
        Quotify(e, 1);
        break;

      case PARAMCLASS_THE:
        Theify(e);
        break;

      default:
        assert(false);
        DEAD_END;
    }

    return e;
}


//
//  /decorate-parameter: native [
//
//  "Based on the parameter type, this gives you e.g. @(foo) or :foo or 'foo"
//
//      return: [element?]
//      parameter [parameter!]
//      element [element?]
//  ]
//
DECLARE_NATIVE(decorate_parameter)
{
    INCLUDE_PARAMS_OF_DECORATE_PARAMETER;

    Element* element = cast(Element*, ARG(element));
    Element* param = cast(Element*, ARG(parameter));
    return COPY(Decorate_According_To_Parameter(element, param));
}


//
//  DECLARE_GENERICS: C
//
DECLARE_GENERICS(Parameter)
{
    Option(SymId) id = Symbol_Id(verb);

    Element* param = cast(Element*, (id == SYM_TO) ? ARG_N(2) : ARG_N(1));
    assert(Is_Parameter(param));

    switch (id) {

    //=//// PICK* (see %sys-pick.h for explanation) ////////////////////////=//

      case SYM_PICK: {
        INCLUDE_PARAMS_OF_PICK;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);
        if (not Is_Word(picker))
            return FAIL(picker);

        switch (Cell_Word_Id(picker)) {
          case SYM_TEXT: {
            Option(const String*) string = Cell_Parameter_String(param);
            if (not string)
                return nullptr;
            return Init_Text(OUT, unwrap string); }

          case SYM_SPEC: {
            Option(const Source*) spec = Cell_Parameter_Spec(param);
            if (not spec)
                return nullptr;
            return Init_Block(OUT, unwrap spec); }

          case SYM_OPTIONAL:
            return Init_Logic(OUT, Get_Parameter_Flag(param, REFINEMENT));

          case SYM_CLASS:
            switch (Cell_ParamClass(param)) {
              case PARAMCLASS_NORMAL:
                return Init_Word(OUT, Canon(NORMAL));

              case PARAMCLASS_META:
                return Init_Word(OUT, Canon(META));

              case PARAMCLASS_THE:
              case PARAMCLASS_SOFT:
                return Init_Word(OUT, Canon(THE));

              case PARAMCLASS_JUST:
                return Init_Word(OUT, Canon(JUST));

              case PARAMCLASS_RETURN:
                return Init_Word(OUT, Canon(RETURN));

              default: assert(false);
            }
            panic (nullptr);

          case SYM_ESCAPABLE:
            return Init_Logic(OUT, Cell_ParamClass(param) == PARAMCLASS_SOFT);

          /* case SYM_DECORATED: */  // No symbol! Use DECORATE-PARAMETER...

          default:
            break;
        }

        return RAISE(Error_Bad_Pick_Raw(picker)); }


    //=//// POKE* (see %sys-pick.h for explanation) ////////////////////////=//

      case SYM_POKE: {
        INCLUDE_PARAMS_OF_POKE;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);
        if (not Is_Word(picker))
            return FAIL(picker);

        Value* setval = ARG(value);

        switch (Cell_Word_Id(picker)) {
          case SYM_TEXT: {
            if (not Is_Text(setval))
                return FAIL(setval);
            String* string = Copy_String_At(setval);
            Manage_Flex(string);
            Freeze_Flex(string);
            Set_Parameter_String(param, string);
            return COPY(param); }  // update to container (e.g. varlist) needed

          default:
            break;
        }

        return FAIL(Error_Bad_Pick_Raw(picker)); }

      default:
        break;
    }

    return UNHANDLED;
}
