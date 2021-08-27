
// !!! Hack used for making a 64-bit value as a struct, which works in
// 32-bit modes.  64 bits, even in 32 bit mode.  Based on the deprecated idea
// that "devices" would not have access to Rebol datatypes, and hence would
// not be able to communicate with Rebol directly with a TIME! or DATE!.
// To be replaced.
//
// (Note: compatible with FILETIME used in Windows)
//
#pragma pack(4)
typedef struct sInt64 {
    int32_t l;
    int32_t h;
} FILETIME_DEVREQ;
#pragma pack()

// RFM - REBOL File Modes
enum {
    RFM_OPEN = 1 << 0,
    RFM_READ = 1 << 1,
    RFM_WRITE = 1 << 2,
    RFM_APPEND = 1 << 3,
    RFM_SEEK = 1 << 4,
    RFM_NEW = 1 << 5,
    RFM_READONLY = 1 << 6,
    RFM_TRUNCATE = 1 << 7,
    RFM_RESEEK = 1 << 8,  // file index has moved, reseek
    RFM_DIR = 1 << 9,
    RFM_TEXT = 1 << 10  // on appropriate platforms, translate LF to CR LF
};

struct devreq_file {
    void *handle;  // windows uses for file, posix uses for directory
    int id;  // posix uses for file

    const REBVAL *path;     // file string (in OS local format)
    uint32_t modes;         // special modes (is directory, etc. see RFM_XXX)
    int64_t size;           // file size
    int64_t index;          // file index position
    FILETIME_DEVREQ time;   // file modification time (struct)
};

typedef struct devreq_file FILEREQ;

extern REBVAL *File_Time_To_Rebol(FILEREQ *file);
extern REBVAL *Query_File_Or_Dir(const REBVAL *port, FILEREQ *file);

#ifdef TO_WINDOWS
    #define OS_DIR_SEP '\\'  // file path separator (Thanks Bill.)
#else
    #define OS_DIR_SEP '/'  // rest of the world uses it
#endif
