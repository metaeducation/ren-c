REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Generic function interface definitions"
    Rights: --{
        Copyright 2012 REBOL Technologies
        Copyright 2012-2018 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }--
    License: --{
        Licensed under the Apache License, Version 2.0.
        See: http://www.apache.org/licenses/LICENSE-2.0
    }--
    Description: --{
        The sense of the term "generic" used here is that of a function which
        has no default implementation--rather each data type supplies its own
        implementation.  The code that runs is based on the argument types:

        https://en.wikipedia.org/wiki/Generic_function
        http://factor-language.blogspot.com/2007/08/mixins.html

        At the moment, only the first argument's type is looked at to choose
        the the dispatch.  This is how common verbs like APPEND or ADD are
        currently implemented.

        !!! The dispatch model in Rebol, and how it might be extended beyond
        the list here (to either more generics, or to user-defined datatypes)
        was not fleshed out, and needs to get attention at some point.
    }--
    Notes: --{
        Historical Rebol called generics "ACTION!"--a term that has been
        retaken for the "one function datatype":

        https://forum.rebol.info/t/596

        This file is executed during the boot process, after collecting its
        top-level SET-WORD! and binding them into the LIB context.  GENERIC
        is an action which quotes its left-hand side--it does this so that
        it knows the symbol that it is being assigned to.  That symbol is
        what is passed in to the "type dispatcher", so each datatype can
        have its own implementation of the generic function.

        The build process scans this file for the SET-WORD!s also, in order
        to add SYM_XXX constants to the word list--so that switch() statements
        in C can be used during dispatch.
    }--
]
