//
//  File: %sys-roots.h
//  Summary: {Definitions for allocating Value* API handles}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// API REBVALs live in singular arrays (which fit inside a Stub node, that
// is the size of 2 cells).  But they aren't kept alive by references from
// other values, like the way that an Array used by a BLOCK! is kept alive.
// They are kept alive by being roots (currently implemented with a flag
// NODE_FLAG_ROOT, but it could also mean living in a distinct pool from
// other series nodes).
//
// The API value content is in the single cell, with LINK().owner holding
// a REBCTX* of the FRAME! that controls its lifetime, or EMPTY_ARRAY.  This
// link field exists in the pointer immediately prior to the Value*, which
// means it can be sniffed as a Node* and distinguished from handles that
// were given back with rebMalloc(), so routines can discern them.
//
// MISC() is currently unused, but could serve as a reference count or other
// purpose.  It's not particularly necessary to have API handles use Stub
// nodes--though the 2*sizeof(Cell) provides some optimality, and it
// means that Stub nodes can be recycled for more purposes.  But it would
// potentially be better to have them in their own pools, because being
// roots could be discovered without a "pre-pass" in the GC.
//

// What distinguishes an API value is that it has both the NODE_FLAG_CELL and
// NODE_FLAG_ROOT bits set.
//
INLINE bool Is_Api_Value(const Cell* v) {
    assert(v->header.bits & NODE_FLAG_CELL);
    return did (v->header.bits & NODE_FLAG_ROOT);
}

// !!! The return cell from this allocation is a trash cell which has had some
// additional bits set.  This means it is not "canonized" trash that can be
// detected as distinct from UTF-8 strings, so don't call IS_TRASH_DEBUG() or
// Detect_Rebol_Pointer() on it until it has been further initialized.
//
// Ren-C manages by default.
//
INLINE Value* Alloc_Value(void)
{
    Array* a = Alloc_Singular(NODE_FLAG_ROOT | NODE_FLAG_MANAGED);

    // Giving the cell itself NODE_FLAG_ROOT lets a Value* be discerned as
    // either an API handle or not.  The flag is not copied by Copy_Cell().
    //
    Value* v = KNOWN(ARR_SINGLE(a));
    v->header.bits |= NODE_FLAG_ROOT; // it's trash (can't use SET_VAL_FLAGS)

    LINK(a).owner = Context_For_Level_May_Manage(TOP_LEVEL);
    return v;
}

INLINE void Free_Value(Value* v)
{
    assert(Is_Api_Value(v));

    Array* a = Singular_From_Cell(v);
    Poison_Cell(ARR_SINGLE(a));
    GC_Kill_Series(a);
}


// "Instructions" are singular arrays; they are intended to be used directly
// with a variadic API call, and will be freed automatically by an enumeration
// to the va_end() point--whether there is an error, throw, or completion.
//
// They are not GC managed, in order to avoid taxing the garbage collector
// (and tripping assert mechanisms).  So they can leak if used incorrectly.
//
// Instructions should be returned as a const void *, in order to discourage
// using these anywhere besides as arguments to a variadic API like rebValue().
//
INLINE Array* Alloc_Instruction(void) {
    Series* s = Alloc_Series_Node(
        SERIES_FLAG_FIXED_SIZE // not tracked as stray manual, but unmanaged
    );
    s->info = Endlike_Header(
        FLAG_WIDE_BYTE_OR_0(0) // signals array, also implicit terminator
            | FLAG_LEN_BYTE_OR_255(1) // signals singular
            | SERIES_INFO_API_INSTRUCTION
            | SERIES_INFO_API_RELEASE
    );
    SER_CELL(s)->header.bits =
        CELL_MASK_ERASE_END | NODE_FLAG_ROOT;
    TRACK_CELL_IF_DEBUG(SER_CELL(s), "<<instruction>>", 0);
    return ARR(s);
}

INLINE void Free_Instruction(Array* instruction) {
    assert(WIDE_BYTE_OR_0(instruction) == 0);
    Poison_Cell(ARR_SINGLE(instruction));
    Free_Pooled(SER_POOL, instruction);
}


// If you're going to just fail() anyway, then loose API handles are safe to
// GC.  It's mildly inefficient to do so compared to generating a local cell:
//
//      DECLARE_VALUE (specific);
//      Derelativize(specific, cell, specifier);
//      fail (Error_Something(specific));
//
// But assuming errors don't happen that often, it's cleaner to have one call.
//
INLINE Value* rebSpecific(const Cell* v, Specifier* specifier)
    { return Derelativize(Alloc_Value(), v, specifier);}
