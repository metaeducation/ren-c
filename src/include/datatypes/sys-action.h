//
//  File: %sys-action.h
//  Summary: {action! defs AFTER %tmp-internals.h (see: %sys-rebact.h)}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2021 Ren-C Open Source Contributors
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
// As in historical Rebol, Ren-C has several different kinds of functions...
// each of which have a different implementation path inside the system.
// But in Ren-C there is only one user-visible datatype from the user's
// perspective for all of them, which is called ACTION!.
//
// Each ACTION! has an associated C function that runs when it is invoked, and
// this is called the "dispatcher".  A dispatcher may be general and reused
// by many different actions.  For example: the same dispatcher code is used
// for most `FUNC [...] [...]` instances--but each one has a different body
// array and spec, so the behavior is different.  Other times a dispatcher can
// be for a single function, such as with natives like IF that have C code
// which is solely used to implement IF.
//
// The identity array for an action is called its "details".  It has an
// archetypal value for the ACTION! in its [0] slot, but the other slots are
// dispatcher-specific.  Different dispatchers lay out the details array with
// different values that define the action instance.
//
// Some examples:
//
//     USER FUNCTIONS: 1-element array w/a BLOCK!, the body of the function
//     GENERICS: 1-element array w/WORD! "verb" (OPEN, APPEND, etc)
//     SPECIALIZATIONS: no contents needed besides the archetype
//     ROUTINES/CALLBACKS: stylized array (REBRIN*)
//     TYPECHECKERS: the TYPESET! to check against
//
// (See the comments in the %src/core/functionals/ directory for each function
// variation for descriptions of how they use their details arrays.)
//
// Every action has an associated context known as the "exemplar" that defines
// the parameters and locals.  The keylist of this exemplar is reused for
// FRAME! instances of invocations (or pending invocations) of the action.
//
// The varlist of the exemplar context is referred to as a "paramlist".  It
// is an array that serves two overlapping purposes: any *unspecialized*
// slots in the paramlist holds the TYPESET! definition of legal types for
// that argument, as well as the PARAM_FLAG_XXX for other properties of the
// parameter.  But a *specialized* parameter slot holds the specialized value
// itself, which is presumed to have been type-checked upon specialization.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// NOTES:
//
// * Unlike contexts, an ACTION! does not have values of its own, only
//   parameter definitions (or "params").  The arguments ("args") come from an
//   action's instantiation on the stack, viewed as a context using a FRAME!.
//
// * Paramlists may contain hidden fields, if they are specializations...
//   because they have to have the right number of slots to line up with the
//   frame of the underlying function.
//
// * The `misc.meta` field of the details holds a meta object (if any) that
//   describes the function.  This is read by help.  A similar facility is
//   enabled by the `misc.meta` field of varlists.
//
// * By storing the C function dispatcher pointer in the `details` array node
//   instead of in the value cell itself, it also means the dispatcher can be
//   HIJACKed--or otherwise hooked to affect all instances of a function.
//

// REBCTX types use this field of their varlist (which is the identity of
// an ANY-CONTEXT!) to find their "keylist".  It is stored in the REBSER
// node of the varlist REBARR vs. in the REBVAL of the ANY-CONTEXT! so
// that the keylist can be changed without needing to update all the
// REBVALs for that object.
//
// It may be a simple REBSER* -or- in the case of the varlist of a running
// FRAME! on the stack, it points to a REBFRM*.  If it's a FRAME! that
// is not running on the stack, it will be the function paramlist of the
// actual phase that function is for.  Since REBFRM* all start with a
// REBVAL cell, this means NODE_FLAG_CELL can be used on the node to
// discern the case where it can be cast to a REBFRM* vs. REBARR*.
//
// (Note: FRAME!s used to use a field `misc.f` to track the associated
// frame...but that prevented the ability to SET-META on a frame.  While
// that feature may not be essential, it seems awkward to not allow it
// since it's allowed for other ANY-CONTEXT!s.  Also, it turns out that
// heap-based FRAME! values--such as those that come from MAKE FRAME!--
// have to get their keylist via the specifically applicable ->phase field
// anyway, and it's a faster test to check this for NODE_FLAG_CELL than to
// separately extract the CTX_TYPE() and treat frames differently.)
//
// It is done as a base-class REBNOD* as opposed to a union in order to
// not run afoul of C's rules, by which you cannot assign one member of
// a union and then read from another.
//
#define BONUS_KeySource_TYPE        REBNOD*
#define BONUS_KeySource_CAST        // none, just use node (NOD() complains)
#define HAS_BONUS_KeySource         FLAVOR_VARLIST

inline static void INIT_BONUS_KEYSOURCE(REBARR *varlist, REBNOD *keysource) {
    if (keysource != nullptr and not Is_Node_Cell(keysource))
        assert(IS_KEYLIST(SER(keysource)));
    mutable_BONUS(KeySource, varlist) = keysource;
}


//=//// PSEUDOTYPES FOR RETURN VALUES /////////////////////////////////////=//
//
// An arbitrary cell pointer may be returned from a native--in which case it
// will be checked to see if it is thrown and processed if it is, or checked
// to see if it's an unmanaged API handle and released if it is...ultimately
// putting the cell into f->out.
//
// However, pseudotypes can be used to indicate special instructions to the
// evaluator.
//

inline static REBVAL *Init_Return_Signal_Untracked(Cell *out, char ch) {
    Reset_Cell_Header_Untracked(out, REB_T_RETURN_SIGNAL, CELL_MASK_NONE);
    mutable_BINDING(out) = nullptr;

    PAYLOAD(Any, out).first.u = ch;
  #ifdef ZERO_UNUSED_CELL_FIELDS
    PAYLOAD(Any, out).second.trash = ZEROTRASH;
  #endif
    return cast(REBVAL*, out);
}

#define Init_Return_Signal(out,ch) \
    Init_Return_Signal_Untracked(TRACK(out), (ch))

#define IS_RETURN_SIGNAL(v) \
    (HEART_BYTE(v) == REB_T_RETURN_SIGNAL)

inline static char VAL_RETURN_SIGNAL(const Cell *v) {
    assert(IS_RETURN_SIGNAL(v));
    return PAYLOAD(Any, v).first.u;
}


// This signals that the evaluator is in a "thrown state".
//
#define C_THROWN 'T'
#define R_THROWN \
    cast(REBVAL*, &PG_R_Thrown)

// It is also used by path dispatch when it has taken performing a SET-PATH!
// into its own hands, but doesn't want to bother saying to move the value
// into the output slot...instead leaving that to the evaluator (as a
// SET-PATH! should always evaluate to what was just set)
//
#define C_INVISIBLE 'I'
#define R_INVISIBLE \
    cast(REBVAL*, &PG_R_Invisible)

// If Eval_Core gets back an REB_R_REDO from a dispatcher, it will re-execute
// the f->phase in the frame.  This function may be changed by the dispatcher
// from what was originally called.
//
// If EXTRA(Any).flag is not set on the cell, then the types will be checked
// again.  Note it is not safe to let arbitrary user code change values in a
// frame from expected types, and then let those reach an underlying native
// who thought the types had been checked.
//
#define C_REDO_UNCHECKED 'r'
#define R_REDO_UNCHECKED \
    cast(REBVAL*, &PG_R_Redo_Unchecked)

#define C_REDO_CHECKED 'R'
#define R_REDO_CHECKED \
    cast(REBVAL*, &PG_R_Redo_Checked)


#define C_UNHANDLED 'U'
#define R_UNHANDLED \
    cast(REBVAL*, &PG_R_Unhandled)


#define CELL_MASK_ACTION \
    (CELL_FLAG_FIRST_IS_NODE | CELL_FLAG_SECOND_IS_NODE)

#define INIT_VAL_ACTION_DETAILS                         INIT_VAL_NODE1
#define VAL_ACTION_PARTIALS_OR_LABEL(v)                 SER(VAL_NODE2(v))
#define INIT_VAL_ACTION_PARTIALS_OR_LABEL               INIT_VAL_NODE2


// An action's details array is stored in the archetype, which is the first
// element of the action array...which is *usually* the same thing as the
// action array itself, -but not always-.  Hijackings fiddle with this, and
// a COPY of an action will get the details array of what it copied...not
// itself.  So an archetype represents -an- action, but it may be a hijacked
// action from what it once was (much like a word reference).
//
inline static REBARR *ACT_DETAILS(REBACT *a) {
    return m_cast(REBARR*, x_cast(const REBARR*,
        x_cast(REBVAL*, x_cast(const REBSER*, a)->content.dynamic.data)
            ->payload.Any.first.node
    ));
}  // ARR() has debug cost, not defined yet

inline static REBARR *ACT_IDENTITY(REBACT *a)
  { return x_cast(REBARR*, a); }


inline static REBCTX *VAL_ACTION_BINDING(noquote(const Cell*) v) {
    assert(CELL_HEART(v) == REB_ACTION);
    return CTX(BINDING(v));
}

inline static void INIT_VAL_ACTION_BINDING(
    Cell *v,
    REBCTX *binding
){
    assert(IS_ACTION(v));
    mutable_BINDING(v) = binding;
}


// An action's "archetype" is data in the head cell (index [0]) of the array
// that is the paramlist.  This is an ACTION! cell which must have its
// paramlist value match the paramlist it is in.  So when copying one array
// to make a new paramlist from another, you must ensure the new array's
// archetype is updated to match its container.

#define ACT_ARCHETYPE(a) \
    SER_AT(REBVAL, ACT_IDENTITY(a), 0)


//=//// PARAMLIST, EXEMPLAR, AND PARTIALS /////////////////////////////////=//
//
// Since partial specialization is somewhat rare, it is an optional splice
// before the place where the exemplar is to be found.
//

#define INODE_Exemplar_TYPE     REBCTX*
#define INODE_Exemplar_CAST     CTX
#define HAS_INODE_Exemplar      FLAVOR_DETAILS


inline static option(REBARR*) ACT_PARTIALS(REBACT *a) {
    return ARR(VAL_NODE2(ACT_ARCHETYPE(a)));
}

#define ACT_EXEMPLAR(a) \
    INODE(Exemplar, (a))

// Note: This is a more optimized version of CTX_KEYLIST(ACT_EXEMPLAR(a)),
// and also forward declared.
//
#define ACT_KEYLIST(a) \
    SER(BONUS(KeySource, ACT_EXEMPLAR(a)))

#define ACT_KEYS_HEAD(a) \
    SER_HEAD(const REBKEY, ACT_KEYLIST(a))

#define ACT_KEYS(tail,a) \
    CTX_KEYS((tail), ACT_EXEMPLAR(a))

#define ACT_PARAMLIST(a)            CTX_VARLIST(ACT_EXEMPLAR(a))

inline static REBPAR *ACT_PARAMS_HEAD(REBACT *a) {
    REBARR *list = CTX_VARLIST(ACT_EXEMPLAR(a));
    return cast(REBPAR*, list->content.dynamic.data) + 1;  // skip archetype
}

#define LINK_DISPATCHER(a)              cast(REBNAT, (a)->link.any.cfunc)
#define mutable_LINK_DISPATCHER(a)      (a)->link.any.cfunc

#define ACT_DISPATCHER(a) \
    LINK_DISPATCHER(ACT_IDENTITY(a))

#define INIT_ACT_DISPATCHER(a,cfunc) \
    mutable_LINK_DISPATCHER(ACT_IDENTITY(a)) = cast(CFUNC*, (cfunc))


#define DETAILS_AT(a,n) \
    SPECIFIC(ARR_AT((a), (n)))

#define IDX_DETAILS_1 1  // Common index used for code body location

// These are indices into the details array agreed upon by actions which have
// the PARAMLIST_FLAG_IS_NATIVE set.
//
enum {
    // !!! Originally the body was introduced as a feature to let natives
    // specify "equivalent usermode code".  As the types of natives expanded,
    // it was used for things like storing the text source of C user natives...
    // or the "verb" WORD! of a "generic" (like APPEND).  So ordinary natives
    // just store blank here, and the usages are sometimes dodgy (e.g. a user
    // native checks to see if it's a user native if this is a TEXT!...which
    // might collide with other natives in the future).  The idea needs review.
    //
    IDX_NATIVE_BODY = 1,

    IDX_NATIVE_CONTEXT,  // libRebol binds strings here (and lib)

    IDX_NATIVE_MAX
};


inline static const REBSYM *KEY_SYMBOL(const REBKEY *key)
  { return *key; }


inline static void Init_Key(REBKEY *dest, const REBSYM *symbol)
  { *dest = symbol; }

#define KEY_SYM(key) \
    ID_OF_SYMBOL(KEY_SYMBOL(key))

#define ACT_KEY(a,n)            CTX_KEY(ACT_EXEMPLAR(a), (n))
#define ACT_PARAM(a,n)          cast_PAR(CTX_VAR(ACT_EXEMPLAR(a), (n)))

#define ACT_NUM_PARAMS(a) \
    CTX_LEN(ACT_EXEMPLAR(a))


//=//// META OBJECT ///////////////////////////////////////////////////////=//
//
// ACTION! details and ANY-CONTEXT! varlists can store a "meta" object.  It's
// where information for HELP is saved, and it's how modules store out-of-band
// information that doesn't appear in their body.

#define mutable_ACT_META(a)     mutable_MISC(DetailsMeta, ACT_IDENTITY(a))
#define ACT_META(a)             MISC(DetailsMeta, ACT_IDENTITY(a))


inline static REBACT *VAL_ACTION(noquote(const Cell*) v) {
    assert(CELL_HEART(v) == REB_ACTION);
    REBSER *s = SER(VAL_NODE1(v));
    if (GET_SERIES_FLAG(s, INACCESSIBLE))
        fail (Error_Series_Data_Freed_Raw());
    return ACT(s);
}

#define VAL_ACTION_KEYLIST(v) \
    ACT_KEYLIST(VAL_ACTION(v))


//=//// ACTION LABELING ///////////////////////////////////////////////////=//
//
// When an ACTION! is stored in a cell (e.g. not an "archetype"), it can
// contain a label of the ANY-WORD! it was taken from.  If it is an array
// node, it is presumed an archetype and has no label.
//
// !!! Theoretically, longer forms like `.not.equal?` for PREDICATE! could
// use an array node here.  But since CHAINs store ACTION!s that can cache
// the words, you get the currently executing label instead...which may
// actually make more sense.

inline static option(const REBSYM*) VAL_ACTION_LABEL(noquote(const Cell*) v) {
    assert(CELL_HEART(v) == REB_ACTION);
    REBSER *s = VAL_ACTION_PARTIALS_OR_LABEL(v);
    if (not s)
        return ANONYMOUS;
    if (IS_SER_ARRAY(s))
        return ANONYMOUS;  // archetype (e.g. may live in paramlist[0] itself)
    return SYM(s);
}

inline static void INIT_VAL_ACTION_LABEL(
    Cell *v,
    option(const REBSYM*) label
){
    ASSERT_CELL_WRITABLE_EVIL_MACRO(v);  // archetype R/O
    if (label)
        INIT_VAL_ACTION_PARTIALS_OR_LABEL(v, unwrap(label));
    else
        INIT_VAL_ACTION_PARTIALS_OR_LABEL(v, ANONYMOUS);
}


//=//// ANCESTRY / FRAME COMPATIBILITY ////////////////////////////////////=//
//
// On the keylist of an object, LINK_ANCESTOR points at a keylist which has
// the same number of keys or fewer, which represents an object which this
// object is derived from.  Note that when new object instances are
// created which do not require expanding the object, their keylist will
// be the same as the object they are derived from.
//
// Paramlists have the same relationship, with each expansion (e.g. via
// AUGMENT) having larger frames pointing to the potentially shorter frames.
// (Something that reskins a paramlist might have the same size frame, with
// members that have different properties.)
//
// When you build a frame for an expanded action (e.g. with an AUGMENT) then
// it can be used to run phases that are from before it in the ancestry chain.
// This informs low-level asserts inside of the specific binding machinery, as
// well as determining whether higher-level actions can be taken (like if a
// sibling tail call would be legal, or if a certain HIJACK would be safe).
//
// !!! When ancestors were introduced, it was prior to AUGMENT and so frames
// did not have a concept of expansion.  So they only applied to keylists.
// The code for processing derivation is slightly different; it should be
// unified more if possible.

#define LINK_Ancestor_TYPE              REBSER*
#define LINK_Ancestor_CAST              SER
#define HAS_LINK_Ancestor               FLAVOR_KEYLIST

inline static bool Action_Is_Base_Of(REBACT *base, REBACT *derived) {
    if (derived == base)
        return true;  // fast common case (review how common)

    if (ACT_DETAILS(derived) == ACT_IDENTITY(base))
        return true;  // Covers COPY + HIJACK cases (seemingly)

    REBSER *keylist_test = ACT_KEYLIST(derived);
    REBSER *keylist_base = ACT_KEYLIST(base);
    while (true) {
        if (keylist_test == keylist_base)
            return true;

        REBSER *ancestor = LINK(Ancestor, keylist_test);
        if (ancestor == keylist_test)
            return false;  // signals end of the chain, no match found

        keylist_test = ancestor;
    }
}


//=//// RETURN HANDLING (WIP) /////////////////////////////////////////////=//
//
// The well-understood and working part of definitional return handling is
// that function frames have a local slot named RETURN.  This slot is filled
// by the dispatcher before running the body, with a function bound to the
// executing frame.  This way it knows where to return to.
//
// !!! Lots of other things are not worked out (yet):
//
// * How do function derivations share this local cell (or do they at all?)
//   e.g. if an ADAPT has prelude code, that code runs before the original
//   dispatcher would fill in the RETURN.  Does the cell hold a return whose
//   phase meaning changes based on which phase is running (which the user
//   could not do themselves)?  Or does ADAPT need its own RETURN?  Or do
//   ADAPTs just not have returns?
//
// * The typeset in the RETURN local key is where legal return types are
//   stored (in lieu of where a parameter would store legal argument types).
//   Derivations may wish to change this.  Needing to generate a whole new
//   paramlist just to change the return type seems excessive.
//
// * To make the position of RETURN consistent and easy to find, it is moved
//   to the first parameter slot of the paramlist (regardless of where it
//   is declared).  This complicates the paramlist building code, and being
//   at that position means it often needs to be skipped over (e.g. by a
//   GENERIC which wants to dispatch on the type of the first actual argument)
//   The ability to create functions that don't have a return complicates
//   this mechanic as well.
//
// The only bright idea in practice right now is that parameter lists which
// have a definitional return in the first slot have a flag saying so.  Much
// more design work on this is needed.
//

#define ACT_HAS_RETURN(a) \
    GET_SUBCLASS_FLAG(VARLIST, ACT_PARAMLIST(a), PARAMLIST_HAS_RETURN)


// A fully constructed action can reconstitute the ACTION! REBVAL
// that is its canon form from a single pointer...the REBVAL sitting in
// the 0 slot of the action's details.  That action has no binding and
// no label.
//
inline static REBVAL *Init_Action_Core(
    Cell *out,
    REBACT *a,
    option(const REBSYM*) label,  // allowed to be ANONYMOUS
    REBCTX *binding  // allowed to be UNBOUND
){
  #if !defined(NDEBUG)
    Extra_Init_Action_Checks_Debug(a);
  #endif
    Force_Series_Managed(ACT_IDENTITY(a));

    Reset_Cell_Header_Untracked(out, REB_ACTION, CELL_MASK_ACTION);
    INIT_VAL_ACTION_DETAILS(out, ACT_IDENTITY(a));
    INIT_VAL_ACTION_LABEL(out, label);
    INIT_VAL_ACTION_BINDING(out, binding);

    return cast(REBVAL*, out);
}

#define Init_Action(out,a,label,binding) \
    Init_Action_Core(TRACK(out), (a), (label), (binding))


// The action frame run dispatchers, which get to take over the STATE_BYTE
// of the frame for their own use.  But before then, the state byte is used
// by action dispatch itself.
//
// So if f->key is END, then this state is not meaningful.
//
enum {
    ST_ACTION_INITIAL_ENTRY = 0,  // is separate "fulfilling" state needed?
    ST_ACTION_TYPECHECKING,
    ST_ACTION_DISPATCHING
};

// This indicates that an evaluation step didn't add any new output, but it
// does not overwrite the contents of the out cell.  This allows the evaluator
// to leave a value in the output slot even if there is trailing invisible
// evaluation to be done, such as in `all [1 + 2 elide print "Hi"]`.  Something
// like ALL wants to hold onto the 3 without needing to cache it in some
// other location between steps.
//
// Stale out cells lie and use the CELL_FLAG_STALE in order to generate asserts
// if they are observed in the stale state, without going through the proper
// functions to reveal the content.  This helps accidental usage ,such as being
// the left side input for enfix: `(1 comment "hi" + 2)` is not legal.
//
// Note: The garbage collector knows explicitly that it's okay for frame output
// slots to have the CELL_FLAG_STALE bit set; it's not usually legal.
//
inline static void Mark_Eval_Out_Stale(REBVAL *out) {
    out->header.bits |= CELL_FLAG_STALE;
    out->header.bits &= (~ CELL_FLAG_OUT_NOTE_VOIDED);
}

inline static void Clear_Void_Flag(REBVAL *out) {
    out->header.bits &= (~ CELL_FLAG_OUT_NOTE_VOIDED);
}

// Must handle the Translucent and Invisible cases before clearing stale.
//
inline static void Clear_Stale_Flag(REBVAL *out) {
    out->header.bits &= ~ (CELL_FLAG_STALE | CELL_FLAG_OUT_NOTE_VOIDED);
}

inline static bool Was_Eval_Step_Void(const REBVAL *out) {
    return did (out->header.bits & CELL_FLAG_OUT_NOTE_VOIDED);
}


// !!! We want a freshly prep'd cell to count as stale, e.g. if a construct does
// a RESET() and then decides never to call Eval() into the cell at all.  But
// we also want fresh cells to be all 0 for fast memset().  (Fresh cells get
// their unreadability from not having NODE_FLAG_NODE set, not from having
// CELL_FLAG_STALE set).  Review design of this.
//
inline static bool Is_Stale(const REBVAL *out) {
    ASSERT_CELL_INITABLE_EVIL_MACRO(out);

    return did (out->header.bits & CELL_FLAG_STALE);
}


inline static REBVAL *Maybe_Move_Cell(REBVAL *out, REBVAL *v) {
    if (v == out)
        return out;
    return Move_Cell(out, v);
}

#define return_thrown(v) \
    do { \
        Maybe_Move_Cell(OUT, (v)); /* must be first, if Init_Thrown() */ \
        assert(not Is_Void(&TG_Thrown_Arg)); \
        return R_THROWN; \
    } while (false)


#define return_branched(v) \
    do { \
        assert((v) == OUT); /* must be first, if Init_XXX() */ \
        assert(not Is_Void(OUT)); \
        assert(not IS_NULLED(OUT)); \
        return OUT; \
    } while (false)


inline static REBVAL *Mark_Eval_Out_Voided(REBVAL *out) {
    ASSERT_CELL_INITABLE_EVIL_MACRO(out);
    assert(Is_Stale(out));

    // We want void evaluations to "vanish", and so we can't overwrite what's
    // sitting in the output cell with a "~void~ isotope".
    //
    //    1 + 2 comment "how would we return 3 if comment overwrites it?"
    //
    // But we have to leave some kind of indicator that an evaluation step
    // produced a void, because it needs to be reified as input to things
    // like ^META enfix operators.
    //
    //     1 + 2 if false [<skip>] else x => [print ["Shouldn't be 3!" x]]
    //
    // When the IF runs it leaves the 3 in the output cell, marked with
    // the translucent bit.  But it clears the stale bit so that it reports
    // a new result is available.  Yet the ELSE wants to get a ~void~ isotope
    // as its input--not the 3!
    //
    // So enfix as well as many operations need to check the voided bit
    // first, before assuming a stale value is unusable.  The way this is
    // kept from having too many accidents is that the functions enforce that
    // you can't test for an eval product being void until you've checked for
    // staleness first.
    //
    out->header.bits |= CELL_FLAG_OUT_NOTE_VOIDED;
    return out;
}

#define return_void(v) \
    do { \
        assert((v) == OUT); \
        Mark_Eval_Out_Voided(OUT); \
        return R_INVISIBLE; \
    } while (false)

#define return_non_void(v) \
    do { \
        assert((v) == OUT); \
        if (Is_Void(OUT)) \
            return Init_None(OUT); \
        return OUT; \
    } while (false)


// Plain reification cannot discern "~void~ isotopes" from none isotopes.
//
//     >> do [comment "conflation is unavoidable"]
//     == ~  ; isotope
//
//     >> do [~]
//     == ~  ; isotope
//
// Functions that want to do this discernment have to handle stale situations
// explicitly.  See ANY and ALL for examples.
//
inline static REBVAL *Reify_Eval_Out_Plain(REBVAL *out) {
    if (Is_Void(out))
        return Init_None(out);

    return out;
}

#define Reify_Stale_Plain_Branch(out) \
    Isotopify_If_Nulled(Reify_Eval_Out_Plain(out))


inline static REBVAL *Reify_Eval_Out_Meta(REBVAL *out) {
    if (Is_Void(out))
        return Init_Meta_Of_Void(out);

    return Meta_Quotify(out);
}


inline static bool Process_Action_Throws(REBFRM *f) {
    RESET(f->out);

    bool threw = Process_Action_Core_Throws(f);

    Reify_Eval_Out_Plain(f->out);

    return threw;
}
