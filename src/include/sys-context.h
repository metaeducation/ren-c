//
//  File: %sys-context.h
//  Summary: {context! defs AFTER %tmp-internals.h (see: %sys-context.h)}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Rebol Open Source Contributors
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
// In Rebol terminology, a "context" is an abstraction which gives two
// parallel arrays, whose indices line up in a correspondence:
//
// * "keylist" - an array that contains TYPESET! values, but which have a
//   symbol ID encoded as an extra piece of information for that key.
//
// * "varlist" - an array of equal length to the keylist, which holds an
//   arbitrary Value in each position that corresponds to its key.
//
// Contexts coordinate with words, which can have their VAL_WORD_CONTEXT()
// set to a context's series pointer.  Then they cache the index of that
// word's symbol in the context's keylist, for a fast lookup to get to the
// corresponding var.  The key is a typeset which has several flags
// controlling behaviors like whether the var is protected or hidden.
//
// !!! This "caching" mechanism is not actually "just a cache".  Once bound
// the index is treated as permanent.  This is why objects are "append only"
// because disruption of the index numbers would break the extant words
// with index numbers to that position.  Ren-C might wind up undoing this by
// paying for the check of the symbol number at the time of lookup, and if
// it does not match consider it a cache miss and re-lookup...adjusting the
// index inside of the word.  For efficiency, some objects could be marked
// as not having this property, but it may be just as efficient to check
// the symbol match as that bit.
//
// Frame key/var indices start at one, and they leave two cell slots open
// in the 0 spot for other uses.  With an ANY-CONTEXT!, the use for the
// "ROOTVAR" is to store a canon value image of the ANY-CONTEXT!'s cell
// itself.  This trick allows a single VarList* to be passed around rather
// than the cell struct which is 4x larger, yet still reconstitute the
// entire cell if it is needed.
//

#ifdef NDEBUG
    #define ASSERT_CONTEXT(c) cast(void, 0)
#else
    #define ASSERT_CONTEXT(c) Assert_Context_Core(c)
#endif

INLINE Array* Varlist_Array(VarList* c) {
    assert(Get_Array_Flag(c, IS_VARLIST));
    return cast(Array*, c);
}


// There may not be any dynamic or stack allocation available for a stack
// allocated context, and in that case it will have to come out of the
// Stub node data itself.
//
INLINE Value* Varlist_Archetype(VarList* c) {
    Array* varlist = Varlist_Array(c);
    if (not Is_Flex_Dynamic(varlist))
        return cast(Value*, &varlist->content.fixed);

    // If a context has its data freed, it must be converted into non-dynamic
    // form if it wasn't already (e.g. if it wasn't a FRAME!)
    //
    assert(Not_Flex_Info(varlist, INACCESSIBLE));
    return cast(Value*, varlist->content.dynamic.data);
}

// Keylist_Of_Varlist is called often, and it's worth it to make it as fast as
// possible--even in an unoptimized build.
//
INLINE Array* Keylist_Of_Varlist(VarList* c) {
    if (Is_Node_A_Stub(LINK(c).keysource))
        return cast_Array(LINK(c).keysource);  // not a Level, so use keylist

    // If the context in question is a FRAME! value, then the ->phase
    // of the frame presents the "view" of which keys should be visible at
    // this phase.  So if the phase is a specialization, then it should
    // not show all the underlying function's keys...just the ones that
    // are not hidden in the facade that specialization uses.  Since the
    // phase changes, a fixed value can't be put into the keylist...that is
    // just the keylist of the underlying function.
    //
    Value* archetype = Varlist_Archetype(c);
    assert(VAL_TYPE_RAW(archetype) == REB_FRAME);
    return ACT_PARAMLIST(archetype->payload.any_context.phase);
}

INLINE void Tweak_Keylist_Of_Varlist_Shared(VarList* c, Array* keylist) {
    Set_Flex_Info(keylist, SHARED_KEYLIST);
    LINK(c).keysource = keylist;
}

INLINE void Tweak_Keylist_Of_Varlist_Unique(VarList* c, Array* keylist) {
    assert(Not_Flex_Info(keylist, SHARED_KEYLIST));
    LINK(c).keysource = keylist;
}

// Navigate from context to context components.  Note that the context's
// "length" does not count the [0] cell of either the varlist or the keylist.
// Hence it must subtract 1.  Internally to the context building code, the
// real length of the two series must be accounted for...so the 1 gets put
// back in, but most clients are only interested in the number of keys/values
// (and getting an answer for the length back that was the same as the length
// requested in context creation).
//
#define Varlist_Len(c) \
    (cast(Flex*, (c))->content.dynamic.len - 1) // len > 1 => dynamic

#define CTX_ROOTKEY(c) \
    cast(Value*, Keylist_Of_Varlist(c)->content.dynamic.data) // len > 1

#define CTX_TYPE(c) \
    VAL_TYPE(Varlist_Archetype(c))

// The keys and vars are accessed by positive integers starting at 1
//
#define Varlist_Keys_Head(c) \
    Flex_At(Value, Keylist_Of_Varlist(c), 1)  // always "specific"

INLINE Level* Level_Of_Varlist_If_Running(VarList* c) {
    Node* keysource = LINK(c).keysource;
    if (Is_Node_A_Stub(keysource))
        return nullptr; // e.g. came from MAKE FRAME! or Encloser_Dispatcher

    assert(Not_Flex_Info(Varlist_Array(c), INACCESSIBLE));
    assert(Is_Frame(Varlist_Archetype(c)));

    Level* L = LVL(keysource);
    assert(L->original); // inline Is_Action_Level() to break dependency
    return L;
}

INLINE Level* Level_Of_Varlist_May_Fail(VarList* c) {
    Level* L = Level_Of_Varlist_If_Running(c);
    if (not L)
        fail (Error_Frame_Not_On_Stack_Raw());
    return L;
}

#define Varlist_Slots_Head(c) \
    Flex_At(Value, Varlist_Array(c), 1)  // may fail() if inaccessible

INLINE Value* Varlist_Key(VarList* c, REBLEN n) {
    assert(Not_Flex_Info(c, INACCESSIBLE));
    assert(Get_Array_Flag(c, IS_VARLIST));
    assert(n != 0 and n <= Varlist_Len(c));
    return cast(Value*, cast(Flex*, Keylist_Of_Varlist(c))->content.dynamic.data)
        + n;
}

INLINE Value* Varlist_Slot(VarList* c, REBLEN n) {
    assert(Not_Flex_Info(c, INACCESSIBLE));
    assert(Get_Array_Flag(c, IS_VARLIST));
    assert(n != 0 and n <= Varlist_Len(c));
    return cast(Value*, cast(Flex*, c)->content.dynamic.data) + n;
}

INLINE Symbol* CTX_KEY_SPELLING(VarList* c, REBLEN n) {
    return Varlist_Key(c, n)->extra.key_symbol;
}

INLINE Symbol* CTX_KEY_CANON(VarList* c, REBLEN n) {
    return Canon_Symbol(CTX_KEY_SPELLING(c, n));
}

INLINE Option(SymId) CTX_KEY_SYM(VarList* c, REBLEN n) {
    return Symbol_Id(CTX_KEY_SPELLING(c, n)); // should be same as canon
}

#define FAIL_IF_READ_ONLY_CONTEXT(c) \
    Fail_If_Read_Only_Flex(Varlist_Array(c))

INLINE void FREE_CONTEXT(VarList* c) {
    Free_Unmanaged_Flex(Keylist_Of_Varlist(c));
    Free_Unmanaged_Flex(Varlist_Array(c));
}


//=////////////////////////////////////////////////////////////////////////=//
//
// ANY-CONTEXT! (`struct Reb_Any_Context`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The Reb_Any_Context is the basic struct used currently for OBJECT!,
// MODULE!, ERROR!, and PORT!.  It builds upon the context datatype VarList,
// which permits the storage of associated KEYS and VARS.
//

INLINE void FAIL_IF_INACCESSIBLE_CTX(VarList* c) {
    if (Get_Flex_Info(c, INACCESSIBLE)) {
        if (CTX_TYPE(c) == REB_FRAME)
            fail (Error_Do_Expired_Frame_Raw()); // !!! different error?
        fail (Error_Series_Data_Freed_Raw());
    }
}

INLINE VarList* Cell_Varlist(const Cell* v) {
    assert(Any_Context(v));
    assert(not v->payload.any_context.phase or VAL_TYPE(v) == REB_FRAME);
    VarList* c = CTX(v->payload.any_context.varlist);
    FAIL_IF_INACCESSIBLE_CTX(c);
    return c;
}

// We approximate definitional errors in the bootstrap executable by making
// a lot of places not tolerant of ERROR!.  This isn't a good answer for the
// new executable, but it's serviceable enough.
//
INLINE void FAIL_IF_ERROR(const Cell* c) {
    if (Is_Error(c))
        fail (cast(Error*, Cell_Varlist(c)));
}

INLINE void INIT_Cell_Varlist(Value* v, VarList* c) {
    v->payload.any_context.varlist = Varlist_Array(c);
}

// Convenience macros to speak in terms of object values instead of the context
//
#define Cell_Varlist_VAR(v,n) \
    Varlist_Slot(Cell_Varlist(v), (n))

#define Cell_Varlist_KEY(v,n) \
    Varlist_Key(Cell_Varlist(v), (n))


// The movement of the SELF word into the domain of the object generators
// means that an object may wind up having a hidden SELF key (and it may not).
// Ultimately this key may well occur at any position.  While user code is
// discouraged from accessing object members by integer index (`pick obj 1`
// is an error), system code has historically relied upon this.
//
// During a transitional period where all MAKE OBJECT! constructs have a
// "real" SELF key/var in the first position, there needs to be an adjustment
// to the indexing of some of this system code.  Some of these will be
// temporary, because not all objects will need a definitional SELF (just as
// not all functions need a definitional RETURN).  Exactly which require it
// and which do not remains to be seen, so this macro helps review the + 1
// more easily than if it were left as just + 1.
//
#define SELFISH(n) \
    ((n) + 1)

// Common routine for initializing OBJECT, MODULE!, PORT!, and ERROR!
//
// A fully constructed context can reconstitute the ANY-CONTEXT! cell
// that is its canon form from a single pointer...the cell sitting in
// the 0 slot of the context's varlist.
//
INLINE Value* Init_Any_Context(
    Cell* out,
    enum Reb_Kind kind,
    VarList* c
){
  #if !defined(NDEBUG)
    Extra_Init_Any_Context_Checks_Debug(kind, c);
  #endif
    UNUSED(kind);
    assert(Is_Flex_Managed(Varlist_Array(c)));
    assert(Is_Flex_Managed(Keylist_Of_Varlist(c)));
    return Copy_Cell(out, Varlist_Archetype(c));
}

#define Init_Object(out,c) \
    Init_Any_Context((out), REB_OBJECT, (c))

#define Init_Port(out,c) \
    Init_Any_Context((out), REB_PORT, (c))

#define Init_Frame(out,c) \
    Init_Any_Context((out), REB_FRAME, (c))


//=////////////////////////////////////////////////////////////////////////=//
//
// COMMON INLINES (macro-like)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// By putting these functions in a header file, they can be inlined by the
// compiler, rather than add an extra layer of function call.
//

#define Copy_Context_Shallow_Managed(src) \
    Copy_Context_Shallow_Extra_Managed((src), 0)

// Returns true if the keylist had to be changed to make it unique.
//
#define Ensure_Keylist_Unique_Invalidated(context) \
    Expand_Context_Keylist_Core((context), 0)

// Useful if you want to start a context out as NODE_FLAG_MANAGED so it does
// not have to go in the unmanaged roots list and be removed later.  (Be
// careful not to do any evaluations or trigger GC until it's well formed)
//
#define Alloc_Context(kind,capacity) \
    Alloc_Context_Core((kind), (capacity), FLEX_FLAGS_NONE)


//=////////////////////////////////////////////////////////////////////////=//
//
// LOCKING
//
//=////////////////////////////////////////////////////////////////////////=//

INLINE void Deep_Freeze_Context(VarList* c) {
    Protect_Context(
        c,
        PROT_SET | PROT_DEEP | PROT_FREEZE
    );
    Uncolor_Array(Varlist_Array(c));
}

INLINE bool Is_Context_Deeply_Frozen(VarList* c) {
    return Get_Flex_Info(c, FROZEN_DEEP);
}


//=////////////////////////////////////////////////////////////////////////=//
//
// ERROR! (uses `struct Reb_Any_Context`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Errors are a subtype of ANY-CONTEXT! which follow a standard layout.
// That layout is in %boot/sysobj.r as standard/error.
//
// Historically errors could have a maximum of 3 arguments, with the fixed
// names of `arg1`, `arg2`, and `arg3`.  They would also have a numeric code
// which would be used to look up a a formatting block, which would contain
// a block for a message with spots showing where the args were to be inserted
// into a message.  These message templates can be found in %boot/errors.r
//
// Ren-C is exploring the customization of user errors to be able to provide
// arbitrary named arguments and message templates to use them.  It is
// a work in progress, but refer to the FAIL native, the corresponding
// `fail()` C macro inside the source, and the various routines in %c-error.c
//

#define ERR_VARS(e) \
    cast(ERROR_VARS*, Varlist_Slots_Head(e))

#define VAL_ERR_VARS(v) \
    ERR_VARS(Cell_Varlist(v))

#define Init_Error(v,c) \
    Init_Any_Context((v), REB_ERROR, (c))


// Ports are unusual hybrids of user-mode code dispatched with native code, so
// some things the user can do to the internals of a port might cause the
// C code to crash.  This wasn't very well thought out in R3-Alpha, but there
// was some validation checking.  This factors out that check instead of
// repeating the code.
//
INLINE void FAIL_IF_BAD_PORT(Value* port) {
    if (not Any_Context(port))
        fail (Error_Invalid_Port_Raw());

    VarList* ctx = Cell_Varlist(port);
    if (
        Varlist_Len(ctx) < (STD_PORT_MAX - 1)
        or not Is_Object(Varlist_Slot(ctx, STD_PORT_SPEC))
    ){
        fail (Error_Invalid_Port_Raw());
    }
}

// It's helpful to show when a test for a native port actor is being done,
// rather than just having the code say Is_Handle().
//
INLINE bool Is_Native_Port_Actor(const Value* actor) {
    if (Is_Handle(actor))
        return true;
    assert(Is_Object(actor));
    return false;
}


//
//  Steal_Context_Vars: C
//
// This is a low-level trick which mutates a context's varlist into a stub
// "free" node, while grabbing the underlying memory for its variables into
// an array of values.
//
// It has a notable use by DO of a heap-based FRAME!, so that the frame's
// filled-in heap memory can be directly used as the args for the invocation,
// instead of needing to push a redundant run of stack-based memory cells.
//
INLINE VarList* Steal_Context_Vars(VarList* c, Node* keysource) {
    Flex* stub = c;

    // Rather than memcpy() and touch up the header and info to remove
    // FLEX_INFO_HOLD put on by Enter_Native(), or NODE_FLAG_MANAGED,
    // etc.--use constant assignments and only copy the remaining fields.
    //
    Flex* copy = Alloc_Flex_Stub(
        SERIES_MASK_CONTEXT
            | FLEX_FLAG_FIXED_SIZE
    );
    copy->info = Endlike_Header(
        FLAG_WIDE_BYTE_OR_0(0) // implicit termination, and indicates array
            | FLAG_LEN_BYTE_OR_255(255) // indicates dynamic (varlist rule)
    );
    Corrupt_Pointer_If_Debug(copy->link_private.keysource); // needs update
    memcpy(
        cast(char*, &copy->content),
        cast(char*, &stub->content),
        sizeof(union StubContentUnion)
    );
    copy->misc_private.meta = nullptr; // let stub have the meta

    Value* rootvar = cast(Value*, copy->content.dynamic.data);

    // Convert the old varlist that had outstanding references into a
    // singular "stub", holding only the Varlist_Archetype.  This is needed
    // for the ->binding to allow Derelativize(), see SPC_BINDING().
    //
    // Note: previously this had to preserve FLEX_INFO_FRAME_FAILED, but now
    // those marking failure are asked to do so manually to the stub
    // after this returns (hence they need to cache the varlist first).
    //
    stub->info = Endlike_Header(
        FLEX_INFO_INACCESSIBLE // args memory now "stolen" by copy
            | FLAG_WIDE_BYTE_OR_0(0) // width byte is 0 for array series
            | FLAG_LEN_BYTE_OR_255(1) // not dynamic any more, new len is 1
    );

    Value* single = cast(Value*, &stub->content.fixed);
    single->header.bits =
        NODE_FLAG_NODE | NODE_FLAG_CELL | FLAG_KIND_BYTE(REB_FRAME);
    INIT_BINDING(single, VAL_BINDING(rootvar));
    single->payload.any_context.varlist = cast_Array(stub);
    Corrupt_Pointer_If_Debug(single->payload.any_context.phase);
    /* single->payload.any_context.phase = L->original; */ // !!! needed?

    rootvar->payload.any_context.varlist = cast_Array(copy);

    // Disassociate the stub from the frame, by degrading the link field
    // to a keylist.  !!! Review why this was needed, vs just nullptr
    //
    LINK(stub).keysource = keysource;

    return CTX(copy);
}
