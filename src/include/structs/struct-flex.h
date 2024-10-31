//
//  File: %struct-flex.h
//  Summary: "Flex structure definitions preceding %tmp-internals.h"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2024 Ren-C Open Source Contributors
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
// Rebol doesn't depend on any data structure library--like C++'s std::vector.
// Instead, it builds on its own structure called a "Flex".
//
// Every TEXT!, BLOCK!, BINARY!, etc. in Rebol has a Flex.  And Flex is also
// used internally whenever there is a need for a dynamically growable
// contiguous memory structure.
//
// A Flex's identity is its "Stub"... a small-ish fixed-size descriptor for
// Flex data.  Often it contains a pointer to a larger allocation for the
// actual contents.  But if the Flex data is small enough, the contents are
// embedded into the stub structure itself.
//
// Flex behaves something like a "double-ended queue".  It can reserve
// capacity at both the tail and the head.  When data is taken from the head,
// it will retain that capacity...reusing it on later insertions at the head.
//
// The space at the head is called the "bias", and to save on pointer math
// per-access, the stored data pointer is actually adjusted to include the
// bias.  This biasing is backed out upon insertions at the head, and also
// must be subtracted completely to free the pointer using the address
// originally given by the allocator.
//
// A pool quickly grants and releases memory ranges that are sizeof(Stub)
// without needing to use malloc() and free() for each individual allocation.
// These nodes can also be enumerated in the pool without needing the Flex
// to be tracked via a linked list or other structure.  The garbage collector
// is one example of code that performs such an enumeration.
//
// A Flex Stub pointer will remain valid as long as outstanding references
// to the Flex exist in values visible to the GC.  On the other hand, the
// Flex's data pointer may be freed and reallocated to respond to the needs
// of resizing.  (In the future, it may be reallocated just as an idle task
// by the GC to reclaim or optimize space.)
//
//    *** THIS MEANS POINTERS INTO THE Flex_Data() FOR A MANAGED Flex
//    MUST NOT BE HELD ONTO ACROSS EVALUATIONS, WITHOUT SPECIAL PROTECTION
//    OR ACCOMMODATION.**
//
// Flex may be either manually memory managed or delegated to the garbage
// collector.  Free_Unmanaged_Flex() may only be called on manual Flex.
// See Manage_Flex()/Push_Lifeguard() for remarks on how to work safely
// with pointers to garbage-collected Flexes, to avoid having them be GC'd
// out from under the code while working with them.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * For the API of operations available on Flex types, see %stub-flex.h
//
// * In R3-Alpha, Flex was called "REBSER".  Ren-C avoids calling this data
//   structure "Series" because the usermode concept of ANY-SERIES? bundles
//   added information (an Index and a Binding), and using the same term
//   would cause confusion for those trying to delve into the implementation:
//
//     https://forum.rebol.info/t/2221
//
// * In C++, Binary, String, Array, etc. are derived from Flex.  This gives
//   desirable type checking properties (like being able to pass an Array to
//   a routine that needs a Flex, but not vice versa).  And it also means
//   that the base class fields are available in the derived classes.
//
//   In order for the inheritance to be known, these definitions cannot occur
//   until Flex is fully defined.  So %struct-array.h and %struct-string.h
//   etc. can't do those definitions until after %struct-flex.h is included.
//
//     https://stackoverflow.com/q/2159390/
//
// * The unit size in a Flex is known as the "width".  R3-Alpha used a
//   byte for this to get from unit sizes ranging from 0-255 bytes.  Ren-C
//   uses that byte for the "Flavor" of the Stub (a name distinguishing
//   the Stub in a way parallel to a Cell's "Heart") and then maps from Flavor
//   to Size.


#if CPLUSPLUS_11
    struct Flex : public Stub {};  // not all stubs are flexes
#else
    typedef Stub Flex;
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
// FLEX <<HEADER>> FLAGS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Flex has two places to store flags...in the "header" and in the "info".
// The following are the FLEX_FLAG_XXX that are used in the header, while
// the FLEX_INFO_XXX flags will be found in the info.
//
// ** Make_Flex() takes FLEX_FLAG_XXX as a parameter, so anything that
// controls Flex creation should be a _FLAG_ as opposed to an _INFO_! **
//
// (Other general rules might be that bits that are to be tested or set as
// a group should be in the same flag group.  Perhaps things that don't change
// for the lifetime of the Flex might prefer header to the info, too?
// Such things might help with caching.)
//

#define FLEX_FLAGS_NONE \
    0  // helps locate places that want to say "no flags"




//=////////////////////////////////////////////////////////////////////////=//
//
// FLEX <<INFO>> BITS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// See remarks on FLEX <<FLAG>> BITS about the two places where a Flex stores
// bits.  These are the info bits, which are more likely to be changed over
// the lifetime of the Flex--defaulting to FALSE.
//
// !!! The current main application of Flex info is a byte's worth of space
// for the Flex_Used() of Flex content that fits in the Cell area, and
// flags pertaining to locking.  The idea of "popping out" that Flex info
// upon a hold lock being taken--such that the info bits move and the slot
// holds a locking pointer--is currently being thought about.  See the INODE()
// for the beginnings of that.


//=//// FLEX_INFO_0_IS_FALSE ////////////////////////////////////////////=//
//
// The INFO bits are resident immediately after the content description, and
// in the case of singular Arrays a cell is stored in the Stub itself.  An
// array traversal might step outside the bounds, so it's easiest just to say
// the location is not a Node to avoid writing it.
//
// !!! This can be reviewed if getting another bit seems important.
//
#define FLEX_INFO_0_IS_FALSE \
    FLAG_LEFT_BIT(0)
STATIC_ASSERT(FLEX_INFO_0_IS_FALSE == NODE_FLAG_NODE);


//=//// FLEX_INFO_1 /////////////////////////////////////////////////////=//
//
#define FLEX_INFO_1 \
    FLAG_LEFT_BIT(1)


//=//// FLEX_INFO_AUTO_LOCKED ///////////////////////////////////////////=//
//
// Some operations lock Flexes automatically, e.g. to use a piece of data as
// MAP! keys.  This approach was chosen after realizing that a lot of times,
// users don't care if something they use as a key gets locked.  So instead
// of erroring by telling them they can't use an unlocked Flex as a MAP! key,
// this locks it but changes the FLEX_FLAG_HAS_FILE_LINE to implicate the
// point where the locking occurs.
//
// !!! The file-line feature is pending.
//
#define FLEX_INFO_AUTO_LOCKED \
    FLAG_LEFT_BIT(2)


//=//// FLEX_INFO_PROTECTED /////////////////////////////////////////////=//
//
// This indicates that the user had a tempoary desire to protect a Flex
// size or values from modification.  It is the usermode analogue of
// FLEX_INFO_FROZEN_DEEP, but can be reversed.
//
// Note: There is a feature in PROTECT (CELL_FLAG_PROTECTED) which protects
// a certain variable in a context from being changed.  It is similar, but
// distinct.  FLEX_INFO_PROTECTED is a protection on a Flex itself--which
// ends up affecting all values with that Flex in the payload.
//
#define FLEX_INFO_PROTECTED \
    FLAG_LEFT_BIT(3)


//=//// FLEX_INFO_FROZEN_DEEP ///////////////////////////////////////////=//
//
// Indicates that the length or values cannot be modified...ever.  It has been
// locked and will never be released from that state for its lifetime, and if
// it's an Array then everything referenced beneath it is also frozen.  This
// means that if a read-only copy of it is required, no copy needs to be made.
//
// (Contrast this with the temporary condition like caused by something
// like FLEX_INFO_HOLD or FLEX_INFO_PROTECTED.)
//
// Note: This and the other read-only Flex checks are honored by some layers
// of abstraction, but if one manages to get a raw non-const pointer into a
// value in the Flex data...then by that point it cannot be enforced.
//
#define FLEX_INFO_FROZEN_DEEP \
    FLAG_LEFT_BIT(4)


//=//// FLEX_INFO_HOLD //////////////////////////////////////////////////=//
//
// Set in the header whenever some stack-based operation wants a temporary
// hold on a Flex, to give it a protected state.  This will happen with a
// DO, or PARSE, or enumerations.  Even REMOVE-EACH will transition the Flex
// it is operating on into a HOLD state while the removal signals are being
// gathered, and apply all the removals at once before releasing the hold.
//
// It will be released when the execution is finished, which distinguishes it
// from FLEX_INFO_FROZEN_DEEP, which will never be cleared once set.
//
#define FLEX_INFO_HOLD \
    FLAG_LEFT_BIT(5)


//=//// FLEX_INFO_FROZEN_SHALLOW ////////////////////////////////////////=//
//
// A Flex can be locked permanently at its top level only, if you want.
//
#define FLEX_INFO_FROZEN_SHALLOW \
    FLAG_LEFT_BIT(6)


//=//// FLEX_INFO_7 /////////////////////////////////////////////////////=//
//
#define FLEX_INFO_7 \
    FLAG_LEFT_BIT(7)


//=//// BITS 8-15 ARE Flex_Used() FOR NON-DYNAMIC NON-ARRAYS ////////////=//

// STUB_FLAG_DYNAMIC indicates that a Flex has a dynamically allocated
// portion, and it has a whole uintptr_t to use for the length.  However, if
// that flag is not set the payload is small, fitting in StubContentUnion
// where the allocation tracking information would be.
//
// If the data is an Array, then the length can only be 0 or 1, since the
// tracking information is the same size as a cell.  This can be encoded by
// having the cell be poisoned or non-poisoned to know the length.
//
// For Binary and other non-Arrays the length has to be stored somewhere.
// The third byte of the INFO is set aside for the purpose.
//
#define FLAG_USED_BYTE(len)     FLAG_SECOND_BYTE(len)


//=//// BITS 16-31 ARE SymId FOR SYMBOLS //////////////////////////////////=//
//
// These bits are currently unused by other types.  One reason to avoid using
// them is the concept that the INFO slot will be used to hold locking info
// for Flex, which would require a full pointer.
//



// ^-- STOP AT FLAG_LEFT_BIT(31) --^
//
// While 64-bit systems have another 32-bits available in the header, core
// functionality shouldn't require using them...only optimization features.

#define FLEX_INFO_MASK_NONE 0
