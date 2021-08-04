//
//  File: %sys-rebctx.h
//  Summary: "context! defs BEFORE %tmp-internals.h"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2020 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
// REBOL is a trademark of REBOL Technologies
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
// See %sys-action.h for information about the workings of REBACT and CONTEXT!
// This file just defines basic structures and flags.
//

#define MISC_VarlistMeta_TYPE      REBCTX*
#define MISC_VarlistMeta_CAST      CTX
#define HAS_MISC_VarlistMeta       FLAVOR_VARLIST


//=//// PARAMLIST_HAS_RETURN //////////////////////////////////////////////=//
//
// See ACT_HAS_RETURN() for remarks.  Note: This is a flag on PARAMLIST, not
// on DETAILS...e.g. the varlist of an exemplar context.
//
#define VARLIST_FLAG_PARAMLIST_HAS_RETURN \
    SERIES_FLAG_24


//=//// FRAME_HAS_BEEN_INVOKED ////////////////////////////////////////////=//
//
// It is intrinsic to the design of Redbols that they are allowed to mutate
// their argument cells.  Hence if you build a frame and then DO it, the
// arguments will very likely be changed.  Being able to see these changes
// from the outside in non-debugging cases is dangerous, since it's part of
// the implementation detail of the function (like how it handles locals)
// and is not part of the calling contract.
//
// This is why you can't say things like `loop 2 [do frame]`...the first time
// you do the frame it could be arbitrarily corrupted.  Instead you must copy
// the frame on all but the last time (e.g. `do copy frame, do frame`)
//
// The initial implementation of DO of FRAME! would actually create a new
// varlist node and move the data to it--expiring the old node.  That is
// expensive, so the cheaper way to do it is to set a flag on the frame.
// Then, if a frame is archetypal (no phase) it can check this flag before
// a DO and say the frame can't be run again...nor can fields be assigned
// or read any longer.
//
// !!! This may not be the best place to put this flag, review.
//
#define VARLIST_FLAG_FRAME_HAS_BEEN_INVOKED \
    SERIES_FLAG_24


#define CELL_MASK_CONTEXT \
    (CELL_FLAG_FIRST_IS_NODE  /* varlist */ \
        | CELL_FLAG_SECOND_IS_NODE  /* phase (for FRAME!) */)



// A context's varlist is always allocated dynamically, in order to speed
// up variable access--no need to test USED_BYTE_OR_255 for 255.
//
// !!! Ideally this would carry a flag to tell a GC "shrinking" process not
// to reclaim the dynamic memory to make a singular cell...but that flag
// can't be SERIES_FLAG_FIXED_SIZE, because most varlists can expand.
//
#define SERIES_MASK_VARLIST \
    (NODE_FLAG_NODE | SERIES_FLAG_DYNAMIC \
        | FLAG_FLAVOR(VARLIST) \
        | SERIES_FLAG_LINK_NODE_NEEDS_MARK  /* keysource */ \
        | SERIES_FLAG_MISC_NODE_NEEDS_MARK  /* meta */)

#define SERIES_MASK_KEYLIST \
    (NODE_FLAG_NODE  /* NOT always dynamic */ \
        | FLAG_FLAVOR(KEYLIST) \
        | SERIES_FLAG_LINK_NODE_NEEDS_MARK  /* ancestor */ )


inline static REBARR *CTX_VARLIST(REBCTX *ctx)
  { return x_cast(REBARR*, ctx); }  // ARR() has debug cost, not defined yet
