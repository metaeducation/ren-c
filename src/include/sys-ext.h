// Extension entry point functions:


#if defined(EXT_DLL) // External extensions
#if defined(REB_EXE)
#define EXT_API EXTERN_C API_IMPORT
#else
#define EXT_API EXTERN_C API_EXPORT
#endif
#define EXT_INIT(e) RX_Init
#define EXT_QUIT(e) RX_Quit
#else // Builtin extensions
#define EXT_API EXTERN_C
#define EXT_INIT(e) RX_Init_ ## e
#define EXT_QUIT(e) RX_Quit_ ## e
#endif

typedef int (*INIT_CFUNC)(REBVAL *, REBVAL *);
typedef int (*QUIT_CFUNC)(void);

// Extension macros
#define DECLARE_EXT_INIT(e) \
EXT_API int EXT_INIT(e) (REBVAL *header, REBVAL *out)

#define DEFINE_EXT_INIT(e, script_bytes, code) \
EXT_API int EXT_INIT(e) (REBVAL *script, REBVAL *out) \
{\
    code \
    Init_Binary(script, Copy_Bytes(script_bytes, sizeof(script_bytes) - 1)); \
    return 0;\
}

#define DEFINE_EXT_INIT_COMPRESSED(e, script_bytes, code) \
EXT_API int EXT_INIT(e) (REBVAL *script, REBVAL *out) \
{\
    code \
    /* binary does not have a \0 terminator */ \
    REBCNT utf8_size; \
    const REBINT max = -1; \
    REBYTE *utf8 = rebGunzipAlloc( \
        &utf8_size, script_bytes, sizeof(script_bytes), max \
    ); \
    REBVAL *bin = rebRepossess(utf8, utf8_size); \
    Move_Value(script, bin); \
    rebRelease(bin); /* should just return the BINARY! REBVAL* */ \
    return 0;\
}

#define DECLARE_EXT_QUIT(e) \
EXT_API int EXT_QUIT(e) (void)

#define DEFINE_EXT_QUIT(e, code) \
EXT_API int EXT_QUIT(e) (void) code

#define LOAD_EXTENSION(exts, e) do {           \
    Add_Boot_Extension(exts, EXT_INIT(e), EXT_QUIT(e));     \
} while(0)

// Module macros
#define DECLARE_MODULE_INIT(m) int Module_Init_ ## m (REBVAL* out)
#define CALL_MODULE_INIT(m) Module_Init_ ## m (out)

#define DECLARE_MODULE_QUIT(m) int Module_Quit_ ## m ()
#define CALL_MODULE_QUIT(m) Module_Quit_ ## m ()
