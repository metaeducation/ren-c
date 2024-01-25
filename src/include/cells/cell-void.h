//
//  File: %cell-void.h
//  Summary: {Non-"Element" for opting out, antiform used for unset variables}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2023 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
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
// VOID is the result of branching constructs that don't take a branch, and if
// code evaluates to void then there will be no `==` in the console (as void
// has no representation).
//
//     >> if false [<d>]
//
//     >> if true [<d>]
//     == <d>
//
// Though void is like an antiform in that it cannot be used as an array
// element, it is not itself considered to be an antiform.  Array operations
// that try to add it will be no-ops instead of errors:
//
//     >> append [a b c] if false [<d>]
//     == [a b c]
//
// While void doesn't have a representation, it has quoted and quasi forms
// that are single characters which can be used as array elements:
//
//     >> append [a b c] quote void
//     == [a b c ']
//
//     >> append [a b c] quasi void
//     == [a b c ~]
//
// These fit pleasingly into the META/UNMETA paradigm, as the quoted form
// evaluates to a plain void, and the quasi form evaluates to an antiform:
//
//     >> meta void
//     == '
//
//     >> '
//
//     >> append [a b c] '
//     == [a b c]
//
//     >> ~
//     == ~  ; anti
//
// The `~` antiform is called TRASH, and is chosen in particular by the system
// to represent variables that have not been assigned, that generate an error
// when accessed by plain WORD!.
//

INLINE bool Is_Void(const Cell* v)
  { return HEART_BYTE(v) == REB_VOID and QUOTE_BYTE(v) == NOQUOTE_1; }

INLINE REBVAL *Init_Void_Untracked(Cell* out, Byte quote_byte) {
    FRESHEN_CELL(out);
    out->header.bits |= (
        NODE_FLAG_NODE | NODE_FLAG_CELL
            | FLAG_HEART_BYTE(REB_VOID) | FLAG_QUOTE_BYTE(quote_byte)
    );

  #ifdef ZERO_UNUSED_CELL_FIELDS
    EXTRA(Any, out).corrupt = CORRUPTZERO;  // not Is_Bindable()
    PAYLOAD(Any, out).first.corrupt = CORRUPTZERO;
    PAYLOAD(Any, out).second.corrupt = CORRUPTZERO;
  #endif

    return cast(REBVAL*, out);
}

#define Init_Void(out) \
    TRACK(Init_Void_Untracked((out), NOQUOTE_1))

#define Init_Quoted_Void(out) \
    TRACK(Init_Void_Untracked((out), ONEQUOTE_3))

INLINE bool Is_Quoted_Void(const Cell* v)
  { return QUOTE_BYTE(v) == ONEQUOTE_3 and HEART_BYTE(v) == REB_VOID; }

#define Init_Quasi_Void(out) \
    TRACK(Init_Void_Untracked((out), QUASIFORM_2))

INLINE bool Is_Quasi_Void(const Cell* v)
  { return QUOTE_BYTE(v) == QUASIFORM_2 and HEART_BYTE(v) == REB_VOID; }

#define Init_Meta_Of_Void(out)       Init_Quoted_Void(out)
#define Is_Meta_Of_Void(v)           Is_Quoted_Void(v)


//=//// '~' ISOTOPE (a.k.a. TRASH) ////////////////////////////////////////=//
//
// Picking antiform void as the contents of unset variables has many benefits
// over choosing something like an `~unset~` or `~trash~` antiforms:
//
//  * Reduces noise when looking at a list of variables to see which are unset
//
//  * We consider variables to be unset and not values, e.g. (unset? 'var).
//    This has less chance for confusion as if it were named ~unset~ people
//    would likely expect `(unset? ~unset~)` to work.
//
//  * Quick way to unset variables, simply `(var: ~)`
//
// While "trash" is a slightly jarring name for ~ antiforms, one doesn't need
// to call it by name to use it.  e.g. return specs can say `return: [~]`
// instead of `return: [trash?]`, and `return ~` instead of `return trash`
//
// The choice of this name (vs. "unset") was meditated on for quite some time,
// and resolved as superior to trying to claim there's such a thing as an
// "unset value".
//

INLINE bool Is_Trash(const Cell* v)
  { return HEART_BYTE(v) == REB_VOID and QUOTE_BYTE(v) == ANTIFORM_0; }

#define Init_Trash(out) \
    TRACK(Init_Void_Untracked((out), ANTIFORM_0))

#define Init_Meta_Of_Trash(out)     Init_Quasi_Void(out)
#define Is_Meta_Of_Trash(v)         Is_Quasi_Void(v)

#define TRASH_CELL \
    cast(const REBVAL*, &PG_Trash_Cell)  // !!! Could we just use Lib(TRASH) ?


//=//// EFFICIENT VOID AND TRASH "FINALIZATION" ///////////////////////////=//
//
// A cell with all its header bits 0 (Is_Fresh(), CELL_MASK_0) is very close
// to being TRASH.  Its HEART_BYTE() is 0 for REB_VOID, and its QUOTE_BYTE()
// is ANTIFORM_0 to say it is an antiform.  However, it can't be a valid cell
// from the API perspective because Detect_Rebol_Pointer() would see the `\0`
// first byte, and that's a legal empty UTF-8 C string.
//
// There is still leverage from the near overlap with fresh cells...because
// it only takes a single masking operation to add NODE_FLAG_NODE and
// NODE_FLAG_CELL to make a valid trash.  This eliminates the need to mask
// out the bits that are CELL_MASK_PERSIST.  And since the quote byte is
// 0, we don't have to mask it out to mask in other levels of void
//
// This trick alone may seem like a micro-optimization, but fresh cells
// are used to help with semantics too.  They can be written to but not read,
// and assist in safety involving accidentally overwriting raised errors.
// Calling Finalize_Trash() and Finalize_Void() also implicitly asserts that
// the cell was previously fresh... which is often an important invariant.
//

STATIC_ASSERT(REB_VOID == 0);  // the optimization depends on this
STATIC_ASSERT(ANTIFORM_0 == 0);  // QUOTE_BYTE() of 0 means it's an antiform

INLINE Value* Finalize_Trash_Untracked(Atom* out) {
    assert(Is_Fresh(out));  // can bitwise OR, need node+cell flags

    assert(HEART_BYTE(out) == 0 and QUOTE_BYTE(out) == 0);

    out->header.bits |= (
        NODE_FLAG_NODE | NODE_FLAG_CELL  // might already be set, might not...
            /* | FLAG_HEART_BYTE(REB_VOID) */  // already 0
            /* | FLAG_QUOTE_BYTE(ANTIFORM_0) */  // already 0
    );

    return cast(Value*, out);
}

#define Finalize_Trash(out) \
    TRACK(Finalize_Trash_Untracked(out))

INLINE Value* Finalize_Void_Untracked(Atom* out) {
    assert(Is_Fresh(out));  // can bitwise OR, need node+cell flags

    assert(HEART_BYTE(out) == 0 and QUOTE_BYTE(out) == 0);

    out->header.bits |= (
        NODE_FLAG_NODE | NODE_FLAG_CELL  // might already be set, might not...
            /* | FLAG_HEART_BYTE(REB_VOID) */  // already 0
            | FLAG_QUOTE_BYTE(NOQUOTE_1)  // mask over top of existing 0
    );

    return cast(Value*, out);
}

#define Finalize_Void(out) \
    TRACK(Finalize_Void_Untracked(out))


//=//// "HEAVY VOIDS" (BLOCK! Antiform Pack with ['] in it) ////////////////=//
//
// This is a way of making it so that branches which evaluate to void can
// carry the void intent, while being in a parameter pack--which is not
// considered a candidate for running ELSE branches:
//
//     >> if false [<a>]
//     ; void (will trigger ELSE)
//
//     >> if true []
//     == ~[']~  ; anti (will trigger THEN, not ELSE)
//
//     >> append [a b c] if false [<a>]
//     == [a b c]
//
//     >> append [a b c] if true []
//     == [a b c]
//
// ("Heavy Nulls" are an analogous concept for NULL.)
//

#define Init_Heavy_Void(out) \
    Init_Pack((out), PG_1_Quoted_Void_Array)

INLINE bool Is_Heavy_Void(const Cell* v) {
    if (not Is_Pack(v))
        return false;
    const Element* tail;
    const Element* at = Cell_Array_At(&tail, v);
    return (tail == at + 1) and Is_Meta_Of_Void(at);
}

INLINE bool Is_Meta_Of_Heavy_Void(const Cell* v) {
    if (not Is_Meta_Of_Pack(v))
        return false;
    const Element* tail;
    const Element* at = Cell_Array_At(&tail, v);
    return (tail == at + 1) and Is_Meta_Of_Void(at);
}
