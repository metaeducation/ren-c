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

template<typename F>
const Stub* stub_cast_impl(const F* p, UpcastTag) {  // trust upcast [C]
    return u_cast(const Flex*, p);
}

template<typename F>
const Stub* stub_cast_impl(const F* p, DowncastTag) {  // validate [C]
    DECLARE_C_TYPE_LIST(type_list,
        void, Byte, Base
    );
    STATIC_ASSERT(In_C_Type_List(type_list, F));

    if (not p)
        return nullptr;

    if ((u_cast(const Stub*, p)->leader.bits & (
        BASE_FLAG_BASE | BASE_FLAG_CELL  // BASE_FLAG_UNREADABLE ok
    )) != (
        BASE_FLAG_BASE
    )){
        crash (p);
    }

    return u_cast(const Stub*, p);
}

template<typename F>  // [A]
struct CastHook<const F*, const Stub*> {  // both must be const [B]
    static const Stub* convert(const F* p) {
        return stub_cast_impl(p, WhichCastDirection<F, Stub>{});
    }
};


//=//// cast(Flex*, ...) //////////////////////////////////////////////////=//

template<typename F>
const Flex* flex_cast_impl(const F* p, UpcastTag) {  // trust upcast [C]
    return u_cast(const Flex*, p);
}

template<typename F>
const Flex* flex_cast_impl(const F* p, DowncastTag) {  // validate [C]
    DECLARE_C_TYPE_LIST(type_list,
        void, Byte, Base, Stub
    );
    STATIC_ASSERT(In_C_Type_List(type_list, F));

    if (not p)
        return nullptr;

    if ((u_cast(const Stub*, p)->leader.bits & (
        BASE_FLAG_BASE | BASE_FLAG_UNREADABLE | BASE_FLAG_CELL
    )) != (
        BASE_FLAG_BASE
    )){
        crash (p);
    }

    return u_cast(const Flex*, p);
}

template<typename F>  // [A]
struct CastHook<const F*, const Flex*> {  // both must be const [B]
    static const Flex* convert(const F* p) {
        return flex_cast_impl(p, WhichCastDirection<F, Flex>{});
    }
};


//=//// cast(Binary*, ...) ////////////////////////////////////////////////=//

template<typename F>
const Binary* binary_cast_impl(const F* p, UpcastTag) {  // trust upcast [C]
    return u_cast(const Binary*, p);
}

template<typename F>
const Binary* binary_cast_impl(const F* p, DowncastTag) {  // validate [C]
    DECLARE_C_TYPE_LIST(type_list,
        void, Byte, Base, Flex
    );
    STATIC_ASSERT(In_C_Type_List(type_list, F));

    if (not p)
        return nullptr;

    const Stub* stub = u_cast(const Stub*, p);

    if ((stub->leader.bits & (
        BASE_FLAG_BASE | BASE_FLAG_UNREADABLE | BASE_FLAG_CELL
    )) != (
        BASE_FLAG_BASE  // BASE_FLAG_UNREADABLE is diminished Stub
    )){
        crash (p);
    }

    impossible(not Stub_Holds_Bytes(stub));  // we *could* check this here

    return u_cast(const Binary*, p);
};

template<typename F>  // [A]
struct CastHook<const F*, const Binary*> {  // both must be const [B]
    static const Binary* convert(const F* p) {
        return binary_cast_impl(p, WhichCastDirection<F, Binary>{});
    }
};


//=//// cast(Strand*, ...) ////////////////////////////////////////////////=//

template<typename F>
const Strand* string_cast_impl(const F* p, UpcastTag) {  // trust upcast [C]
    return u_cast(const Strand*, p);
}

template<typename F>
const Strand* string_cast_impl(const F* p, DowncastTag) {  // validate [C]
    DECLARE_C_TYPE_LIST(type_list,
        void, Byte, Base, Stub, Flex, Binary
    );
    STATIC_ASSERT(In_C_Type_List(type_list, F));

    if (not p)
        return nullptr;

    const Stub* stub = u_cast(const Stub*, p);

    TasteByte taste_byte = TASTE_BYTE(stub);
    if (taste_byte != FLAVOR_NONSYMBOL and taste_byte != FLAVOR_SYMBOL)
        crash (p);

    if ((stub->leader.bits & (
        STUB_MASK_SYMBOL_STRING_COMMON
            | BASE_FLAG_UNREADABLE
            | BASE_FLAG_CELL
    )) !=
        STUB_MASK_SYMBOL_STRING_COMMON
    ){
        assert(stub->leader.bits & STUB_FLAG_CLEANS_UP_BEFORE_GC_DECAY);
        crash (p);
    }

    impossible(not Stub_Holds_Bytes(stub));  // we *could* check this here

    return u_cast(const Strand*, p);
};

template<typename F>  // [A]
struct CastHook<const F*, const Strand*> {  // both must be const [B]
    static const Strand* convert(const F* p) {
        return string_cast_impl(p, WhichCastDirection<F, Strand>{});
    }
};


//=//// cast(Symbol*, ...) ////////////////////////////////////////////////=//

template<typename F>  // [A]
struct CastHook<const F*, const Symbol*> {
    static const Symbol* convert(const F* p) {
        DECLARE_C_TYPE_LIST(type_list,
            void, Byte, Base, Stub, Flex, Binary, Strand
        );
        STATIC_ASSERT(In_C_Type_List(type_list, F));

        if (not p)
            return nullptr;

        const Stub* stub = u_cast(const Stub*, p);
        if ((stub->leader.bits & (
            (STUB_MASK_SYMBOL | STUB_MASK_TASTE)
                | BASE_FLAG_UNREADABLE
                | BASE_FLAG_CELL
        )) !=
            STUB_MASK_SYMBOL
        ){
            crash (p);
        }

        impossible(not Stub_Holds_Bytes(stub));  // we *could* check this here

        return u_cast(const Symbol*, p);
    }
};


//=//// cast(Array*, ...) /////////////////////////////////////////////////=//

template<typename F>
const Array* array_cast_impl(const F* p, UpcastTag) {  // trust upcast [C]
    return u_cast(const Array*, p);
}

template<typename F>
const Array* array_cast_impl(const F* p, DowncastTag) {  // validate [C]
    DECLARE_C_TYPE_LIST(type_list,
        void, Byte, Base, Stub, Flex
    );
    STATIC_ASSERT(In_C_Type_List(type_list, F));

    if (not p)
        return nullptr;

    if ((u_cast(const Stub*, p)->leader.bits & (
        BASE_FLAG_BASE | BASE_FLAG_UNREADABLE | BASE_FLAG_CELL
    )) != (
        BASE_FLAG_BASE
    )){
        crash (p);
    }

    return u_cast(const Array*, p);
};

template<typename F>  // [A]
struct CastHook<const F*, const Array*> {  // both must be const [B]
    static const Array* convert(const F* p) {
        return array_cast_impl(p, WhichCastDirection<F, Array>{});
    }
};


//=//// cast(VarList*, ...) ///////////////////////////////////////////////=//

template<typename F>
const VarList* varlist_cast_impl(const F* p, UpcastTag) {  // trust upcast [C]
    return u_cast(const VarList*, p);
}

template<typename F>
const VarList* varlist_cast_impl(const F* p, DowncastTag) {  // validate [C]
    DECLARE_C_TYPE_LIST(type_list,
        void, Byte, Base, Stub, Flex, Array
    );
    STATIC_ASSERT(In_C_Type_List(type_list, F));

    if (not p)
        return nullptr;

    if ((u_cast(const Stub*, p)->leader.bits & (
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

    return u_cast(const VarList*, p);
};

template<typename F>  // [A]
struct CastHook<const F*, const VarList*> {  // both must be const [B]
    static const VarList* convert(const F* p) {
        return varlist_cast_impl(p, WhichCastDirection<F, VarList>{});
    }
};


//=//// cast(Phase*, ...) ////////////////////////////////////////////////=//

template<typename F>  // [A]
struct CastHook<const F*, const Phase*> {  // both must be const [B]
    static const Phase* convert(const F* p) {
        DECLARE_C_TYPE_LIST(type_list,
            void, Byte, Base, Stub, Flex, Array
        );
        STATIC_ASSERT(In_C_Type_List(type_list, F));

        if (not p)
            return nullptr;

        const Stub* stub = u_cast(const Stub*, p);

        if (TASTE_BYTE(stub) == FLAVOR_DETAILS) {
            if ((stub->leader.bits & (
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
            if ((stub->leader.bits & ((
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

        return u_cast(const Phase*, p);
    }
};
