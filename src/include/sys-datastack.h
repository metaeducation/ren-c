//
//  file: %sys-datastack.h
//  summary: -[Definitions for the "Data Stack"]-
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// The data stack is for pushing one individual Cell at a time.  These cells
// can then be popped in a Last-In-First-Out way.  It is also possible to mark
// a stack position as a "Base", do any number of pushes, and then ask for the
// range of values pushed since the mark to be placed into a newly-made Array.
// As long as a cell is on the data stack, any payload it refers to will be
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
// necessary to balance the stack in the case of calling a `panic`--because
// it is restored to where it was by the mechanics of RECOVER_SCOPE.
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
// * Do not store the result of a PUSH() directly into a Stable* variable.
//   Instead, use the OnStack(Stable*) type, which makes sure that you don't try
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

// The result of PUSH() and TOP_STABLE is not Stable*, but OnStack(Stable*).  In an
// unchecked build this is just a Stable*, but with DEBUG_EXTANT_STACK_POINTERS
// it becomes a checked C++ wrapper class...which keeps track of how many
// such stack values are extant.  If the number is not zero, then you will
// get an assert if you try to PUSH() or DROP(), as well as if you
// try to run any evaluations.
//
// NOTE: Due to the interactions of longjmp() with crossing C++ destructors,
// using this debug setting is technically undefined behavior if a panic()
// occurs while a stack value is outstanding.  However, we just assume the
// destructor is not called in this case...and the panic mechanism sets the
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
                assert(!"PUSH() while OnStack(Cell*) pointers are extant"); \
        } } while (0)

    template<typename TP>
    struct OnStackPointer {
        using T = typename std::remove_pointer<TP>::type;

        static_assert(std::is_base_of<Cell, T>::value,
            "OnStack(T) must be used with a Cell-derived type");

        NEEDFUL_DECLARE_WRAPPED_FIELD (TP, p);

        OnStackPointer () : p (nullptr) {}
        OnStackPointer (T* p) : p (p) {
            if (p != nullptr)
                ++g_ds.num_refs_extant;
        }
        OnStackPointer (const OnStackPointer &stk) : p (stk.p) {
            if (p != nullptr)
                ++g_ds.num_refs_extant;
        }
        ~OnStackPointer() {
            if (p != nullptr)
                --g_ds.num_refs_extant;
        }

        OnStackPointer& operator=(const OnStackPointer& other) {
            if (p != nullptr)
                --g_ds.num_refs_extant;
            p = other.p;
            if (p != nullptr)
                ++g_ds.num_refs_extant;
            return *this;
        }

        template<
            typename U,
            typename std::enable_if<
                std::is_convertible<T*, U>::value
            >::type* = nullptr
        >
        operator U () const { return p; }

        template<typename U>
        explicit operator U*() const { return u_cast(U*, p); }

        T* operator->() const { return p; }

        bool operator==(const OnStackPointer &other)
            { return this->p == other.p; }
        bool operator!=(const OnStackPointer &other)
            { return this->p != other.p; }
        bool operator<(const OnStackPointer &other)
            { return this->p < other.p; }
        bool operator<=(const OnStackPointer &other)
            { return this->p <= other.p; }
       bool operator>(const OnStackPointer &other)
            { return this->p > other.p; }
        bool operator>=(const OnStackPointer &other)
            { return this->p >= other.p; }

        OnStackPointer operator+(ptrdiff_t diff)
            { return this->p + diff; }
        OnStackPointer& operator+=(ptrdiff_t diff)
            { this->p += diff; return *this; }
        OnStackPointer operator-(ptrdiff_t diff)
            { return this->p - diff; }
        OnStackPointer& operator-=(ptrdiff_t diff)
            { this->p -= diff; return *this; }

        OnStackPointer& operator--()  // prefix decrement
            { --this->p; return *this; }
        OnStackPointer operator--(int)  // postfix decrement
        {
           OnStackPointer temp = *this;
           --*this;
           return temp;
        }

        OnStackPointer& operator++()  // prefix increment
            { ++this->p; return *this; }
        OnStackPointer operator++(int)  // postfix increment
        {
           OnStackPointer temp = *this;
           ++*this;
           return temp;
        }
    };
#endif

#define TOP_INDEX \
    cast(StackIndex, g_ds.index)  // cast helps stop ++TOP_INDEX, etc.

// TOP is the most recently pushed item.
//
#define TOP_STABLE \
    cast(OnStack(Stable*), Known_Stable(g_ds.movable_top))

#define TOP_ELEMENT \
    cast(OnStack(Element*), Known_Element(g_ds.movable_top))

#define TOP \
    cast(OnStack(Value*), g_ds.movable_top)  // assume valid


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
INLINE Cell* Data_Stack_Cell_At(StackIndex i) {
    Cell* at = Flex_Head_Dynamic(Cell, g_ds.array) + i;  // [1]

    if (i == 0) {
        assert(Is_Cell_Poisoned(at));
    }
    else if (i < TOP_INDEX + 1) {  // in the range of PUSH()'d cells
        Assert_Cell_Writable(at);
    }
    else {
        assert(i == TOP_INDEX + 1);  // allow getting tail's address [3]

      #if DEBUG_POISON_DROPPED_STACK_CELLS
        assert(Is_Cell_Poisoned(at));
      #endif
    }
    return at;
}

#define Data_Stack_At(T,i)  /* may be erased cell */ \
    cast(OnStack(T*), u_cast(T*, Data_Stack_Cell_At(i)))  // u_cast() needed

#if RUNTIME_CHECKS
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
INLINE OnStack(Value*) PUSH(void) {
    Assert_No_DataStack_Pointers_Extant();

    ++g_ds.index;
    ++g_ds.movable_top;
    if (g_ds.movable_top == g_ds.movable_tail)
        Expand_Data_Stack_May_Panic(STACK_EXPAND_BASIS);

  #if DEBUG_POISON_DROPPED_STACK_CELLS
    assert(Is_Cell_Poisoned(g_ds.movable_top));
  #endif

    Erase_Cell(g_ds.movable_top);
    return u_cast(Value*, g_ds.movable_top);  // must u_cast(), erased
}



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

#define Pop_Source_From_Stack(base) \
    cast(Source*, Pop_Stack_Values_Core(STUB_MASK_UNMANAGED_SOURCE, (base)))

#define Pop_Managed_Source_From_Stack(base) \
    cast(Source*, Pop_Stack_Values_Core(STUB_MASK_MANAGED_SOURCE, (base)))


// Since stack overflows are memory-related errors, don't try to do any
// error allocations...just use an already made error.
//
#define Panic_Stack_Overflow() \
    panic (Cell_Error(g_error_stack_overflow));
