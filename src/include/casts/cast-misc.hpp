
//=//// cast(Level*, ...) /////////////////////////////////////////////////=//

template<typename F>
struct CastHook<const F*, const Level*> {
  DECLARE_C_TYPE_LIST(allowed_from, void, Byte);
  static const bool legal = allowed_from::contains<F>::value;

  static void Validate_Bits(const F* p) {
    if (not p)
        return;

    if (not Is_Base_A_Level(raw_cast(const Base*, p)))
        crash (p);
  }
};
