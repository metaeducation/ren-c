
//=//// cast(Level*, ...) /////////////////////////////////////////////////=//

template<typename F>
struct CastHook<const F*, const Level*> {
  static const bool legal = false;

  static void Validate_Bits(const F* p) {
    if (not p)
        return;

    if (not Is_Base_A_Level(c_cast(const Base*, p)))
        crash (p);
  }
};
