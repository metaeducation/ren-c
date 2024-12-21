//
//  File: %struct-patch.h
//  Summary: "Patch structure definitions preceding %tmp-internals.h"
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
// !!! There were once three separate singular arrays that were all considered
// to be a "patch" type.  This overloading was confusing, and so once Flex
// "flavors" arrived it made more sense to split them out...and make their
// minor overlap in function accounted for explicitly.  Revamping this is
// a work in progress, scheduled for a large-scale revisiting of virtual
// binding and "sea of words", now that some experience has been had with it.
//


//=//// USE_FLAG_REUSED ///////////////////////////////////////////////////=//
//
// It's convenient to be able to know when a virtual binding returned from a
// make call is reused or not.  But adding that parameter to the interface
// complicates it, and all not clients care.  There's plenty of bits free on
// use array flags, so just use one.
//
// !!! This feature currently doesn't seem to be active.
//
#define USE_FLAG_REUSED \
    STUB_SUBCLASS_FLAG_24


//=//// "PATCHES" FOR MODULE VARIABLES ////////////////////////////////////=//
//
// 1. Module variables are in a circularly linked list that includes the
//    symbol series holding that variable's name.  This means the variable
//    can be looked up in that module by following the list reachable through
//    the symbol in a WORD!.  It also means the spelling can be found in
//    that list looking for the symbol.
//
//    LINK: Unused
//    MISC: "Hitch" (links to other patches for other modules, and symbol) [1]
//    INFO: Info_Patch_Sea()


//=//// "LET" FOR VIRTUAL BINDING OF "MINI-OBJECT" ////////////////////////=//
//
// Next node is either to another let, a frame binding context, or nullptr.
//
//    LINK: Link_Inherit_Bind()
//    MISC: Unused
//    INFO: Info_Let_Symbol()




//=//// "USE" FOR VIRTUAL BINDING TO OBJECTS //////////////////////////////=//
//
// !!! Once virtual binding patches kept a circularly linked list of their
// variants with distinct ->Next pointers.  This way, they could look through
// that list before creating an equivalent chain to one that already exists.
// This optimization was adding complexity and didn't seem to be hitting
// all that often in practice.  It was removed for now.
//
//     LINK: Link_Inherit_Bind()
//     MISC: Unused
//     INFO: Unused
