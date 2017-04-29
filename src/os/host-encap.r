REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Host Script and Resource Embedding Services ('encapping')"
    Rights: {
        Copyright 2017 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Description: {
        Encapping grafts data into an already-compiled executable, to add
        resources to it "after the fact".  Note that there are different
        executable formats used on different operating systems, each with a
        header that tells the operating system how to interpret the file:

        Linux: https://en.wikipedia.org/wiki/Executable_and_Linkable_Format
        Windows: https://en.wikipedia.org/wiki/Portable_Executable
        OS X: https://en.wikipedia.org/wiki/Mach-O

        A "naive" form of adding data into an executable is to append the
        data at the tail, which generally does not affect operation:

        http://stackoverflow.com/a/5801598/211160

        This is a common approach, yet it has some flaws.  e.g. on Linux,
        running the `strip` command will see the added data as irrelevant,
        and remove it.  Other manipulations like adding an icon resource may
        add the icon resource data to the end.  There are other things, like
        executable compression (although some executable compressors are aware
        of this well-known embedding tactic, and account for it).

        It may be reasonable to say that it is the burden of those doing
        executable manipulations to de-encap it, do the modification, and then
        re-encap the executable.  But all things being equal, it's desirable
        to find ways to slipstream the information into the "valid/known"
        resource logic of the OS.

        This can be done with OS-specific tools or system calls, but the
        advantage of writing it standalone as Rebol is that it reduces the
        dependencies.  It allows encapping of executables built on a platform
        different than the one you are running on.  So attempts are made
        here to manipulate the published formats with Rebol code itself.

        For formats not supported currently by the encapper, the simple
        appending strategy is used.
    }
]


;
; https://en.wikipedia.org/wiki/Executable_and_Linkable_Format
;
; The ELF format contains named data sections, and the encap payload is
; injected as one of these sections (with a specific name).  Injecting or
; modifying a section requires updating file offsets in affected headers.
;
; Note: since section headers are fixed-size, the names for the sections are
; zero-terminated strings which are themselves stored in a section.  This
; can be any section (specified in the header as `e_shstrndx`), so processing
; names requires a pre-pass to find it, hence is a little bit convoluted.
;
elf-format: context [
    encap-section-name: ".rebol.encap.1"

    ; (E)LF overall header properties read or written during parse

    EI_CLASS: _
    EI_DATA: _
    EI_VERSION: _
    bits: _             ; 32 or 64
    endian: _           ; 'little or 'big
    e_phoff: _          ; Offset of program header table start.
    e_phnum: _          ; Number of entries in the section header table.
    e_phentsize: _      ; Size of a program header table entry.
    e_shoff: _          ; Offset of section header table start.
    e_shnum: _          ; Number of entries in the section header table.
    e_shentsize: _      ; Size of a section header table entry.
    e_shstrndx: _       ; section header index with section names.

    ; (P)rogram Header properties read or written during parse

    p_type: _
    p_offset: _
    p_filesz: _

    ; (S)ection (H)eader properties extracted during parse

    sh_name: _          ; .shstrtab section offset w/this section's name
    sh_type: _
    sh_flags: _
    sh_addr: _
    sh_offset: _
    sh_size: _
    sh_link: _
    sh_info: _
    sh_addralign: _
    sh_entsize: _

    begin: _            ; Capture position in the series

    ; When parsing a binary header, the properties are either 'read or 'write
    ; In the current update pattern, a read phase is followed by tweaking
    ; the desired parameters, then seeking back and doing a write phase.
    ; For safety, the mode is reset to blank after each rule, to force being
    ; explicit at the callsites.
    ;
    mode: _
    handler: function [name [word!] num-bytes [integer!]] [
        assert [
            binary? begin | num-bytes <= length begin
            | find [read write] mode
        ]

        either mode = 'read [
            bin: copy/part begin num-bytes
            if endian = 'little [reverse bin]
            set name (to-integer/unsigned bin)
        ][
            val: ensure integer! get name
            bin: skip (tail to-binary val) (negate num-bytes) ;-- big endian
            if endian = 'little [reverse bin]
            change begin bin
        ]
    ]

    header-rule: [
        #{7F} "ELF"
        set EI_CLASS skip (bits: either EI_CLASS = 1 [32] [64])
        set EI_DATA skip (endian: either EI_DATA = 1 ['little] ['big])
        set EI_VERSION skip (assert [EI_VERSION = 1])
        skip ; EI_OSABI
        skip ; EI_ABIVERSION
        7 skip ; EI_PAD
        2 skip ; e_type
        2 skip ; e_machine
        4 skip ; e_version
        [
            if (bits = 32) [
                4 skip ; e_entry
                begin: 4 skip (handler 'e_phoff 4)
                begin: 4 skip (handler 'e_shoff 4)
            ]
        |
            if (bits = 64) [
                8 skip ; e_entry
                begin: 8 skip (handler 'e_phoff 8)
                begin: 8 skip (handler 'e_shoff 8)
            ]
        ]
        4 skip ; e_flags
        2 skip ; e_ehsize
        begin: 2 skip (handler 'e_phentsize 2)
        begin: 2 skip (handler 'e_phnum 2)
        begin: 2 skip (handler 'e_shentsize 2)
        begin: 2 skip (handler 'e_shnum 2)
        begin: 2 skip (handler 'e_shstrndx 2)

        (mode: _)
    ]

    program-header-rule: [
        begin: 4 skip (handler 'p_type 4)
        [
            if (bits = 32) [
                begin: 4 skip (handler 'p_offset 4)
                4 skip ; p_vaddr
                4 skip ; p_paddr
                begin: 4 skip (handler 'p_filesz 4)
                4 skip ; p_memsz
            ]
        |
            if (bits = 64) [
                4 skip ; p_flags, different position in 64-bit
                begin: 8 skip (handler 'p_offset 8)
                8 skip ; p_vaddr
                8 skip ; p_paddr
                begin: 8 skip (handler 'p_filesz 8)
                8 skip ; p_memsz
            ]
        ]
        [
            if (bits = 32) [
                4 skip ; p_flags, different position in 32-bit
                4 skip ; p_align
            ]
        |
            if (bits = 64) [
                8 skip ; p_align
            ]
        ]

        (mode: _)
    ]

    section-header-rule: [
        begin: 4 skip (handler 'sh_name 4)
        begin: 4 skip (handler 'sh_type 4)
        [
            if (bits = 32) [
                begin: 4 skip (handler 'sh_flags 4)
                begin: 4 skip (handler 'sh_addr 4)
                begin: 4 skip (handler 'sh_offset 4)
                begin: 4 skip (handler 'sh_size 4)
            ]
        |
            if (bits = 64) [
                begin: 8 skip (handler 'sh_flags 8)
                begin: 8 skip (handler 'sh_addr 8)
                begin: 8 skip (handler 'sh_offset 8)
                begin: 8 skip (handler 'sh_size 8)
            ]
        ]
        begin: 4 skip (handler 'sh_link 4)
        begin: 4 skip (handler 'sh_info 4)
        [
            if (bits = 32) [
                begin: 4 skip (handler 'sh_addralign 4)
                begin: 4 skip (handler 'sh_entsize 4)
            ]
        |
            if (bits = 64) [
                begin: 8 skip (handler 'sh_addralign 8)
                begin: 8 skip (handler 'sh_entsize 8)
            ]
        ]

        (mode: _)
    ]

    find-section: function [
        return: [blank! integer!]
            {The index of the section header with encap (sh_xxx vars set)}
        name [string!]
        section-headers [binary!]
        string-section [binary!]

        <in> self
    ][
        index: 0
        parse section-headers [
            (assert [integer? e_shnum])
            e_shnum [ ; the number of times to apply the rule
                (mode: 'read) section-header-rule
                (
                    name-start: skip string-section sh_name
                    name-end: ensure binary! find name-start #{00}
                    section-name: to-string copy/part name-start name-end
                    if name = section-name [
                        return index ;-- sh_offset, sh_size, etc. are set
                    ]
                    index: index + 1
                )
            ]
        ]
        return blank
    ]

    update-offsets: procedure [
        {Adjust headers to account for insertion or removal of data @ offset}

        executable [binary!]
        offset [integer!]
        delta [integer!]

        <in> self
    ][
        assert [e_phoff < offset] ;-- program headers are before any changes
        unless parse skip executable e_phoff [
            e_phnum [
                (mode: 'read) pos: program-header-rule
                (if p_offset >= offset [p_offset: p_offset + delta])
                (mode: 'write) :pos program-header-rule
            ]
            to end
        ][
            fail "Error updating offsets in program headers"
        ]

        assert [e_shoff >= offset] ;-- section headers are after any changes
        unless parse skip executable e_shoff [
            e_shnum [
                (mode: 'read) pos: section-header-rule
                (if sh_offset >= offset [sh_offset: sh_offset + delta])
                (mode: 'write) :pos section-header-rule
            ]
            to end
        ][
            fail "Error updating offsets in section headers"
        ]
    ]

    remove-section: procedure [
        executable [binary!]
        section-index [integer!]
            {Section to remove (assumed to be current section header)}

        <in> self
    ][
        ; assumes we're already seeked at the section header
    ]

    update-embedding: procedure [
        executable [binary!]
            {Executable to be mutated to either add or update an embedding}
        embedding [binary!]

        <in> self
    ][
        ; Up front, let's check to see if the executable has data past the
        ; tail or not--which indicates some other app added data using the
        ; simple concatenation method of "poor man's encap"
        ;
        section-header-tail: e_shoff + (e_shnum * e_shentsize)
        case [
            section-header-tail = length executable [
                print "Executable has no appended data past ELF image size"
            ]
            section-header-tail > length executable [
                print [
                    "Executable has"
                    (length executable) - section-header-tail
                    "bytes of extra data past the formal ELF image size"
                ]
            ]
            true [
                fail "Section header table in ELF binary is corrupt"
            ]
        ]

        ; The string names of the sections are themselves stored in a section,
        ; (at index `e_shstrndx`)
        ;
        string-header-offset: e_shoff + (e_shstrndx * e_shentsize)
        unless parse skip executable string-header-offset [
            (mode: 'read) section-header-rule to end
        ][
            fail "Error finding string section in ELF binary"
        ]

        string-section-offset: sh_offset
        string-section-size: sh_size

        ; Now that we have the string section, we can go through the
        ; section names and see if there's any match for an existing encap
        ;
        section-index: (
            find-section
                encap-section-name
                skip executable e_shoff ; section headers
                skip executable string-section-offset ; section offset
        )

        either section-index [
            ;
            ; There's already an embedded section, and we're either going to
            ; grow it or shrink it.  We don't have to touch the string table,
            ; though we might wind up displacing it (if the embedded section
            ; somehow got relocated from being the last)
            ;
            print [
                "Embedded section exists ["
                    "index:" section-index
                    "offset:" sh_offset
                    "size:" sh_size
                "]"
            ]

            old-size: sh_size
            new-size: length embedding

            ; Update the size of the embedded section in it's section header
            ;
            parse skip executable e_shoff + (section-index * e_shentsize) [
                (sh_size: new-size)
                (mode: 'write) section-header-rule
            ]

            ; Adjust all the program and section header offsets that are
            ; affected by this movement
            ;
            delta: new-size - old-size
            print ["Updating embedding by delta of" delta "bytes."]
            (update-offsets
                executable
                (sh_offset + old-size) ; offset of change
                delta ; amount of change
            )

            ; With offsets adjusted, delete old embedding, and insert the new
            ;
            remove/part (skip executable sh_offset) old-size
            insert (skip executable sh_offset) embedding

            ; We moved the section headers at the tail of the file, which are
            ; pointed to by the main ELF header.  Updated after branch.
            ;
            e_shoff: e_shoff + delta
        ][
            print "No existing embedded section was found, adding one."

            ; ADD STRING TABLE ENTRY

            ; Loop through all the section and program headers that will be
            ; affected by an insertion (could be 0 if string table is the
            ; last section, could be all of them if it's the first).  Update
            ; their offsets to account for the string table insertion, but
            ; don't actually move any data in `executable` yet.
            ;
            (update-offsets
                executable
                (string-section-offset + string-section-size)
                (1 + length encap-section-name) ; include null terminator
            )

            ; Update string table size in its corresponding header.
            ;
            unless parse skip executable string-header-offset [
                (mode: 'read) pos: section-header-rule
                (
                    assert [sh_offset = string-section-offset]
                    sh_size: sh_size + (1 + length encap-section-name)
                )
                (mode: 'write) :pos section-header-rule
                to end
            ][
                fail "Error updating string table size in string header"
            ]

            ; MAKE NEW SECTION TO BE THE LAST SECTION

            ; Start by cloning the string table section, and assume that its
            ; fields will be mostly okay for the platform.
            ;
            (new-section-header: copy/part
                (skip executable string-header-offset) e_shentsize)

            ; Tweak the fields of the copy to be SHT_NOTE, which is used for
            ; miscellaneous program-specific purposes, and hence not touched
            ; by strip...it is also not mapped into memory.
            ;
            unless parse new-section-header [
                (
                    sh_name: string-section-size ; w.r.t string-section-offset
                    sh_type: 7 ; SHT_NOTE
                    sh_flags: 0
                    sh_size: length embedding
                    sh_offset: e_shoff + (1 + length encap-section-name)
                )
                (mode: 'write) section-header-rule
                to end
            ][
                fail "Error creating new section for the embedded data"
            ]

            ; Append new header to the very end of the section headers.  This
            ; may or may not be the actual end of the executable.  It will
            ; affect no ELF offsets, just the `e_shnum`.
            ;
            insert (skip executable section-header-tail) new-section-header

            ; Do the insertion of the data for the embedding itself.  Since
            ; we're adding it right where the old section headers used to
            ; start, this only affects `e_shoff`.
            ;
            insert (skip executable e_shoff) embedding

            ; Now do the string table insertion, which all the program and
            ; section headers were already adjusted to account for.
            ;
            (insert
                (skip executable string-section-offset + string-section-size)
                (join-of (to-binary encap-section-name) #{00})
            )

            ; We added a section (so another section header to account for),
            ;
            e_shnum: e_shnum + 1

            ; We expanded the string table and added the embedding, so the
            ; section header table offset has to be adjusted.
            ;
            e_shoff: (
                e_shoff
                + (length embedding)
                + (1 + length encap-section-name)
            )

            ; (main header write is done after the branch.)
        ]

        unless parse executable [
            (mode: 'write) header-rule to end
        ][
            fail "Error updating the ELF header"
        ]
    ]

    get-embedding: function [
        return: [binary! blank!]
        file [file!]

        <in> self
    ][
        header-data: read/part file 64 ; 64-bit size, 32-bit is smaller

        if not parse header-data [(mode: 'read) header-rule to end] [
            return blank
        ]

        section-headers-data:
            read/seek/part file e_shoff (e_shnum * e_shentsize)

        ; The string names of the sections are themselves stored in a section,
        ; (at index `e_shstrndx`)
        ;
        unless parse skip section-headers-data (e_shstrndx * e_shentsize) [
            (mode: 'read) section-header-rule to end
        ][
            fail "Error finding string section in ELF binary"
        ]

        string-section-data: read/seek/part file sh_offset sh_size

        ; Now that we have the string section, we can go through the
        ; section names and see if there's any match for an existing encap
        ;
        if not section-index: (
            find-section
                encap-section-name
                section-headers-data
                string-section-data
        )[
            return blank
        ]

        return read/seek/part file sh_offset sh_size
    ]
]


generic-format: context [
    signature: to-binary "ENCAP000"
    sig-length: (length signature)

    update-embedding: procedure [
        executable [binary!]
            {Executable to be mutated to either add or update an embedding}
        embedding [binary!]

        <in> self
    ][
        embed-size: length embedding

        ; The executable we're looking at is already encapped if it ends with
        ; the encapping signature.
        ;
        sig-location: skip tail executable (negate length signature)
        case [
            sig-location = signature [
                print "Binary contains encap version 0 data block."

                size-location: skip sig-location -8
                embed-size: to-integer/unsigned copy/part size-location 8
                print ["Existing embedded data is" embed-size "bytes long."]

                print ["Trimming out existing embedded data."]
                clear skip size-location (negate embed-size)

                print ["Trimmed executable size is" length executable]
            ]
            true [
                print "Binary contains no pre-existing encap data block"
            ]
        ]

        while [0 != modulo (length executable) 4096] [
            append executable #{00}
        ] then [
            print ["Executable padded to" length executable "bytes long."]
        ] else [
            print ["No padding of executable length required."]
        ]

        append executable embedding

        size-as-binary: to-binary length embedding
        assert [8 = length size-as-binary]
        append executable size-as-binary

        append executable signature
    ]

    get-embedding: function [
        file [file!]

        <in> self
    ][
        info: query file

        test-sig: read/seek/part file (info/size - sig-length) sig-length

        if test-sig != signature [return blank]

        embed-size: to-integer/unsigned (
            read/seek/part file (info/size - sig-length - 8) 8
        )

        embed: read/seek/part file (
            info/size - sig-length - 8 - embed-size
        ) embed-size

        return embed
    ]
]


encap: procedure [
    resources [file!]
        {File to embed in the executable (directory zipping/unzipping TBD)}
    /old
    in-rebol-path [file!]
        {Path to the Rebol executable (currently running one is default)}
][
    in-rebol-path: default [system/options/boot]
    out-rebol-path: join-of in-rebol-path "-encap"

    print ["Encapping from original executable:" in-rebol-path]

    executable: read in-rebol-path

    print ["Original executable is" length executable "bytes long."]

    embed: read resources
    print ["New embedded resource size is" length embed "bytes long."]

    compressed: compress embed
    print ["Compressed embedded resource is" length compressed "bytes long."]

    case [
        parse executable [
            (elf-format/mode: 'read) elf-format/header-rule to end
        ][
            print "ELF format found"
            elf-format/update-embedding executable compressed
        ]
        true [
            print "Unidentified executable format, using naive concatenation."

            generic-format/update-embedding executable compressed
        ]
    ]

    print ["Writing executable with encap, size, signature to" out-rebol-path]

    write out-rebol-path executable

    print ["Output executable written with total size" length executable]

    if embed != extracted: get-encap out-rebol-path [
        print ["Test extraction size:" length extracted]
        print ["Embedded bytes" mold embed]
        print ["Extracted bytes" mold extracted]

        fail "Test extraction of embedding did not match original data."
    ]
]


get-encap: function [
    return: [blank! binary!]
    file [file!]
][
    case [
        compressed-data: elf-format/get-embedding file [
            return decompress compressed-data
        ]
        compressed-data: generic-format/get-embedding file [
            return decompress compressed-data
        ]
    ] else [
        return blank
    ]
]
