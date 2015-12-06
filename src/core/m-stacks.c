/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  m-stack.c
**  Summary: data and function call stack implementation
**  Section: memory
**  Notes:
**
***********************************************************************/

#include "sys-core.h"


#define CHUNK_FROM_VALUES(cv) \
    cast(struct Reb_Chunk *, cast(REBYTE*, (cv)) \
        - offsetof(struct Reb_Chunk, values))

#define CHUNKER_FROM_CHUNK(c) \
    cast(struct Reb_Chunker*, \
        cast(REBYTE*, (c)) \
        + (c)->size \
        + (c)->payload_left \
        - sizeof(struct Reb_Chunker) \
    )


//
//  Init_Stacks: C
//
void Init_Stacks(REBCNT size)
{
    // We always keep one chunker around for the first chunk push, and prep
    // one chunk so that the push and drop routines never worry about testing
    // for the empty case.

    TG_Root_Chunker = ALLOC(struct Reb_Chunker);
#if !defined(NDEBUG)
    memset(TG_Root_Chunker, 0xBD, sizeof(struct Reb_Chunker));
#endif
    TG_Root_Chunker->next = NULL;
    TG_Top_Chunk = cast(struct Reb_Chunk*, &TG_Root_Chunker->payload);
    TG_Top_Chunk->prev = NULL;
    TG_Top_Chunk->size = BASE_CHUNK_SIZE; // zero values for initial chunk
    TG_Top_Chunk->payload_left = CS_CHUNKER_PAYLOAD - BASE_CHUNK_SIZE;
    cast(
        struct Reb_Chunk*, cast(REBYTE*, TG_Top_Chunk) + BASE_CHUNK_SIZE
    )->prev = TG_Top_Chunk;
    assert(IS_END(&TG_Top_Chunk->values[0]));
    TG_Head_Chunk = TG_Top_Chunk;

    CS_Top = NULL;
    CS_Running = NULL;

    DS_Series = Make_Array(size);
    Set_Root_Series(TASK_STACK, DS_Series, "data stack"); // uses special GC
}


//
//  Shutdown_Stacks: C
//
void Shutdown_Stacks(void)
{
    assert(TG_Top_Chunk == cast(struct Reb_Chunk*, &TG_Root_Chunker->payload));

    // Because we always keep one chunker of headroom allocated, and the
    // push/drop is not designed to manage the last chunk, we *might* have
    // that next chunk of headroom still allocated.
    //
    if (TG_Root_Chunker->next)
        FREE(struct Reb_Chunker, TG_Root_Chunker->next);

    // OTOH we always have to free the root chunker.
    //
    FREE(struct Reb_Chunker, TG_Root_Chunker);

    assert(!CS_Running);
    assert(!CS_Top);

    assert(DSP == -1);
}


//
//  Pop_Stack_Values: C
// 
// Pop_Stack_Values computed values from the stack into the series
// specified by "into", or if into is NULL then store it as a
// block on top of the stack.  (Also checks to see if into
// is protected, and will trigger a trap if that is the case.)
// 
// Protocol for /INTO is to set the position to the tail.
//
void Pop_Stack_Values(REBVAL *out, REBINT dsp_start, REBOOL into)
{
    REBSER *series;
    REBCNT len = DSP - dsp_start;
    REBVAL *values = BLK_SKIP(DS_Series, dsp_start + 1);

    if (into) {
        assert(ANY_ARRAY(out));
        series = VAL_SERIES(out);

        FAIL_IF_PROTECTED_SERIES(series);

        VAL_INDEX(out) = Insert_Series(
            series, VAL_INDEX(out), cast(REBYTE*, values), len
        );
    }
    else {
        series = Copy_Values_Len_Shallow(values, len);
        Val_Init_Block(out, series);
    }

    DS_DROP_TO(dsp_start);
}


//
//  Expand_Stack: C
// 
// Expand the datastack. Invalidates any references to stack
// values, so code should generally use stack index integers,
// not pointers into the stack.
//
void Expand_Stack(REBCNT amount)
{
    if (SERIES_REST(DS_Series) >= STACK_LIMIT) Trap_Stack_Overflow();
    Extend_Series(DS_Series, amount);
    Debug_Fmt(cs_cast(BOOT_STR(RS_STACK, 0)), DSP, SERIES_REST(DS_Series));
}


//
//  Push_Ended_Trash_Chunk: C
//
// This doesn't necessarily call Alloc_Mem, because chunks are allocated
// sequentially inside of "chunker" blocks, in their ordering on the stack.
// Allocation is only required if we need to step into a new chunk (and even
// then only if we aren't stepping into a chunk that we are reusing from
// a prior expansion).
//
// The "Ended" indicates that there is no need to manually put an end in the
// `num_values` slot.  Chunks are implicitly terminated by their layout,
// because the pointer which indicates the previous chunk on the next chunk
// always has its low bit clear (pointers are not odd on 99% of architectures,
// this is checked by an assertion).
//
REBVAL* Push_Ended_Trash_Chunk(REBCNT num_values) {
    const REBCNT size = BASE_CHUNK_SIZE + num_values * sizeof(REBVAL);

    struct Reb_Chunk *chunk;

    // Establish invariant where 'chunk' points to a location big enough to
    // hold the data (with data's size accounted for in chunk_size).  Note
    // that TG_Top_Chunk is never NULL, due to the initialization leaving
    // one empty chunk at the beginning and manually destroying it on
    // shutdown (this simplifies Push)

    if (
        TG_Top_Chunk->payload_left >= size + sizeof(struct Reb_Chunk *)
    ) {
        //
        // Topmost chunker has space for the chunk *and* a pointer with the
        // END marker bit (e.g. last bit 0).  So advance past the topmost
        // chunk (whose size will depend upon num_values)
        //
        chunk = cast(struct Reb_Chunk*,
            cast(REBYTE*, TG_Top_Chunk) + TG_Top_Chunk->size
        );

        // top's payload_left accounted for previous chunk, account for ours
        //
        chunk->payload_left = TG_Top_Chunk->payload_left - size;
    }
    else {
        //
        // Topmost chunker has insufficient space
        //
        struct Reb_Chunker *chunker = CHUNKER_FROM_CHUNK(TG_Top_Chunk);

        //
        // If not big enough for the chunk (and a next chunk's prev pointer,
        // needed to signal END on the values[]), a new chunk wouldn't be
        // big enough, either!
        //
        // !!! Extend model so that it uses an ordinary ALLOC of memory in
        // cases where no chunk is big enough.
        //
        assert(size + sizeof(struct Reb_Chunk *) <= CS_CHUNKER_PAYLOAD);

        if (chunker->next) {
            //
            // Previously allocated chunker exists already to grow into
            //
            assert(!chunker->next->next);
        }
        else {
            // No previously allocated chunker...we have to allocate it
            //
            chunker->next = ALLOC(struct Reb_Chunker);
            chunker->next->next = NULL;
        }

        assert(size + sizeof(struct Reb_Chunk *) <= CS_CHUNKER_PAYLOAD);

        chunk = cast(struct Reb_Chunk*, &chunker->next->payload);
        chunk->payload_left = CS_CHUNKER_PAYLOAD - size;

        TG_Head_Chunk = chunk;

        // Though we can usually trust a chunk to have its prev set in advance
        // by the chunk before it, a new allocation wouldn't be initialized,
        // so set it manually.
        //
        chunk->prev = TG_Top_Chunk;
    }

    chunk->size = size;

    // Set end marker bit in next element.  We save time by going ahead and
    // setting it to the pointer for this chunk.
    //
    cast(struct Reb_Chunk*, cast(REBYTE*, chunk) + size)->prev = chunk;
    assert(IS_END(&chunk->values[num_values]));

    // chunk->prev is already set, due to the above code running under the
    // prior allocation...
    //
    assert(chunk->prev == TG_Top_Chunk);

    TG_Top_Chunk = chunk;

#if !defined(NDEBUG)
    //
    // In debug builds we make sure we put in GC-unsafe trash in the chunk.
    // This helps make sure that the caller fills in the values before a GC
    // ever actually happens.  (We could set it to UNSET! or something
    // GC-safe, but that might wind up being wasted work if unset is not
    // what the caller was wanting...so leave it to them.)
    {
        REBCNT index;
        for (index = 0; index < num_values; index++)
            SET_TRASH_IF_DEBUG(&chunk->values[index]);
    }
#endif

    assert(CHUNK_FROM_VALUES(&chunk->values[0]) == chunk);
    return &chunk->values[0];
}


//
//  Drop_Chunk: C
//
// Free a call frame.  This only occasionally requires an actual
// call to Free_Mem(), due to allocating call frames sequentially
// in chunks of memory.
//
void Drop_Chunk(REBVAL values[])
{
    struct Reb_Chunk* chunk = TG_Top_Chunk;

    // Passing in `values` is optional, but a good check to make sure you are
    // actually dropping the chunk you think you are.  (On an error condition
    // when dropping chunks to try and restore the top chunk to a previous
    // state, this information isn't available because the call frame data
    // containing the chunk pointer has been longjmp'd past into oblivion.)
    //
    assert(!values || CHUNK_FROM_VALUES(values) == chunk);

    // Drop to the prior top chunk
    TG_Top_Chunk = chunk->prev;

    if (chunk == TG_Head_Chunk) {
        // This chunk sits at the head of a chunker.

        struct Reb_Chunker *chunker = cast(struct Reb_Chunker*,
            cast(REBYTE*, chunk) - sizeof(struct Reb_Chunker*)
        );
        assert(CHUNKER_FROM_CHUNK(chunk) == chunker);
        assert(chunk->payload_left + chunk->size == CS_CHUNKER_PAYLOAD);

        assert(TG_Top_Chunk);
        TG_Head_Chunk = cast(
            struct Reb_Chunk*, &CHUNKER_FROM_CHUNK(TG_Top_Chunk)->payload
        );

        // When we've completely emptied a chunker, we check to see if the
        // chunker after it is still live.  If so, we free it.  But we
        // want to keep *this* just-emptied chunker alive for overflows if we
        // rapidly get another push, to avoid Make_Mem()/Free_Mem() costs.

        if (chunker->next) {
            FREE(struct Reb_Chunker, chunker->next);
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
        chunk->size - sizeof(struct Reb_Chunk*)
    );
    assert(IS_END(cast(REBVAL*, chunk)));
#endif
}


//
//  Push_New_Arglist_For_Call: C
// 
// Allocate the series of REBVALs inspected by a non-frameless function when
// executed (the values behind D_ARG(1), D_REF(2), etc.)  Since the call
// contains the function, it is known how many parameters are needed.
//
// The call frame will be pushed onto the call stack, and hence its fields
// will be seen by the GC and protected.
// 
// However...we do not set the frame as "Running" at the same time we create
// it.  We need to fulfill its arguments in the caller's frame before we
// actually invoke the function, so it's Dispatch_Call that actually moves
// it to the running status.
//
void Push_New_Arglist_For_Call(struct Reb_Call *c) {
    REBVAL *slot;
    REBCNT num_slots; // args and other key/value slots (e.g. func value in 0)

    // Should not already have an arglist.  We zero out the union field for
    // the series, so that's the one we should check.
    //
#if !defined(NDEBUG)
    assert(!c->arglist.array);
#endif

    // `num_vars` is the total number of elements in the series, including the
    // function's "Self" REBVAL in the 0 slot.
    //
    assert(ANY_FUNC(&c->func));
    num_slots = SERIES_LEN(VAL_FUNC_PARAMLIST(&c->func));
    assert(num_slots >= 1);

    // Make REBVALs to hold the arguments.  It will always be at least one
    // variable long, because function frames start with the value of the
    // function in slot 0.
    //
    // We use the chunk stack unless we are making an ordinary user function
    // (what R3-Alpha called a CLOSURE!)  In that case, we make a series.
    // CLOSURE! will wind up managing this series and taking it over.
    //
    // !!! Though it may seem expensive to create this array, it may be that
    // 0, 1, or 2-element arrays will be very cheap to make in the future.
    //
    if (IS_CLOSURE(&c->func)) {
        c->arglist.array = Make_Array(num_slots);
        c->arglist.array->tail = num_slots;
        SET_END(BLK_SKIP(c->arglist.array, num_slots));
        slot = BLK_HEAD(c->arglist.array);
    }
    else {
        // Same as above, but in a raw array vs. a series.  Note that chunks
        // implicitly have an END at the end; no need to put one there.
        //
        c->arglist.chunk = Push_Ended_Trash_Chunk(num_slots);
        slot = &c->arglist.chunk[0];
    }

    // This will be a function or closure frame, and we always have the
    // 0th element set to the value of the function itself.  This allows
    // the single REBSER* to be able to lead us back to access the entire
    // REBVAL worth of information.
    //
    // !!! Review to see if there's a cheap way to put the closure frame
    // here instead of the closure function value, as Do_Closure_Throws()
    // is just going to overwrite this slot.
    //
    *slot = c->func;
    slot++;

    // Make_Call does not fill the args in the frame--that's up to Do_Core
    // and Apply_Block as they go along.  But the frame has to survive
    // Recycle() during arg fulfillment, slots can't be left uninitialized.
    // It is important to set to UNSET for bookkeeping so that refinement
    // scanning knows when it has filled a refinement slot (and hence its
    // args) or not.
    //
    while (--num_slots) {
        SET_UNSET(slot);
        slot++;
    }

    // Write some garbage (that won't crash the GC) into the `cell` slot in
    // the debug build.  `out` and `func` are known to be GC-safe.
    //
    SET_TRASH_SAFE(&c->cell);

    // Even though we can't push this stack frame to be CS_Running yet, it
    // still needs to be considered for GC.  In a recursive DO we can get
    // many pending frames before we come back to actually putting the
    // topmost one in effect.
    //
    c->prior = CS_Top;
    CS_Top = c;
}


//
//  Drop_Call_Arglist: C
// 
// Free a call frame's arglist series.  These are done in a stack, so the
// call being dropped needs to be the last one pushed.
//
// Note that if a `fail` occurs this function will *not* be called, because
// a longjmp will skip the code that would have called it.  The point
// where it longjmps to will not be able to read the stack-allocated Reb_Call,
// because that stack will be done.
//
// Hence there cannot be anything in the Reb_Call structure that would not
// be able to be freed by the trap handlers implicitly (no malloc'd members,
// no cleanup needing imperative code, etc.)
//
void Drop_Call_Arglist(struct Reb_Call* c)
{
    // Drop to the prior top call stack frame
    //
    assert(c == CS_Top);
    CS_Top = c->prior;

    if (IS_CLOSURE(&c->func)) {
        //
        // CLOSURE! should have converted the arglist array to be managed,
        // and it may have been GC'd by this point (Mark_Call_Frame() does
        // not keep the arglist alive if it's not needed).  So it sets it
        // to trash in the debug build, but doesn't in release.
        //
    #if !defined(NDEBUG)
        assert(c->arglist.array == cast(REBSER*, 0xDECAFBAD));
    #endif
    }
    else {
        // For other function types we drop the chunk.  This is not dangerous
        // for natives/etc. because there is no word binding to "leak" and be
        // dereferenced after the call.  But FUNCTION! words have some issues
        // related to this leak.
        //
        // !!! Review if a performant FUNCTION!/CLOSURE! unification exists,
        // to plug this problem with FUNCTION!.
        //
        Drop_Chunk(c->arglist.chunk);
    }

#if !defined(NDEBUG)
    c->arglist.array = NULL;
    c->arg = cast(REBVAL *, 0xDECAFBAD);
#endif
}


#if !defined(NDEBUG)

//
//  DSF_ARG_Debug: C
// 
// Debug-only version of getting a variable out of a call
// frame, which asserts if you use an index that is higher
// than the number of arguments in the frame.
//
REBVAL *DSF_ARG_Debug(struct Reb_Call *call, REBCNT n)
{
    assert(n != 0 && n <= DSF_ARGC(call));
    return &call->arg[n];
}

#endif
