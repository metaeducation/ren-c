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
        if ((opt Parameter_Spec(a)) > (opt Parameter_Spec(b)))
            return 1;
        return -1;
    }

    if (Parameter_Strand(a) != Parameter_Strand(b)) {
        if ((opt Parameter_Strand(a)) > (opt Parameter_Strand(b)))
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
// checking in a very efficient way, using intrinsics.  They have to be
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

        Init_Action(Sink_Lib_Value(id), details, Canon_Symbol(id), UNCOUPLED);
        assert(Ensure_Frame_Details(Lib_Stable(id)));
    }

    // Shorthands used in native specs, so have to be available in boot
    //
    Copy_Cell(Sink_LIB(PLAIN_Q), LIB(ANY_PLAIN_Q));
    Copy_Cell(Sink_LIB(FUNDAMENTAL_Q), LIB(ANY_FUNDAMENTAL_Q));
    Copy_Cell(Sink_LIB(ELEMENT_Q), LIB(ANY_ELEMENT_Q));
    Copy_Cell(Sink_LIB(QUASI_Q), LIB(QUASIFORM_Q));
}


//
//  Shutdown_Type_Predicates: C
//
void Shutdown_Type_Predicates(void)
{
}


//
//  Set_Spec_Of_Parameter_In_Top: C
//
// This copies the input spec as an array stored in the parameter, while
// setting flags appropriately and making notes for optimizations to help in
// the later typechecking.
//
Result(None) Set_Spec_Of_Parameter_In_Top(
    Level* const L,
    const Element* spec,
    Context* spec_binding
){
    USE_LEVEL_SHORTHANDS (L);

    StateByte saved_state = STATE;
    STATE = 1;

    assert(not Parameter_Spec(TOP_ELEMENT));
    possibly(Parameter_Strand(TOP_ELEMENT));

    ParamClass pclass = Parameter_Class(TOP_ELEMENT);
    assert(pclass != PARAMCLASS_0);  // must have class
    UNUSED(pclass);

    Flags flags = CELL_PARAMETER_PAYLOAD_2_FLAGS(TOP_ELEMENT);
    if (flags & PARAMETER_FLAG_REFINEMENT)
        assert(flags & PARAMETER_FLAG_NULL_DEFINITELY_OK);

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
  //
  // 1. Type specs can get pretty long now (especially with text comments that
  //    are preserved inside them).  Can be unreadable if newlines stripped.

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
        dont(Clear_Cell_Flag(dest, NEWLINE_BEFORE));  // assume significant [1]
    }

    CELL_PARAMETER_PAYLOAD_1_SPEC(TOP_ELEMENT) = copy;  // GC-protects copy
    Clear_Cell_Flag(TOP_ELEMENT, DONT_MARK_PAYLOAD_1);  // sync flag

} process_parameter_spec: {

  // 1. Since we copied the spec and relativized it, we could just walk
  //    the `dest` array and not look at the original items.  But keep it
  //    stylized this way in case this changes to copying-as-we-go.

    const Element* tail;
    const Element* item = List_At(&tail, spec);

    Element* dest = Array_Head(copy);  // assume item and dest distinct [1]

    TypesetByte* optimized = copy->misc.at_least_4;
    TypesetByte* optimized_tail = optimized + sizeof(uintptr_t);

    goto process_spec_item_if_not_at_tail;

  spoken_for: {  /////////////////////////////////////////////////////////////

  // The spec processing loop jumps here when the item's influence is fully
  // captured by PARAMETER_FLAG_XXX and the optimization bytes.

    Set_Cell_Flag(dest, PARAMSPEC_SPOKEN_FOR);
    goto loop;

} cant_optimize: {  //////////////////////////////////////////////////////////

  // The spec processing loop jumps here when there's not an optimization
  // captured by PARAMETER_FLAG_XXX and the optimization bytes, and hence if
  // the optimization checks fail the typechecker has to physically walk the
  // spec array and test the non-PARAMSPEC_SPOKEN_FOR items before deciding
  // the typecheck failed.

    flags |= PARAMETER_FLAG_INCOMPLETE_OPTIMIZATION;
    goto loop;

} loop: {  ///////////////////////////////////////////////////////////////////

    ++item;
    ++dest;
    goto process_spec_item_if_not_at_tail;

} process_spec_item_if_not_at_tail: {  ///////////////////////////////////////

  // Each spec item gets processed, and if PARAMETER_FLAG_PARAMSPEC_SPOKEN_FOR
  // is not set then it will be considered an incomplete optimization, and
  // it may be necessary for typechecking to walk the spec block to avoid any
  // false negatives.

    if (item == tail)
        goto finished_processing_spec;

} handle_text: {

  // It is now possible for people to write type specs with strings in them:
  //
  //       func [
  //           return: [
  //               block! "list of records as [person name age]"
  //               integer! "count of entries if :COUNT-ONLY used"
  //               ...
  //           ]
  //       ]

    if (Is_Text(item)) {  // TEXT! literals just ignored (kept for HELP)
        goto spoken_for;
    }

} handle_space: {

    if (Is_Space(item)) {
        flags |= PARAMETER_FLAG_SPACE_DEFINITELY_OK;
        goto spoken_for;
    }

} handle_tags: {

  // 1. TAG! parameter modifiers can't be abstracted.  So you can't say:
  //
  //        modifier: either condition [<end>] [<opt-out>]
  //        foo: func [arg [modifier integer!]] [...]
  //
  // 2. !!! The actual final notation for variadics is not decided on; it
  //    once used <...> but was changed to <variadic>`, may change back

    if (Heart_Of(item) == TYPE_TAG) {  // literal check of tag [1]
        bool strict = false;

        if (
            0 == CT_Utf8(item, g_tag_variadic, strict)
        ){
            flags |= PARAMETER_FLAG_VARIADIC;  // <variadic>, not <...> [2]
        }
        else if (0 == CT_Utf8(item, g_tag_end, strict)) {
            flags |= PARAMETER_FLAG_ENDABLE;
            flags |= PARAMETER_FLAG_VOID_DEFINITELY_OK;
        }
        else if (0 == CT_Utf8(item, g_tag_opt_out, strict)) {
            flags |= PARAMETER_FLAG_OPT_OUT;
            flags |= PARAMETER_FLAG_VOID_DEFINITELY_OK;
        }
        else if (0 == CT_Utf8(item, g_tag_opt, strict)) {
            flags |= PARAMETER_FLAG_UNDO_OPT;
            flags |= PARAMETER_FLAG_VOID_DEFINITELY_OK;
        }
        else if (0 == CT_Utf8(item, g_tag_const, strict)) {
            flags |= PARAMETER_FLAG_CONST;
        }
        else if (0 == CT_Utf8(item, g_tag_unrun, strict)) {
            flags |= PARAMETER_FLAG_UNRUN;
        }
        else if (0 == CT_Utf8(item, g_tag_divergent, strict)) {
            //
            // !!! Currently just commentary so we can find the divergent
            // functions.  Review what the best notation or functionality
            // concept is.
        }
        else if (0 == CT_Utf8(item, g_tag_null, strict)) {
            flags |= PARAMETER_FLAG_NULL_DEFINITELY_OK;
        }
        else if (0 == CT_Utf8(item, g_tag_void, strict)) {
            flags |= PARAMETER_FLAG_VOID_DEFINITELY_OK;
        }
        else {
            panic (item);
        }
        goto spoken_for;
    }

} handle_lifted_forms: {

  // optimize some cases? (e.g. ~word!~ or 'word! to be in optimized?)
  //
  // 1. You can end up with a FENCE! in a spec block, e.g. with:
  //
  //        func compose:deep [x [word! (lift integer!)]] [ ... ]
  //
  //    (But then the help will show the types as [word! ~{integer!}~].  Is it
  //    preferable to enforce words for some things?  That's not viable for
  //    type predicate actions, like ANY-ELEMENT?...)

    if (LIFT_BYTE(item) != NOQUOTE_2) {  // [~word!~ 'word! ''~block!~]...
        goto cant_optimize;  // no optimization strategy yet
    }

} handle_sigiled_forms: {

    if (Sigil_Of(item)) {  // [@integer! ^word!]...
        goto cant_optimize;  // no optimization strategy yet
    }

} handle_sequences: {

  // Sequences are how we recognize patterns like [word!: .block! /group!:],
  // as opposed to having predicates like SET-RUN-WORD? or figuring out how
  // to name all these variations.
  //
  // 1. Not going to optimize crazy cases, but maybe things like SET-WORD
  //    should have a TypesetByte.
  //
  // 2. We want to catch things like `[/worrd!:]` at spec creation time, so
  //   that you get the errors earlier rather than at typecheck time.

    if (Any_Sequence(item)) {
        Element* scratch = Copy_Cell(SCRATCH, item);
        do {
            SingleHeart singleheart = opt Try_Get_Sequence_Singleheart(scratch);
            if (not singleheart)
                panic ("No non-Singleheart sequences in typespec dialect ATM");
            assume (
              Unsingleheart_Sequence(scratch)
            );
        } while (Any_Sequence(scratch));

        if (not Is_Word(scratch))
            panic ("Only singleheart WORD! sequences in parameter specs");

        goto handle_word_in_scratch;  // lookup to ensure bound
    }

    if (Is_Word(item)) {
        Copy_Cell(SCRATCH, item);
        goto handle_word_in_scratch;
    }

} unrecognized_literal_spec_item: {

    panic (item);

} handle_word_in_scratch: {  /////////////////////////////////////////////////

  // We check WORD! last (which can also be a WORD! that was produced by
  // unsinglehearting sequences).
  //
  // Ren-C disallows unbounds, and validates what the word looks up to at the
  // time of creation.  If it didn't, then optimizations could not be
  // calculated at creation-time.
  //
  // (R3-Alpha had a fallback hack where unbound variables were interpreted
  // as their word.  So if you said `word!: integer!` and used WORD!, you'd
  // get the integer typecheck.  But if WORD! is unbound then it would act
  // as a WORD! typecheck.  This seems bad.)

    assert(Is_Word(Known_Element(SCRATCH)));
    heeded (Bind_If_Unbound(Known_Element(SCRATCH), spec_binding));
    heeded (Corrupt_Cell_If_Needful(SPARE));

    require (
      Get_Var_In_Scratch_To_Out(L, NO_STEPS)
    );

    if (Not_Cell_Stable(OUT))
        panic ("Parameter spec words must be bound to stable values");

    Stable* fetched = Known_Stable(OUT);

    Option(Type) type = Type_Of(fetched);

  handle_datatype: {

    if (type == TYPE_DATATYPE) {
        if (not Is_Word(item)) {
            assert(Any_Sequence(item));
            goto cant_optimize;
        }

        if (optimized == optimized_tail)
            goto cant_optimize;

        Option(Type) datatype_type = Datatype_Type(fetched);
        if (not datatype_type)
            goto cant_optimize;

        *optimized = u_cast(TypesetByte, unwrap datatype_type);
        ++optimized;
        goto spoken_for;
    }

} handle_action: {

    if (type == TYPE_ACTION) {
        if (not Is_Word(item)) {
            assert(Any_Sequence(item));
            goto cant_optimize;  // no optimizations for `/word!:` etc. ATM
        }

        Details* details = opt Try_Frame_Details(fetched);
        if (
            not details
            or Not_Details_Flag(details, CAN_DISPATCH_AS_INTRINSIC)
        ){
            goto cant_optimize;
        }

        Dispatcher* dispatcher = Details_Dispatcher(details);
        if (dispatcher == NATIVE_CFUNC(ANY_STABLE_Q)) {
            flags |= PARAMETER_FLAG_ANY_STABLE_OK;
            goto spoken_for;
        }
        if (dispatcher == NATIVE_CFUNC(ANY_VALUE_Q)) {
            flags |= PARAMETER_FLAG_ANY_ATOM_OK;
            goto spoken_for;
        }
        if (dispatcher == NATIVE_CFUNC(VOID_Q)) {
            flags |= PARAMETER_FLAG_VOID_DEFINITELY_OK;
            goto spoken_for;
        }
        if (dispatcher == &Typechecker_Dispatcher) {
            if (optimized == optimized_tail)
                goto cant_optimize;

            assert(Details_Max(details) == MAX_IDX_TYPECHECKER);

            Stable* index = Details_At(
                details, IDX_TYPECHECKER_TYPESET_BYTE
            );
            *optimized = VAL_UINT8(index);
            ++optimized;
            goto spoken_for;
        }
        goto cant_optimize;
    }

}} unrecognized_fetched_spec_item: {

  // By pre-checking we can avoid needing to double check in the actual
  // type-checking phase.

    panic (item);

} finished_processing_spec: {  ///////////////////////////////////////////////

    if (optimized != optimized_tail)
        *optimized = 0;  // signal termination (else tail is termination)

    Freeze_Source_Shallow(copy);  // !!! copy and freeze should likely be deep

    assert(Not_Cell_Flag(TOP_ELEMENT, VAR_MARKED_HIDDEN));
    CELL_PARAMETER_PAYLOAD_2_FLAGS(TOP_ELEMENT) = flags;

    STATE = saved_state;

    return none;
}}}


IMPLEMENT_GENERIC(MAKE, Is_Parameter)
{
    panic (UNHANDLED);  // !!! Needs to be designed!
}


IMPLEMENT_GENERIC(MOLDIFY, Is_Parameter)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(VALUE);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = did ARG(FORM);

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

    return TRASH;
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
        require (
          Refinify(e)
        );
    }

    switch (Parameter_Class(param)) {
      case PARAMCLASS_NORMAL:
        break;

      case PARAMCLASS_META:
        Metafy_Cell(e);
        break;

      case PARAMCLASS_SOFT: {
        Source *a = Alloc_Singular(STUB_MASK_MANAGED_SOURCE);
        Move_Cell(Stub_Cell(a), e);
        Pinify_Cell(Init_Group(e, a));
        break; }

      case PARAMCLASS_JUST:
        Quotify(e);
        break;

      case PARAMCLASS_THE:
        Pinify_Cell(e);
        break;

      default:
        assert(false);
        DEAD_END;
    }

    return e;
}


//
//  Undecorate_Element: C
//
// There's a bit of a question when asking what the "decoration" of a sequence
// is when it's like [.:foo] or [/:foo] or [.:/foo]
//
// We're willing to add a decoration to anything that will take it, so you
// can graft a slash onto the front of a CHAIN!.  But if you ask afterward
// what the decoration was, does it think that's `/` or `/:`?
//
// This isn't the most pressing question, so for now we just do something
// sort of random...and we'll see what usage patterns emerge.
//
void Undecorate_Element(Element* e)
{
    Noquotify(e);
    Clear_Cell_Sigil(e);

    if (not Any_Sequence(e))
        return;

    DECLARE_ELEMENT(temp);
    Copy_Sequence_At(temp, e, 0);

    if (
        Is_Space(temp)  // .foo :foo /foo
        or (
            Is_Word(temp) and (  // composite decoration weirdness :-(  [1]
                Word_Id(temp) == SYM_COLON_1  // (:/foo) "decoration" is colon
                or Word_Id(temp) == SYM_DOT_1  // (.:foo) "decoration" is dot
                // (/:foo) would be leading space, no lone slash in sequences!
            )
        )
    ) {
        Context* binding = Cell_Binding(e);

        StackIndex base = TOP_INDEX;
        Length len = Sequence_Len(e);
        if (Sequence_Len(e) == 2) {  // just make second element result
            Copy_Cell(temp, e);
            Copy_Sequence_At(e, temp, 1);
        }
        else {
            for (Length i = 1; i < len; i++)
                Copy_Sequence_At(PUSH(), e, i);
            assume (  // anything that could be in a sequence works outside
              Pop_Sequence_Or_Conflation(e, unwrap Heart_Of(e), base)
            );
        }
        Tweak_Cell_Binding(e, binding);  // preserve original binding
    }
}


//
//  Decorate_Element: C
//
Result(None) Decorate_Element(const Element* decoration, Element* element)
{
    Option(Count) quotes_to_add = Quotes_Of(decoration);
    Option(Sigil) sigil_to_add = Cell_Underlying_Sigil(decoration);
    Option(Heart) sequence_to_add = none;

    if (Is_Space_Underlying(decoration))
        goto finalize_decorations;  // not a sequence, just sigilize + quote

    if (Heart_Of(decoration) == TYPE_WORD) {
        switch (unwrap Word_Id(decoration)) {
          case SYM_DOT_1:
            sequence_to_add = TYPE_TUPLE;
            break;

          case SYM_COLON_1:
            sequence_to_add = TYPE_CHAIN;
            break;

          case SYM_SLASH_1:
            sequence_to_add = TYPE_PATH;
            break;

          default:
            panic ("DECORATE to make sequences only supports [. : /] WORD!s");
        }
        goto finalize_decorations;
    }

    if (Is_Parameter(decoration)) {
        Decorate_According_To_Parameter(element, decoration);
        goto finalize_decorations;
    }

    panic ("Unrecognized decoration in DECORATE");

  finalize_decorations: {  ///////////////////////////////////////////////////

  // 1. If we're trying to do something like adding a visual leading dot to a
  //    sequence, we're actually adding a space and making a TUPLE!.  If it
  //    was already a tuple, then we need to itemwise copy the existing tuple
  //    elements, as we cannot put a tuple-in-a-tuple.
  //
  //        >> decorate '. 'a:b
  //        == .a:b  ; easy we just push a space and the CHAIN!, then pop
  //
  //        >> decorate '. 'a.1.2.3
  //        == .a.1.2.3  ; harder: have to incorporate existing tuple items

    if (sequence_to_add) {
        Context* binding = Cell_Binding(element);

        StackIndex base = TOP_INDEX;
        Init_Space(PUSH());  // try to make sequence with leading space

        if (Heart_Of(element) != unwrap sequence_to_add) {  // push one item
            Copy_Cell(PUSH(), element);
            Tweak_Cell_Binding(TOP_ELEMENT, UNBOUND);  // we'll proxy binding
        }
        else {  // trickier case: push N items and merge [1]
            Length len = Sequence_Len(element);
            for (Length i = 0; i < len; i++)
                Copy_Sequence_At(PUSH(), element, i);
        }

        trap (  // note that this may fail; propagate error if so
          Pop_Sequence_Or_Conflation(element, unwrap sequence_to_add, base)
        );

        Tweak_Cell_Binding(element, binding);  // preserve original binding
    }

    if (sigil_to_add) {
        assert(not Sigil_Of(element));  // took PLAIN? in the type spec
        if (not Any_Sigilable(element))
            return fail ("DECORATE cannot apply Sigils to this type");

        Add_Cell_Sigil(element, unwrap sigil_to_add);
    }

    if (quotes_to_add)
        Quotify_Depth(element, unwrap quotes_to_add);

    return none;
}}


//
//  decorate: native [
//
//  "Based on the parameter type, this gives you e.g. @(foo) or :foo or 'foo"
//
//      return: [element?]
//      decoration [  ; TBD: create DECORATION? type constraint
//          <opt> "give back value as-is"
//          ~(@ ^ $ ~)~ "sigils on SPACE character act as proxy for sigilizing"
//          ~(. : /)~ "words as proxies for leading-blank sequence decoration"
//          ~(^. ^: ^/)~ "meta-word proxies"
//          ~(@. @: @/)~ "pinned-word proxies"
//          ~($. $: $/)~ "tied-word proxies"
//          quoted! "allows things like [' '' ''' '@ ''^ ''$] etc."
//          parameter! "apply parameter decoration rules as in spec dialect"
//          error! "if decoration is invalid for the value"
//      ]
//      value [<opt-out> plain?]
//  ]
//
DECLARE_NATIVE(DECORATE)
{
    INCLUDE_PARAMS_OF_DECORATE;

    Element* v = Element_ARG(VALUE);

    if (not ARG(DECORATION))
        return COPY(v);

    Element* decoration = Element_ARG(DECORATION);

    trap (
      Decorate_Element(decoration, v)
    );

    return COPY(v);
}


//
//  redecorate: native [
//
//  "Based on the parameter type, this gives you e.g. @(foo) or :foo or 'foo"
//
//      return: [element?]
//      decoration [  ; TBD: create DECORATION? type constraint
//          <opt> "give back value as-is"
//          ~(@ ^ $ ~)~ "sigils on SPACE character act as proxy for sigilizing"
//          ~(. : /)~ "words as proxies for leading-blank sequence decoration"
//          ~(^. ^: ^/)~ "meta-word proxies"
//          ~(@. @: @/)~ "pinned-word proxies"
//          ~($. $: $/)~ "tied-word proxies"
//          quoted! "allows things like [' '' ''' '@ ''^ ''$] etc."
//          parameter! "apply parameter decoration rules as in spec dialect"
//          error! "if decoration is invalid for the value"
//      ]
//      value [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(REDECORATE)
{
    INCLUDE_PARAMS_OF_REDECORATE;

    Element* v = Element_ARG(VALUE);

    if (not ARG(DECORATION))
        return COPY(v);

    Element* decoration = Element_ARG(DECORATION);

    Undecorate_Element(v);
    trap (
      Decorate_Element(decoration, v)
    );

    return COPY(v);
}


//
//  decoration-of: native [
//
//  "Give back the decoration of a value as per DECORATE"
//
//      return: [
//          ~(@ ^ $ ~)~ "sigils on SPACE character act as proxy for sigils"
//          ~(. : /)~ "words as proxies for leading-blank sequences"
//          quoted! "quotes are in decoration, like [' '' ''' '@ ''^ ''$] etc."
//          <null> "if none of the above decorations are present"
//      ]
//      value [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(DECORATION_OF)
{
    INCLUDE_PARAMS_OF_DECORATION_OF;

    Element* element = Element_ARG(VALUE);

    Option(Count) quotes = Quotes_Of(element);
    Noquotify(element);

    Option(Sigil) sigil = Sigil_Of(element);
    Clear_Cell_Sigil(element);

    Element* out;
    switch (unwrap Heart_Of(element)) {
      case TYPE_TUPLE:
        out = Init_Word(OUT, CANON(DOT_1));
        break;

      case TYPE_CHAIN:
        out = Init_Word(OUT, CANON(COLON_1));
        break;

      case TYPE_PATH:
        out = Init_Word(OUT, CANON(SLASH_1));
        break;

      default:
        if (not quotes and not sigil)
            return nullptr;  // no decoration

        out = Init_Space(OUT);  // need space to put decorations on
        break;
    }

    if (sigil)
        Add_Cell_Sigil(out, unwrap sigil);

    if (quotes)
        Quotify_Depth(out, unwrap quotes);

    return OUT;
}


//
//  undecorate: native [
//
//  "Remove decorations (Sigils, Quotes, leading-space sequences) from VALUE"
//
//      return: [<null> plain?]
//      value [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(UNDECORATE)
{
    INCLUDE_PARAMS_OF_UNDECORATE;

    Element* e = Element_ARG(VALUE);

    Undecorate_Element(e);

    return COPY(e);
}


IMPLEMENT_GENERIC(TWEAK_P, Is_Parameter)
{
    INCLUDE_PARAMS_OF_TWEAK_P;

    Element* param = Element_ARG(LOCATION);

    const Stable* picker = ARG(PICKER);
    if (not Is_Word(picker))
        panic (picker);

    Stable* dual = ARG(DUAL);
    if (Not_Lifted(dual)) {
        if (Is_Dual_Nulled_Pick_Signal(dual))
            goto handle_pick;

        panic (Error_Bad_Poke_Dual_Raw(dual));
    }

    goto handle_poke;

  handle_pick: { /////////////////////////////////////////////////////////////

    switch (opt Word_Id(picker)) {
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

    switch (opt Word_Id(picker)) {
      case SYM_TEXT: {
        if (not Is_Text(poke))
            panic (poke);
        require (
          Strand* strand = Copy_String_At(poke)
        );
        Manage_Stub(strand);
        Freeze_Flex(strand);
        Set_Parameter_Strand(param, strand);
        return WRITEBACK(Copy_Cell(OUT, param)); }  // need Cell pointer update

      default:
        break;
    }

    panic (Error_Bad_Pick_Raw(picker));
}}
