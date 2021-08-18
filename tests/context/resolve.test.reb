; functions/context/resolve.r
;
; RESOLVE was a function that was theoretically somewhat simple...that it would
; let you give a list of words that you wanted to transfer the keys of from
; one context to another.  In practice there are a lot of variant behaviors,
; regarding whether you want to add keys that don't exist yet or only update
; variables that are common between the two contexts.
;
; Historically this was offered for ANY-CONTEXT!.  But its only notable use was
; as the mechanism by which the IMPORT command would transfer the variables
; named by the `Exports:` block of a module to the module that was doing the
; importing.  Some of the most convoluted code dealt with managing the large
; growing indexes of modules as items were added.
;
; Ren-C's "Sea of Words" model means MODULE! leverages the existing hash table
; for global symbols.  The binding tables and complex mechanics behind RESOLVE
; are thus not necessary for that purpose.  So at time of writing, RESOLVE has
; been pared back to only that function, and only on MODULE!.
;
; Longer term it seems that RESOLVE should be folded into a more traditional
; EXTEND primitive, perhaps with a /WORDS refinement to take a BLOCK! of words.


; !!! This was the only R3-Alpha RESOLVE test relating to a bug with usage
; of both the /EXTEND and /ONLY refinements.  Those refinements do not exist
; at the time of writing.
;
[#2017
    (get in resolve (module [] []) (module [] [a: true]) [a] 'a)
]
