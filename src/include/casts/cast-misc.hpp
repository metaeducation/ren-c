
//=//// cast(Level*, ...) /////////////////////////////////////////////////=//

template<typename V>  // [A]
struct CastHelper<V*,Level*> {  // Level* case [B]
    static Level* convert(V* p) {
        static_assert(
            c_type_list<void,Byte,Node>::contains<V>()
                and not std::is_const<V>::value,
            "Invalid type for downcast to Level*"
        );

        if (not p)
            return nullptr;

        if ((*reinterpret_cast<const Byte*>(p) & (
            NODE_BYTEMASK_0x80_NODE | NODE_BYTEMASK_0x40_UNREADABLE
                | NODE_BYTEMASK_0x08_CELL
        )) != (
            NODE_BYTEMASK_0x80_NODE | NODE_BYTEMASK_0x08_CELL
        )){
            crash (p);
        }

        return reinterpret_cast<Level*>(p);
    }
};

template<typename V>  // [A]
struct CastHelper<V*,const Level*> {  // const Level* case [B]
    static const Level* convert(V* p) = delete;  // no const Level*
};

