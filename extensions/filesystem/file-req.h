// These standard UNIX stat macros not present in "uv.h"
// Found this list in another project that uses libuv (NeoVim)
// https://github.com/neovim/neovim/blob/master/src/nvim/os/os_defs.h
//
#ifndef S_ISDIR
# ifdef S_IFDIR
#  define S_ISDIR(m)    (((m) & S_IFMT) == S_IFDIR)
# else
#  define S_ISDIR(m)    0
# endif
#endif
#ifndef S_ISREG
# ifdef S_IFREG
#  define S_ISREG(m)    (((m) & S_IFMT) == S_IFREG)
# else
#  define S_ISREG(m)    0
# endif
#endif
#ifndef S_ISBLK
# ifdef S_IFBLK
#  define S_ISBLK(m)    (((m) & S_IFMT) == S_IFBLK)
# else
#  define S_ISBLK(m)    0
# endif
#endif
#ifndef S_ISSOCK
# ifdef S_IFSOCK
#  define S_ISSOCK(m)   (((m) & S_IFMT) == S_IFSOCK)
# else
#  define S_ISSOCK(m)   0
# endif
#endif
#ifndef S_ISFIFO
# ifdef S_IFIFO
#  define S_ISFIFO(m)   (((m) & S_IFMT) == S_IFIFO)
# else
#  define S_ISFIFO(m)   0
# endif
#endif
#ifndef S_ISCHR
# ifdef S_IFCHR
#  define S_ISCHR(m)    (((m) & S_IFMT) == S_IFCHR)
# else
#  define S_ISCHR(m)    0
# endif
#endif
#ifndef S_ISLNK
# ifdef S_IFLNK
#  define S_ISLNK(m)    (((m) & S_IFMT) == S_IFLNK)
# else
#  define S_ISLNK(m)    0
# endif
#endif


// The BSD legacy names S_IREAD and S_IWRITE are not defined several places.
// That includes building on Android, or if you compile as C99.

#ifndef S_IREAD
    #define S_IREAD S_IRUSR
#endif

#ifndef S_IWRITE
    #define S_IWRITE S_IWUSR
#endif


#define FILEHANDLE_NONE -1
#define FILESIZE_UNKNOWN UINT64_MAX
#define FILEOFFSET_UNKNOWN UINT64_MAX

typedef struct {
    uv_dir_t *handle;  // stored during directory enumeration
    uv_file id;  // an int, FILEHANDLE_NONE means not open

    // !!! A pointer to a Stable* was originally found in the port spec and
    // pointed to here.  That hinged on the lifetime of the value being the
    // same as the lifetime of the filereq.  However, direct pointers into
    // objects are not used now, since object slots may be abstracted to
    // be getters or typechecked/etc.  So you should instead read the path
    // each time.
    //
    /* Stable* path; */

    // !!! To the extent Ren-C can provide any value in this space at all,
    // one thing it can do is make sure it is unambiguous that all directories
    // are represented by a terminal slash.  It's an uphill battle to enforce
    // this, but perhaps a battle worth fighting.  `is_dir` should thus
    // reflect whether the last character of the path is a slash.
    //
    bool is_dir;

    // Cache of the `flags` argument passed to the open call.
    //
    // !!! Is it worth caching this, or should they be requested if needed?
    // They're not saved in the uv_fs_t req.
    //
    int flags;

    uint64_t size_cache;  // may be FILESIZE_UNKNOWN, use accessors

    uint64_t offset;
} FileReq;

INLINE Option(FileReq*) Filereq_Of_Port(const Stable* port)
{
    VarList* ctx = Cell_Varlist(port);
    Slot* state_slot = Varlist_Slot(ctx, STD_PORT_STATE);
    DECLARE_STABLE (state);
    require (
      Read_Slot(state, state_slot)
    );
    if (Is_Nulled(state))
        return nullptr;  // no filereq, port not open
    if (not Is_Blob(state))
        panic ("Expected filereq to be a BLOB! or NULL");

    return cast(FileReq*, Blob_At_Ensure_Mutable(state));
}
