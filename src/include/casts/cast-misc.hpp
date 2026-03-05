
//=//// cast(Level*, ...) /////////////////////////////////////////////////=//

template<typename F>
struct CastHook<const F*, const Level*> {
  static void Validate_Bits(const F* p) {
    DECLARE_C_TYPE_LIST(type_list,
        void, Byte, Base
    );
    STATIC_ASSERT(In_C_Type_List(type_list, F));

    if (not p)
        return;

    if (not Is_Base_A_Level(u_cast(const Base*, p)))
        crash (p);
  }
};
