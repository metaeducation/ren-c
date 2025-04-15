extern Option(ErrorValue*) Trap_Get_Environment_Variable(
    Sink(Option(Value*)) out,  // nullptr means not set
    const Value* key  // Note: POSIX mandates case-sensitive keys
);

extern Option(ErrorValue*) Trap_Set_Environment_Variable(
    const Value* key,  // Note: POSIX mandates case-sensitive keys
    Option(const Value*) value  // nullptr means unset
);

extern Option(ErrorValue*) Trap_List_Environment(Sink(Value*) map_out);
