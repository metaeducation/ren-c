
//=//// cast(Level*, ...) /////////////////////////////////////////////////=//

template<typename F>
struct CastHook<const F*, const Level*> {
    static const Level* convert(const F* p) {
        DECLARE_C_TYPE_LIST(type_list,
            void, Byte, Base
        );
        STATIC_ASSERT(In_C_Type_List(type_list, F));

        if (not p)
            return nullptr;

        if ((*u_cast(const Byte*, p) & (
            BASE_BYTEMASK_0x80_NODE | BASE_BYTEMASK_0x40_UNREADABLE
                | BASE_BYTEMASK_0x08_CELL
        )) != (
            BASE_BYTEMASK_0x80_NODE | BASE_BYTEMASK_0x08_CELL
        )){
            crash (p);
        }

        return u_cast(const Level*, p);
    }
};
