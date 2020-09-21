//
//  File: %sys-path.h
//  Summary: "Definition of Structures for Path Processing"
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
// When a path like `a/(b + c)/d` is evaluated, it moves in steps.  The
// evaluative result of chaining the prior steps is offered as input to
// the next step.  The path evaluator `Eval_Path_Throws` delegates steps to
// type-specific "(P)ath (D)ispatchers" with names like PD_Context,
// PD_Array, etc.
//
// R3-Alpha left several open questions about the handling of paths.  One
// of the trickiest regards the mechanics of how to use a SET-PATH! to
// write data into native structures when more than one path step is
// required.  For instance:
//
//     >> gob/size
//     == 10x20
//
//     >> gob/size/x: 304
//     >> gob/size
//     == 10x304
//
// Because GOB! stores its size as packed bits that are not a full PAIR!,
// the `gob/size` path dispatch can't give back a pointer to a REBVAL* to
// which later writes will update the GOB!.  It can only give back a
// temporary value built from its internal bits.  So workarounds are needed,
// as they are for a similar situation in trying to set values inside of
// C arrays in STRUCT!.
//
// The way the workaround works involves allowing a SET-PATH! to run forward
// and write into a temporary value.  Then in these cases the temporary
// REBVAL is observed and used to write back into the native bits before the
// SET-PATH! evaluation finishes.  This means that it's not currently
// prohibited for the effect of a SET-PATH! to be writing into a temporary.
//
// Further, the `value` slot is writable...even when it is inside of the path
// that is being dispatched:
//
//     >> code: compose [(make set-path! [12-Dec-2012 day]) 1]
//     == [12-Dec-2012/day: 1]
//
//     >> do code
//
//     >> probe code
//     [1-Dec-2012/day: 1]
//
// Ren-C has largely punted on resolving these particular questions in order
// to look at "more interesting" ones.  However, names and functions have
// been updated during investigation of what was being done.
//

// Paths cannot mechanically contain other paths, and allowing anything that
// does not require delimiters would be bad as well (e.g. FILE! or URL!).
// So some types must be ruled out.  There may be more types than in this
// list which *could* work in theory (e.g. DECIMAL! or BINARY!), but they
// would likely cause more trouble than they were worth.
//
// The Try_Init_Any_Path_XXX variants will return nullptr if any of the
// requested path elements are not valid.
//
inline static bool Is_Valid_Path_Element(const RELVAL *v) {
    return IS_BLANK(v)
        or IS_INTEGER(v)
        or IS_WORD(v)
        or IS_TUPLE(v)
        or IS_GROUP(v)
        or IS_BLOCK(v)
        or IS_TEXT(v)
        or IS_TAG(v);
}

#define Try_Init_Any_Path_Arraylike(v,k,a) \
    Try_Init_Any_Path_At_Arraylike_Core((v), (k), (a), 0, nullptr)

#define Try_Init_Path_Arraylike(v,a) \
    Try_Init_Any_Path_Arraylike((v), REB_PATH, (a))

// The `/` path maps to the 2-element array [_ _].  But to save on storage,
// no array is used and paths of this form are always optimized into a single
// cell.  Though the cell reports its VAL_TYPE() as a PATH!, it uses the
// underlying contents of a word cell...which makes it pick up and carry
// bindings.  That allows it to be bound to a function that runs divide.
//
inline static REBVAL *Init_Any_Path_Slash_1(RELVAL *out, enum Reb_Kind kind) {
    assert(ANY_PATH_KIND(kind));
    Init_Word(out, PG_Slash_1_Canon);
    mutable_KIND_BYTE(out) = REB_PATH;
    return SPECIFIC(out);
}

// Ren-C has no REFINEMENT! datatype, so `/foo` is a PATH!, which generalizes
// to where `/foo/bar` is a PATH! as well, etc.
//
// !!! Optimizations are planned to allow single element paths to fit in just
// *one* array cell.  This will make use of the fourth header byte, to
// encode when the type byte is a container for what is inside.  Use of this
// routine to mutate cells into refinements marks places where that will
// be applied.
//
inline static REBVAL *Try_Leading_Blank_Pathify(
    REBVAL *v,
    enum Reb_Kind kind
){
    assert(ANY_PATH_KIND(kind));

    if (IS_BLANK(v))
        return Init_Any_Path_Slash_1(v, kind);

    if (not Is_Valid_Path_Element(v))
        return nullptr;

    REBARR *a = Make_Array(2);  // optimize with pairlike storage!
    Init_Blank(Alloc_Tail_Array(a));
    Move_Value(Alloc_Tail_Array(a), v);
    Freeze_Array_Shallow(a);

    REBVAL *check = Try_Init_Any_Path_Arraylike(v, kind, a);
    assert(check);
    UNUSED(check);

    return v;
}

inline static REBVAL *Refinify(REBVAL *v) {
    bool success = (Try_Leading_Blank_Pathify(v, REB_PATH) != nullptr);
    assert(success);
    return v;
}

// !!! Making paths out of two items is intended to be optimized as well,
// using the "pairing" nodes.
//
inline static REBVAL *Try_Init_Any_Path_Pairlike(
    RELVAL *out,
    enum Reb_Kind kind,
    const REBVAL *v1,
    const REBVAL *v2
){
    if (IS_BLANK(v1))
        return Try_Leading_Blank_Pathify(Move_Value(out, v2), kind);

    REBARR *a = Make_Array(2);
    Move_Value(ARR_AT(a, 0), v1);
    Move_Value(ARR_AT(a, 1), v2);
    TERM_ARRAY_LEN(a, 2);
    return Try_Init_Any_Path_Arraylike(out, kind, Freeze_Array_Shallow(a));

}

// This is a general utility for turning stack values into something that is
// either pathlike or value like.  It is used in COMPOSE of paths, which
// allows things like:
//
//     >> compose (null)/a
//     == a
//
//     >> compose (try null)/a
//     == /a
//
//     >> compose (null)/(null)/(null)
//     ; null
//
// Not all clients will want to be this lenient, but that lack of lenience
// should be done by calling this generic routine and raising an error if
// it's not a PATH!...because the optimizations on special cases are all
// in this code.
//
inline static REBVAL *Try_Pop_Path_Or_Element_Or_Nulled(
    RELVAL *out,  // will be the error-triggering value if nullptr returned
    enum Reb_Kind kind,
    REBDSP dsp_orig
){
    assert(not IN_DATA_STACK_DEBUG(out));

    if (DSP == dsp_orig)
        return Init_Nulled(out);

    if (DSP - 1 == dsp_orig) {  // only one item, use as-is if possible
        if (not Is_Valid_Path_Element(DS_TOP))
            return nullptr;

        Move_Value(out, DS_TOP);
        DS_DROP();

        if (kind != REB_PATH) {  // carry over : or @ decoration (if possible)
            if (
                not IS_WORD(out)
                and not IS_BLOCK(out)
                and not IS_GROUP(out)
                and not IS_BLOCK(out)
                and not IS_TUPLE(out)  // !!! TBD, will support decoration
            ){
                // !!! `out` is reported as the erroring element for why the
                // path is invalid, but this would be valid in a path if we
                // weren't decorating it...rethink how to error on this.
                //
                return nullptr;
            }

            if (kind == REB_SET_PATH)
                Setify(SPECIFIC(out));
            else if (kind == REB_GET_PATH)
                Getify(SPECIFIC(out));
            else if (kind == REB_SYM_PATH)
                Symify(SPECIFIC(out));
        }
        
        return SPECIFIC(out);  // valid path element, but it's standing alon
    }

    if (DSP - dsp_orig == 2) {  // two-element path optimization
        if (not Try_Init_Any_Path_Pairlike(out, kind, DS_TOP - 1, DS_TOP)) {
            DS_DROP_TO(dsp_orig);
            return nullptr;
        }

        DS_DROP_TO(dsp_orig);
        return SPECIFIC(out);
    }

    // !!! Tuples will have optimizations for "all byte-sized integers", which
    // will compact into the cell itself.  This should be reusable by path,
    // so it's yet another step of optimization we'd use here.

    REBARR *a = Pop_Stack_Values(dsp_orig);
    if (not Try_Init_Any_Path_Arraylike(out, kind, Freeze_Array_Shallow(a)))
        return nullptr;

    return SPECIFIC(out);
}


// Note that paths can be initialized with an array, which they will then
// take as immutable...or you can create a `/foo`-style path in a more
// optimized fashion using Refinify()

inline static REBLEN VAL_PATH_LEN(REBCEL(const*) path) {
    assert(ANY_PATH_KIND(CELL_TYPE(path)));
    if (MIRROR_BYTE(path) == REB_WORD)
        return 2;  // simulated 2-blanks path
    REBARR *a = ARR(VAL_NODE(path));
    assert(ARR_LEN(a) >= 2);
    assert(Is_Array_Frozen_Shallow(a));
    return ARR_LEN(a);
}

// !!! This is intended to return either a pairing node or an array node.
// If it is a pairing it will not be terminated.  Either way, it usually
// only represents the non-BLANK! contents of the path...blank contents are
// compressed by means of the second payload slot, which counts the number
// of blanks at the head and the tail.
//
inline static const REBNOD *VAL_PATH_NODE(REBCEL(const*) path) {
    assert(ANY_PATH_KIND(CELL_TYPE(path)));
    assert(MIRROR_BYTE(path) != REB_WORD);

    const REBNOD *n = VAL_NODE(path);
    assert(not (FIRST_BYTE(n) & NODE_BYTEMASK_0x01_CELL));  // !!! not yet...
    return n;
}

// Paths may not always be implemented as arrays, so this mechanism needs to
// be used to read the pointers.  If the value is not in an array, it may
// need to be written to a passed-in storage location.
//
inline static REBCEL(const*) VAL_PATH_AT(
    RELVAL *store,  // return result may or may not point at this cell
    REBCEL(const*) path,
    REBLEN n
){
    assert(store != path);  // cannot be the same

    assert(ANY_PATH_KIND(CELL_TYPE(path)));
    if (MIRROR_BYTE(path) == REB_WORD) {
        assert(VAL_WORD_SYM(path) == SYM__SLASH_1_);
        assert(n < 2);
      #if !defined(NDEBUG)
        Init_Unreadable_Void(store);
      #endif
        return BLANK_VALUE;
    }
    REBARR *a = ARR(VAL_NODE(path));
    assert(ARR_LEN(a) >= 2);
    if (not Is_Array_Frozen_Shallow(a))
        panic (a);
    assert(Is_Array_Frozen_Shallow(a));
    return ARR_AT(a, n);
}

inline static REBSPC *VAL_PATH_SPECIFIER(const RELVAL *path)
{
    assert(ANY_PATH_KIND(CELL_TYPE(path)));
    if (MIRROR_BYTE(path) == REB_WORD) {
        assert(VAL_WORD_SYM(VAL_UNESCAPED(path)) == SYM__SLASH_1_);
        return SPECIFIED;
    }
    return VAL_SPECIFIER(path);
}

inline static bool IS_REFINEMENT_CELL(REBCEL(const*) v) {
    if (CELL_KIND(v) != REB_PATH)
        return false;

    if (ANY_WORD_KIND(MIRROR_BYTE(v)))
        return false;  // all refinements *should* be this form!

/*    if (not (FIRST_BYTE(node) & NODE_BYTEMASK_0x01_CELL))
        return false;  */  // should be only pairings

    REBARR *a = ARR(VAL_NODE(v));
    return IS_BLANK(ARR_AT(a, 0)) and IS_WORD(ARR_AT(a, 1));
}

inline static bool IS_REFINEMENT(const RELVAL *v)
  { return IS_PATH(v) and IS_REFINEMENT_CELL(VAL_UNESCAPED(v)); }

inline static REBSTR *VAL_REFINEMENT_SPELLING(REBCEL(const*) v) {
    assert(IS_REFINEMENT_CELL(v));
    return VAL_WORD_SPELLING(ARR_AT(ARR(VAL_NODE(v)), 1));
}


#define PVS_OPT_SETVAL(pvs) \
    pvs->special

#define PVS_IS_SET_PATH(pvs) \
    (PVS_OPT_SETVAL(pvs) != nullptr)

#define PVS_PICKER(pvs) \
    pvs->param

inline static bool Get_Path_Throws_Core(
    REBVAL *out,
    const RELVAL *any_path,
    REBSPC *specifier
){
    return Eval_Path_Throws_Core(
        out,
        ARR(VAL_PATH_NODE(any_path)),
        Derive_Specifier(specifier, any_path),
        NULL, // not requesting value to set means it's a get
        0 // Name contains Get_Path_Throws() so it shouldn't be neutral
    );
}


inline static void Get_Path_Core(
    REBVAL *out,
    const RELVAL *any_path,
    REBSPC *specifier
){
    assert(ANY_PATH(any_path)); // *could* work on ANY_ARRAY(), actually

    if (Eval_Path_Throws_Core(
        out,
        ARR(VAL_PATH_NODE(any_path)),
        Derive_Specifier(specifier, any_path),
        NULL, // not requesting value to set means it's a get
        EVAL_FLAG_NO_PATH_GROUPS
    )){
        panic (out); // shouldn't be possible... no executions!
    }
}


inline static bool Set_Path_Throws_Core(
    REBVAL *out,
    const RELVAL *any_path,
    REBSPC *specifier,
    const REBVAL *setval
){
    assert(ANY_PATH(any_path)); // *could* work on ANY_ARRAY(), actually

    return Eval_Path_Throws_Core(
        out,
        ARR(VAL_PATH_NODE(any_path)),
        Derive_Specifier(specifier, any_path),
        setval,
        0 // Name contains Set_Path_Throws() so it shouldn't be neutral
    );
}


inline static void Set_Path_Core(  // !!! Appears to be unused.  Unnecessary?
    const RELVAL *any_path,
    REBSPC *specifier,
    const REBVAL *setval
){
    assert(ANY_PATH(any_path)); // *could* work on ANY_ARRAY(), actually

    // If there's no throw, there's no result of setting a path (hence it's
    // not in the interface)
    //
    DECLARE_LOCAL (out);

    REBFLGS flags = EVAL_FLAG_NO_PATH_GROUPS;

    if (Eval_Path_Throws_Core(
        out,
        ARR(VAL_PATH_NODE(any_path)),
        Derive_Specifier(specifier, any_path),
        setval,
        flags
    )){
        panic (out); // shouldn't be possible, no executions!
    }
}
