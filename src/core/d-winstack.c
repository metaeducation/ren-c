//
//  File: %d-winstack.h
//  Summary: "Windows Stack Capture and Reporting"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2020 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This helps generate stack traces.  Valgrind and Address Sanitizer can be
// leveraged to show origins of allocations (so fake allocations followed by
// frees can act as a poor man's way of capturing stack traces at certain
// moments).  On Windows, however, Dr. Memory does not have that feature.
// We use this in pathological debugging situations which only manifest on
// Windows.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Because the debug headers contains several conflicting definitions with
//   the core, it cannot be included without causing conflicts like SYM_TYPE,
//   which can't be simply #undef'd away as with Windows.h `IS_ERROR`, etc.
//   So this file has to stand alone without including %sys-core.h
//
// * The original code this was following was written in C++.  For expedience
//   in development and experimenting with this debug-only feature, it is
//   kept as C++ (for now).
//
// * Debug interface is derived from a code sample by Sean Farrell:
//   http://www.rioki.org/2017/01/09/windows_stacktrace.html
//
// * String interning Trie comes from Loup Valliant:
//   http://loup-vaillant.fr/projects/string-interning/
//

#include "reb-config.h"  // needed for TO_WINDOWS, DEBUG_SERIES_ORIGINS

#if TO_WINDOWS && defined(DEBUG_SERIES_ORIGINS)

#include <windows.h>
#include <stdio.h>
#include <intrin.h>
#include <dbghelp.h>
#include <vector>
#include <string>
#include <sstream>

#pragma comment(lib, "dbghelp.lib")


#include <assert.h>
#include "reb-c.h"

// We cannot include the "tmp-internals.h" headers; Rebol types conflict.
// Must manually export the APIs used by the core when Winstack is enabled.
//
extern "C" void Startup_Winstack(void);
extern "C" void* Make_Winstack_Debug(void);
extern "C" void Print_Winstack_Debug(void*);
extern "C" void Free_Winstack_Debug(void*);
extern "C" void Shutdown_Winstack(void);


// "Trie"-based string interning (since the file paths are absolute, then they
// likely will share a lot of common base path data):
//
// http://loup-vaillant.fr/projects/string-interning/
//
class Trie {
  public:
    unsigned int intern(const std::string & s) {
        unsigned int index = -1;
        unsigned int block = 0;
        for (unsigned int c : s) {
            if (block >= fwd.size()) {
                block = fwd.size();
                fwd.resize(fwd.size() + 256, -1);
                bwd.push_back(index);  // back reference
                if (index != cast(unsigned int, -1))
                    fwd[index] = block;
            }
            index = block + c;
            block = fwd[index];
        }
        return index;
    }

    std::string get(unsigned int index) {
        std::string res;
        while (index != cast(unsigned int, -1)) {
            res.insert(0, 1, index % 256);  // TODO: fix that O(N^2) insertion
            index = bwd[index / 256];
        }
        return res;
    }

  private:
    std::vector<unsigned int> fwd;
    std::vector<unsigned int> bwd;
};

static Trie filenames;
static HANDLE process;
static bool initialized = false;


std::string basename(const std::string & file) {
    unsigned int i = file.find_last_of("\\/");
    if (i == std::string::npos)
        return file;
    return file.substr(i + 1);
}


// This is a "compressed" form of stack frame, designed to pack into a vector.
// Follows pattern in code from this article:
//
// http://www.rioki.org/2017/01/09/windows_stacktrace.html
//
class StackFrame {
  private:
    ULONG64 modBase;  // base address of the .EXE or .DLL
    ULONG index;  // unique value representing symbol in PDB for this run
    unsigned int line;
    unsigned int file_id;  // stored in string interning structure (a "Trie")

  public:
    StackFrame (const STACKFRAME & frame) {
        DWORD64 displacement = 0;

        // !!! Used to save frame.AddrPC.Offset as `address`. Is that
        // interesting to keep or display?

        // Getting symbols is supposed to get their modBase, but it seems
        // to come back with 0.  :-/  This API works however, and would be
        // useful even if SYMBOL_INFO was right, if symbol wasn't found.
        //
        modBase = SymGetModuleBase(process, frame.AddrPC.Offset);

        SYMBOL_INFO symbol;
        symbol.SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol.MaxNameLen = 0;

        if (SymFromAddr(
            process,
            frame.AddrPC.Offset,
            &displacement,
            &symbol
        )) {
            index = symbol.Index;
            /* assert(symbol.ModBase == modBase); */  // !!! comes back 0?
        }
        else {
            DWORD error = GetLastError();
            printf(
                "Failed to get symbol index for %p: %u\n",
                cast(void*, cast(uintptr_t, frame.AddrPC.Offset)),
                cast(unsigned int, error)
            );
            index = 0;  // seems to never be a valid ID (?)
        }

        IMAGEHLP_LINE line_info;
        line_info.SizeOfStruct = sizeof(IMAGEHLP_LINE);

        DWORD offset_ln = 0;
        if (SymGetLineFromAddr(
            process,
            frame.AddrPC.Offset,
            &offset_ln,
            &line_info
        )) {
            // !!! We didn't have to allocate a buffer for the filename, so
            // the pointer we're given is owned by the debug system... for now
            // we assume it will be valid until SymCleanup().  Otherwise we
            // would have to create a table for these.
            //
            file_id = filenames.intern(line_info.FileName);
            line = line_info.LineNumber;
        }
        else {
            // File and line may be unknown in various system stacks/thunks
            //
            file_id = cast(unsigned int, -1);
            line = 0;
        }
    }

    std::string getName() const {
        if (index == 0)
            return "Unknown Function";

        char symbolBuffer[sizeof(SYMBOL_INFO) + 255];
        SYMBOL_INFO *psymbol = cast(SYMBOL_INFO*, symbolBuffer);
        psymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        psymbol->MaxNameLen = 255;  // nullterm, but 1st char in struct

        if (SymFromIndex(process, modBase, index, psymbol))
            return psymbol->Name;

        DWORD error = GetLastError();
        printf(
            "Failed to resolve symbol index %lu: %u\n",
            index,
            cast(unsigned int, error)
        );
        return "Unknown Function";
    }

    std::string getModule() const {
        char moduleBuff[MAX_PATH];
        if (
            modBase &&
            GetModuleFileNameA((HINSTANCE)modBase, moduleBuff, MAX_PATH)
        ) {
            return basename(moduleBuff);
        }

        return "Unknown Module";
    }

    unsigned int getLine() const
        { return line; }

    std::string getFile() const {
        if (file_id == cast(unsigned int, -1))
            return "Unknown File";

        return filenames.get(file_id);
    }
};


//
//  Startup_Winstack: C
//
void Startup_Winstack(void) {
    process = GetCurrentProcess();

    // For compactness, we get function names via indexes into the symbol
    // table (as opposed to copying the string names of functions into each
    // trace).  The indices into the .PDB file are dynamically allocated as
    // we ask for symbols, and will be freed each time you run SymCleanup().
    //
    if (not SymInitialize(
        process,
        NULL,  // paths to look for .PDB files (besides the defaults)
        TRUE  // "invade process", e.g. load symbols for all loaded DLLs
    )) {
        printf("** Failed to call SymInitialize for DEBUG_SERIES_ORIGINS\n");
        return;  // don't crash the executable, let it still run
    }

    SymSetOptions(SYMOPT_LOAD_LINES);  // Get line number information
    initialized = true;
}


//
//  Make_Winstack_Debug: C
//
void* Make_Winstack_Debug(void) {
    if (not initialized)
        return nullptr;

  #if _WIN64
    DWORD machine = IMAGE_FILE_MACHINE_AMD64;
  #else
    DWORD machine = IMAGE_FILE_MACHINE_I386;
  #endif

    HANDLE thread = GetCurrentThread();

    CONTEXT context = {};
    context.ContextFlags = CONTEXT_FULL;
    RtlCaptureContext(&context);

  #if _WIN64
    STACKFRAME frame = {};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
  #else
    STACKFRAME frame = {};
    frame.AddrPC.Offset = context.Eip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Ebp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Esp;
    frame.AddrStack.Mode = AddrModeFlat;
  #endif

    bool first = true;

    auto stack = new std::vector<StackFrame>;
    while (StackWalk(
        machine,
        process,
        thread,
        &frame,
        &context,
        NULL,
        SymFunctionTableAccess,
        SymGetModuleBase,
        NULL
    )){
        if (first) {  // !!! Why throw out first stack frame?
            first = false;
            continue;
        }

        stack->emplace_back(frame);
    }

    stack->shrink_to_fit();  // (may) compact capacity to save on memory
    return stack;
}


//
//  Print_Winstack_Debug: C
//
void Print_Winstack_Debug(void *p) {
    if (not p) {
        assert(p == nullptr);
        printf("** CAN'T PRINT STACK, (null or SymInitialize() failed)\n");
        return;
    }

    auto stack = reinterpret_cast<std::vector<StackFrame>*>(p);

    for (auto frame : *stack) {
        printf(
            "%s (%s:%d) in %s\n",
            frame.getName().c_str(),
            frame.getFile().c_str(),
            frame.getLine(),
            frame.getModule().c_str()
        );
    }
}


//
//  Free_Winstack_Debug: C
//
void Free_Winstack_Debug(void *p) {
    if (p == nullptr)
        return;  // accept nulls (in case uninitialized or not tracked)

    auto stack = reinterpret_cast<std::vector<StackFrame>*>(p);
    delete stack;
}


//
//  Shutdown_Winstack: C
//
void Shutdown_Winstack(void) {
    if (initialized)
        SymCleanup(process);

    initialized = false;
}

#else
    // !!! There is a rule in standard C that you can't have an empty
    // translation unit.  For now, just have an unused variable here.
    //
    char winstack_unused_variable = 0;
#endif
