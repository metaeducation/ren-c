//
//  File: %sys-rebpat.h
//  Summary: {Definitions for the Virtual Bind and Single Variable LET Node}
//  Project: "Ren-C Interpreter and Run-time"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2021 Ren-C Open Source Contributors
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// See %sys-patch.h for a description of virtual binding patches.
//
// There is currently not a separate REBPAT* type (it's just a REBARR) but
// there might need to be one for clarity, eventually.  This file defines the
// flags and layout because they're needed by inline functions before
// %sys-patch.h is included.
//


// The virtual binding patches keep a circularly linked list of their variants
// that have distinct next pointers.  This way, they can look through that
// list before creating an equivalent chain to one that already exists.
//
// !!! This optimization was adding complexity and didn't seem to be hitting
// all that often in practice.  It was removed temporarily in order to free
// up the field in varlists to point to the specifier that was in effect when
// it was created.
//
#define MISC_Variant_TYPE        REBARR*
#define MISC_Variant_CAST        ARR
#define HAS_MISC_Variant         FLAVOR_PATCH


//=//// PATCH_FLAG_REUSED /////////////////////////////////////////////////=//
//
// It's convenient to be able to know when a patch returned from a make call
// is reused or not.  But adding that parameter to the interface complicates
// it, and all not clients care.  There's plenty of bits free on patch array
// flags, so just use one.
//
#define PATCH_FLAG_REUSED \
    SERIES_FLAG_24


//=//// PATCH_FLAG_LET ////////////////////////////////////////////////////=//
//
// This signifies that a patch was made using LET, and hence it doesn't point
// to an object...rather the contents are the variable itself.  The LINK()
// holds the symbol.
//
#define PATCH_FLAG_LET \
    SERIES_FLAG_25



// The LINK() slot in a patch can be:
//
// * a REBSYM, if it's a standalone LET variable; there'd be no way to know
//   its name otherwise.
//
// * a REBCTX, if it's an field in a "sea context" like Lib or User.  In this
//   case, the spelling is found by following the MISC linkages, which are
//   part of the "Hitch" circularly linked list that ends in the symbol
//
// * Currently unused if the payload is for a virtual binding patch.
//
#define INODE_PatchSymbol_TYPE           const REBSYM*
#define INODE_PatchSymbol_CAST           SYM
#define HAS_INODE_PatchSymbol            FLAVOR_PATCH

#define INODE_PatchContext_TYPE          REBCTX*
#define INODE_PatchContext_CAST          CTX
#define HAS_INODE_PatchContext           FLAVOR_PATCH


// Next node is either to another patch, a frame specifier REBCTX, or nullptr.
//
#define LINK_NextPatch_TYPE            REBARR*
#define LINK_NextPatch_CAST            ARR
#define HAS_LINK_NextPatch             FLAVOR_PATCH


#define NextPatch(patch) \
    LINK(NextPatch, patch)
