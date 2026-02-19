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


IMPLEMENT_GENERIC(EQUAL_Q, Is_Parameter)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;

    Element* a = Element_ARG(VALUE1);
    Element* b = Element_ARG(VALUE2);
    UNUSED(PARAM(RELAX));

    if (Parameter_Spec(a) != Parameter_Spec(b))
        return LOGIC_OUT(false);

    if (Parameter_Strand(a) != Parameter_Strand(b))
        return LOGIC_OUT(false);

    if (CELL_PARAMETER_PAYLOAD_2_FLAGS(a) != CELL_PARAMETER_PAYLOAD_2_FLAGS(b))
        return LOGIC_OUT(false);

    return LOGIC_OUT(true);
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
        SymId16 typeset_byte = id16 - MIN_SYM_TYPESETS + 1;
        assert(typeset_byte == id16);  // MIN_SYM_TYPESETS should be 1
        assert(typeset_byte > 0 and typeset_byte < 256);

        Details* details = Make_Typechecker(typeset_byte);

        SymId id = i_cast(SymId, id16);
        Value* v = Sink_Lib_Value(id);
        Init_Action(v, details, Canon_Symbol(id), UNCOUPLED);
        Set_Cell_Flag(v, FINAL);  // so it can be used in PURE functions

        assert(Ensure_Frame_Details(Lib_Value(id)));
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
    Context* spec_binding,
    bool is_returner
){
    USE_LEVEL_SHORTHANDS (L);

    StateByte saved_state = STATE;
    STATE = 1;

    assert(not Parameter_Spec(TOP_SLOT));
    possibly(Parameter_Strand(TOP_SLOT));

    ParamClass pclass = Parameter_Class(TOP_SLOT);
    assert(pclass != PARAMCLASS_0);  // must have class

    Flags flags = CELL_PARAMETER_PAYLOAD_2_FLAGS(TOP_SLOT);
    if (flags & PARAMETER_FLAG_REFINEMENT)
        assert(flags & PARAMETER_FLAG_NULL_DEFINITELY_OK);

    Source* copy;

  notice_request_for_no_typechecking: {

  // See CELL_FLAG_PARAM_NOT_CHECKED_OR_COERCED for rationale and guidance on
  // using a quote mark on type specs to suppress type checking.

    Count quotes = Quotes_Of(spec);
    if (quotes) {
        if (quotes > 1)
            return fail (
                "Only one level of quote to suppress checking of types in spec"
            );

        Set_Cell_Flag(TOP_SLOT, PARAM_NOT_CHECKED_OR_COERCED);
    }

} copy_derelativized_spec_array: {

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
        Copy_Cell_May_Bind(dest, item, spec_binding);
        dont(Clear_Cell_Flag(dest, NEWLINE_BEFORE));  // assume significant [1]
    }

    CELL_PARAMETER_PAYLOAD_1_SPEC(TOP_SLOT) = copy;  // GC-protects copy
    Clear_Cell_Flag(TOP_SLOT, DONT_MARK_PAYLOAD_1);  // sync flag

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

    Set_Cell_Flag(dest, TYPE_MARKED_SPOKEN_FOR);
    goto loop;

} cant_optimize: {  //////////////////////////////////////////////////////////

  // The spec processing loop jumps here when there's not an optimization
  // captured by PARAMETER_FLAG_XXX and the optimization bytes, and hence if
  // the optimization checks fail the typechecker has to physically walk the
  // spec array and test the non-TYPE_MARKED_SPOKEN_FOR items before deciding
  // the typecheck failed.

    flags |= PARAMETER_FLAG_INCOMPLETE_OPTIMIZATION;
    goto loop;

} loop: {  ///////////////////////////////////////////////////////////////////

    ++item;
    ++dest;
    goto process_spec_item_if_not_at_tail;

} process_spec_item_if_not_at_tail: {  ///////////////////////////////////////

  // Each spec item gets processed, and if PARAMETER_FLAG_TYPE_MARKED_SPOKEN_FOR
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
  //        modifier: either condition [<hole>] [<opt>]
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
        else if (0 == CT_Utf8(item, g_tag_hole, strict)) {
            flags |= PARAMETER_FLAG_HOLE_OK;
        }
        else if (0 == CT_Utf8(item, g_tag_veto, strict)) {
            if (pclass != PARAMCLASS_META)
                panic ("<veto> tag only valid on ^META parameters");
            flags |= PARAMETER_FLAG_WANT_VETO;
        }
        else if (0 == CT_Utf8(item, g_tag_opt, strict)) {
            flags |= PARAMETER_FLAG_UNDO_OPT;
        }
        else if (0 == CT_Utf8(item, g_tag_const, strict)) {
            flags |= PARAMETER_FLAG_CONST;
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

    if (LIFT_BYTE(item) != NOQUOTE_3) {  // [~word!~ 'word! ''~block!~]...
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
  //
  // 1. !!! In order to get the system booting, if a parameter is marked as
  //    being not checked or coerced, we allow it to not exist at all.
  //    But we shouldn't be doing that.  There needs to be better logic
  //    for validating the case with parameters that you aren't checking.
  //
  // 2. Pursuant to [1] there may be someone who wants to check the parameter
  //    when the NOT_CHECKED_OR_COERCED flag is flipped in the Cell.  We
  //    should be optimizing for if that happens.

    assert(Is_Word(As_Element(SCRATCH)));
    Add_Cell_Sigil(As_Element(SCRATCH), SIGIL_META);  // want ACTION!, etc

    if (Not_Parameter_Checked_Or_Coerced(TOP_SLOT))
        goto cant_optimize;  // Typecheck will work, but slowly [2]

    heeded (Bind_Cell_If_Unbound(As_Element(SCRATCH), spec_binding));
    heeded (Corrupt_Cell_If_Needful(SPARE));

    require (  // !!! should have a persistent Level to hold var
      Get_Word_Or_Tuple(OUT, As_Element(SCRATCH))
    );

  handle_datatype: {

    if (Is_Possibly_Unstable_Value_Datatype(OUT)) {
        if (not Is_Word(item)) {
            assert(Any_Sequence(item));
            goto cant_optimize;
        }

        if (optimized == optimized_tail)
            goto cant_optimize;

        Option(Type) datatype_type = Datatype_Type(As_Stable(OUT));
        if (not datatype_type)
            goto cant_optimize;

        if (
            not is_returner
            and i_cast(TypeByte, datatype_type) > MAX_TYPEBYTE_ELEMENT
        ){
            HeartByte heart_byte =
                i_cast(TypeByte, datatype_type) - MAX_TYPEBYTE_ELEMENT;

            if (
                Not_Stable_Antiform_Heart(i_cast(Heart, heart_byte)) and
                (pclass != PARAMCLASS_META and pclass != PARAMCLASS_SOFT)
            ){
                panic ("Unstable type unusable unless ^META or soft param");
            }
        }

        *optimized = i_cast(TypesetByte, unwrap datatype_type);
        ++optimized;
        goto spoken_for;
    }

} handle_action: {

    if (Is_Action(OUT)) {
        if (not Is_Word(item)) {
            assert(Any_Sequence(item));
            goto cant_optimize;  // no optimizations for `/word!:` etc. ATM
        }

        Details* details = opt Try_Frame_Details(OUT);
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
        if (dispatcher == NATIVE_CFUNC(TYPECHECKER_ARCHETYPE)) {
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

    assert(Not_Cell_Flag(TOP_SLOT, PARAM_MARKED_SEALED));
    CELL_PARAMETER_PAYLOAD_2_FLAGS(TOP_SLOT) = flags;

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
        Init_Block(temp, EMPTY_ARRAY);
    Decorate_According_To_Parameter(temp, v);

    Push_Lifeguard(temp);
    Mold_Or_Form_Element(mo, temp, form);
    Drop_Lifeguard(temp);

    if (not form) {
        End_Non_Lexical_Mold(mo);
    }

    return TRASH_OUT;
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
    Element* v,
    const Element* param
){
    if (Get_Parameter_Flag(param, REFINEMENT)) {
        require (
          Refinify(v)
        );
    }

    switch (Parameter_Class(param)) {
      case PARAMCLASS_NORMAL:
        break;

      case PARAMCLASS_META:
        Add_Cell_Sigil(v, SIGIL_META);
        break;

      case PARAMCLASS_SOFT: {
        Source *a = Alloc_Singular(STUB_MASK_MANAGED_SOURCE);
        Move_Cell(Stub_Cell(a), v);
        Add_Cell_Sigil(Init_Group(v, a), SIGIL_PIN);
        break; }

      case PARAMCLASS_LITERAL:
        Add_Cell_Sigil(v, SIGIL_PIN);
        break;

      default:
        assert(false);
        DEAD_END;
    }

    if (Get_Parameter_Flag(param, UNBIND_ARG))
        Quote_Cell(v);

    return v;
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

    if (Any_Sigiled_Blank(decoration))
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
//  /decorate: pure native [
//
//  "Based on the parameter type, this gives you e.g. @(foo) or :foo or 'foo"
//
//      return: [element? failure!]
//      decoration [  ; TBD: create DECORATION? type constraint
//          <opt> "give back value as-is"
//          ~[@ ^ $ ~]~ "sigils on SPACE character act as proxy for sigilizing"
//          ~[. : /]~ "words as proxies for leading-blank sequence decoration"
//          ~[^. ^: ^/]~ "meta-word proxies"
//          ~[@. @: @/]~ "pinned-word proxies"
//          ~[$. $: $/]~ "tied-word proxies"
//          quoted! "allows things like [' '' ''' '@ ''^ ''$] etc."
//          parameter! "apply parameter decoration rules as in spec dialect"
//      ]
//      value [plain?]
//  ]
//
DECLARE_NATIVE(DECORATE)
{
    INCLUDE_PARAMS_OF_DECORATE;

    Element* v = Element_ARG(VALUE);

    if (not ARG(DECORATION))
        return COPY_TO_OUT(v);

    Element* decoration = unwrap Element_ARG(DECORATION);

    trap (
      Decorate_Element(decoration, v)
    );

    return COPY_TO_OUT(v);
}


//
//  /redecorate: pure native [
//
//  "Based on the parameter type, this gives you e.g. @(foo) or :foo or 'foo"
//
//      return: [element? failure!]
//      decoration [  ; TBD: create DECORATION? type constraint
//          <opt> "give back value as-is"
//          ~[@ ^ $ ~]~ "sigils on SPACE character act as proxy for sigilizing"
//          ~[. : /]~ "words as proxies for leading-blank sequence decoration"
//          ~[^. ^: ^/]~ "meta-word proxies"
//          ~[@. @: @/]~ "pinned-word proxies"
//          ~[$. $: $/]~ "tied-word proxies"
//          quoted! "allows things like [' '' ''' '@ ''^ ''$] etc."
//          parameter! "apply parameter decoration rules as in spec dialect"
//      ]
//      value [element?]
//  ]
//
DECLARE_NATIVE(REDECORATE)
{
    INCLUDE_PARAMS_OF_REDECORATE;

    Element* v = ARG(VALUE);

    if (not ARG(DECORATION))
        return COPY_TO_OUT(v);

    Element* decoration = unwrap Element_ARG(DECORATION);

    Undecorate_Element(v);
    trap (
      Decorate_Element(decoration, v)
    );

    return COPY_TO_OUT(v);
}


//
//  /decoration-of: pure native [
//
//  "Give back the decoration of a value as per DECORATE"
//
//      return: [
//          ~[@ ^ $ ~]~ "sigils on SPACE character act as proxy for sigils"
//          ~[. : /]~ "words as proxies for leading-blank sequences"
//          quoted! "quotes are in decoration, like [' '' ''' '@ ''^ ''$] etc."
//          <null> "if none of the above decorations are present"
//      ]
//      value [element?]
//  ]
//
DECLARE_NATIVE(DECORATION_OF)
{
    INCLUDE_PARAMS_OF_DECORATION_OF;

    Element* element = ARG(VALUE);

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
//  /undecorate: pure native [
//
//  "Remove decorations (Sigils, Quotes, leading-space sequences) from VALUE"
//
//      return: [plain?]
//      value [element?]
//  ]
//
DECLARE_NATIVE(UNDECORATE)
{
    INCLUDE_PARAMS_OF_UNDECORATE;

    Element* e = ARG(VALUE);

    Undecorate_Element(e);

    return COPY_TO_OUT(e);
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
        if (Is_Null_Signifying_Tweak_Is_Pick(dual))
            goto handle_pick;

        panic (Error_Bad_Poke_Dual_Raw(dual));
    }

    goto handle_poke;

  handle_pick: { /////////////////////////////////////////////////////////////

    switch (opt Word_Id(picker)) {
      case SYM_TEXT: {
        Option(const Strand*) string = Parameter_Strand(param);
        if (not string)
            return LIFT_NULL_OUT_FOR_DUAL_PICK;

        Init_Text(OUT, unwrap string);
        return LIFT_OUT_FOR_DUAL_PICK; }

      case SYM_SPEC: {
        Option(const Source*) spec = Parameter_Spec(param);
        if (not spec)
            return LIFT_NULL_OUT_FOR_DUAL_PICK;

        Init_Block(OUT, unwrap spec);
        return LIFT_OUT_FOR_DUAL_PICK; }

      case SYM_OPTIONAL:
        Init_Logic(OUT, Get_Parameter_Flag(param, REFINEMENT));
        return LIFT_OUT_FOR_DUAL_PICK;

      case SYM_CLASS:
        switch (Parameter_Class(param)) {
          case PARAMCLASS_NORMAL:
            Init_Word(OUT, CANON(NORMAL));
            return LIFT_OUT_FOR_DUAL_PICK;

          case PARAMCLASS_META:
            Init_Word(OUT, CANON(META));
            return LIFT_OUT_FOR_DUAL_PICK;

          case PARAMCLASS_LITERAL:
          case PARAMCLASS_SOFT:
            Init_Word(OUT, CANON(THE));
            return LIFT_OUT_FOR_DUAL_PICK;
          default: assert(false);
        }
        crash (nullptr);

      case SYM_ESCAPABLE:
        Init_Logic(OUT, Parameter_Class(param) == PARAMCLASS_SOFT);
        return LIFT_OUT_FOR_DUAL_PICK;

      /* case SYM_DECORATED: */  // No symbol! Use DECORATE-PARAMETER...

      default:
        break;
    }

    return fail (Error_Bad_Pick_Raw(picker));

} handle_poke: { /////////////////////////////////////////////////////////////

    Known_Stable_Unlift_Cell(dual);

    if (Is_Antiform(dual))
        panic (Error_Bad_Antiform(dual));

    Element* poke = As_Element(dual);

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
        Copy_Cell(OUT, param);
        return LIFT_OUT_FOR_DUAL_WRITEBACK; }  // need Cell pointer update

      default:
        break;
    }

    panic (Error_Bad_Pick_Raw(picker));
}}
