
//=//// cast(Level*, ...) /////////////////////////////////////////////////=//

template<typename F>
struct CastHelper<const F*, const Level*> {
    static const Level* convert(const F* p) {
        STATIC_ASSERT((
            c_type_list<void,Byte,Node>::contains<F>()
            and not std::is_const<F>::value
        ));

        if (not p)
            return nullptr;

        if ((*u_cast(const Byte*, p) & (
            NODE_BYTEMASK_0x80_NODE | NODE_BYTEMASK_0x40_UNREADABLE
                | NODE_BYTEMASK_0x08_CELL
        )) != (
            NODE_BYTEMASK_0x80_NODE | NODE_BYTEMASK_0x08_CELL
        )){
            crash (p);
        }

        return u_cast(const Level*, p);
    }
};
