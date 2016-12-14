//
//  File: %sys-stack.h
//  Summary: {Definitions for "Data Stack", "Chunk Stack" and the C stack}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2015 Rebol Open Source Contributors
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
// The data stack and chunk stack are two different data structures for
// temporarily storing REBVALs.  With the data stack, values are pushed one
// at a time...while with the chunk stack, an array of value cells of a given
// length is returned.
//
// A key difference between the two stacks is pointer stability.  Though the
// data stack can accept any number of pushes and then pop the last N pushes
// into a series, each push could potentially change the memory address of
// every other value in the stack.  That's because the data stack is really
// a REBARR series under the hood.  But the chunk stack is a custom structure,
// and guarantees that the address of the values in a chunk will stay stable
// until that chunk is popped.
//
// Another difference is that values on the data stack are implicitly GC safe,
// while clients of the chunk stack needing GC safety must do so manually.
//
// Because of their differences, they are applied to different problems:
//
// A notable usage of the data stack is by REDUCE and COMPOSE.  They use it
// as a buffer for values that are being gathered to be inserted into the
// final array.  It's better to use the data stack as a buffer because it
// means the size of the accumulated result is known before either creating
// a new series or inserting /INTO a target.  This prevents wasting space on
// expansions or resizes and shuffling due to a guessed size.
//
// The chunk stack has an important use as the storage for arguments to
// functions being invoked.  The pointers to these arguments are passed by
// natives through the stack to other routines, which may take arbitrarily
// long to return...and may call code involving many data stack pushes and
// pops.  Argument pointers must be stable, so using the data stack would
// not work.  Also, to efficiently implement argument fulfillment without
// pre-filling the cells, uninitialized memory is allowed in the chunk stack
// across potentical garbage collections.  This means implicit GC protection
// can't be performed, with a subset of valid cells marked by the frame. 
//


//=////////////////////////////////////////////////////////////////////////=//
//
//  DATA STACK
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The data stack (DS_) is for pushing one individual REBVAL at a time.  The
// values can then be popped in a Last-In-First-Out way.  It is also possible
// to mark a stack position, do any number of pushes, and then ask for the
// range of values pushed since the mark to be placed into a REBARR array.
// As long as a value is on the data stack, any series it refers to will be
// protected from being garbage-collected.
//
// The data stack has many applications, and can be used by any piece of the
// system.  But there is a rule that when that piece is finished, it must
// "balance" the stack back to where it was when it was called!  There is
// a check in the main evaluator loop that the stack has been balanced to
// wherever it started by the time a function call ends.  However, it is not
// necessary to balance the stack in the case of calling a `fail`--because
// it will be automatically restored to where it was at the PUSH_TRAP().
//
// To speed pushes and pops to the stack while also making sure that each
// push is tested to see if an expansion is needed, a trick is used.  This
// trick is to grow the stack in blocks, and always maintain that the block
// has an END marker at its point of capacity--and ensure that there are no
// end markers between the DSP and that capacity.  This way, if a push runs
// up against an END it knows to do an expansion.
//

// DSP stands for "(D)ata (S)tack "(P)osition", and is the index of the top
// of the data stack (last valid item in the underlying array)
//
#define DSP \
    DS_Index

// DS_AT accesses value at given stack location
//
#define DS_AT(d) \
    (DS_Movable_Base + (d))

// DS_TOP is the most recently pushed item
//
#define DS_TOP \
    DS_AT(DSP)

#if !defined(NDEBUG)
    #define IN_DATA_STACK_DEBUG(v) \
        IS_VALUE_IN_ARRAY_DEBUG(DS_Array, (v))
#endif

//
// PUSHING
//
// If you push "unsafe" trash to the stack, it has the benefit of costing
// nothing extra in a release build for setting the value (as it is just
// left uninitialized).  But you must make sure that a GC can't run before
// you have put a valid value into the slot you pushed.
//
// If the stack runs out of capacity then it will be expanded by the basis
// defined below.  The number is arbitrary and should be tuned.  Note the
// number of bytes will be sizeof(REBVAL) * STACK_EXPAND_BASIS
//

#define STACK_EXPAND_BASIS 128

#define DS_PUSH_TRASH \
    (++DSP, IS_END(DS_TOP) \
        ? Expand_Data_Stack_May_Fail(STACK_EXPAND_BASIS) \
        : SET_TRASH_IF_DEBUG(DS_TOP))

inline static void DS_PUSH(const REBVAL *v) {
    ASSERT_VALUE_MANAGED(v); // would fail on END marker
    DS_PUSH_TRASH;
    *DS_TOP = *v;
}


//
// POPPING
//
// Since it's known that END markers were never pushed, a pop can just leave
// whatever bits had been previously pushed, dropping only the index.  The
// only END marker will be the one indicating the tail of the stack.  
//

#ifdef NDEBUG
    #define DS_DROP \
        (--DS_Index, NOOP)

    #define DS_DROP_TO(dsp) \
        (DS_Index = dsp, NOOP)
#else
    #define DS_DROP \
        (SET_UNREADABLE_BLANK(DS_TOP), --DS_Index, NOOP)

    inline static void DS_DROP_TO(REBDSP dsp) {
        assert(DSP >= dsp);
        while (DSP != dsp)
            DS_DROP;
    }
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  CHUNK STACK
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Unlike the data stack, values living in the chunk stack are not implicitly
// protected from garbage collection.
//
// Also, unlike the data stack, the chunk stack allows the pushing and popping
// of arbitrary-sized arrays of values which will not be relocated during
// their lifetime.
//
// This is accomplished using a custom "chunked" allocator.  The two structs
// involved are a list of "Chunkers", which internally have a list of
// "Chunks" threaded between them.  The method keeps one spare chunker
// allocated, and only frees a chunker when a full chunker prior has the last
// element popped out of it.  In memory it looks like this:
//
//      [chunker->next
//          (->offset size [value1][value2][value3]...)   // chunk 1
//          (->offset size [value1]...)                   // chunk 2
//          (->offset size [value1][value2]...)           // chunk 3
//          ...remaining payload space in chunker...
//      ]
//
// Since the chunker size is a known constant, it's possible to quickly deduce
// the chunker a chunk lives in from its pointer and the remaining payload
// amount in the chunker.
//

struct Reb_Chunker;

struct Reb_Chunker {
    struct Reb_Chunker *next;
    // use REBUPT for `size` so 'payload' is 64-bit aligned on 32-bit platforms
    REBUPT size;
    REBYTE payload[1];
};

#define BASE_CHUNKER_SIZE (sizeof(struct Reb_Chunker*) + sizeof(REBUPT))
#define CS_CHUNKER_PAYLOAD (4096 - BASE_CHUNKER_SIZE) // 12 bits for offset


struct Reb_Chunk;

struct Reb_Chunk {
    //
    // We start the chunk with a Reb_Header, which has as its `bits`
    // field a REBUPT (unsigned integer size of a pointer).  We are relying
    // on the fact that the high 2 bits of this value is always 0 in order
    // for it to be an implicit END for the value array of the previous chunk.
    //
    // !!! Previously this was used to store arbitrary numbers that ended
    // with the low 2 bits 0, e.g. the size.  New endianness-dependent
    // features restrict this somewhat, so it's really just a free set of
    // flags and byte-sized quantities...currently not used except in this
    // termination role.  But available if needed...
    //
    struct Reb_Header header;

    REBUPT size;

    REBUPT offset;

    // Pointer to the previous chunk.  As the second pointer in this chunk,
    // with the chunk 64-bit aligned to start with, it means the values will
    // be 64-bit aligned on 32-bit platforms.
    //
    struct Reb_Chunk *prev;

    // The `values` is an array whose real size exceeds the struct.  (It is
    // set to a size of one because it cannot be [0] if built with C++.)
    // When the value pointer is given back to the user, the address of
    // this array is how they speak about the chunk itself.
    //
    // See note above about how the next chunk's `size` header serves as
    // an END marker for this array (which may or may not be necessary for
    // the client's purposes, but function arg lists do make use of it)
    //
    // These are actually non-relative values, but REBVAL has a constructor
    // and that interferes with the use of offsetof in the C++ build.  So
    // RELVAL is chosen as a POD-type to use in the structure.
    //
    RELVAL values[1];
};

inline static REBCNT CHUNK_SIZE(struct Reb_Chunk *chunk) {
    return chunk->size;
}

// The offset of this chunk in the memory chunker this chunk lives in
// (its own size has already been subtracted from the amount).
//
inline static REBCNT CHUNK_OFFSET(struct Reb_Chunk *chunk) {
    return chunk->offset;
}

// If we do a sizeof(struct Reb_Chunk) then it includes a value in it that we
// generally don't want for our math, due to C++ "no zero element array" rule
//
#define BASE_CHUNK_SIZE (sizeof(struct Reb_Chunk) - sizeof(REBVAL))

#define CHUNK_FROM_VALUES(v) \
    cast(struct Reb_Chunk *, cast(REBYTE*, (v)) \
        - offsetof(struct Reb_Chunk, values))

#define CHUNK_LEN_FROM_VALUES(v) \
    ((CHUNK_SIZE(CHUNK_FROM_VALUES(v)) - offsetof(struct Reb_Chunk, values)) \
        / sizeof(REBVAL))

inline static struct Reb_Chunker *CHUNKER_FROM_CHUNK(struct Reb_Chunk *c) {
    return cast(
        struct Reb_Chunker*,
        cast(REBYTE*, c)
            - CHUNK_OFFSET(c)
            - offsetof(struct Reb_Chunker, payload)
    );
}


// This doesn't necessarily call Alloc_Mem, because chunks are allocated
// sequentially inside of "chunker" blocks, in their ordering on the stack.
// Allocation is only required if we need to step into a new chunk (and even
// then only if we aren't stepping into a chunk that we are reusing from
// a prior expansion).
//
// The "Ended" indicates that there is no need to manually put an end in the
// `num_values` slot.  Chunks are implicitly terminated by their layout,
// because the low bit of subsequent chunks is set to 0, for data that does
// double-duty as a END marker.
//
inline static REBVAL* Push_Value_Chunk_Of_Length(REBCNT num_values) {
    const REBCNT size = BASE_CHUNK_SIZE + num_values * sizeof(REBVAL);
    assert(size % 4 == 0); // low 2 bits must be zero for terminator trick

    // an extra Reb_Header is placed at the very end of the array to
    // denote a block terminator without a full REBVAL
    //
    const REBCNT size_with_terminator = size + sizeof(struct Reb_Header);

    struct Reb_Chunker *chunker = CHUNKER_FROM_CHUNK(TG_Top_Chunk);

    // Establish invariant where 'chunk' points to a location big enough to
    // hold the data (with data's size accounted for in chunk_size).  Note
    // that TG_Top_Chunk is never NULL, due to the initialization leaving
    // one empty chunk at the beginning and manually destroying it on
    // shutdown (this simplifies Push)
    //
    const REBCNT payload_left =
        chunker->size
            - CHUNK_OFFSET(TG_Top_Chunk)
            - CHUNK_SIZE(TG_Top_Chunk);

    assert(chunker->size >= CS_CHUNKER_PAYLOAD);

    struct Reb_Chunk *chunk;
    if (payload_left >= size_with_terminator) {
        //
        // Topmost chunker has space for the chunk *and* a header to signal
        // that chunk's END marker.  So advance past the topmost chunk (whose
        // size will depend upon num_values)
        //
        chunk = cast(struct Reb_Chunk*,
            cast(REBYTE*, TG_Top_Chunk) + CHUNK_SIZE(TG_Top_Chunk)
        );

        Init_Header_Aliased(&chunk->header, 0);
        chunk->size = size;

        // top's offset accounted for previous chunk, account for ours
        //
        chunk->offset = CHUNK_OFFSET(TG_Top_Chunk) + CHUNK_SIZE(TG_Top_Chunk);
    }
    else { // Topmost chunker has insufficient space
        REBOOL need_alloc = TRUE;
        if (chunker->next) {
            //
            // Previously allocated chunker exists, check if it is big enough
            //
            assert(!chunker->next->next);
            if (chunker->next->size >= size_with_terminator)
                need_alloc = FALSE;
            else
                Free_Mem(chunker->next, chunker->next->size + BASE_CHUNKER_SIZE);
        }
        if (need_alloc) {
            //
            // No previously allocated chunker...we have to allocate it
            //
            const REBCNT payload_size = BASE_CHUNKER_SIZE
                + (size_with_terminator < CS_CHUNKER_PAYLOAD ?
                    CS_CHUNKER_PAYLOAD : (size_with_terminator << 1));
            chunker->next = cast(struct Reb_Chunker*, Alloc_Mem(payload_size));
            chunker->next->next = NULL;
            chunker->next->size = payload_size - BASE_CHUNKER_SIZE;
        }

        assert(chunker->next->size >= size_with_terminator);

        chunk = cast(struct Reb_Chunk*, &chunker->next->payload);

        Init_Header_Aliased(&chunk->header, 0);
        chunk->size = size;
        chunk->offset = 0;
    }


    // Set header in next element to 0, so it can serve as a terminator
    // for the data range of this until it gets instantiated (if ever)
    //
    Init_Header_Aliased(
        &cast(struct Reb_Chunk*, cast(REBYTE*, chunk) + size)->header,
        0
    );
    assert(IS_END(&chunk->values[num_values]));

    chunk->prev = TG_Top_Chunk;

    TG_Top_Chunk = chunk;

#if !defined(NDEBUG)
    //
    // Despite the implicit END marker, the caller is responsible for putting
    // values in the chunk cells.  Noisily enforce this by setting cells to
    // writable trash in the debug build.
    {
        REBCNT index;
        for (index = 0; index < num_values; index++)
            INIT_CELL_IF_DEBUG(&chunk->values[index]);
    }
#endif

    assert(CHUNK_FROM_VALUES(&chunk->values[0]) == chunk);
    return KNOWN(&chunk->values[0]);
}


// Free an array of previously pushed REBVALs.  This only occasionally
// requires an actual call to Free_Mem(), as the chunks are allocated
// sequentially inside containing allocations.
//
inline static void Drop_Chunk_Of_Values(REBVAL *opt_head)
{
    struct Reb_Chunk* chunk = TG_Top_Chunk;

    // Passing in `values` is optional, but a good check to make sure you are
    // actually dropping the chunk you think you are.  (On an error condition
    // when dropping chunks to try and restore the top chunk to a previous
    // state, this information isn't available.)
    //
    assert(!opt_head || CHUNK_FROM_VALUES(opt_head) == chunk);

    // Drop to the prior top chunk
    TG_Top_Chunk = chunk->prev;

    if (CHUNK_OFFSET(chunk) == 0) {
        // This chunk sits at the head of a chunker.

        struct Reb_Chunker *chunker = CHUNKER_FROM_CHUNK(chunk);

        assert(TG_Top_Chunk);

        // When we've completely emptied a chunker, we check to see if the
        // chunker after it is still live.  If so, we free it.  But we
        // want to keep *this* just-emptied chunker alive for overflows if we
        // rapidly get another push, to avoid Make_Mem()/Free_Mem() costs.

        if (chunker->next) {
            Free_Mem(chunker->next, chunker->next->size + BASE_CHUNKER_SIZE);
            chunker->next = NULL;
        }
    }

    // In debug builds we poison the memory for the chunk... but not the `prev`
    // pointer because we expect that to stick around!
    //
#if !defined(NDEBUG)
    memset(
        cast(REBYTE*, chunk) + sizeof(struct Reb_Chunk*),
        0xBD,
        CHUNK_SIZE(chunk) - sizeof(struct Reb_Chunk*)
    );
    assert(IS_END(cast(REBVAL*, chunk)));
#endif
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  C STACK
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Rebol doesn't want to crash in the event of a stack overflow, but would
// like to gracefully trap it and return the user to the console.  While it
// is possible for Rebol to set a limit to how deeply it allows function
// calls in the interpreter to recurse, there's no *portable* way to
// catch a stack overflow in the C code of the interpreter itself.
//
// Hence, by default Rebol will use a non-standard heuristic.  It looks
// at the compiled addresses of local (stack-allocated) variables in a
// function, and decides from their relative pointers if memory is growing
// "up" or "down".  It then extrapolates that C function call frames will
// be laid out consecutively, and the memory difference between a stack
// variable in the topmost stacks can be checked against some limit.
//
// This has nothing to do with guarantees in the C standard, and compilers
// can really put variables at any address they feel like:
//
// http://stackoverflow.com/a/1677482/211160
//
// Additionally, it puts the burden on every recursive or deeply nested
// routine to sprinkle calls to the C_STACK_OVERFLOWING macro somewhere
// in it.  The ideal answer is to make Rebol itself corral an interpreted
// script such that it can't cause the C code to stack overflow.  Lacking
// that ideal this technique could break, so build configurations should
// be able to turn it off if needed.
//
// In the meantime, C_STACK_OVERFLOWING is a macro which takes the
// address of some variable local to the currently executed function.
// Note that because the limit is noticed before the C stack has *actually*
// overflowed, you still have a bit of stack room to do the cleanup and
// raise an error trap.  (You need to take care of any unmanaged series
// allocations, etc).  So cleaning up that state should be doable without
// making deep function calls.
//
// !!! Future approaches should look into use of Windows stack exceptions
// or libsigsegv:
//
// http://stackoverflow.com/questions/5013806/
//

#ifdef OS_STACK_GROWS_UP
    #define C_STACK_OVERFLOWING(address_of_local_var) \
        (cast(REBUPT, address_of_local_var) >= Stack_Limit)
#else
    #define C_STACK_OVERFLOWING(address_of_local_var) \
        (cast(REBUPT, address_of_local_var) <= Stack_Limit)
#endif

#define STACK_BOUNDS (4*1024*1000) // note: need a better way to set it !!
// Also: made somewhat smaller than linker setting to allow trapping it

#define Trap_Stack_Overflow() \
    fail (VAL_CONTEXT(TASK_STACK_ERROR));
