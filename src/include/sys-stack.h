//
// Rebol 3 Language Interpreter and Run-time Environment
// "Ren-C" branch @ https://github.com/metaeducation/ren-c
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
//  Summary: Definitions for "Data Stack", "Chunk Stack" and the C stack
//  File: %sys-stack.h
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The data stack and chunk stack are two different data structures which
// are optimized for temporarily storing REBVALs and protecting them from
// garbage collection.  With the data stack, values are pushed one at a
// time...while with the chunk stack, an array of value cells of a given
// length is returned.
//
// A key difference between the two stacks is pointer stability.  Though the
// data stack can accept any number of pushes and then pop the last N pushes
// into a series, each push could potentially change the memory address of
// every value in the stack.  This is because the data stack uses a REBARR
// series as its implementation.  The chunk stack guarantees that the address
// of the values in a chunk will stay stable over the course of its lifetime.
//
// Because of their differences, they are applied to different problems:
//
// A notable usage of the data stack is by REDUCE and COMPOSE.  They use it
// as a buffer for values that are being gathered to be inserted into the
// final array.  It's better to use the data stack as a buffer because it
// means the precise size of the result can be known before either creating
// a new series or inserting /INTO a target.  This prevents wasting space on
// expansions or resizes and shuffling due to a guessed size.
//
// The chunk stack has an important use as the storage for arguments to
// functions being invoked.  The pointers to these arguments are passed by
// natives through the stack to other routines, which may take arbitrarily
// long to return...and may call code involving many pushes and pops.  These
// pointers must be stable, so using the data stack would not work.
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

// A standard integer is currently used to represent the data stack pointer.
// `unsigned int` instead of a `REBCNT` in order to leverage the native
// performance of the integer type unconstrained by bit size, as data stack
// pointers are not stored in REBVALs or similar, and performance in comparing
// and manipulation is more important than hte size.
//
// Note that a value of 0 indicates an empty stack; the [0] entry is made to
// be alerting trash to trap invalid reads or writes of empty stacks.
//
typedef unsigned int REBDSP;

// (D)ata (S)tack "(P)ointer" is an integer index into Rebol's data stack
//
#define DSP DS_Index

// Access value at given stack location
//
#define DS_AT(d) (DS_Movable_Base + (d))

// Most recently pushed item
//
#define DS_TOP DS_AT(DS_Index)

#if !defined(NDEBUG)
    #define IN_DATA_STACK(p) \
        (ARR_LEN(DS_Array) != 0 && (p) >= DS_AT(0) && (p) <= DS_TOP)
#endif

//
// PUSHING: Note the DS_PUSH macros inherit the property of SET_XXX that
// they use their parameters multiple times.  Don't use with the result of
// a function call because that function could be called multiple times.
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

#define STACK_EXPAND_BASIS 100

#define DS_PUSH_TRASH \
    (++DSP, IS_END(DS_TOP) \
        ? Expand_Data_Stack_May_Fail(100) \
        : SET_TRASH_IF_DEBUG(DS_TOP))

#define DS_PUSH_TRASH_SAFE \
    (DS_PUSH_TRASH, SET_TRASH_SAFE(DS_TOP), NOOP)

#define DS_PUSH(v) \
    (ASSERT_VALUE_MANAGED(v), DS_PUSH_TRASH, *DS_TOP = *(v), NOOP)

#define DS_PUSH_UNSET \
    (DS_PUSH_TRASH, SET_UNSET(DS_TOP), NOOP)

#define DS_PUSH_NONE \
    (DS_PUSH_TRASH, SET_NONE(DS_TOP), NOOP)

#define DS_PUSH_TRUE \
    (DS_PUSH_TRASH, SET_TRUE(DS_TOP), NOOP)

#define DS_PUSH_INTEGER(n) \
    (DS_PUSH_TRASH, SET_INTEGER(DS_TOP, (n)), NOOP)

#define DS_PUSH_DECIMAL(n) \
    (DS_PUSH_TRASH, SET_DECIMAL(DS_TOP, (n)), NOOP)

// POPPING AND "DROPPING"

#ifdef NDEBUG
    #define DS_DROP (--DS_Index, NOOP)
#else
    #define DS_DROP \
        (SET_TRASH_SAFE(DS_TOP), --DS_Index, NOOP)
#endif

#define DS_POP_INTO(v) \
    do { \
        assert(!IS_TRASH_DEBUG(DS_TOP) || VAL_TRASH_SAFE(DS_TOP)); \
        *(v) = *DS_TOP; \
        DS_DROP; \
    } while (0)

#ifdef NDEBUG
    #define DS_DROP_TO(dsp) \
        (DS_Index = dsp, NOOP)
#else
    #define DS_DROP_TO(dsp) \
        do { \
            assert(DSP >= (dsp)); \
            while (DSP != (dsp)) {DS_DROP;} \
        } while (0)
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  CHUNK STACK
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Like the data stack, the values living in the chunk stack are protected
// from garbage collection.
//
// Unlike the data stack, the chunk stack allows for the pushing and popping
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
//          (->payload_left size [value1][value2][value3]...)   // chunk 1
//          (->payload_left size [value1]...)                   // chunk 2
//          (->payload_left size [value1][value2]...)           // chunk 3
//          ...remaining payload space in chunker...
//      ]
//
// Since the chunker size is a known constant, it's possible to quickly deduce
// the chunker a chunk lives in from its pointer and the remaining payload
// amount in the chunker.
//

struct Reb_Chunker;

#define CS_CHUNKER_PAYLOAD (2048 - sizeof(struct Reb_Chunker*))

struct Reb_Chunker {
    struct Reb_Chunker *next;
    REBYTE payload[CS_CHUNKER_PAYLOAD];
};

struct Reb_Chunk;

struct Reb_Chunk {
    //
    // We start the chunk with a Reb_Value_Header, which has as its `bits`
    // field a REBUPT (unsigned integer size of a pointer).  We are relying
    // on the fact that the low 2 bits of this value is always 0 in order
    // for it to be an implicit END for the value array of the previous chunk.
    //
    // (REBVALs are multiples of 4 bytes in size on all platforms Rebol
    // will run on, hence the low two bits of a byte size of N REBVALs will
    // always have the two lowest bits clear.)
    //
    struct Reb_Value_Header size;

    // How many bytes are left in the memory chunker this chunk lives in
    // (its own size has already been subtracted from the amount)
    //
    REBUPT payload_left;

#if defined(__LP64__) || defined(__LLP64__)
    //
    // !!! Sizes above are wasteful compared to theory!  Both the size and
    // payload_left could fit into a single REBUPT, so this is wasting a
    // pointer...saving only 2 pointers per chunk instead of 3 over a full
    // REBVAL termination.
    //
    // However it would be easy enough to get the 3 by masking the REBUPT's
    // high and low portions on 64-bit, and using a separate REBCNT field
    // on 32-bit.  This would complicate the code for now, and a previous
    // field test of a riskier technique showed it to not work on some
    // machines.  So this is a test to see if this follows the rules.
    //
#endif

    // If this serves as the backing memory for a context's stackvars then
    // when the data goes away it is necessary to mark that context as
    // not having its memory any more.  This cannot be managed purely by the
    // client, because a fail() can longjmp...and the chunk stack needs enough
    // information stored to find that series to mark.
    //
    REBCTX *opt_context;

    // Pointer to the previous chunk.
    //
    struct Reb_Chunk *prev;

    // The `values` is an array whose real size exceeds the struct.  (It is
    // set to a size of one because it cannot be [0] if the sources wind
    // up being built as C++.)  When the value pointer is given back to the
    // user, this is how they speak about the chunk itself.
    //
    // See note above about how the next chunk's `prev` pointer serves as
    // an END marker for this array (which may or may not be necessary for
    // the client's purposes, but function arg lists do make use of it)
    //
    REBVAL values[1];
};

// If we do a sizeof(struct Reb_Chunk) then it includes a value in it that we
// generally don't want for our math, due to C++ "no zero element array" rule
//
#define BASE_CHUNK_SIZE (sizeof(struct Reb_Chunk) - sizeof(REBVAL))

#define CHUNK_FROM_VALUES(v) \
    cast(struct Reb_Chunk *, cast(REBYTE*, (v)) \
        - offsetof(struct Reb_Chunk, values))

#define CHUNK_LEN_FROM_VALUES(v) \
    ((CHUNK_FROM_VALUES(v)->size.bits - offsetof(struct Reb_Chunk, values)) \
        / sizeof(REBVAL))


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
//     http://stackoverflow.com/a/1677482/211160
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
