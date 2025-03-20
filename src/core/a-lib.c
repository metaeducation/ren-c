//
//  File: %a-lib.c
//  Summary: "Lightweight Export API (RebolValue as opaque type)"
//  Section: environment
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2024 Ren-C Open Source Contributors
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
// This is the "external" API, and %rebol.h contains its exported
// definitions.  That file (and %make-librebol.r which generates it) contains
// comments and notes which will help understand it.
//
// What characterizes the external API is that it is not necessary to #include
// the extensive definitions of `struct Stub` or the APIs for dealing with
// all the internal details (e.g. Push_Lifeguard(), which are easy to get
// wrong).  Not only does this simplify the interface, but it also means that
// the C code using the library isn't competing as much for definitions in
// the global namespace.
//
// Also, due to the nature of the Node superclass (see %sys-node.h), it's
// possible to feed the scanner with a list of pointers that may be to UTF-8
// strings or to Rebol values.  The behavior is to "splice" in the values at
// the point in the scan that they occur, e.g.
//
//     RebolValue* item1 = ...;
//     RebolValue* item2 = ...;
//     RebolValue* item3 = ...;
//
//     RebolValue* result = rebValue(
//         "if not", item1, "[\n",
//             item2, "| print -{Close brace separate from content}-\n",
//         "] else [\n",
//             item3, "| print -{Close brace with content}-]\n"
//     );
//
// (Note: C can't count how many arguments a variadic takes, so this is done
// by making things like rebValue() a macro that uses __VA_ARGS__ and tacks
// a rebEND onto the tail of the list.  There's lots of tricks in play--see
// %make-librebol.r for the nitty-gritty details.)
//
// While the approach is flexible, any token must be completed within its
// UTF-8 string component.  So you can't--for instance--divide a scan up like
// ("{abc", "def", "ghi}") and get the TEXT! {abcdefghi}.  On that note,
// ("a", "/", "b") produces `a / b` and not the PATH! `a/b`.
//
//==//// EXPORT NOTES /////////////////////////////////////////////////////=//
//
// Each exported routine here has a name API_rebXxxYyy.  This is a name by
// which it can be called internally from the codebase like any other function
// that is part of the core.  However, macros for calling it from the core
// are given as `#define rebXxxYyy API_rebXxxYyy`.  This is a little bit nicer
// and consistent with the way it looks when an external client calls the
// functions.
//
// Then extension clients use macros which have you call the functions through
// a struct-based "interface" (similar to the way that interfaces work in
// something like COM).  Here the macros merely pick the API functions through
// a table, e.g. `#define rebXxxYyy interface_struct->rebXxxYyy`.  This means
// paying a slight performance penalty to dereference that API per call, but
// it keeps API clients from depending on the conventional C linker...so that
// DLLs can be "linked" against a Rebol EXE.
//
// (It is not generically possible to export symbols from an executable, and
// just in general there's no cross-platform assurances about how linking
// works, so this provides the most flexibility.)
//

#define REBOL_LEVEL_SHORTHAND_MACROS 0  // we include Windows.h for errors
#include "sys-core.h"

static bool PG_Api_Initialized = false;


// The API tolerates internal cells that are Is_Nulled(), but all handles that
// are Is_Api_Value() mustn't be nulled.  nullptr is the only currency exposed
// to the clients of the API for NULL.
//
INLINE const RebolValue* NULLIFY_NULLED(const Value* value) {
    if (Is_Nulled(value))
        return nullptr;
    return value;
}

//
// ENTER_API macro
//
// For a time, this was done by the wrapping code...so that the APIs here
// would not have to remember to do it.  That made the header file look
// longer, and added function call overhead where it might not be needed.
// Given that the number of APIs is being kept somewhat limited, the macro
// is just included manually.
//
// !!! Review how much checking one wants to do when calling API routines,
// and what the balance should be of debug vs. release.  Right now, this helps
// in particular notice if the core tries to use an API function before the
// proper moment in the boot.
//
#define ENTER_API_RECYCLING_OK \
    do { \
        if (not PG_Api_Initialized) \
            panic ("rebStartup() not called before API call"); \
    } while (0)

#define ENTER_API \
    do { \
        ENTER_API_RECYCLING_OK; \
        if (g_gc.recycling) \
            panic ("Can't call libRebol API from HANDLE!'s RebolHandleCleaner()"); \
    } while (0)


//=//// FLEX-BACKED ALLOCATORS ////////////////////////////////////////////=//
//
// These are replacements for malloc(), realloc(), and free() which use a
// byte-sized Flex as the backing store for the data.
//
// One benefit of using a Flex is that it offers more options for automatic
// memory management (such as being freed in case of a fail(), vs. leaked as
// a malloc() would, or perhaps being GC'd when a particular FRAME! ends).
//
// It also has the benefit of helping interface with client code that has
// been stylized to use malloc()-ish hooks to produce data, when the eventual
// target of that data is a Rebol Flex.  It does this without exposing
// Flex internals to the external API, by allowing one to "rebRepossess()"
// the underlying data as a BLOB! value.
//


//
//  rebAllocBytes: API
//
// * Unlike something like malloc(), this will fail() instead of return null
//   if an allocation cannot be fulfilled.
//
// * Like plain malloc(), if size is zero, the implementation just has to
//   return something that free() will take.  A backing Flex is added in
//   this case vs. returning null, in order to avoid null handling in other
//   routines (e.g. rebRepossess() or handle lifetime control functions).
//
// * Because of the above points, null is *never* returned.
//
// * In order to make it possible to rebRepossess() the memory as a BLOB!
//   that is then safe to alias as TEXT!, it always has an extra 0 byte at
//   the end of the data area.
//
// * It tries to be like malloc() by giving back a pointer "suitably aligned
//   for the size of any fundamental type".  See notes on ALIGN_SIZE.
//
// !!! rebAllocBytesAligned() could exist to take an alignment, which could
// save on wasted bytes when ALIGN_SIZE > sizeof(Flex*)...or work with
// "weird" large fundamental types that need more alignment than ALIGN_SIZE.
//
unsigned char* API_rebAllocBytes(size_t size)
{
    ENTER_API;

    Binary* b = Make_Flex(
        FLAG_FLAVOR(BINARY)  // rebRepossess() only creates BLOB! ATM
            | NODE_FLAG_ROOT  // indicate this originated from the API
            | STUB_FLAG_DYNAMIC  // rebRepossess() needs bias field
            | FLEX_FLAG_DONT_RELOCATE,  // direct data pointer handed back
        Binary,
        ALIGN_SIZE  // stores Binary* (must be at least big enough for void*)
            + size  // for the actual data capacity (may be 0, see notes)
            + 1  // for termination (AS TEXT! of rebRepossess(), see notes)
    );

    Byte* ptr = Binary_Head(b) + ALIGN_SIZE;

    Binary** pb = (cast(Binary**, ptr) - 1);
    *pb = b;  // save self in bytes that appear immediately before the data
    Poison_Memory_If_Sanitize(pb, sizeof(Binary*));  // catch underruns

    // !!! The data is uninitialized, and if it is turned into a BLOB! via
    // rebRepossess() before all bytes are assigned initialized, it could be
    // worse than just random data...MOLDing such a binary and reading those
    // bytes could be bad (due to, for instance, "trap representations"):
    //
    // https://stackoverflow.com/a/37184840
    //
    // It may be that rebAlloc() and rebRealloc() should initialize with 0
    // to defend against that, but that has a cost.  For now we make no such
    // promise--and leave it uninitialized so that address sanitizer notices
    // when bytes are used that haven't been assigned.
    //
    Term_Binary_Len(b, ALIGN_SIZE + size);

    return ptr;
}

//
//  rebTryAllocBytes: API
//
// Variant of rebAllocBytes() that returns nullptr on failure.  To accomplish
// this it just uses a RESCUE_SCOPE to intercept any fail() that happens in the
// course of the underlying Flex creation.
//
unsigned char* API_rebTryAllocBytes(size_t size)
{
    Byte* p;

    RESCUE_SCOPE_IN_CASE_OF_ABRUPT_FAILURE {
        p = API_rebAllocBytes(size);
        CLEANUP_BEFORE_EXITING_RESCUE_SCOPE;
        return p;
    } ON_ABRUPT_FAILURE (Error* e) {
        UNUSED(e);
        CLEANUP_BEFORE_EXITING_RESCUE_SCOPE;
        return nullptr;
    }
}


//
//  rebReallocBytes: API
//
// * Like plain realloc(), null is legal for ptr
//
// * Like plain realloc(), it preserves the lesser of the old data range or
//   the new data range, and memory usage drops if new_size is smaller:
//
// https://stackoverflow.com/a/9575348
//
// * Unlike plain realloc() (but like rebAllocBytes()), this fails instead of
//   returning null, so "safe" to say `ptr = rebReallocBytes(ptr, new_size)`
//
// * A 0 size is considered illegal.  This is consistent with the C11 standard
//   for realloc(), but not with malloc() or rebAllocBytes()...which allow it.
//
unsigned char* API_rebReallocBytes(void *ptr, size_t new_size)
{
    ENTER_API;

    assert(new_size > 0);  // realloc() deprecated this as of C11 DR 400

    if (not ptr)  // C realloc() accepts null
        return API_rebAllocBytes(new_size);

    Binary** pb = cast(Binary**, ptr) - 1;
    Unpoison_Memory_If_Sanitize(pb, sizeof(Binary*));  // fetch `b` underruns

    Binary* b = *pb;
    assert(Is_Node_Root_Bit_Set(b));

    Size old_size = Binary_Len(b) - ALIGN_SIZE;

    // !!! It's less efficient to create a new Flex with another call to
    // rebAlloc(), but simpler for the time being.  Switch to do this with
    // the same Flex Stub.
    //
    Size nsize = new_size;  // see `Size`: we use unsigned sizes internally
    Byte* reallocated = API_rebAllocBytes(new_size);
    memcpy(reallocated, ptr, old_size < nsize ? old_size : nsize);
    API_rebFree(ptr);

    return reallocated;
}


//
//  rebFreeMaybe: API
//
// * As with free(), null is accepted as a no-op.  Use rebFree() if you want
//   the code to error on null input
//
// * Because of the practical usefulness, this operation is legal to call
//   during a GC... although it's a little bit shaky to do so.
//
void API_rebFreeMaybe(void *ptr)
{
    ENTER_API_RECYCLING_OK;

    if (not ptr)
        return;

    Binary** pb = cast(Binary**, ptr) - 1;
    Unpoison_Memory_If_Sanitize(pb, sizeof(Binary*));  // fetch `b` underruns

    Binary* b = *pb;

    if (Is_Node_A_Cell(b) or not (NODE_BYTE(b) & NODE_BYTEMASK_0x02_ROOT)) {
        rebJumps(
            "panic [",
                "-{rebFree() mismatched with allocator!}-"
                "-{Did you mean to use free() instead of rebFree()?}-",
            "]"
        );
    }

    assert(Flex_Wide(b) == 1);

    if (g_gc.recycling and Is_Node_Marked(b)) {
        assert(Is_Node_Managed(b));
        Clear_Node_Marked_Bit(b);
      #if RUNTIME_CHECKS
        g_gc.mark_count -= 1;
      #endif
    }

    Clear_Node_Root_Bit(b);

    if (Is_Node_Managed(b))  // set by rebUnmanageMemory()
        GC_Kill_Flex(b);
    else
        Free_Unmanaged_Flex(b);
}


//
//  rebFree: API
//
// Variant of rebFreeMaybe() that errors on null input
//
void API_rebFree(void *ptr) {
    ENTER_API_RECYCLING_OK;

    if (ptr == nullptr)
        fail ("rebFree() does not take NULL, see rebFreeMaybe()");

    API_rebFreeMaybe(ptr);
}


//
//  rebRepossess: API
//
// Alternative to rebFree() is to take over the underlying Flex as a
// BLOB!.  The old void* should not be used after the transition, as this
// operation makes the Flex underlying the memory subject to relocation.
//
// If the passed in size is less than the size with which the Flex was
// allocated, the overage will be treated as unused Flex capacity.
//
// Note that all rebRepossess()'d data will be terminated by an 0x00 byte
// after the end of its capacity.
//
// !!! All bytes in the allocation are expected to be initialized by this
// point, as failure to do so will mean reads crash the interpreter.  See
// remarks in rebAllocBytes() about the issue, and possibly doing zero fills.
//
// !!! It might seem tempting to use (Binary_Len(s) - ALIGN_SIZE).  However,
// some routines make allocations bigger than they ultimately need and do not
// realloc() before converting the memory to a Flex...rebInflate() and
// rebDeflate() do this.  So a version passing the size will be necessary,
// and since C does not have the size exposed in malloc() and you track it
// yourself, it seems fair to *always* ask the caller to pass in a size.
//
RebolValue* API_rebRepossess(void* ptr, size_t size)
{
    ENTER_API;

    Binary** pb = cast(Binary**, ptr) - 1;
    Unpoison_Memory_If_Sanitize(pb, sizeof(Binary*));  // fetch `b` underruns

    Binary* b = *pb;
    assert(Is_Node_Root_Bit_Set(b));  // may or may not be managed
    assert(Get_Flex_Flag(b, DONT_RELOCATE));

    if (size > Binary_Len(b) - ALIGN_SIZE)
        fail ("Attempt to rebRepossess() more than rebAlloc() capacity");

    Clear_Node_Root_Bit(b);
    Clear_Flex_Flag(b, DONT_RELOCATE);

    if (Get_Stub_Flag(b, DYNAMIC)) {
        //
        // Dynamic Flexes have the concept of a "bias", which is unused
        // allocated capacity at the head of the Flex.  Bump the "bias" to
        // treat the embedded Binary* (aligned to REBI64) as unused capacity.
        //
        Set_Flex_Bias(b, ALIGN_SIZE);
        b->content.dynamic.data += ALIGN_SIZE;
        b->content.dynamic.rest -= ALIGN_SIZE;
    }
    else {
        // Data is in Binary Stub itself, no bias.  Just slide the bytes down.
        //
        memmove(  // src overlaps destination, can't use memcpy()
            Binary_Head(b),
            Binary_Head(b) + ALIGN_SIZE,
            size
        );
    }

    Term_Binary_Len(b, size);
    return Init_Blob(Alloc_Value(), b);
}


//
//  rebUnmanageMemory: API
//
// By default, any memory allocated by rebAlloc() will be freed in case of
// a fail().  However it must be rebRepossess()'d or rebFree()'d before the
// frame terminates if there is no error.
//
// This removes that limitation--making it more like a regular malloc() with
// an indefinite lifetime.  It has the corresponding downside of not being
// freed automatically in case of a failure.
//
// The return value is the same as the input pointer, so you can write:
//
//    void* p = rebUnmanageMemory(rebAlloc(...));
//
void* API_rebUnmanageMemory(void* ptr)
{
    ENTER_API;

    Binary** pb = cast(Binary**, ptr) - 1;
    Unpoison_Memory_If_Sanitize(pb, sizeof(Binary*));  // fetch `b` underruns

    // We "manage" the Flex to remove it from the tracked manuals list.
    // But the fact that it still has NODE_FLAG_ROOT means it should not be
    // garbage collected.
    //
    Binary* b = *pb;
    assert(Is_Node_Root_Bit_Set(b));
    Manage_Flex(b);  // panics if already unmanaged... should it tolerate?

    Poison_Memory_If_Sanitize(pb, sizeof(Binary*));  // catch underruns

    return ptr;
}


//
//  Startup_Api: C
//
// API routines may be used by extensions (which are invoked by a fully
// initialized Rebol core) or by normal linkage (such as from within the core
// itself).  A call to rebStartup() won't be needed in the former case.  So
// setup code that is needed to interact with the API needs to be done by the
// core independently.
//
void Startup_Api(void)
{
    assert(not PG_Api_Initialized);
    PG_Api_Initialized = true;
}


//
//  Shutdown_Api: C
//
// See remarks on Startup_Api() for the difference between this idea and
// rebShutdown.
//
void Shutdown_Api(void)
{
    assert(PG_Api_Initialized);
    PG_Api_Initialized = false;
}


//
//  rebStartup: API
//
// This function will allocate and initialize all memory structures used by
// the REBOL interpreter. This is an extensive process that takes time.
//
void API_rebStartup(void)
{
    Startup_Core();
}


//
//  rebShutdown: API
//
// Shut down a Rebol interpreter initialized with rebStartup().
//
// The `clean` parameter tells whether you want Rebol to release all of its
// memory accrued since initialization.  If you pass false, then it will
// only do the minimum needed for data integrity (it assumes you are planning
// to exit the process, and hence the OS will automatically reclaim all
// memory/handles/etc.)
//
// 1. Checked builds do a clean Shutdown, Startup, and then shutdown again
//    to make sure we can do so in case a system wanted to uninitialize then
//    reinitialize.  It suffers the performance penalty of being clean in
//    order to ensure that valgrind/etc. report no leaks possible.
//
void API_rebShutdown(bool clean)
{
    ENTER_API;

  #if RUNTIME_CHECKS  // shutdown, startup, shutdown...always clean [1]
    UNUSED(clean);
    Shutdown_Core(true);
    Startup_Core();
    Shutdown_Core(true);
  #else
    Shutdown_Core(clean);
  #endif
}


//
//  rebTick: API
//
// If the executable is built with tick counting, this will return the tick
// without requiring any Rebol code to run (which would disrupt the tick).
//
// !!! Note that while Tick is a uint64_t, we currently don't want to make
// the API rely on a uint64_t definition.  It is likely better to have a way
// to disable the API_rebTick() for platforms without 64-bit numbers.
//
uintptr_t API_rebTick(void)
{
    ENTER_API;

    return cast(uintptr_t, TICK);  // TICK is 0 if not TRAMPOLINE_COUNTS_TICKS
}


//=//// VALUE CONSTRUCTORS ////////////////////////////////////////////////=//
//
// These routines are for constructing Rebol values from C primitive types.
// The general philosophy is that this stay limited.  Hence there is no
// constructor for making DATE! directly (one is expected to use MAKE DATE!
// and pass in parts that were constructed from integers.)  This also avoids
// creation of otherwise useless C structs, while the Rebol function designs
// are needed to create the values from the interpreter itself.
//
// * There's no function for returning a null pointer, because C's notion of
//   (void*)0 is used.  But note that the C standard permits NULL defined as
//   simply 0.  This breaks use in variadics, so it is advised to use C++'s
//   nullptr, or do `#define nullptr (void*)0
//
// * Routines with full written out names like `rebInteger()` return API
//   handles which must be rebRelease()'d.  Shorter versions like rebI() don't
//   return Value* but are designed for transient use when evaluating, e.g.
//   `rebElide("print [", rebI(count), "]");` does not need to rebRelease()
//   the count variable because the evaluator frees it immediately after use.
//
//=////////////////////////////////////////////////////////////////////////=//


// rebNull is a #define of `(RebolValue*)0` in %rebol.h, so no API entry point
//
// See notes in %rebol.h about why NOT to use ordinary `#define NULL 0` !
// But C++ nullptr (or shim) should be used instead if available.


//
//  rebVoid: API
//
RebolValue* API_rebVoid(void)
{
    ENTER_API;

    Value* v = Alloc_Value();  // just has NODE_FLAG_ROOT, counts as "fresh"
    Init_Void(v);
    return v;
}


//
//  rebNothing: API
//
RebolValue* API_rebNothing(void)
{
    ENTER_API;

    return Init_Nothing(Alloc_Value());
}


//
//  rebTrash: API
//
RebolValue* API_rebTrash(void)
{
    ENTER_API;

    return Init_Trash(Alloc_Value());
}


//
//  rebBlank: API
//
RebolValue* API_rebBlank(void)
{
    ENTER_API;

    return Init_Blank(Alloc_Value());
}


//
//  rebLogic: API
//
// !!! For the C and C++ builds to produce compatible APIs, we assume the
// C <stdbool.h> gives a bool that is the same size as for C++.  This is not
// a formal guarantee, but there's no "formal" guarantee the `int`s would be
// compatible either...more common sense: https://stackoverflow.com/q/3529831
//
// Use DID on the bool, in case it's a "shim bool" (e.g. just some integer
// type) and hence may have values other than strictly 0 or 1.
//
RebolValue* API_rebLogic(bool logic)
{
    ENTER_API;

    if (not logic)
        return nullptr;
    return Init_Logic(Alloc_Value(), true);
}


//
//  rebChar: API
//
RebolValue* API_rebChar(uint32_t codepoint)
{
    ENTER_API;

    Value* v = Alloc_Value();
    Option(Error*) error = Trap_Init_Char(v, codepoint);
    if (error) {
        rebRelease(v);
        fail (unwrap error);
    }
    return v;
}


//
//  rebInteger: API
//
// !!! Should there be rebSigned() and rebUnsigned(), in order to catch cases
// of using out of range values?
//
RebolValue* API_rebInteger(int64_t i)
{
    ENTER_API;

    return Init_Integer(Alloc_Value(), i);
}


//
//  rebDecimal: API
//
RebolValue* API_rebDecimal(double dec)
{
    ENTER_API;

    return Init_Decimal(Alloc_Value(), dec);
}


//
//  rebSizedBinary: API
//
// The name "rebBinary()" is reserved for use in languages who have some
// concept of data which can serve as a single argument because it knows its
// own length.  C doesn't have this for raw byte buffers, but JavaScript has
// things like Int8Array.
//
RebolValue* API_rebSizedBinary(const void* bytes, size_t size)
{
    ENTER_API;

    Binary* b = Make_Binary(size);
    memcpy(Binary_Head(b), bytes, size);
    Term_Binary_Len(b, size);

    return Init_Blob(Alloc_Value(), b);
}


//
//  rebUninitializedBinary_internal: API
//
// !!! This is a dicey construction routine that users shouldn't have access
// to, because it gives the internal pointer of the binary out.  The reason
// it exists is because emscripten's writeArrayToMemory() is based on use of
// an Int8Array.set() call.
//
// When large amounts of data come back from file reads/etc. the caller
// already has one copy of it.  We don't want to extract it into a temporary
// malloc'd buffer just to be able to pass it to reb.Binary() to make yet
// *another* copy.
//
// Note: It might be interesting to have a concept of "external" memory by
// which the data wasn't copied but a handle was kept to the JavaScript
// Int8Array that came back from fetch() (or whatever).  But emscripten does
// not at this time have a way to read anything besides the HEAP8:
//
// https://stackoverflow.com/a/43325166
//
RebolValue* API_rebUninitializedBinary_internal(size_t size)
{
    ENTER_API;

    Binary* b = Make_Binary(size);

    // !!! Caution, unfilled bytes, access or molding may be *worse* than
    // random by the rules of C if they don't get written!  Must be filled
    // immediately by caller--before a GC or other operation.
    //
    Term_Binary_Len(b, size);

    return Init_Blob(Alloc_Value(), b);
}


//
//  rebBinaryHead_internal: API
//
// Complementary "evil" routine to rebUninitializedBinary().  Should not
// be generally used, as passing out raw pointers to binaries can have them
// get relocated out from under the caller.  If pointers are going to be
// given out in this fashion, there has to be some kind of locking semantics.
//
// (Note: This could be a second return value from rebUninitializedBinary(),
// but that would involve pointers-to-pointers which are awkward in
// emscripten and probably cheaper to make two direct WASM calls.
//
unsigned char* API_rebBinaryHead_internal(const RebolValue* binary)
{
    ENTER_API;

    return Binary_Head(Cell_Binary_Known_Mutable(binary));
}


//
//  rebBinaryAt_internal: API
//
unsigned char* API_rebBinaryAt_internal(const RebolValue* binary)
{
    ENTER_API;

    return Cell_Blob_At_Known_Mutable(binary);
}


//
//  rebBinarySizeAt_internal: API
//
unsigned int API_rebBinarySizeAt_internal(const RebolValue* binary)
{
    ENTER_API;

    return Cell_Series_Len_At(binary);
}


//
//  rebSizedText: API
//
// If utf8 does not contain valid UTF-8 data, this may fail().
//
// !!! Should there be variants for Strict/Relaxed, e.g. a version that does
// not accept CR and one that does?
//
RebolValue* API_rebSizedText(const char* utf8, size_t size)
{
    ENTER_API;

    return Init_Text(
        Alloc_Value(),
        Append_UTF8_May_Fail(nullptr, utf8, size, STRMODE_ALL_CODEPOINTS)
    );
}


//
//  rebText: API
//
RebolValue* API_rebText(const char* utf8)
{
    ENTER_API;

    return rebSizedText(utf8, strsize(utf8));
}


//
//  rebLengthedTextWide: API
//
RebolValue* API_rebLengthedTextWide(
    const REBWCHAR* wstr,
    unsigned int num_chars
){
    ENTER_API;

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    for (; num_chars != 0; --num_chars, ++wstr)
        Append_Codepoint(mo->string, *wstr);

    return Init_Text(Alloc_Value(), Pop_Molded_String(mo));
}


//
//  rebTextWide: API
//
// Imports a TEXT! from UTF-16 (potentially multi-wchar-per-codepoint encoding)
//
RebolValue* API_rebTextWide(const REBWCHAR* wstr)
{
    ENTER_API;

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    while (*wstr != 0) {
        if (*wstr >= UNI_SUR_HIGH_START and *wstr <= UNI_SUR_HIGH_END) {
            if (not (
                *(wstr + 1) >= UNI_SUR_LOW_START
                and *(wstr + 1) <= UNI_SUR_LOW_END
            )){
                fail ("Invalid UTF-16 surrogate pair passed to rebTextWide()");
            }
            Append_Codepoint(mo->string, Decode_UTF16_Pair(wstr));
            wstr += 2;
        }
        else {
            Append_Codepoint(mo->string, *wstr);
            ++wstr;
        }
    }
    return Init_Text(Alloc_Value(), Pop_Molded_String(mo));
}


//
//  rebHandle: API
//
// !!! The HANDLE! type has some complexity to it, because function pointers
// in C and C++ are not actually guaranteed to be the same size as data
// pointers.  Also, there is an optional size stored in the handle, and a
// cleanup function the GC may call when references to the handle are gone.
//
RebolValue* API_rebHandle(
    void *data,  // !!! What about `const void*`?  How to handle const?
    size_t length,
    RebolHandleCleaner* cleaner
){
    ENTER_API;

    return Init_Handle_Cdata_Managed(Alloc_Value(), data, length, cleaner);
}


//
//  rebModifyHandleCData: API
//
void API_rebModifyHandleCData(
    RebolValue* v,
    void *data  // !!! What about `const void*`?  How to handle const?
){
    ENTER_API;

    if (not Is_Handle(v))
        fail ("rebModifyHandleCData() called on non-HANDLE!");

    assert(Cell_Has_Node1(v));  // api only sees managed handles

    Tweak_Cell_Handle_Cdata(v, data);
}


//
//  rebModifyHandleLength: API
//
void API_rebModifyHandleLength(RebolValue* v, size_t length) {
    ENTER_API;

    if (not Is_Handle(v))
        fail ("rebModifyHandleLength() called on non-HANDLE!");

    assert(Cell_Has_Node1(v));  // api only sees managed handles

    Tweak_Cell_Handle_Len(v, length);
}


//
//  rebModifyHandleCleaner: API
//
void API_rebModifyHandleCleaner(RebolValue* v, RebolHandleCleaner* cleaner) {
    ENTER_API;

    if (not Is_Handle(v))
        fail ("rebModifyHandleCleaner() called on non-HANDLE!");

    assert(Cell_Has_Node1(v));  // api only sees managed handles

    Extract_Cell_Handle_Stub(v)->misc.cleaner = cleaner;
}


//
//  rebArgR: API
//
// This is the version of getting an argument that does not require a release.
// However, it is more optimal than `rebR(rebArg(...))`, because how it works
// is by returning the actual Value* to the argument in the frame.  It's not
// good to have client code having those as handles--however--as they do not
// follow the normal rules for lifetime, so rebArg() should be used if the
// client really requires a Value*.
//
// !!! When code is being used to look up arguments of a function, exactly
// how that will work is being considered:
//
// https://forum.rebol.info/t/817
// https://forum.rebol.info/t/820
//
// For the moment, this routine specifically accesses arguments of the most
// recent ACTION! on the stack.
//
const RebolNodeInternal* API_rebArgR(
    RebolContext* binding,
    const void* p, void* vaptr
){
    ENTER_API;

    UNUSED(binding);  // not used...should not be a variadic

    Level* L = TOP_LEVEL;
    Phase* phase = Level_Phase(L);

    // !!! Currently the JavaScript wrappers do not do the right thing for
    // taking just a `const char*`, so this falsely is a variadic to get the
    // JavaScript string proxying.
    //
    const char* name;
    const void* p2;
    if (vaptr) {
        name = c_cast(char*, p);
        p2 = va_arg(*cast(va_list*, vaptr), const void*);
    }
    else {
        const void* const *packed = cast(const void* const*, p);
        name = c_cast(char*, *packed++);
        p2 = *packed++;
    }
    if (Detect_Rebol_Pointer(p2) != DETECTED_AS_END)
        fail ("rebArg() isn't actually variadic, it's arity-1");

    const Symbol* symbol = Intern_UTF8_Managed(cb_cast(name), strsize(name));

    const Key* tail;
    const Key* key = Phase_Keys(&tail, phase);
    Value* arg = Level_Args_Head(L);
    for (; key != tail; ++key, ++arg) {
        if (Are_Synonyms(Key_Symbol(key), symbol))
            return c_cast(RebolNodeInternal*, NULLIFY_NULLED(arg));
    }

    fail ("Unknown rebArg(...) name.");
}


//
//  rebArg: API
//
// Wrapper over the more optimal rebArgR() call, which can be used to get
// an "safer" API handle to the argument.
//
RebolValue* API_rebArg(
    RebolContext* binding,
    const void* p, void* vaptr
){
    ENTER_API;

    const void* argR = API_rebArgR(binding, p, vaptr);
    if (not argR)
        return nullptr;

    const Value* arg = c_cast(Value*, argR);  // sneaky, but we know!
    return Copy_Cell(Alloc_Value(), arg);  // don't give Value* arg directly
}


//=//// EVALUATIVE EXTRACTORS /////////////////////////////////////////////=//
//
// The libRebol API evaluative routines are all variadic, and call the
// evaluator on multiple pointers.  Each pointer may be:
//
// - a Value*
// - a UTF-8 string to be scanned as one or more values in the sequence
// - a Stub* that represents an "API instruction"
//
// There isn't a separate concept of routines that perform evaluations and
// ones that extract C fundamental types out of Rebol values.  Hence you
// don't have to say:
//
//      RebolValue* value = rebValue("1 +", some_rebol_integer);
//      int sum = rebUnboxInteger(value);
//      rebRelease(value);
//
// You can just write:
//
//      int sum = rebUnboxInteger("1 +", some_rebol_integer);
//
// The default evaluators splice Rebol values "as-is" into the feed.  This
// means that any evaluator active types (like WORD!, ACTION!, GROUP!...)
// will run.  This can be mitigated with rebQ or "@"
//
//=//// EXCEPTION HANDLING ////////////////////////////////////////////////=//
//
// Exception handling with the API is a work-in-progress.  A lot has changed
// in the implementation, in particular support for either compiling with
// setjmp/longjmp -or- try/catch as the mechanic managing abrupt failures.
//
// But also, with stackless processing, only one setjmp() or try{} is needed
// for each invocation of the trampoline.  This means that abrupt failure
// protection comes "for free" with every API call that invokes a new
// trampoline...and it's just a matter of deciding what to do at the
// interface level if the result is an unhandled raised error.
//
// It's a largely uncharted territory at this time...so the hope is that
// your code "just works".  The JavaScript ReplPad runs the console extension
// code which is already protected by SYS.UTIL/RESCUE, so this captures the
// failures in API calls in JS-NATIVEs pretty well...but other scenarios
// might be messier.
//
//=////////////////////////////////////////////////////////////////////////=//


//
//  Run_Va_Throws: C
//
// 1. va_end() will be called on va_lists regardless of what happens (e.g.
//    FAILs, THROWS, etc.).  The feed processing traverses to the end in all
//    cases, making sure it frees any instructions that were allocated as
//    parameters to this call.
//
// 2. When vaptr is null, p is a pointer to a packed C array of `const void*`.
//    This method is preferred by the C++ build, because using variadic
//    templates it can recursively process the arguments and pack them into
//    that array...doing additional type checking and conversions.
//
//    (The WebAssembly build also uses this packed array format, as it does not
//    require delving into the compiler-specific details of how a va_list is
//    encoded...and can stick to the standardized layout of a pointer array.)
//
// 3. When vaptr is non-null, it is a pointer to a va_list:
//
//      http://stackoverflow.com/a/3369762/211160
//
//    `p` represents the first parameter *before* the va_list.  It's done
//    this way because due to the nature of C, you always have to have one
//    non-variadic parameter in a variadic function.  This turns out all right
//    with Ren-C being able to even work with something like `rebValue()`,
//    because rebValue() is actually a macro that throws in a rebEND to get
//    `rebValue_inline(rebEND)`.
//
// 4. Interruptibility means that TRAMPOLINE_FLAG_HALT will be heeded when the
//    flag has been set.  If not, it will leave the flag set and continue
//    processing.  Most code using the API doesn't want to react to the
//    signal...because it would cause an exception/longjmp() and the C API
//    call would not return.  So only a few functions that are specifically
//    designed to give back errors react, e.g. rebEntrapInterruptible()
//
static bool Run_Va_Throws(  // va_end() handled by feed for all cases [1]
    RebolContext* binding,
    Atom* out,
    bool interruptible,  // whether a HALT can cause a longjmp/throw
    Flags flags,
    const void* p,  // null vaptr means void* array [2] else first param [3]
    void* vaptr
){
    Feed* feed = Make_Variadic_Feed(
        p, cast(va_list*, vaptr),
        FEED_MASK_DEFAULT
    );

    if (binding == nullptr) {
        assert(Is_Stub_Sea(g_user_context));
        binding = cast(RebolContext*, g_user_context);
    }

    assert(Is_Node_Managed(binding));
    Tweak_Feed_Binding(feed, cast(Stub*, binding));

    Level* L = Make_Level(&Evaluator_Executor, feed, flags);
    Init_Void(Evaluator_Primed_Cell(L));

    if (interruptible)
        L->flags.bits &= (~ LEVEL_FLAG_UNINTERRUPTIBLE);
    else
        L->flags.bits |= LEVEL_FLAG_UNINTERRUPTIBLE;

    Push_Level_Dont_Inherit_Interruptibility(out, L);
    bool threw = Trampoline_With_Top_As_Root_Throws();
    Drop_Level(L);

    if (not threw and (flags & LEVEL_FLAG_META_RESULT))
        assert(QUOTE_BYTE(out) >= QUASIFORM_2);

    return threw;
}


// This version of the variadic run does not do isotope decay, because that
// errors on empty parameter packs ("nihil"), e.g. ~[]~ antiforms.  It would be
// deceptive to have that unpack and conflate as any other state (including
// void and null, which have specific meanings).  So it simply won't.
//
// An API like rebValue() or rebUnboxLogic() thus won't handle ~[]~, and you
// need to use rebMeta() to get the parameter pack and understand its nature,
// or use rebElide() and say it doesn't matter.  (or just find a way in the
// executed code passed to not have it evaluate to nihil)
//
INLINE void Run_Va_Undecayed_May_Fail_Calls_Va_End(
    RebolContext* binding,
    Sink(Atom) out,
    const void* p,  // first pointer (may be END, nullptr means NULLED)
    void* vaptr  // va_end() handled by feed for all cases (throws, fails)
){
    bool interruptible = false;
    if (Run_Va_Throws(
        binding,
        out,
        interruptible,
        LEVEL_MASK_NONE,
        p, vaptr
    )){
        // !!! Being able to THROW across C stacks is necessary in the general
        // case (consider implementing QUIT or HALT).  Probably need to be
        // converted to a kind of error, and then re-converted into a THROW
        // to bubble up through Rebol stacks?  Development on this is ongoing.
        //
        fail (Error_No_Catch_For_Throw(TOP_LEVEL));
    }
}

// This is the decaying form.  Decaying is done for convenience in the basic
// APIs just as if you'd tried to do a SET-WORD!...so you don't have to worry
// about multi-return packs etc.
//
INLINE void Run_Va_Decay_May_Fail_Calls_Va_End(
    RebolContext* binding,
    Sink(Value) out,
    const void* p,  // first pointer (may be END, nullptr means NULLED)
    void* vaptr  // va_end() handled by feed for all cases (throws, fails)
){
    Run_Va_Undecayed_May_Fail_Calls_Va_End(binding, cast(Atom*, out), p, vaptr);

    Decay_If_Unstable(cast(Atom*, out));
}


//
//  rebRunCoreThrows_internal: API
//
// Most API routines (rebValue(), rebDid(), etc.) have no way of handling
// thrown values or invisibles.  They also run more than one step.
//
// This function runs one evaluation step, and allows you to say whether the
// step must consume all input or not (see EVAL_EXECUTOR_FLAG_NO_RESIDUE).
//
// 1. The output is written into a cell that is provided--which is not
//    something the API should do.  But it's in the API file because we want
//    the wrapping machinery that handles the variadics to be applied here.
//
// 2. When calling this routine, use either the rebRunThrowsInterruptible() or
//    rebRunThrows() macros.  (The policy on when to use which is still
//    evolving, but as an example of a place where interruptibility is
//    okay is that WRITE-STDOUT calls into the API to do an AS TEXT! call,
//    and allowing that to interrupt makes reacting to Ctrl-C while printing
//    large things more responsive.)
//
bool API_rebRunCoreThrows_internal(  // use interruptible or non macros [2]
    RebolContext* binding,
    RebolValue* out,
    uintptr_t flags,  // Flags not exported in API
    const void* p, void* vaptr
){
    Feed* feed = Make_Variadic_Feed(
        p, cast(va_list*, vaptr),
        FEED_MASK_DEFAULT
    );

    if (binding == nullptr) {
        assert(Is_Stub_Sea(g_user_context));
        binding = cast(RebolContext*, g_user_context);
    }

    assert(Is_Node_Managed(binding));
    Tweak_Feed_Binding(feed, cast(Stub*, binding));

    Level* L = Make_Level(&Stepper_Executor, feed, flags);
    Push_Level_Erase_Out_If_State_0(cast(Atom*, out), L);

    if (Trampoline_With_Top_As_Root_Throws()) {
        Drop_Level(L);
        return true;
    }

    bool too_many = (flags & EVAL_EXECUTOR_FLAG_NO_RESIDUE)
        and Not_Feed_At_End(feed);  // feed will be freed in Drop_Level()

    Drop_Level(L);  // will va_end() if not reified during evaluation

    if (too_many)
        fail (Error_Apply_Too_Many_Raw());

    if (Is_Raised(cast(Atom*, out)) and (flags & LEVEL_FLAG_RAISED_RESULT_OK))
        return false;  // !!! Lying about the result being a RebolValue !

    Decay_If_Unstable(cast(Atom*, out));
    return false;
}


//
//  rebValue: API
//
// Most basic evaluator that returns a Value*, which must be rebRelease()'d.
//
RebolValue* API_rebValue(
    RebolContext* binding,
    const void* p, void* vaptr
){
    ENTER_API;

    Value* result = Alloc_Value_Core(CELL_MASK_ERASED_0);
    Run_Va_Decay_May_Fail_Calls_Va_End(binding, result, p, vaptr);

    if (Is_Nulled(result)) {
        Free_Value(result);
        return nullptr;  // No NULLED cells in API, see NULLIFY_NULLED()
    }

    Set_Node_Root_Bit(result);
    return result;  // caller must rebRelease()
}


//
//  rebTranscodeInto: API
//
// Just scans the source given into a BLOCK! without executing it.
//
RebolValue* API_rebTranscodeInto(
    RebolContext* binding,  // Note: corrupt on purpose if RUNTIME_CHECKS
    RebolValue* out,
    const void* p, void* vaptr
){
    ENTER_API;

    Feed* feed = Make_Variadic_Feed(
        p, cast(va_list*, vaptr),
        FEED_MASK_DEFAULT
    );
    Add_Feed_Reference(feed);
    Sync_Feed_At_Cell_Or_End_May_Fail(feed);

    UNUSED(binding);  // transcode should not heed binding

    StackIndex base = TOP_INDEX;
    while (Not_Feed_At_End(feed)) {
        Derelativize(PUSH(), At_Feed(feed), Feed_Binding(feed));
        Fetch_Next_In_Feed(feed);
    }

    Release_Feed(feed);  // Note: exhausting feed takes care of the va_end()

    Init_Block(out, Pop_Managed_Source_From_Stack(base));
    return out;
}


//
//  rebPushContinuation_internal: API
//
// Helper for when variadic code wants to run as its own stack level.
//
// 1. We don't call `rebTranscodeInto()` here, because that would package
//    up an arbitrary number of variadic parameters that are meant to
//    be things like Value* and UTF8.  But we have exactly 3 parameters
//    in hand, and want to pass them directly to the implementation routine,
//    as they're encodings of variadic parameters--not the actual parameters!
//
// 2. TRANSCODE does not put any binding in the block it takes, so we have
//    to apply the context here.
//
void API_rebPushContinuation_internal(
    RebolContext* binding,
    RebolValue* out,
    uintptr_t flags,
    const void* p, void* vaptr
){
    ENTER_API;

    DECLARE_VALUE (block);
    RebolContext* dummy_binding = nullptr;  // transcode ignores
    Corrupt_Pointer_If_Debug(dummy_binding);
    API_rebTranscodeInto(dummy_binding, block, p, vaptr);  // use "API_" [1]

    if (binding)
        Tweak_Cell_Binding(block, cast(Context*, binding));  // [2]
    else
        Tweak_Cell_Binding(block, g_lib_context);  // [3]

    Level* L = Make_Level_At(&Evaluator_Executor, block, flags);
    Init_Void(Evaluator_Primed_Cell(L));
    Push_Level_Erase_Out_If_State_0(cast(Atom*, out), L);
}


//
//  rebMeta: API
//
// Builds in a ^META operation to rebValue; shorthand that's more efficient.
//
//     rebMeta(...) => rebValue("meta", ...")
//
// Will return parameter packs as-is.
//
RebolValue* API_rebMeta(
    RebolContext* binding,
    const void* p, void* vaptr
){
    ENTER_API;

    Value* v = Alloc_Value_Core(CELL_MASK_ERASED_0);
    bool interruptible = false;
    if (Run_Va_Throws(
        binding,
        v,
        interruptible,
        LEVEL_FLAG_META_RESULT,
        p, vaptr
    )){
        fail (Error_No_Catch_For_Throw(TOP_LEVEL));  // panic?
    }
    assert(not Is_Nulled(v));  // meta operations cannot produce NULL

    Set_Node_Root_Bit(v);
    return v;  // caller must rebRelease()
}


//
//  rebEntrap: API
//
// Builds in an ENTRAP operation to rebValue; shorthand that's more efficient.
//
//     rebEntrap(...) => rebValue("entrap [", ..., "]")
//
RebolValue* API_rebEntrap(
    RebolContext* binding,
    const void* p, void* vaptr
){
    ENTER_API;

    Value* v = Alloc_Value_Core(CELL_MASK_ERASED_0);
    bool interruptible = false;
    if (Run_Va_Throws(
        binding,
        v,
        interruptible,
        LEVEL_FLAG_META_RESULT,
        p, vaptr
    )){
        fail (Error_No_Catch_For_Throw(TOP_LEVEL));  // panic?
    }

    assert(not Is_Nulled(v));  // meta operations cannot produce NULL

    if (Is_Meta_Of_Raised(v))
        QUOTE_BYTE(v) = NOQUOTE_1;  // plain error
    else
        assert(QUOTE_BYTE(v) > NOQUOTE_1);

    Set_Node_Root_Bit(v);
    return v;  // caller must rebRelease()
}


//
//  rebRescue: API
//
// Builds in an RESCUE operation to rebValue; shorthand that's more efficient.
//
//     rebRescue(...) => rebValue("enrescue [", ..., "]")
//
RebolValue* API_rebRescue(
    RebolContext* binding,
    RebolValue** value,
    const void* p, void* vaptr
){
    ENTER_API;

    Value* v = Alloc_Value_Core(CELL_MASK_ERASED_0);
    bool interruptible = false;
    if (Run_Va_Throws(
        binding,
        cast(Atom*, v),
        interruptible,
        LEVEL_MASK_NONE,
        p, vaptr
    )){
        Init_Error(v, Error_No_Catch_For_Throw(TOP_LEVEL));
        Set_Node_Root_Bit(v);
        Corrupt_If_Debug(*value);  // !!! corrupt in release builds?
        return v;
    }
    assert(not Is_Raised(cast(Atom*, v)));  // no LEVEL_FLAG_RAISED_RESULT_OK
    Decay_If_Unstable(cast(Atom*, v));
    Set_Node_Root_Bit(v);
    *value = v;
    return nullptr;
}


//
//  rebRescueInterruptible: API
//
// !!! How should interruptibility be communicated more generally in the
// API, if more functions have Rescue variations?  An API instruction, like
// rebINTERRUPTIBLE(), that you pass in?
//
RebolValue* API_rebRescueInterruptible(
    RebolContext* binding,
    RebolValue** value,
    const void* p, void* vaptr
){
    ENTER_API;

    Value* v = Alloc_Value_Core(CELL_MASK_ERASED_0);
    bool interruptible = true;
    if (Run_Va_Throws(
        binding,
        cast(Atom*, v),
        interruptible,
        LEVEL_MASK_NONE,
        p, vaptr
    )){
        Init_Error(v, Error_No_Catch_For_Throw(TOP_LEVEL));
        Set_Node_Root_Bit(v);
        Corrupt_If_Debug(*value);  // !!! corrupt in release builds?
        return v;
    }
    assert(not Is_Raised(cast(Atom*, v)));  // no LEVEL_FLAG_RAISED_RESULT_OK
    Decay_If_Unstable(cast(Atom*, v));
    Set_Node_Root_Bit(v);
    *value = v;
    return nullptr;
}


//
//  rebQuote: API
//
// Variant of rebValue() that simply quotes its result.  So `rebQuote(...)` is
// equivalent to `rebValue("quote", ...)`, with the advantage of being faster
// and not depending on what the QUOTE word looks up to.
//
// (It also has the advantage of not showing QUOTE on the call stack.  That
// is important for the console when trapping its generated result, to be
// able to quote it without the backtrace showing a QUOTE stack frame.)
//
RebolValue* API_rebQuote(
    RebolContext* binding,
    const void* p, void* vaptr
){
    ENTER_API;

    Value* result = Alloc_Value();
    Run_Va_Decay_May_Fail_Calls_Va_End(binding, result, p, vaptr);
    if (Is_Antiform(result))
        fail ("rebQuote() called on expression that returned an antiform");

    return Quotify(cast(Element*, result));
}


//
//  rebElide: API
//
// Variant of rebValue() which assumes you don't need the result.  This saves on
// allocating an API handle, or the caller needing to manage its lifetime.
//
// Also means that if the product is something like a ~[]~ antiform ("nihil")
// that is not an issue.
//
void API_rebElide(
    RebolContext* binding,
    const void* p, void* vaptr
){
    ENTER_API;

    DECLARE_ATOM (discarded);
    Run_Va_Undecayed_May_Fail_Calls_Va_End(binding, discarded, p, vaptr);
}


//
//  rebJumps: API [
//      #noreturn
//  ]
//
//=//// WARNING: USE OF rebJumps() SHOULD BE AVOIDED IF POSSIBLE //////////=//
//
// It's preferable to gracefully give control back to the trampoline and let
// it run jumping constructs like FAIL or THROW, e.g. by using rebDelegate().
//
//    return rebDelegate("fail {This is a better way to jump!}");
//
// This prevents your code from having its own stack crossed by whatever
// exception or jumping model is used (which could be longjump(), exceptions,
// or possibly an interpreter compiled with no exceptions whatsoever...but
// it can still handle a cooperative FAIL).
//
//=//// IF YOU MUST DISREGARD THAT, HERE'S WHAT rebJumps() DOES... ////////=//
//
// rebJumps() is like rebElide(), but has the noreturn attribute.  This helps
// inform the compiler that the routine is not expected to return.  Use it
// with things like `rebJumps("fail", ...)` or `rebJumps("THROW", ...)`.  If
// by some chance the code passed to it does not jump and finishes normally,
// then an error will be raised.
//
// !!! The name is not ideal, but other possibilites aren't great:
//
//    rebDeadEnd(...) -- doesn't sound like it should take arguments
//    rebNoReturn(...) -- whose return?
//    rebStop(...) -- STOP is rather final sounding, the code keeps going
//
void API_rebJumps(
    RebolContext* binding,
    const void* p, void* vaptr
){
    ENTER_API;

    DECLARE_VALUE (dummy);
    Run_Va_Decay_May_Fail_Calls_Va_End(binding, dummy, p, vaptr);

    // Note: If we just `fail()` here, then while MSVC compiles %a-lib.c at
    // higher optimization levels it can conclude that API_rebJumps() never
    // returns.  Then it will give an error on the attempt to put a DEAD_END()
    // notification in the inline wrapper `rebJumps()`, which is needed to
    // suppress the warning when API_rebJumps() isn't available.  This Catch-22
    // of saying the DEAD_END() itself is unreachable code is annoying...but
    // it's best not to turn off the warning.  Throw in a runtime twist that
    // it can't guarantee won't happen (but won't) so it doesn't use special
    // knowledge that API_rebJumps() does not return.
    //
    assert(p != nullptr);
    if (p == nullptr)
        return;

    fail ("rebJumps() ran code, but it didn't FAIL or QUIT or THROW, etc.");
}


//
//  rebDid: API
//
// Simply returns the logical result, with no returned handle to release.
//
// If you know the argument is either NULL or OKAY antiforms, then you can
// use rebUnboxLogic() to get a runtime check of that.  This tests ANY-VALUE?
//
bool API_rebDid(
    RebolContext* binding,
    const void* p, void* vaptr
){
    ENTER_API;

    DECLARE_VALUE (condition);
    Run_Va_Decay_May_Fail_Calls_Va_End(binding, condition, p, vaptr);

    return Is_Trigger(condition);  // will fail() on (most) antiforms
}


//
//  rebNot: API
//
// !!! If this were going to be a macro like (not (rebDid(...))) it
// would have to be a variadic macro.  Not worth it. use separate entry point.
//
bool API_rebNot(
    RebolContext* binding,
    const void* p, void* vaptr
){
    ENTER_API;

    DECLARE_VALUE (condition);
    Run_Va_Decay_May_Fail_Calls_Va_End(binding, condition, p, vaptr);

    return Is_Inhibitor(condition);  // will fail() on (most) antiforms
}


//
//  rebDidnt: API
//
// Synonym for rebNot()
//
bool API_rebDidnt(
    RebolContext* binding,
    const void* p, void* vaptr
){
    ENTER_API;

    DECLARE_VALUE (condition);
    Run_Va_Decay_May_Fail_Calls_Va_End(binding, condition, p, vaptr);

    return Is_Inhibitor(condition);  // will fail() on (most) antiforms
}



//
//  rebUnbox: API
//
// C++, JavaScript, and other languages can do some amount of intelligence
// with a generic `rebUnbox()` operation...either picking the type to return
// based on the target in static typing, or returning a dynamically typed
// value.  For convenience in C, make the generic unbox operation return
// an integer for INTEGER!, LOGIC!, CHAR!...assume it's most common so the
// short name is worth it.
//
intptr_t API_rebUnbox(
    RebolContext* binding,
    const void* p, void* vaptr
){
    ENTER_API;

    DECLARE_VALUE (result);
    Run_Va_Decay_May_Fail_Calls_Va_End(binding, result, p, vaptr);

    if (Is_Logic(result)) {
        return Cell_Logic(result) ? 1 : 0;
    }
    else switch (Type_Of(result)) {
      case TYPE_INTEGER:
        return VAL_INT64(result);

      case TYPE_ISSUE:
        return Cell_Codepoint(result);

      default:
        fail ("C-based rebUnbox() only supports INTEGER!, CHAR!, and LOGIC!");
    }
}


//
//  rebUnboxLogic: API
//
bool API_rebUnboxLogic(
    RebolContext* binding,
    const void* p, void* vaptr
){
    ENTER_API;

    DECLARE_VALUE (result);
    Run_Va_Decay_May_Fail_Calls_Va_End(binding, result, p, vaptr);

    if (not Is_Logic(result))
        fail ("rebUnboxLogic() called on non-LOGIC!");

    return Cell_Logic(result);
}


//
//  rebUnboxBoolean: API
//
bool API_rebUnboxBoolean(
    RebolContext* binding,
    const void* p, void* vaptr
){
    ENTER_API;

    DECLARE_VALUE (result);
    Run_Va_Decay_May_Fail_Calls_Va_End(binding, result, p, vaptr);

    if (not Is_Boolean(result))
        fail ("rebUnboxBoolean() called on non-[true false]!");

    return Cell_True(result);
}


//
//  rebUnboxYesNo: API
//
bool API_rebUnboxYesNo(
    RebolContext* binding,
    const void* p, void* vaptr
){
    ENTER_API;

    DECLARE_VALUE (result);
    Run_Va_Decay_May_Fail_Calls_Va_End(binding, result, p, vaptr);

    if (not Is_YesNo(result))
        fail ("rebUnboxYesNo() called on non-[yes no]!");

    return Cell_Yes(result);
}


//
//  rebUnboxOnOff: API
//
bool API_rebUnboxOnOff(
    RebolContext* binding,
    const void* p, void* vaptr
){
    ENTER_API;

    DECLARE_VALUE (result);
    Run_Va_Decay_May_Fail_Calls_Va_End(binding, result, p, vaptr);

    if (not Is_OnOff(result))
        fail ("rebUnboxOnOff() called on non-[on off]!");

    return Cell_On(result);
}




//
//  rebUnboxInteger: API
//
intptr_t API_rebUnboxInteger(
    RebolContext* binding,
    const void* p, void* vaptr
){
    ENTER_API;

    DECLARE_VALUE (result);
    Run_Va_Decay_May_Fail_Calls_Va_End(binding, result, p, vaptr);

    if (not Is_Integer(result))
        fail ("rebUnboxInteger() called on non-INTEGER!");

    return VAL_INT64(result);
}


//
//  rebUnboxDecimal: API
//
double API_rebUnboxDecimal(
    RebolContext* binding,
    const void* p, void* vaptr
){
    ENTER_API;

    DECLARE_VALUE (result);
    Run_Va_Decay_May_Fail_Calls_Va_End(binding, result, p, vaptr);

    if (Is_Decimal(result))
        return VAL_DECIMAL(result);

    if (Is_Integer(result))
        return cast(double, VAL_INT64(result));

    fail ("rebUnboxDecimal() called on non-DECIMAL! or non-INTEGER!");
}


//
//  rebUnboxChar: API
//
uint32_t API_rebUnboxChar(
    RebolContext* binding,
    const void* p, void* vaptr
){
    ENTER_API;

    DECLARE_VALUE (result);
    Run_Va_Decay_May_Fail_Calls_Va_End(binding, result, p, vaptr);

    if (not IS_CHAR(result))
        fail ("rebUnboxChar() called on non-CHAR");

    return Cell_Codepoint(result);
}


//
//  rebUnboxHandleCData: API
//
void* API_rebUnboxHandleCData(
    RebolContext* binding,
    size_t* size_out,
    const void* p, void* vaptr
){
    ENTER_API_RECYCLING_OK;

    DECLARE_VALUE (v);
    Run_Va_Decay_May_Fail_Calls_Va_End(binding, v, p, vaptr);

    if (Type_Of(v) != TYPE_HANDLE)
        fail ("rebUnboxHandleCData() called on non-HANDLE!");

    if (size_out)
        *size_out = Cell_Handle_Len(v);
    return Cell_Handle_Pointer(void*, v);
}


//
//  rebExtractHandleCleaner: API
//
// May return nullptr.
//
RebolHandleCleaner* API_rebExtractHandleCleaner(
    RebolContext* binding,
    const void* p, void* vaptr
){
    ENTER_API_RECYCLING_OK;

    DECLARE_VALUE (v);
    Run_Va_Decay_May_Fail_Calls_Va_End(binding, v, p, vaptr);

    if (Type_Of(v) != TYPE_HANDLE)
        fail ("rebUnboxHandleCleaner() called on non-HANDLE!");

    Stub* stub = Extract_Cell_Handle_Stub(v);
    return maybe stub->misc.cleaner;
}


// Helper function for `rebSpellInto()` and `rebSpell()`
//
static Size Spell_Into(
    char* buf,
    Size buf_size,  // number of bytes
    const Value* v
){
    if (not Any_Utf8(v))
        fail ("rebSpell() APIs require UTF-8 types (strings, words, tokens)");

    Size bsize = buf_size;  // see `Size`: we use signed sizes internally

    Size utf8_size;
    Utf8(const*) utf8 = Cell_Utf8_Size_At(&utf8_size, v);

    if (not buf) {
        assert(bsize == 0);
        return utf8_size;  // caller must allocate a buffer of size + 1
    }

    Size limit = MIN(bsize, utf8_size);
    memcpy(buf, utf8, limit);
    buf[limit] = 0;
    return utf8_size;
}


//
//  rebSpellInto: API
//
// Extract UTF-8 data from an ANY-STRING? or ANY-WORD?.
//
// API does not return the number of UTF-8 characters for a value, because
// the answer to that is always cached for any value position as LENGTH OF.
// The more immediate quantity of concern to return is the number of bytes.
//
size_t API_rebSpellInto(
    RebolContext* binding,
    char* buf,
    size_t buf_size,  // number of bytes
    const void* p, void* vaptr
){
    ENTER_API;

    DECLARE_VALUE (v);
    Run_Va_Decay_May_Fail_Calls_Va_End(binding, v, p, vaptr);

    return Spell_Into(buf, buf_size, v);
}


//
//  rebSpellMaybe: API
//
// This gives the spelling as UTF-8 bytes.  Length in codepoints should be
// extracted with LENGTH OF.  If size in bytes of the encoded UTF-8 is needed,
// use the rebBytes() extraction API (works on ANY-STRING? to get UTF-8)
//
// Can return nullptr.  Use rebSpell() if you want a failure instead.
//
char* API_rebSpellMaybe(
    RebolContext* binding,
    const void* p, void* vaptr
){
    ENTER_API;

    DECLARE_VALUE (v);
    Run_Va_Decay_May_Fail_Calls_Va_End(binding, v, p, vaptr);

    if (Is_Nulled(v))
        return nullptr;

    size_t size = Spell_Into(nullptr, 0, v);
    char* result = rebAllocN(char, size);  // no +1 for term needed...
    assert(result[size] == '\0');  // ...see rebRepossess() for why this is

    size_t check = Spell_Into(result, size, v);
    assert(check == size);
    UNUSED(check);

    return result;
}

//
//  rebSpell: API
//
// Raises error on NULL input
//
char* API_rebSpell(
    RebolContext* binding,
    const void* p, void* vaptr
){
    char* spell = API_rebSpellMaybe(binding, p, vaptr);
    if (spell == nullptr)
        fail ("rebSpell() does not take NULL, see rebSpellMaybe()");
    return spell;
}


// Helper function for `rebSpellIntoWide()` and `rebSpellWide()`
//
static unsigned int Spell_Into_Wide(
    REBWCHAR* buf,
    unsigned int buf_wchars,  // chars buf can hold (not including terminator)
    const Value* v
){
    if (not Any_Utf8(v))
        fail ("rebSpell() APIs require UTF-8 types (strings, words, tokens)");

    if (not buf)  // querying for size
        assert(buf_wchars == 0);

    unsigned int num_wchars = 0;  // some codepoints need 2 wchars

    Utf8(const*) cp = Cell_Utf8_At(v);

    Codepoint c;
    cp = Utf8_Next(&c, cp);

    REBLEN i = 0;
    while (c != '\0' and i < buf_wchars) {
        if (c <= 0xFFFF) {
            buf[i] = c;
            ++i;
            ++num_wchars;
        }
        else {  // !!! Should there be a UCS-2 version that fails here?
            if (i == buf_wchars - 1)
                break;  // not enough space for surrogate pair

            Encode_UTF16_Pair(c, &buf[i]);
            i += 2;
            num_wchars += 2;
        }
        cp = Utf8_Next(&c, cp);
    }

    if (buf)
        buf[i] = 0;

    while (c != '\0') {  // count residual wchars there was no capacity for
        if (c <= 0xFFFF)
            num_wchars += 1;  // fits in one 16-bit wchar
        else
            num_wchars += 2;  // requires surrogate pair to represent

        cp = Utf8_Next(&c, cp);
    }

    return num_wchars;  // if allocating, caller needs space for num_wchars + 1
}


//
//  rebSpellIntoWide: API
//
// Extract UTF-16 data from an ANY-STRING? or ANY-WORD?.  Note this is *not*
// UCS-2, so codepoints that won't fit in one WCHAR will take up two WCHARs
// by means of a surrogate pair.  Hence the returned value is a count of
// wchar units...not *necesssarily* a length in codepoints.
//
unsigned int API_rebSpellIntoWide(
    RebolContext* binding,
    REBWCHAR* buf,
    unsigned int buf_chars,  // chars buf can hold (not including terminator)
    const void* p, void* vaptr
){
    ENTER_API;

    DECLARE_VALUE (v);
    Run_Va_Decay_May_Fail_Calls_Va_End(binding, v, p, vaptr);

    return Spell_Into_Wide(buf, buf_chars, v);
}


//
//  rebSpellWideMaybe: API
//
// Gives the spelling as WCHARs.  The result is UTF-16, so some codepoints
// won't fit in single WCHARs.
//
REBWCHAR* API_rebSpellWideMaybe(
    RebolContext* binding,
    const void* p, void* vaptr
){
    ENTER_API;

    DECLARE_VALUE (v);
    Run_Va_Decay_May_Fail_Calls_Va_End(binding, v, p, vaptr);

    if (Is_Nulled(v))
        return nullptr;

    REBLEN len = Spell_Into_Wide(nullptr, 0, v);
    REBWCHAR* result = cast(
        REBWCHAR*, rebAllocBytes(sizeof(REBWCHAR) * (len + 1))
    );

    REBLEN check = Spell_Into_Wide(result, len, v);
    assert(check == len);
    UNUSED(check);

    return result;
}


//
//  rebSpellWide: API
//
// Raises error on NULL
//
REBWCHAR* API_rebSpellWide(
    RebolContext* binding,
    const void* p, void* vaptr
){
    REBWCHAR* spelling = API_rebSpellWideMaybe(binding, p, vaptr);
    if (spelling == nullptr)
        fail ("rebSpellWide() does not take NULL, see rebSpellWideMaybe()");
    return spelling;
}


// Helper function for `rebBytesInto()` and `rebBytes()`
//
// CHAR!, ANY-STRING?, and ANY-WORD? are allowed without an AS BLOB!.
//
// !!! How many types should be allowed to convert automatically?
//
static size_t Bytes_Into(
    unsigned char* buf,
    size_t buf_size,
    const Value* v
){
    Size bsize = buf_size;  // see `Size`: we use signed sizes internally

    if (Is_Blob(v)) {
        Size size;
        const Byte* data = Cell_Blob_Size_At(&size, v);
        if (buf == nullptr) {
            assert(bsize == 0);
            return size;
        }

        Size limit = MIN(bsize, size);
        memcpy(buf, data, limit);
        return size;
    }

    if (IS_CHAR(v)) {  // Note: CHAR! caches its UTF-8 encoding in the cell
        Size size = Encoded_Size_For_Codepoint(Cell_Codepoint(v));
        if (buf == nullptr) {
            assert(bsize == 0);
            return size;
        }

        Size limit = MIN(bsize, size);
        if (limit == 0)
            return size;

        if (Is_Blob(v)) {
            assert(Is_NUL(v));
            assert(limit > 0);
            buf[0] = '\0';
            return size;
        }

        assert(Is_Issue(v));
        assert(not Stringlike_Has_Node(v));
        assert(v->extra.at_least_4[IDX_EXTRA_LEN] == 1);

        memcpy(buf, v->payload.at_least_8, limit);  // !!! '\0' term?
        return size;
    }

    if (Any_Word(v) or Any_String(v)) {
        Size size = Spell_Into(nullptr, 0, v);
        if (buf == nullptr) {
            assert(buf_size == 0);
            return size;
        }

        Size check = Spell_Into(s_cast(buf), bsize, v);
        assert(check == size);
        UNUSED(check);

        return size;
    }

    fail ("rebBytes() only works with ANY-STRING?/ANY-WORD?/BLOB!/CHAR!");
}


//
//  rebBytesInto: API
//
// Extract data from a BLOB!
//
// !!! Caller must allocate a buffer of the returned size + 1.  It's not clear
// if this is a good idea; but this is based on a longstanding convention of
// zero termination of Rebol TEXT! and BLOB!.
//
size_t API_rebBytesInto(
    RebolContext* binding,
    unsigned char* buf,
    size_t buf_size,
    const void* p, void* vaptr
){
    ENTER_API;

    DECLARE_VALUE (v);
    Run_Va_Decay_May_Fail_Calls_Va_End(binding, v, p, vaptr);

    return Bytes_Into(buf, buf_size, v);
}


//
//  rebBytesMaybe: API
//
// Can be used to get the bytes of a BLOB! and its size, or the UTF-8
// encoding of an ANY-STRING? or ANY-WORD? and that size in bytes.  (Hence,
// for strings it is like rebSpell() except telling you how many bytes.)
//
unsigned char* API_rebBytesMaybe(
    RebolContext* binding,
    size_t* size_out,  // !!! Enforce non-null, to ensure type safety?
    const void* p, void* vaptr
){
    ENTER_API;

    DECLARE_VALUE (v);
    Run_Va_Decay_May_Fail_Calls_Va_End(binding, v, p, vaptr);

    if (Is_Nulled(v)) {
        *size_out = 0;
        return nullptr;  // blank in, null out
    }

    Size size = Bytes_Into(nullptr, 0, v);

    unsigned char* result = rebAllocN(unsigned char, size);  // no +1 needed...
    assert(result[size] == '\0');  // ...see rebRepossess() for why
    Bytes_Into(result, size, v);

    *size_out = size;
    return cast(unsigned char*, result);
}


//
//  rebBytes: API
//
// Raises error on NULL
//
unsigned char* API_rebBytes(
    RebolContext* binding,
    size_t* size_out,  // !!! Enforce non-null, to ensure type safety?
    const void* p, void* vaptr
){
    unsigned char* bytes = API_rebBytesMaybe(binding, size_out, p, vaptr);
    if (bytes == nullptr)
        fail ("rebBytes() does not take NULL, see rebBytesMaybe()");
    return bytes;
}


//
//  rebRequestHalt: API
//
// This function sets a signal that is checked during evaluation of code
// when it is run interruptibly.  Most API evaluations are not interruptible,
// because that would create unsafe situations.
//
// !!! Halting, exceptions, and stack overflows are all areas where the
// computing world in general doesn't have great answers.  Ren-C is nothing
// special in this regard, and more thought needs to be put into it!
//
void API_rebRequestHalt(void)
{
    ENTER_API_RECYCLING_OK;

    Set_Trampoline_Flag(HALT);
}


//
//  rebWasHaltRequested: API
//
// Returns whether or not the halting signal is set, but clears it if set.
// Hence the question it answers is "was it halting" (previous to this call),
// because it never will be after it.
//
// Hence whoever checks this flag has erased the knowledge of a Ctrl-C signal,
// and bears the burden for propagating the signal up to something that does
// a HALT later--or it will be lost.
//
bool API_rebWasHaltRequested(void)
{
    ENTER_API;

    bool halting = Get_Trampoline_Flag(HALT);
    Clear_Trampoline_Flag(HALT);
    return halting;
}


//=//// API "INSTRUCTIONS" ////////////////////////////////////////////////=//
//
// The evaluator API takes further advantage of Detect_Rebol_Pointer() when
// processing variadic arguments to do things more efficiently.
//
// All instructions must be handed *directly* to an evaluator feed.  That
// feed is what guarantees that if a GC occurs that the variadic will be
// spooled forward and their contents guarded.
//
// NOTE THIS IS NOT LEGAL:
//
//     void *instruction = rebQ("stuff");  // not passed direct to evaluator
//     rebElide("print {Hi!}");  // a RECYCLE could be triggered here
//     rebValue(..., instruction, ...);  // the instruction may be corrupt now!
//
//=////////////////////////////////////////////////////////////////////////=//


//
//  rebQUOTING: API
//
// This is #defined as rebQ.
//
// Note: This arity-1 version is pared back from a more complex variadic form:
// https://forum.rebol.info/t/1050/4
//
const RebolNodeInternal* API_rebQUOTING(const void* p)
{
    ENTER_API;

    if (p == nullptr)
        return c_cast(RebolNodeInternal*, Root_Quasi_Null);

    const Stub* stub;

    switch (Detect_Rebol_Pointer(p)) {
      case DETECTED_AS_STUB: {
        stub = c_cast(Stub*, p);
        if (Not_Flavor_Flag(API, stub, RELEASE))
            fail ("Can't quote instructions (besides rebR())");
        break; }

      case DETECTED_AS_CELL: {
        const Value* at = cast(const Value*, p);
        if (Is_Nulled(at)) {
            assert(not Is_Api_Value(at));  // only internals use nulled cells
            return c_cast(RebolNodeInternal*, Root_Quasi_Null);
        }

        Value* v = Copy_Cell(Alloc_Value(), at);
        stub = Compact_Stub_From_Cell(v);
        Set_Flavor_Flag(API, stub, RELEASE);
        break; }

      default:
        fail ("Unknown pointer");
    }

    Value* v = x_cast(Value*, Stub_Cell(stub));
    Meta_Quotify(v);
    return c_cast(RebolNodeInternal*, stub);  // C needs cast
}


//
//  rebUNQUOTING: API
//
// This is #defined as rebU.
//
// Note: This arity-1 version is pared back from a more complex variadic form:
// https://forum.rebol.info/t/1050/4
//
RebolNodeInternal* API_rebUNQUOTING(const void* p)
{
    ENTER_API;

    if (p == nullptr)
        fail ("Cannot unquote NULL");

    Stub* stub;

    switch (Detect_Rebol_Pointer(p)) {
      case DETECTED_AS_STUB: {
        stub = m_cast(Stub*, c_cast(Stub*, p));
        if (Not_Flavor_Flag(API, stub, RELEASE))
            fail ("Can't unquote instructions (besides rebR())");
        break; }

      case DETECTED_AS_CELL: {
        Value* v = Copy_Cell(Alloc_Value(), c_cast(Value*, p));
        stub = Compact_Stub_From_Cell(v);
        Set_Flavor_Flag(API, stub, RELEASE);
        break; }

      default:
        fail ("Unknown pointer");
    }

    Cell* v = Stub_Cell(stub);
    if (not Is_Quoted(v))
        fail ("rebUNQUOTING()/rebU() can only unquote QUOTED? values");

    Unquotify(cast(Element*, v));
    return cast(RebolNodeInternal*, stub);  // cast needed in C
}


//
//  rebRELEASING: API
//
// Convenience tool for making "auto-release" form of values.  They will only
// exist for one API call.  They will be automatically rebRelease()'d when
// they are seen (or even if they are not seen, if there is a failure on that
// call it will still process the va_list in order to release these handles)
//
RebolNodeInternal* API_rebRELEASING(RebolValue* v)
{
    ENTER_API;

    if (v == nullptr)
        return nullptr;

    if (not Is_Api_Value(v))
        fail ("Cannot apply rebR() to non-API value");

    Stub* stub = Compact_Stub_From_Cell(v);
    if (Get_Flavor_Flag(API, stub, RELEASE))
        fail ("Cannot apply rebR() more than once to the same API value");

    Set_Flavor_Flag(API, stub, RELEASE);
    return cast(RebolNodeInternal*, stub);  // cast needed in C
}


//
//  rebINLINE: API
//
// This will splice a list, single value, or no-op into the execution feed.
//
RebolNodeInternal* API_rebINLINE(const RebolValue* v)
{
    ENTER_API;

    Stub* s = Make_Untracked_Stub(FLAG_FLAVOR(INSTRUCTION_SPLICE));

    if (not (Is_Block(v) or Is_Quoted(v) or Is_Blank(v)))
        fail ("rebINLINE() requires argument to be a BLOCK!/QUOTED?/BLANK!");

    Copy_Cell(Stub_Cell(s), v);

    return cast(RebolNodeInternal*, s);  // cast needed in C
}


//
//  rebRUN: API
//
// If a Value* holds an action, this will convert it to a regular FRAME!
// so that it runs inline.  If it were ^META'd then it would produce a
// quasi-action, that would just evaluate to an antiform.  Something like a
// rebREIFY would also work, but it would not do type checking.
//
RebolNodeInternal* API_rebRUN(const void* p)
{
    ENTER_API;

    if (p == nullptr)
        fail ("rebRUN() received nullptr");

    Stub* stub;

    switch (Detect_Rebol_Pointer(p)) {
      case DETECTED_AS_STUB: {
        stub = m_cast(Stub*, c_cast(Stub*, p));
        if (Not_Flavor_Flag(API, stub, RELEASE))
            fail ("Can't quote instructions (besides rebR())");
        break; }

      case DETECTED_AS_CELL: {
        const Value* at = cast(const Value*, p);
        if (Is_Nulled(at))
            fail ("rebRUN() received null cell");

        Value* v = Copy_Cell(Alloc_Value(), at);
        stub = Compact_Stub_From_Cell(v);
        Set_Flavor_Flag(API, stub, RELEASE);
        break; }

      default:
        fail ("Unknown pointer");
    }

    Value* v = Stub_Cell(stub);
    if (Is_Action(v))
        QUOTE_BYTE(v) = NOQUOTE_1;
    else if (not Is_Frame(v))
        fail ("rebRUN() requires FRAME! or actions (aka FRAME! antiforms)");

    return cast(RebolNodeInternal*, stub);  // cast needed in C
}



//
//  rebManage: API
//
// The "friendliest" default for the API is to assume you want handles to be
// tied to the lifetime of the frame they're in.  Long-running top-level
// processes like the C code running the console would eventually exhaust
// memory if that were the case...so there should be some options for metrics
// as a form of "leak detection" even so.
//
RebolValue* API_rebManage(RebolValue* v)
{
    ENTER_API;

    assert(Is_Api_Value(v));

    Stub* stub = Compact_Stub_From_Cell(v);
    assert(Is_Node_Root_Bit_Set(stub));

    if (Is_Node_Managed(stub))
        fail ("Attempt to rebManage() an API value that's already managed.");

    Set_Node_Managed_Bit(stub);
    Connect_Api_Handle_To_Level(stub, TOP_LEVEL);

    return v;
}


//
//  rebUnmanage: API
//
// This converts an API handle value to indefinite lifetime.
//
void API_rebUnmanage(void *p)
{
    ENTER_API;

    Node* n = cast(Node*, p);
    if (Is_Node_A_Stub(n))
        fail ("rebUnmanage() not yet implemented for rebAlloc() data");

    Value* v = cast(Value*, n);
    assert(Is_Api_Value(v));

    Stub* stub = Compact_Stub_From_Cell(v);
    assert(Is_Node_Root_Bit_Set(stub));

    if (Not_Node_Managed(stub))
        fail ("Attempt to rebUnmanage() API value with indefinite lifetime.");

    // It's not safe to convert the average Flex that might be referred to
    // from managed to unmanaged, because you don't know how many references
    // might be in Cells.  But the singular array holding API handles has
    // pointers to its cell being held by client C code only.  It's at their
    // own risk to do this, and not use those pointers after a free.
    //
    Clear_Node_Managed_Bit(stub);
    Disconnect_Api_Handle_From_Level(stub);
}


//
//  rebRelease: API
//
// An API handle is only 4 platform pointers in size (plus some bookkeeping),
// but it still takes up some storage.  The intended default for API handles
// is that they live as long as the function frame they belong to, but there
// will be several lifetime management tricks to ease releasing them.
//
// !!! For the time being, we lean heavily on explicit release.  Near term
// leak avoidance will need to at least allow for GC of handles across errors
// for their associated frames.
//
void API_rebRelease(const RebolValue* v)
{
    ENTER_API_RECYCLING_OK;  // !!! Needs bulletproofing, but needs to work

    if (not v)
        return;  // less rigorous, but makes life easier for C programmers

    if (not Is_Api_Value(v))
        panic ("Attempt to rebRelease() a non-API handle");

    Free_Value(m_cast(Value*, v));
}


//
//  rebZdeflateAlloc: API
//
// Variant of rebDeflateAlloc() which adds a zlib envelope...which is a 2-byte
// header and 32-bit ADLER32 CRC at the tail.
//
// !!! TBD: Clients should be able to use a plain Rebol call to ZDEFLATE and
// be able to get the data back using something like rebRepossess.  That
// would eliminate this API.
//
void* API_rebZdeflateAlloc(
    size_t* out_len,
    const void* input,
    size_t in_len
){
    ENTER_API;

    Size len;  // see `Size`: we use signed sizes internally
    void* deflated = Compress_Alloc_Core(&len, input, in_len, SYM_ZLIB);
    assert(len > 0);
    *out_len = len;
    return deflated;
}


//
//  rebZinflateAlloc: API
//
// Variant of rebInflateAlloc() which assumes a zlib envelope...checking for
// the 2-byte header and verifying the 32-bit ADLER32 CRC at the tail.
//
// !!! TBD: Clients should be able to use a plain Rebol call to ZINFLATE and
// be able to get the data back using something like rebRepossess.  That
// would eliminate this API.
//
void *API_rebZinflateAlloc(
    size_t* len_out,
    const void* input,
    size_t len_in,
    int max
){
    ENTER_API;

    Size len;  // see `Size`: we use signed sizes internally
    void* inflated = Decompress_Alloc_Core(&len, input, len_in, max, SYM_ZLIB);
    assert(len >= 0);
    *len_out = len;
    return inflated;
}


// !!! Although it is very much the goal to get all OS-specific code out of
// the core (including the API), this particular hook is extremely useful to
// have available to all clients.  It might be done another way (e.g. by
// having hosts HIJACK the FAIL native with an adaptation that processes
// integer arguments).  But for now, stick it in the API just to get the
// wide availability.
//
#if TO_WINDOWS
    #define WIN32_LEAN_AND_MEAN  // trim down the Win32 headers
    #include <windows.h>
#else
    #include <errno.h>
    #define MAX_POSIX_ERROR_LEN 1024
#endif

//
//  rebError_OS: API
//
// Produce an error from an OS error code, by asking the OS for textual
// information it knows internally from its database of error strings.
//
// Note that error codes coming from WSAGetLastError are the same as codes
// coming from GetLastError in 32-bit and above Windows:
//
// https://stackoverflow.com/q/15586224/
//
// !!! Should not be in core, but extensions need a way to trigger the
// common functionality one way or another.
//
RebolValue* API_rebError_OS(int errnum)  // see also macro rebFail_OS()
{
    ENTER_API;

    Error* error;

  #if TO_WINDOWS
    if (errnum == 0)
        errnum = GetLastError();

    WCHAR* lpMsgBuf;  // FormatMessage writes allocated buffer address here

    // Specific errors have %1 %2 slots, and if you know the error ID and
    // that it's one of those then this lets you pass arguments to fill
    // those in.  But since this is a generic error, we have no more
    // parameterization (hence FORMAT_MESSAGE_IGNORE_INSERTS)
    //
    va_list* Arguments = nullptr;

    // Apparently FormatMessage can find its error strings in a variety of
    // DLLs, but we don't have any context here so just use the default.
    //
    LPCVOID lpSource = nullptr;

    DWORD ok = FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER // see lpMsgBuf
            | FORMAT_MESSAGE_FROM_SYSTEM // e.g. ignore lpSource
            | FORMAT_MESSAGE_IGNORE_INSERTS, // see Arguments
        lpSource,
        errnum, // message identifier
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // default language
        cast(WCHAR*, &lpMsgBuf), // allocated buffer address written here
        0, // buffer size (not used since FORMAT_MESSAGE_ALLOCATE_BUFFER)
        Arguments
    );

    if (ok == 0) {
        //
        // Might want to show the value of GetLastError() in this message,
        // but trying to FormatMessage() on *that* would be excessive.
        //
        error = Error_User("FormatMessage() gave no error description");
    }
    else {
        Value* message = rebTextWide(lpMsgBuf);
        LocalFree(lpMsgBuf);

        error = Make_Error_Managed(SYM_0, SYM_0, message, rebEND);
        rebRelease(message);
    }
  #elif defined(USE_STRERROR_NOT_STRERROR_R)
    char* shared = strerror(errnum);  // not thread safe, deprecated
    error = Error_User(shared);
  #else
    // strerror() is not thread-safe, but strerror_r is. Unfortunately, at
    // least in glibc, there are two different protocols for strerror_r(),
    // depending on whether you are using the POSIX-compliant implementation
    // or the GNU implementation.
    //
    // It was once possible to tell the difference between which protocol
    // you were using based on this test:
    //
    //   The XSI-compliant version of strerror_r() is provided if:
    //   (_POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600) && ! _GNU_SOURCE
    //   Otherwise, the GNU-specific version is provided.
    //
    // Sadly, in GCC 9.3.0 using C99, _GNU_SOURCE is defined but the POSIX
    // definition (int returning) is in effect.  Other libraries like musl
    // seem to get this #define tapdance wrong as well.
    //
    // There are many attempted workarounds on the Internet (trying to use the
    // lower-level `sys_errlist` directly--which may not include all errors,
    // or using function overloading that only works on C++).  This takes a
    // different tactic in pure C by capturing either result cast to intptr_t.
    //
    // 1. Use old-style parentheses cast to get past ambiguity of whether the
    //    strerr_r function returns a char* or an int.  (The "casts for the
    //    masses) casts like c_cast/x_cast etc. don't support this scenario.)

    char buf[MAX_POSIX_ERROR_LEN];
    buf[0] = cast(char, 255);  // never valid in UTF-8 sequences
    int old_errno = errno;
    intptr_t r = (intptr_t)strerror_r(errnum, buf, MAX_POSIX_ERROR_LEN);  // [1]

    // !!! TCC appears to use the `int` returning form of strerror_r().  But
    // it appears to return a random positive or negative value.  It simply
    // appears to be broken.  More research would be needed, but we can just
    // give up an go with strerror.  Leaving in the call to strerror_r() to
    // show that it's there...and it links in TCC.
    //
  #if defined(__TINYC__)
    r = (intptr_t)strerror(errnum); // [1] for why old-style cast used
  #endif

    int new_errno = errno;

    if (r == -1 or new_errno != old_errno) {
        //
        // errno was changed, so probably the return value is just -1 or
        // something else that doesn't provide info, and errno is the error.
        //
        assert(false);
        error = Error_User("Error during strerror_r call");  // w/new_errno?
    }
    else if (r == 0) {
        //
        // Quoting glibc's strerror_r manpage: "The XSI-compliant strerror_r()
        // function returns 0 on success. On error, a (positive) error number
        // is returned (since glibc 2.13), or -1 is returned and errno is set
        // to indicate the error (glibc versions before 2.13)."  GNU version
        // always succeds and should never return 0 (a null char*).
        //
        // Documentation isn't clear on whether the buffer is terminated if
        // the message is too long, or ERANGE always returned.  Terminate.
        //
        buf[MAX_POSIX_ERROR_LEN - 1] = '\0';
        error = Error_User(buf);
    }
    else if (r == EINVAL)  // documented result from POSIX strerror_r
        error = Error_User("EINVAL: bad errno passed to strerror_r()");
    else if (r == ERANGE)  // documented result from POSIX strerror_r
        error = Error_User("ERANGE: insufficient buffer size for error");
    else if (r == i_cast(intptr_t, buf)) {
        //
        // The POSIX version gives us our error back as a pointer if it
        // filled the buffer successfully.  Sanity check that's what happened.
        //
        if (buf[0] == cast(char, 255)) {
            assert(false);
            error = Error_User("Buffer not correctly updated by strerror_r");
        }
        else
            error = Error_User(buf);
    }
    else if (  // small + or - numbers very unlikely to be string buffer
        (r > 0 and r < 256)
        or (r < 0 and - r < 256)
    ){
        assert(false);
        error = Error_User("Unknown POSIX strerror_r error result code");
    }
    else {
        // The GNU version never fails, but may return an immutable string
        // instead of filling the buffer. Unknown errors get an
        // "unknown error" message.  The result is always null terminated.
        //
        // (This is the risky part, if `r` is not a valid pointer but some
        // weird large int return result from POSIX strerror_r.)
        //
        error = Error_User(p_cast(const char*, r));
    }
  #endif

    return Init_Error(Alloc_Value(), error);
}


//
//  /api-transient: native [
//
//  "Produce an API handle pointer (returned via INTEGER!) for a value"
//
//      return: "Heap address of the autoreleasing (rebR()) API handle"
//          [integer!]
//      value [any-value?]
//  ]
//
DECLARE_NATIVE(API_TRANSIENT)
{
    INCLUDE_PARAMS_OF_API_TRANSIENT;

    Value* v = Copy_Cell(Alloc_Value(), ARG(VALUE));
    rebUnmanage(v);  // has to survive the API-TRANSIENT's frame
    Stub* stub = Compact_Stub_From_Cell(v);
    Set_Flavor_Flag(API, stub, RELEASE);

    // Regarding adddresses in WASM:
    //
    // "In wasm32, address operands and offset attributes have type i32"
    // "In wasm64, address operands and offsets have type i64"
    //
    // "Note that the value types i32 and i64 are not inherently signed or
    //  unsigned. The interpretation of these types is determined by
    //  individual operators."
    //
    // :-/  Well, which is it?  R3-Alpha integers were signed 64-bit, Ren-C is
    // targeting arbitrary precision...use signed as status quo for now.
    //
    return Init_Integer(level_->out, i_cast(intptr_t, stub));
}


//
//  Api_Function_Dispatcher: C
//
// Puts a definitional return ACTION! in the RETURN slot of the frame, and
// runs the CFunction associated with this action.
//
Bounce Api_Function_Dispatcher(Level* const L)
{
    Details* details = Ensure_Level_Details(L);

    enum {
        ST_API_FUNC_INITIAL_ENTRY = STATE_0,
        ST_API_FUNC_CONTINUING,
        ST_API_FUNC_DELEGATING
    };

    switch (LEVEL_STATE_BYTE(L)) {
      case ST_API_FUNC_INITIAL_ENTRY:
        goto initial_entry;

      case ST_API_FUNC_CONTINUING:  // the CFunc wants to get called again
        goto run_cfunction;

      case ST_API_FUNC_DELEGATING:  // doesn't want callback, but typecheck
        goto typecheck_out;

      default:
        assert(false);
    }

  initial_entry: { ///////////////////////////////////////////////////////////

    // 1. The Level L gives us the visibility of variables for function args,
    //    but we need to see more than that to run code.  When rebFunction()
    //    was run there was a context in effect.  It's in a dummy BLOCK!
    //    so that the GC will keep it alive (if we poked the context into a
    //    HANDLE! it would not be GC marked).  Make the context passed to the
    //    CFunction inherit that original environment.
    //
    // 2. If you use rebFunction(), we still put a RETURN in the frame.  It may
    //    seem like that's not useful, because while a C function is on the
    //    stack we cannot RETURN across it.  *BUT* it is possible for the C
    //    function to return a continuation of code.  When that happens, the C
    //    function is off the stack and the return can be acted upon.

    Force_Level_Varlist_Managed(L);  // may or may not be managed

    Value* holder = Details_At(details, IDX_API_ACTION_BINDING_BLOCK);

    Add_Link_Inherit_Bind(L->varlist, Cell_List_Binding(holder));  // [1]

    Inject_Definitional_Returner(L, LIB(DEFINITIONAL_RETURN), SYM_RETURN);

    goto run_cfunction;

} run_cfunction: { ///////////////////////////////////////////////////////////

    // 1. RebolContext accepts an Array* in the C++ build, but not the C build.
    //    So the cast is needed here.
    //
    // 2. As of yet, no API functions have been exported which return an
    //    unstable Atom directly.  If it did, it would have to return it as
    //    a RebolBounce* not a Value*.  There's no particular reason why
    //    we couldn't offer a `rebPack()` function that did give back a
    //    pack, solely intended to use in the form `return rebPack(...)`,
    //    but it hasn't yet happened...because even if it returned a Bounce
    //    it would backed by an API cell form holding an unstable value,
    //    which is currently not legal.  Some rules and tightening would
    //    be needed, so for now we do `rebContinue("pack [...]")`
    //
    // 3. While it might seem more obvious for `return "some string"` to
    //    give back a text string, it's actually far more useful to run
    //    UTF-8 returns as delegated code:
    //
    //    https://forum.rebol.info/t/returning-a-string-from-a-native/2357

    assert(Is_Node_Managed(L->varlist));
    assert(Link_Inherit_Bind(L->varlist));  // must inherit from something (?)

    RebolContext* context = cast(RebolContext*, L->varlist);  // [1]

    Value* cfunc_handle = Details_At(details, IDX_API_ACTION_CFUNC);
    RebolActionCFunction* cfunc = cast(RebolActionCFunction*,
        Cell_Handle_Cfunc(cfunc_handle)
    );

    Bounce bounce = cast(Bounce, (*cfunc)(context));

    if (Is_Bounce_An_Atom(bounce)) {
        Value* result = cast(Value*, Atom_From_Bounce(bounce));
        Assert_Cell_Stable(result);  // can't make unstable directly [2]
        assert(Is_Api_Value(result));  // rebArg(), other violators?
        Copy_Cell(L->out, result);
        rebRelease(result);
        goto typecheck_out;
    }

    if (bounce == BOUNCE_DELEGATE) {  // still need to type check
        LEVEL_STATE_BYTE(L) = ST_API_FUNC_DELEGATING;
        return BOUNCE_CONTINUE;
    }

    if (bounce == BOUNCE_CONTINUE) {  // wants callback after execution
        LEVEL_STATE_BYTE(L) = ST_API_FUNC_CONTINUING;
        return BOUNCE_CONTINUE;
    }

    PointerDetect detect = Detect_Rebol_Pointer(bounce);

    if (detect == DETECTED_AS_UTF8) {  // runs code! [3]
        if (FIRST_BYTE(bounce) == '~' and SECOND_BYTE(bounce) == '\0') {
            Init_Nothing(L->out);
            goto typecheck_out;  // make return "~" fast!
        }
        // ...could do other optimizations here...
        LEVEL_STATE_BYTE(L) = ST_API_FUNC_DELEGATING;
        return rebDelegateCore(context, cast(const char*, bounce));
    }

    return Native_Fail_Result(
        L,
        Derive_Error_From_Pointer(
            "Bad RebolBounce return in rebFunction() C implementation"
        )
    );

} typecheck_out: { ///////////////////////////////////////////////////////////

    const Element* param = Quoted_Returner_Of_Paramlist(
        Phase_Paramlist(details), SYM_RETURN
    );

    if (not Typecheck_Coerce_Return_Uses_Spare_And_Scratch(L, param, L->out))
        fail (Error_Bad_Return_Type(L, L->out));

    return L->out;
}}


//
//  Api_Function_Details_Querier: C
//
bool Api_Function_Details_Querier(
    Sink(Value) out,
    Details* details,
    SymId property
){
    assert(Details_Dispatcher(details) == &Api_Function_Dispatcher);
    assert(Details_Max(details) == MAX_IDX_API_ACTION);

    switch (property) {
      case SYM_RETURN_OF: {
        ParamList* paramlist = Phase_Paramlist(details);
        Extract_Paramlist_Returner(out, paramlist, SYM_RETURN);
        return true; }

      default:
        break;
    }

    return false;
}


//
//  rebFunctionFlipped: API
//
// This version of rebFunction() has its arguments "flipped" (in the Haskell
// sense of "flip")...it takes the CFunction first, then the spec:
//
//    Value* a = rebFunction("[-{I'm a Spec}- arg [text!]", &F_Impl);
//
//    Value* b = rebFlipFunction(&F_Impl, "[-{I'm A Spec}- arg [text!]]");
//
// The reason for the existence of the flipped form is that it is variadic,
// so you can make the spec from composed values:
//
//    Value* description = rebValue("-{I'm A Spec}-");
//
//    Value* c = rebFlipFunction(&F_Impl, "[", description, "arg [text!]]");
//
// 1. Due to technical limitations of the variadic machinery, the C function
//    pointer can't be the last item in the va_list--it has to be the first
//    in order to get typechecking in C.
//
// 2. Can't leave the spec unbound, or it wouldn't find definitions like
//    INTEGER! to use in typechecking.  (This probably should be some kind of
//    one-off module, because we don't want code executed in the spec to
//    write to lib by default.)
//
RebolValue* API_rebFunctionFlipped(
    RebolContext* binding,
    RebolActionCFunction* cfunc,  // for typechecking, must be first [1]
    const void* p, void* vaptr
){
    Feed* feed = Make_Variadic_Feed(
        p, cast(va_list*, vaptr),
        FEED_MASK_DEFAULT
    );
    Add_Feed_Reference(feed);
    Sync_Feed_At_Cell_Or_End_May_Fail(feed);

    DECLARE_ELEMENT (spec);

    if (Is_Feed_At_End(feed)) {  // act like `func [] [...]`
        Init_Block(spec, EMPTY_ARRAY);
    }
    else {
        Copy_Cell(spec, At_Feed(feed));
        Fetch_Next_In_Feed(feed);

        if (Not_Feed_At_End(feed) or not Is_Block(spec))
            fail ("rebFunc() expects either no spec, or just one BLOCK!");
    }

    Release_Feed(feed);  // Note: exhausting feed takes care of the va_end()

    if (binding)
        Tweak_Cell_Binding(spec, cast(Context*, binding));  // [2]
    else
        Tweak_Cell_Binding(spec, g_lib_context);  // !!! needs module isolation

    VarList* adjunct;
    ParamList* paramlist = Make_Paramlist_Managed_May_Fail(
        &adjunct,
        spec,
        MKF_MASK_NONE,
        SYM_RETURN  // has return for type checking and continuation use
    );

    Details* details = Make_Dispatch_Details(
        DETAILS_FLAG_OWNS_PARAMLIST |
            DETAILS_FLAG_API_CONTINUATIONS_OK,
        Phase_Archetype(paramlist),
        &Api_Function_Dispatcher,
        MAX_IDX_API_ACTION
    );

    Init_Handle_Cfunc(
        Details_At(details, IDX_API_ACTION_CFUNC),
        cast(CFunction*, cfunc)
    );
    Value* holder = Details_At(details, IDX_API_ACTION_BINDING_BLOCK);
    Init_Block(holder, EMPTY_ARRAY);  // only care about binding GC safety
    Tweak_Cell_Binding(holder, Cell_Binding(spec));

    assert(Misc_Phase_Adjunct(details) == nullptr);
    Tweak_Misc_Phase_Adjunct(details, adjunct);

    return Init_Action(Alloc_Value(), details, ANONYMOUS, UNBOUND);
}


//
//  rebFunction: API
//
// Version of rebFunc() that isn't variadic, but takes the spec as a single
// item (UTF-8, Value*, Instruction*).  This way it can swap the order of
// the parameters so that the CFunction is in the final spot and still
// gets typechecked.  This can give a more pleasing ordering in some
// situations (e.g. C++ lambdas as second arg).
//
// 1. By design, we cannot pass void* to the variadic API (this helps to
//    prevent accidents, as that would accept any random pointer type you
//    happened to have around).  But there is no "base type" besides void*
//    that can have char* alignment and be checked by to_rebarg().  Easiest
//    thing to do is to bypass the variadic packing code checks by just
//    building a pack ourselves.
//
RebolValue* API_rebFunction(
    RebolContext* binding,
    const void* spec,
    RebolActionCFunction* cfunc
){
    const void* packed[2];  // manually pack to subvert void* check in APIs [1]
    packed[0] = spec;
    packed[1] = rebEND;

    void* vaptr = nullptr;  // packed form C++ API clients use, so no vaptr

    return API_rebFunctionFlipped(binding, cfunc, packed, vaptr);
}


static void Fail_If_Top_Level_Not_Continuable() {
    if (
        TOP_LEVEL->executor != &Action_Executor
        or not Is_Stub_Details(Level_Phase(TOP_LEVEL))
        or Not_Details_Flag(
            cast(Details*, Level_Phase(TOP_LEVEL)),
            API_CONTINUATIONS_OK
        )
    ){
        fail ("Can't Delegate/Continue unless inside API-ready function call");
    }
}


//
//  rebDelegate: API
//
// This lets a native delegate its response to some code, which is run through
// the trampoline.  It can only be used as `return rebDelegate(...)`.
//
// While the C implementation function is removed from the C stack (due to
// executing the `return`) an interpreter stack level for the native remains
// in effect, so it will show up in stack traces if there's an error.
//
// rebDelegate() can be used to work around the inability of Value* to store
// unstable isotopes.  So if a native based on the librebol API wants to
// return something like a raised error or a pack, it must use rebDelegate().
//
// It's also the right way to perform an abrupt failure, by doing a call
// to `rebDelegate("fail" ...)`.  Otherwise, exceptions or longjmp() have
// to dangerously cross arbitrary C stack levels of user code that may not
// be designed for it (e.g. if the interpreter was built with longjmp() and
// the API client uses C++ code, things like destructors won't be run.)
//
RebolBounce API_rebDelegate(
    RebolContext* binding,
    const void* p, void* vaptr
){
    ENTER_API;

    Fail_If_Top_Level_Not_Continuable();

    API_rebPushContinuation_internal(
        binding,
        cast(Value*, TOP_LEVEL->out),
        LEVEL_FLAG_RAISED_RESULT_OK,  // definitional error if raised
        p, vaptr
    );
    return BOUNCE_DELEGATE;
}


//
//  rebContinue: API
//
// 1. Typically internal natives use the LEVEL_STATE_BYTE() to track what
//    mode of a continuation they are in.  But actions made by rebFunction()
//    don't speak directly in terms of their "level", and also can't receive
//    a value in OUT or SPARE as the result of a continuation.  So they should
//    track their state in a local variable, and also the code they pass to
//    the continuation should write to some other local variable or argument
//    to get the result, e.g.
//
//         return rebContinue(
//             "local-state: 'evaluating",
//             "local-result: eval", block
//         );
//
RebolBounce API_rebContinue(
    RebolContext* binding,
    const void* p, void* vaptr
){
    ENTER_API;

    Fail_If_Top_Level_Not_Continuable();

    API_rebPushContinuation_internal(
        binding,
        cast(Value*, TOP_LEVEL->out),  // rebFunction() also won't see result
        LEVEL_FLAG_UNINTERRUPTIBLE,  // default, see rebContinueInterruptbile()
        p, vaptr
    );
    return BOUNCE_CONTINUE;
}


//
//  rebContinueInterruptible: API
//
// If you want an interruptible continuation,
//
RebolBounce API_rebContinueInterruptible(
    RebolContext* binding,
    const void* p, void* vaptr
){
    ENTER_API;

    Fail_If_Top_Level_Not_Continuable();

    API_rebPushContinuation_internal(
        binding,
        cast(Value*, TOP_LEVEL->out),  // rebFunction() also won't see result
        LEVEL_MASK_NONE,  // will inherit interruptibility of parent.
        p, vaptr
    );
    Clear_Level_Flag(TOP_LEVEL, UNINTERRUPTIBLE);
    return BOUNCE_CONTINUE;
}


//
//  rebCollateExtension_internal: API
//
// This routine gathers information which can be called to bring an extension
// to life.  It does not itself decompress any of the data it is given, or run
// any startup code.  This allows extensions which are built into an
// executable to do deferred loading.
//
// !!! For starters, this just returns a block of the values...but this is
// the same array that would be used as the Phase_Details() of an action.  So
// it could return a generator ACTION!.
//
// !!! It may be desirable to separate out the module header and go ahead and
// get that loaded as part of this process, in order to allow queries of the
// dependencies and other information.  That might suggest returning a block
// with an OBJECT! header and an ACTION! to run to do the load?  Or maybe
// a HANDLE! which can be passed as a module body with a spec?
//
// !!! If a DLL gets loaded, it's possible these pointers could be unloaded
// if the information were not used immediately or it otherwise was not run.
// This has to be considered in the unloading mechanics.
//
// 1. The proto parser chokes on `void (**cfuncs)(void)`.  We would have to
//    fix that, or create some typedef that's available in the API to
//    clients who don't include %c-enhanced.h.  This is all due to the fact
//    that the cfuncs can be Dispatcher* or RebolActionCFunction*  :-(
//
RebolValue* API_rebCollateExtension_internal(
    RebolContext** binding_ref,  // initialized to module when loaded
    bool use_librebol,
    const unsigned char* script_compressed,
    size_t script_compressed_size,
    int script_num_codepoints,
    void* cfuncs,  // should be CFunction** [1]
    int cfuncs_len
){
    Source* a = Make_Source(MAX_COLLATOR + 1);  // details
    Set_Flex_Len(a, MAX_COLLATOR + 1);

    Init_Handle_Cdata(
        Array_At(a, COLLATOR_BINDING_REF),
        binding_ref,
        1
    );
    Init_Handle_Cdata(
        Array_At(a, COLLATOR_SCRIPT),
        m_cast(Byte*, script_compressed),  // !!! by contract, don't change!
        script_compressed_size
    );
    Init_Integer(
        Array_At(a, COLLATOR_SCRIPT_NUM_CODEPOINTS),
        script_num_codepoints
    );
    Element* cfuncs_handle = Init_Handle_Cdata(
        Array_At(a, COLLATOR_CFUNCS),
        cfuncs,
        cfuncs_len
    );
    if (use_librebol)
        Set_Cell_Flag(cfuncs_handle, CFUNCS_NOTE_USE_LIBREBOL);

    return Init_Block(Alloc_Value(), a);
}
