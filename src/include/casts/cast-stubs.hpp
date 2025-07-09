//
//  file: %cast-stubs.hpp
//  summary: "Instrumented operators for casting Stub subclasses"
//  project: "Ren-C Interpreter and Run-time"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2025 Ren-C Open Source Contributors
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
// See src/include/casts/README.md for general information about CastHook.
//
// This file is specifically for checking casts to Stub-derived types.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// A. CastHook<> has two parameters (From and To types), but we pin down the
//    "To" type, then match a pattern for any "From" type (F).
//
// B. See the definition of CastHook for why the generalized casting
//    mechanic runs through const pointers only.
//
// C. See the definitions of UpcastTag and DowncastTag for an explanation of
//    why we trust upcasts by default (you can override it if needed).
//
// D. If you find yourself having trouble with `static_assert(false, ...)`
//    triggering in SFINAE, see `static_assert(always_false<T>::value, ...)`
//

//=//// cast(Stub*, ...) //////////////////////////////////////////////////=//

template<typename F>  // [A]
struct CastHook<const F*, const Stub*> {  // both must be const [B]
  static void Validate_Bits(const F* p) {
    DECLARE_C_TYPE_LIST(type_list,
        void, Byte, Base
    );
    STATIC_ASSERT(In_C_Type_List(type_list, F));

    if (not p)
        return;

    if ((u_cast(const Stub*, p)->header.bits & (
        BASE_FLAG_BASE | BASE_FLAG_CELL  // BASE_FLAG_UNREADABLE ok
    )) != (
        BASE_FLAG_BASE
    )){
        crash (p);
    }
  }
};


//=//// cast(Flex*, ...) //////////////////////////////////////////////////=//

template<typename F>  // [A]
struct CastHook<const F*, const Flex*> {  // both must be const [B]
  static void Validate_Bits(const F* p) {
    DECLARE_C_TYPE_LIST(type_list,
        void, Byte, Base, Stub, HashList
    );
    STATIC_ASSERT(In_C_Type_List(type_list, F));

    if (not p)
        return;

    if ((u_cast(const Stub*, p)->header.bits & (
        BASE_FLAG_BASE | BASE_FLAG_UNREADABLE | BASE_FLAG_CELL
    )) != (
        BASE_FLAG_BASE
    )){
        crash (p);
    }
  }
};


//=//// cast(Binary*, ...) ////////////////////////////////////////////////=//

template<typename F>  // [A]
struct CastHook<const F*, const Binary*> {  // both must be const [B]
  static void Validate_Bits(const F* p) {
    DECLARE_C_TYPE_LIST(type_list,
        void, Byte, Base, Flex
    );
    STATIC_ASSERT(In_C_Type_List(type_list, F));

    if (not p)
        return;

    const Stub* stub = u_cast(const Stub*, p);

    if ((stub->header.bits & (
        BASE_FLAG_BASE | BASE_FLAG_UNREADABLE | BASE_FLAG_CELL
    )) != (
        BASE_FLAG_BASE  // BASE_FLAG_UNREADABLE is diminished Stub
    )){
        crash (p);
    }

    impossible(not Stub_Holds_Bytes(stub));  // we *could* check this here
  }
};


//=//// cast(Strand*, ...) ////////////////////////////////////////////////=//

template<typename F>  // [A]
struct CastHook<const F*, const Strand*> {  // both must be const [B]
  static void Validate_Bits(const F* p) {
    DECLARE_C_TYPE_LIST(type_list,
        void, Byte, Base, Stub, Flex, Binary
    );
    STATIC_ASSERT(In_C_Type_List(type_list, F));

    if (not p)
        return;

    const Stub* stub = u_cast(const Stub*, p);

    TasteByte taste_byte = TASTE_BYTE(stub);
    if (taste_byte != FLAVOR_NONSYMBOL and taste_byte != FLAVOR_SYMBOL)
        crash (p);

    if ((stub->header.bits & (
        STUB_MASK_SYMBOL_STRING_COMMON
            | BASE_FLAG_UNREADABLE
            | BASE_FLAG_CELL
    )) !=
        STUB_MASK_SYMBOL_STRING_COMMON
    ){
        assert(stub->header.bits & STUB_FLAG_CLEANS_UP_BEFORE_GC_DECAY);
        crash (p);
    }

    impossible(not Stub_Holds_Bytes(stub));  // we *could* check this here
  }
};


//=//// cast(Symbol*, ...) ////////////////////////////////////////////////=//

template<typename F>  // [A]
struct CastHook<const F*, const Symbol*> {
    static void Validate_Bits(const F* p) {
        DECLARE_C_TYPE_LIST(type_list,
            void, Byte, Base, Stub, Flex, Binary, Strand
        );
        STATIC_ASSERT(In_C_Type_List(type_list, F));

        if (not p)
            return;

        const Stub* stub = u_cast(const Stub*, p);
        if ((stub->header.bits & (
            (STUB_MASK_SYMBOL | STUB_MASK_TASTE)
                | BASE_FLAG_UNREADABLE
                | BASE_FLAG_CELL
        )) !=
            STUB_MASK_SYMBOL
        ){
            crash (p);
        }

        impossible(not Stub_Holds_Bytes(stub));  // we *could* check this here
    }
};


//=//// cast(Array*, ...) /////////////////////////////////////////////////=//

template<typename F>  // [A]
struct CastHook<const F*, const Array*> {  // both must be const [B]
  static void Validate_Bits(const F* p) {
    DECLARE_C_TYPE_LIST(type_list,
        void, Byte, Base, Stub, Flex
    );
    STATIC_ASSERT(In_C_Type_List(type_list, F));

    if (not p)
        return;

    if ((u_cast(const Stub*, p)->header.bits & (
        BASE_FLAG_BASE | BASE_FLAG_UNREADABLE | BASE_FLAG_CELL
    )) != (
        BASE_FLAG_BASE
    )){
        crash (p);
    }
  }
};


//=//// cast(VarList*, ...) ///////////////////////////////////////////////=//

template<typename F>  // [A]
struct CastHook<const F*, const VarList*> {  // both must be const [B]
  static void Validate_Bits(const F* p) {
    DECLARE_C_TYPE_LIST(type_list,
        void, Byte, Base, Stub, Array, Flex, Context
    );
    STATIC_ASSERT(In_C_Type_List(type_list, F));

    if (not p)
        return;

    if ((u_cast(const Stub*, p)->header.bits & (
        (STUB_MASK_LEVEL_VARLIST
            & (~ STUB_FLAG_LINK_NEEDS_MARK)  // next virtual, maybe null
            & (~ STUB_FLAG_MISC_NEEDS_MARK)  // adjunct, maybe null
        )   | BASE_FLAG_UNREADABLE
            | BASE_FLAG_CELL
            | STUB_MASK_TASTE
    )) !=
        STUB_MASK_LEVEL_VARLIST
    ){
        crash (p);
    }
  }
};


//=//// cast(Phase*, ...) ////////////////////////////////////////////////=//

template<typename F>  // [A]
struct CastHook<const F*, const Phase*> {  // both must be const [B]
  static void Validate_Bits(const F* p) {
    DECLARE_C_TYPE_LIST(type_list,
        void, Byte, Base, Stub, Flex, ParamList, Details
    );
    STATIC_ASSERT(In_C_Type_List(type_list, F));

    if (not p)
        return;

    const Stub* stub = u_cast(const Stub*, p);

    if (TASTE_BYTE(stub) == FLAVOR_DETAILS) {
        if ((stub->header.bits & (
            (STUB_MASK_DETAILS | STUB_MASK_TASTE)
                | BASE_FLAG_UNREADABLE
                | BASE_FLAG_CELL
        )) !=
            STUB_MASK_DETAILS
        ){
            crash (p);
        }
    }
    else {
        if ((stub->header.bits & ((
            (STUB_MASK_LEVEL_VARLIST | STUB_MASK_TASTE)
                | BASE_FLAG_UNREADABLE
                | BASE_FLAG_CELL
            )
        )) !=
            STUB_MASK_LEVEL_VARLIST  // maybe no MISC_NEEDS_MARK
        ){
            crash (p);
        }
    }
  }
};
