//
//  File: %sys-stack.h
//  Summary: {Definitions for "Data Stack" and the C stack}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Ren-C Open Source Contributors
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
// The data stack (DS_) is for pushing one individual Cell at a time.  The
// values can then be popped in a Last-In-First-Out way.  It is also possible
// to mark a stack position, do any number of pushes, and then ask for the
// range of values pushed since the mark to be placed into a newly-made Array.
// As long as a value is on the data stack, any series it refers to will be
// protected from being garbage-collected.
//
// A notable usage of the data stack is by REDUCE and COMPOSE.  They use it
// to gather values prior to their insertion into a final array.  It's better
// for many clients to use the data stack as a common preallocated working
// space.  This way the size of the accumulated result is known, preventing
// wasting space on expansions or resizes and shuffling due to a guessed size.
//
// The data stack has many applications, and can be used by any piece of the
// system.  But there is a rule that when that piece is finished, it must
// "balance" the stack back to where it was when it was called!  There is
// a check in the main evaluator loop that the stack has been balanced to
// wherever it started by the time a function call ends.  It's not necessary
// necessary to balance the stack in the case of calling a `fail`--because
// it is restored to where it was by the mechanics of RESCUE_SCOPE.
//
// To speed pushes and pops to the stack while also making sure that each
// push is tested to see if an expansion is needed, a trick is used.  This
// trick is to grow the stack in blocks, and always maintain that the block
// has an END marker at its point of capacity--and ensure that there are no
// end markers between the TOP_INDEX and that capacity.  This way, if a push
// runs up against an END it knows to do an expansion.
//
//=// NOTES ///////////////////////////////////////////////////////////////=//
//
// * Do not store the result of a PUSH() directly into a Value* variable.
//   Instead, use the StackValue(*) type, which makes sure that you don't try
//   to hold a pointer into the stack across another push or an evaluation.
//
// * The data stack is limited in size, and this means code that uses it may
//   break down when working on larger cases:
//
//   https://github.com/metaeducation/ren-c/issues/679
//
// * Although R3-Alpha used the data stack for pushing function arguments,
//   the arguments were frequently passed around by pointer (vs. using a
//   StackIndex position).  This was bad since the data stack could relocate
//   its contents due to growth.  This has completely changed in Ren-C, with
//   memory-pooled levels and stacklessness (see %c-trampoline.c)
//

// The result of PUSH() and TOP is not Value*, but StackValue(*).  In an
// unchecked build this is just a Value*, but with DEBUG_EXTANT_STACK_POINTERS
// it becomes a checked C++ wrapper class...which keeps track of how many
// such stack values are extant.  If the number is not zero, then you will
// get an assert if you try to PUSH() or DROP(), as well as if you
// try to run any evaluations.
//
// NOTE: Due to the interactions of longjmp() with crossing C++ destructors,
// using this debug setting is technically undefined behavior if a fail()
// occurs while a stack value is outstanding.  However, we just assume the
// destructor is not called in this case...and the fail mechanism sets the
// outstanding count to zero.
//
#if ! DEBUG_EXTANT_STACK_POINTERS

    #define Assert_No_DataStack_Pointers_Extant()

#else

    // Concession is made when the GC is disabled (e.g. during a PROBE()) to
    // not report the problem unless it would cause a crash, e.g. an actual
    // case of pushing across a stack expansion.
    //
    #define Assert_No_DataStack_Pointers_Extant() \
        do { if (g_ds.num_refs_extant != 0) { \
            if (not g_gc.disabled or g_ds.movable_top == g_ds.movable_tail) \
                assert(!"PUSH() while StackValue(*) pointers are extant"); \
        } } while (0)

    struct StackValuePointer {
        Value* p;

      public:
        StackValuePointer () : p (nullptr) {}
        StackValuePointer (Value* v) : p (v) {
            if (p != nullptr)
                ++g_ds.num_refs_extant;
        }
        StackValuePointer (const StackValuePointer &stk) : p (stk.p) {
            if (p != nullptr)
                ++g_ds.num_refs_extant;
        }
        ~StackValuePointer() {
            if (p != nullptr)
                --g_ds.num_refs_extant;
        }

        StackValuePointer& operator=(const StackValuePointer& other) {
            if (p != nullptr)
                --g_ds.num_refs_extant;
            p = other.p;
            if (p != nullptr)
                ++g_ds.num_refs_extant;
            return *this;
        }

        operator Value* () const { return p; }
        operator Sink(Value*) () const { return p; }
        operator Sink(const Value*) () const { return p; }
        operator Sink(Element*) () const { return p; }
        operator Sink(const Element*) () const { return p; }

        explicit operator Byte* () const { return cast(Byte*, p); }

        Value* operator->() { return p; }

        bool operator==(const StackValuePointer &other)
            { return this->p == other.p; }
        bool operator!=(const StackValuePointer &other)
            { return this->p != other.p; }
        bool operator<(const StackValuePointer &other)
            { return this->p < other.p; }
        bool operator<=(const StackValuePointer &other)
            { return this->p <= other.p; }
       bool operator>(const StackValuePointer &other)
            { return this->p > other.p; }
        bool operator>=(const StackValuePointer &other)
            { return this->p >= other.p; }

        StackValuePointer operator+(ptrdiff_t diff)
            { return this->p + diff; }
        StackValuePointer& operator+=(ptrdiff_t diff)
            { this->p += diff; return *this; }
        StackValuePointer operator-(ptrdiff_t diff)
            { return this->p - diff; }
        StackValuePointer& operator-=(ptrdiff_t diff)
            { this->p -= diff; return *this; }

        StackValuePointer& operator--()  // prefix decrement
            { --this->p; return *this; }
        StackValuePointer operator--(int)  // postfix decrement
        {
           StackValuePointer temp = *this;
           --*this;
           return temp;
        }

        StackValuePointer& operator++()  // prefix increment
            { ++this->p; return *this; }
        StackValuePointer operator++(int)  // postfix increment
        {
           StackValuePointer temp = *this;
           ++*this;
           return temp;
        }
    };

    template<>
    struct c_cast_helper<Byte*, StackValue(*) const&> {
        typedef Byte* type;
    };

    template<typename T>
    struct cast_helper<StackValuePointer,T>
      { static T convert(StackValuePointer v) { return (T)(v.p);} };
#endif

#define TOP_INDEX \
    cast(StackIndex, g_ds.index)  // cast helps stop ++TOP_INDEX, etc.

// TOP is the most recently pushed item.
//
#define TOP \
    cast(StackValue(*), cast(Value*, g_ds.movable_top))

#define atom_TOP \
    cast(Atom*, g_ds.movable_top)  // only legal in narrow cases


// 1. Use the fact that the data stack is always dynamic to avoid having to
//    check if it is or not.
//
// 2. Although the stack can only hold fully specified values, this can be
//    used to access slots that have been PUSH()'d but not fulfilled yet.
//    So no validation besides writability can be done here.  (Which may be
//    wasteful, and just letting the caller do it could make more sense.)
//
// 3. Access beyond the end of the stack is allowed, but only to the direct
//    position after top.  This is used by things like Pop_Stack() which want
//    to know the address after the content.
//
INLINE StackValue(*) Data_Stack_At(StackIndex i) {
    Value* at = cast(Value*, g_ds.array->content.dynamic.data) + i;  // [1]

    if (i == 0) {
        assert(Is_Cell_Poisoned(at));
    }
    else if (i < TOP_INDEX + 1) {  // in the range of PUSH()'d cells
        if (not Is_Cell_Erased(at))
            Assert_Cell_Readable(at);
    }
    else {
        assert(i == TOP_INDEX + 1);  // allow getting tail's address [3]

      #if DEBUG_POISON_DROPPED_STACK_CELLS
        assert(Is_Cell_Poisoned(at));
      #endif
    }
    return at;
}

#if !defined(NDEBUG)
    #define IN_DATA_STACK_DEBUG(v) \
        IS_VALUE_IN_ARRAY_DEBUG(g_ds.array, (v))
#endif

//
// PUSHING
//
// If the stack runs out of capacity then it will be expanded by the basis
// defined below.  The number is arbitrary and should be tuned.  Note the
// number of bytes will be sizeof(Cell) * STACK_EXPAND_BASIS
//

#define STACK_EXPAND_BASIS 128

// Note: g_ds.movable_top is just TOP, but accessing TOP asserts on ENDs
//
INLINE StackValue(*) PUSH(void) {
    Assert_No_DataStack_Pointers_Extant();

    ++g_ds.index;
    ++g_ds.movable_top;
    if (g_ds.movable_top == g_ds.movable_tail)
        Expand_Data_Stack_May_Fail(STACK_EXPAND_BASIS);

  #if DEBUG_POISON_DROPPED_STACK_CELLS
    assert(Is_Cell_Poisoned(g_ds.movable_top));
  #endif

    Erase_Cell(g_ds.movable_top);
    return cast(Value*, g_ds.movable_top);
}

#define atom_PUSH() cast(Atom*, PUSH())


//
// POPPING
//
// Each POP resets the cell, to reclaim its resources and make it ready to
// use with an Init_Xxx() routine on the next push.
//

INLINE void DROP(void) {
    Assert_No_DataStack_Pointers_Extant();

  #if DEBUG_POISON_DROPPED_STACK_CELLS
    Poison_Cell(g_ds.movable_top);
  #endif

    --g_ds.index;
    --g_ds.movable_top;
}

INLINE void Drop_Data_Stack_To(StackIndex i) {
    Assert_No_DataStack_Pointers_Extant();

    assert(TOP_INDEX >= i);
    while (TOP_INDEX != i)
        DROP();
}

// If Pop_Stack_Values_Core is used ARRAY_HAS_FILE_LINE, it means the system
// will try to capture the file and line number associated with the current
// level into the generated array.  But if there are other flags--like
// ARRAY_FLAG_IS_DETAILS or ARRAY_FLAG_IS_VARLIST--you don't want to do
// this, because the ->link and ->misc fields have other uses.
//
#define Pop_Stack_Values(base) \
    Pop_Stack_Values_Core((base), ARRAY_MASK_HAS_FILE_LINE)

#define Pop_Stack_Values_Core(base,flags) \
    Pop_Stack_Values_Core_Masked((base), (flags), CELL_MASK_COPY)

#define Pop_Stack_Values_Core_Keep_Notes(base,flags) \
    Pop_Stack_Values_Core_Masked((base), (flags), \
        CELL_MASK_COPY_KEEP_NOTES)


// Since stack overflows are memory-related errors, don't try to do any
// error allocations...just use an already made error.
//
#define Fail_Stack_Overflow() \
    fail (Cell_Varlist(Root_Stackoverflow_Error));
