//
//  file: %needful-poison.h
//  summary: "Poisoning memory helpers (reversible corruption with alerts)"
//  homepage: <needful homepage TBD>
//
//=/////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2015-2025 hostilefork.com
//
// Licensed under the MIT License
//
// https://en.wikipedia.org/wiki/MIT_License
//
//=/////////////////////////////////////////////////////////////////////////=//
//
// This should add the "poor man's poison" from the article:
//
//   http://blog.hostilefork.com/poison-memory-without-asan/
//
// It might be useful to instrument C++-based builds on platforms that do not
// have address sanitizer (if that ever becomes interesting).
//


//=//// MEMORY POISONING and POINTER CORRUPTING ///////////////////////////=//
//
// If one wishes to indicate a region of memory as being "off-limits", modern
// tools like Address Sanitizer allow instrumented builds to augment reads
// from memory to check to see if that region is in a blacklist.
//
// These "poisoned" areas are generally sub-regions of valid malloc()'d memory
// that contain bad data.  Yet they cannot be free()d because they also
// contain some good data.  (Or it is merely desirable to avoid freeing and
// then re-allocating them for performance reasons, yet a checked build still
// would prefer to intercept accesses as if they were freed.)
//
// Also, in order to overwrite a pointer with garbage, the historical method
// of using 0xBADF00D or 0xDECAFBAD is formalized in Corrupt_If_Needful().
// This makes the instances easier to find and standardizes how it is done.
//
// 1. <IMPORTANT>: Address sanitizer's memory poisoning must not have two
//    threads both poisoning/unpoisoning the same addresses at the same time.
//
// 2. @HostileFork wrote a tiny C++ "poor man's memory poisoner" that uses
//    XOR to poison bits and then unpoison them back.
//
//        http://blog.hostilefork.com/poison-memory-without-asan/
//
#if __has_feature(address_sanitizer)
    #include <sanitizer/asan_interface.h>

    #define ATTRIBUTE_NO_SANITIZE_ADDRESS __attribute__ ((no_sanitize_address))

    #define Poison_Memory_If_Sanitize(reg, mem_size) \
        ASAN_POISON_MEMORY_REGION(reg, mem_size)  // one thread at a time [1]

    #define Unpoison_Memory_If_Sanitize(reg, mem_size) \
        ASAN_UNPOISON_MEMORY_REGION(reg, mem_size)  // one thread at a time [1]
#else
    #define ATTRIBUTE_NO_SANITIZE_ADDRESS  // cheap approaches possible [2]

    #define Poison_Memory_If_Sanitize(reg, mem_size)    NOOP
    #define Unpoison_Memory_If_Sanitize(reg, mem_size)  NOOP
#endif
