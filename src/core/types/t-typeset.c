//
//  file: %t-typeset.c
//  summary: "typeset datatype"
//  section: datatypes
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
REBINT CT_Parameter(const Element* a, const Element* b, bool strict)
{
    UNUSED(strict);

    assert(Heart_Of(a) == TYPE_PARAMETER);
    assert(Heart_Of(b) == TYPE_PARAMETER);

    if (Parameter_Spec(a) != Parameter_Spec(b)) {
        if ((maybe Parameter_Spec(a)) > (maybe Parameter_Spec(b)))
            return 1;
        return -1;
    }

    if (Parameter_Strand(a) != Parameter_Strand(b)) {
        if ((maybe Parameter_Strand(a)) > (maybe Parameter_Strand(b)))
            return 1;
        return -1;
    }

    if (Parameter_Class(a) != Parameter_Class(b))
        return Parameter_Class(a) > Parameter_Class(b) ? 1 : -1;

    return 0;
}


//
//  Startup_Type_Predicates: C
//
// Functions like ANY-SERIES? leverage the g_typesets[] table, to do type
// checking in a very efficient away, using intrinsics.  They have to be
// defined before the natives try to form their parameter lists so they can be
// queried for which TypesetByte to cache in the parameter.
//
void Startup_Type_Predicates(void)
{
    SymId16 id16;
    for (id16 = MIN_SYM_TYPESETS; id16 <= MAX_SYM_TYPESETS; id16 += 1) {
        SymId id = cast(SymId, id16);
        SymId16 typeset_byte = id16 - MIN_SYM_TYPESETS + 1;
        assert(typeset_byte == id16);  // MIN_SYM_TYPESETS should be 1
        assert(typeset_byte > 0 and typeset_byte < 256);

        Details* details = Make_Typechecker(typeset_byte);

        Init_Action(Sink_Lib_Var(id), details, Canon_Symbol(id), NONMETHOD);
        assert(Ensure_Cell_Frame_Details(Lib_Var(id)));
    }

    // Shorthands used in native specs, so have to be available in boot
    //
    Copy_Cell(Mutable_Lib_Var(SYM_PLAIN_Q), LIB(ANY_PLAIN_Q));
    Copy_Cell(Mutable_Lib_Var(SYM_FUNDAMENTAL_Q), LIB(ANY_FUNDAMENTAL_Q));
    Copy_Cell(Mutable_Lib_Var(SYM_ELEMENT_Q), LIB(ANY_ELEMENT_Q));
    Copy_Cell(Mutable_Lib_Var(SYM_QUASI_Q), LIB(QUASIFORM_Q));
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
// 1. Right now the assumption is that the param is GC safe.
//
// 2. TAG! parameter modifiers can't be abstracted.  So you can't say:
//
//        modifier: either condition [<end>] [<opt-out>]
//        foo: func [arg [modifier integer!]] [...]
//
// 3. Everything non-TAG! can be abstracted via WORD!.  This can lead to some
//    strange mixtures:
//
//        func compose:deep [x [word! (^integer!)]] [ ... ]
//
//    (But then the help will show the types as [word! ~{integer}~].  Is it
//    preferable to enforce words for some things?  That's not viable for
//    type predicate actions, like ANY-ELEMENT?...)
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
Result(Zero) Set_Parameter_Spec(
    Element* param,  // target should be GC safe [1]
    const Element* spec,
    Context* spec_binding
){
    ParamClass pclass = Parameter_Class(param);
    assert(pclass != PARAMCLASS_0);  // must have class

    uintptr_t* flags = &CELL_PARAMETER_PAYLOAD_2_FLAGS(param);
    if (*flags & PARAMETER_FLAG_REFINEMENT) {
        assert(*flags & PARAMETER_FLAG_NULL_DEFINITELY_OK);
    }
    UNUSED(pclass);

    Source* copy;

  copy_derelativized_spec_array: {

  // We go ahead and make a copy of the spec array, because we want to write
  // optimization bytes into it as we go.  Also, we do lookups of words which
  // may run arbitrary code (in theory), so we have to make sure the array
  // is in good enough shape to be GC protected.  So we make two passes.
  //
  // (This could be more efficient by doing a memcpy and then adjusting the
  // binding on the second walk, but just trying to keep the spec array from
  // getting GC'd in the middle of a first walk for now.)

    const Element* tail;
    const Element* item = List_At(&tail, spec);

    Length len = tail - item;

    copy = cast(Source*, Make_Array_For_Copy(
        STUB_MASK_MANAGED_SOURCE,
        Cell_Array(spec),
        len
    ));
    Set_Flex_Len(copy, len);

    Element* dest = Array_Head(copy);
    for (; item != tail; ++item, ++dest) {
        Derelativize(dest, item, spec_binding);
        Clear_Cell_Flag(dest, NEWLINE_BEFORE);
    }

} process_parameter_spec: {

    CELL_PARAMETER_PAYLOAD_1_SPEC(param) = copy;  // should GC protect the copy
    Clear_Cell_Flag(param, DONT_MARK_PAYLOAD_1);  // sync flag

    const Element* tail;
    const Element* item = List_At(&tail, spec);

    Element* dest = Array_Head(copy);

    TypesetByte* optimized = copy->misc.at_least_4;
    TypesetByte* optimized_tail = optimized + sizeof(uintptr_t);

    if (item == tail) {
        *flags |= PARAMETER_FLAG_TRASH_DEFINITELY_OK;
    }
    else for (; item != tail; ++item, ++dest) {
        if (Is_Space(item)) {
            *flags |= PARAMETER_FLAG_SPACE_DEFINITELY_OK;
            Set_Cell_Flag(dest, PARAMSPEC_SPOKEN_FOR);
            continue;
        }

        if (Is_Quasiform(item)) {  // optimize some cases? (e.g. ~word!~ ?)
            *flags |= PARAMETER_FLAG_INCOMPLETE_OPTIMIZATION;
            continue;
        }

        if (Heart_Of(item) == TYPE_TAG) {  // literal check of tag [2]
            bool strict = false;

            if (
                0 == CT_Utf8(item, g_tag_variadic, strict)
            ){
                // !!! The actual final notation for variadics is not decided
                // on, so there is compatibility for now with the <...> form
                // from when that was a TAG! vs. a 5-element TUPLE!  While
                // core sources were changed to `<variadic>`, asking users
                // to shuffle should only be done once (when final is known).
                //
                *flags |= PARAMETER_FLAG_VARIADIC;
            }
            else if (0 == CT_Utf8(item, g_tag_end, strict)) {
                *flags |= PARAMETER_FLAG_ENDABLE;
                *flags |= PARAMETER_FLAG_NULL_DEFINITELY_OK;
            }
            else if (0 == CT_Utf8(item, g_tag_opt_out, strict)) {
                *flags |= PARAMETER_FLAG_OPT_OUT;
                *flags |= PARAMETER_FLAG_VOID_DEFINITELY_OK;
            }
            else if (0 == CT_Utf8(item, g_tag_opt, strict)) {
                *flags |= PARAMETER_FLAG_UNDO_OPT;
                *flags |= PARAMETER_FLAG_VOID_DEFINITELY_OK;
            }
            else if (0 == CT_Utf8(item, g_tag_const, strict)) {
                *flags |= PARAMETER_FLAG_CONST;
            }
            else if (0 == CT_Utf8(item, g_tag_unrun, strict)) {
                // !!! Currently just commentary, degrading happens due
                // to type checking.  Review this.
            }
            else if (0 == CT_Utf8(item, g_tag_divergent, strict)) {
                //
                // !!! Currently just commentary so we can find the divergent
                // functions.  Review what the best notation or functionality
                // concept is.
            }
            else {
                abrupt_panic (item);
            }
            Set_Cell_Flag(dest, PARAMSPEC_SPOKEN_FOR);
            continue;
        }

        Option(Sigil) sigil = Sigil_Of(item);
        if (sigil) {  // !!! no sigil optimization yet (ever?)
            *flags |= PARAMETER_FLAG_INCOMPLETE_OPTIMIZATION;
            continue;
        }

        DECLARE_VALUE (lookup);

        if (Is_Word(item)) {  // allow abstraction [3]
            required (Get_Word(lookup, item, spec_binding));
        }
        else
            Copy_Cell(lookup, item);

        Option(Type) type = Type_Of(lookup);

        if (type == TYPE_DATATYPE) {
            if (optimized == optimized_tail) {
                *flags |= PARAMETER_FLAG_INCOMPLETE_OPTIMIZATION;
                continue;
            }
            Option(Type) datatype_type = Cell_Datatype_Type(lookup);
            if (not datatype_type) {
                *flags |= PARAMETER_FLAG_INCOMPLETE_OPTIMIZATION;
                continue;
            }
            *optimized = u_cast(Byte, unwrap datatype_type);
            ++optimized;
            Set_Cell_Flag(dest, PARAMSPEC_SPOKEN_FOR);
        }
        else if (type == TYPE_ACTION) {
            Details* details = maybe Try_Cell_Frame_Details(lookup);
            if (
                details
                and Get_Details_Flag(details, CAN_DISPATCH_AS_INTRINSIC)
            ){
                Dispatcher* dispatcher = Details_Dispatcher(details);
                if (dispatcher == NATIVE_CFUNC(ANY_VALUE_Q))
                    *flags |= PARAMETER_FLAG_ANY_VALUE_OK;
                else if (dispatcher == NATIVE_CFUNC(ANY_ATOM_Q))
                    *flags |= PARAMETER_FLAG_ANY_ATOM_OK;
                else if (dispatcher == NATIVE_CFUNC(VOID_Q))
                    *flags |= PARAMETER_FLAG_VOID_DEFINITELY_OK;
                else if (dispatcher == &Typechecker_Dispatcher) {
                    if (optimized == optimized_tail) {
                        *flags |= PARAMETER_FLAG_INCOMPLETE_OPTIMIZATION;
                        continue;
                    }

                    assert(Details_Max(details) == MAX_IDX_TYPECHECKER);

                    Value* index = Details_At(
                        details, IDX_TYPECHECKER_TYPESET_BYTE
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
        else if (KIND_BYTE(lookup) == TYPE_WORD) {  // @word! etc.
            *flags |= PARAMETER_FLAG_INCOMPLETE_OPTIMIZATION;
        }
        else {
            // By pre-checking we can avoid needing to double check in the
            // actual type-checking phase.

            panic (item);
        }
    }

    if (optimized != optimized_tail)
        *optimized = 0;  // signal termination (else tail is termination)

    Freeze_Source_Shallow(copy);  // !!! copy and freeze should likely be deep

    assert(Not_Cell_Flag(param, VAR_MARKED_HIDDEN));

    return zero;
}}


IMPLEMENT_GENERIC(MAKE, Is_Parameter)
{
    panic (UNHANDLED);  // !!! Needs to be designed!
}


IMPLEMENT_GENERIC(MOLDIFY, Is_Parameter)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(ELEMENT);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = Bool_ARG(FORM);

    if (not form) {
        Begin_Non_Lexical_Mold(mo, v);  // &[parameter!
    }

    DECLARE_ELEMENT(temp);
    Option(const Source*) param_array = Parameter_Spec(v);
    if (param_array)
        Init_Block(temp, unwrap param_array);
    else
        Init_Block(temp, g_empty_array);
    Decorate_According_To_Parameter(temp, v);

    Push_Lifeguard(temp);
    Mold_Or_Form_Element(mo, temp, form);
    Drop_Lifeguard(temp);

    if (not form) {
        End_Non_Lexical_Mold(mo);
    }

    return TRIPWIRE;
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
    const Element* param
){
    if (Get_Parameter_Flag(param, REFINEMENT)) {
        required (Refinify(e));
    }

    switch (Parameter_Class(param)) {
      case PARAMCLASS_NORMAL:
        break;

      case PARAMCLASS_META:
        Metafy(e);
        break;

      case PARAMCLASS_SOFT: {
        Source *a = Alloc_Singular(STUB_MASK_MANAGED_SOURCE);
        Move_Cell(Stub_Cell(a), e);
        Pinify(Init_Group(e, a));
        break; }

      case PARAMCLASS_JUST:
        Quotify(e);
        break;

      case PARAMCLASS_THE:
        Pinify(e);
        break;

      default:
        assert(false);
        DEAD_END;
    }

    return e;
}


//
//  decorate-parameter: native [
//
//  "Based on the parameter type, this gives you e.g. @(foo) or :foo or 'foo"
//
//      return: [element?]
//      parameter [parameter!]
//      element [element?]
//  ]
//
DECLARE_NATIVE(DECORATE_PARAMETER)
{
    INCLUDE_PARAMS_OF_DECORATE_PARAMETER;

    Element* element = Element_ARG(ELEMENT);
    Element* param = Element_ARG(PARAMETER);
    return COPY(Decorate_According_To_Parameter(element, param));
}


IMPLEMENT_GENERIC(TWEAK_P, Is_Parameter)
{
    INCLUDE_PARAMS_OF_TWEAK_P;

    Element* param = Element_ARG(LOCATION);

    const Value* picker = ARG(PICKER);
    if (not Is_Word(picker))
        panic (picker);

    Value* dual = ARG(DUAL);
    if (Not_Lifted(dual)) {
        if (Is_Dual_Nulled_Pick_Signal(dual))
            goto handle_pick;

        panic (Error_Bad_Poke_Dual_Raw(dual));
    }

    goto handle_poke;

  handle_pick: { /////////////////////////////////////////////////////////////

    switch (maybe Word_Id(picker)) {
      case SYM_TEXT: {
        Option(const Strand*) string = Parameter_Strand(param);
        if (not string)
            return DUAL_LIFTED(nullptr);
        return DUAL_LIFTED(Init_Text(OUT, unwrap string)); }

      case SYM_SPEC: {
        Option(const Source*) spec = Parameter_Spec(param);
        if (not spec)
            return DUAL_LIFTED(nullptr);
        return DUAL_LIFTED(Init_Block(OUT, unwrap spec)); }

      case SYM_OPTIONAL:
        return DUAL_LIFTED(Init_Logic(OUT, Get_Parameter_Flag(param, REFINEMENT)));

      case SYM_CLASS:
        switch (Parameter_Class(param)) {
          case PARAMCLASS_NORMAL:
            return DUAL_LIFTED(Init_Word(OUT, CANON(NORMAL)));

          case PARAMCLASS_META:
            return DUAL_LIFTED(Init_Word(OUT, CANON(META)));

          case PARAMCLASS_THE:
          case PARAMCLASS_SOFT:
            return DUAL_LIFTED(Init_Word(OUT, CANON(THE)));

          case PARAMCLASS_JUST:
            return DUAL_LIFTED(Init_Word(OUT, CANON(JUST)));

          default: assert(false);
        }
        crash (nullptr);

      case SYM_ESCAPABLE:
        Init_Logic(OUT, Parameter_Class(param) == PARAMCLASS_SOFT);
        return DUAL_LIFTED(OUT);

      /* case SYM_DECORATED: */  // No symbol! Use DECORATE-PARAMETER...

      default:
        break;
    }

    return fail (Error_Bad_Pick_Raw(picker));

} handle_poke: { /////////////////////////////////////////////////////////////

    Unliftify_Known_Stable(dual);

    if (Is_Antiform(dual))
        panic (Error_Bad_Antiform(dual));

    Element* poke = Known_Element(dual);

    switch (maybe Word_Id(picker)) {
      case SYM_TEXT: {
        if (not Is_Text(poke))
            panic (poke);
        Strand* strand = Copy_String_At(poke);
        Manage_Flex(strand);
        Freeze_Flex(strand);
        Set_Parameter_Strand(param, strand);
        return WRITEBACK(Copy_Cell(OUT, param)); }  // need Cell pointer update

      default:
        break;
    }

    panic (Error_Bad_Pick_Raw(picker));
}}
