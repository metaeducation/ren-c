REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Host Script and Resource Embedding Services ('encapping')"
    Rights: {
        Copyright 2017 Ren-C Open Source Contributors
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

    EI_CLASS: ~
    EI_DATA: ~
    EI_VERSION: ~
    bits: ~             ; 32 or 64
    endian: ~           ; 'little or 'big
    e_phoff: ~          ; Offset of program header table start.
    e_phnum: ~          ; Number of entries in the section header table.
    e_phentsize: ~      ; Size of a program header table entry.
    e_shoff: ~          ; Offset of section header table start.
    e_shnum: ~          ; Number of entries in the section header table.
    e_shentsize: ~      ; Size of a section header table entry.
    e_shstrndx: ~       ; section header index with section names.

    ; (P)rogram Header properties read or written during parse

    p_type: ~
    p_offset: ~
    p_filesz: ~

    ; (S)ection (H)eader properties extracted during parse

    sh_name: ~          ; .shstrtab section offset w/this section's name
    sh_type: ~
    sh_flags: ~
    sh_addr: ~
    sh_offset: ~
    sh_size: ~
    sh_link: ~
    sh_info: ~
    sh_addralign: ~
    sh_entsize: ~

    begin: ~            ; Capture position in the series

    ; When parsing a binary header, the properties are either 'read or 'write
    ; In the current update pattern, a read phase is followed by tweaking
    ; the desired parameters, then seeking back and doing a write phase.
    ; For safety, the mode is reset to blank after each rule, to force being
    ; explicit at the callsites.
    ;
    mode: ~
    handler: func [name [word!] num-bytes [integer!]] [
        assert [
            binary? begin, num-bytes <= length of begin,
            find [read write] ^mode
        ]

        either mode = 'read [
            let bin: copy/part begin num-bytes
            set name debin [(either endian = 'little ['le] ['be]) +] bin
        ][
            let val: ensure integer! get name
            change begin enbin [
                (either endian = 'little ['le] ['be]) + (num-bytes)
            ] val
        ]
    ]

    header-rule: [
        #{7F} "ELF"
        set EI_CLASS skip (bits: either EI_CLASS = 1 [32] [64])
        set EI_DATA skip (endian: either EI_DATA = 1 ['little] ['big])
        set EI_VERSION skip (assert [EI_VERSION = 1])
        skip  ; EI_OSABI
        skip  ; EI_ABIVERSION
        7 skip  ; EI_PAD
        2 skip  ; e_type
        2 skip  ; e_machine
        4 skip  ; e_version
        [
            :(bits = 32) [
                4 skip ; e_entry
                begin: here, 4 skip (handler 'e_phoff 4)
                begin: here, 4 skip (handler 'e_shoff 4)
            ]
        |
            :(bits = 64) [
                8 skip ; e_entry
                begin: here, 8 skip (handler 'e_phoff 8)
                begin: here, 8 skip (handler 'e_shoff 8)
            ]
        ]
        4 skip  ; e_flags
        2 skip  ; e_ehsize
        begin: here, 2 skip (handler 'e_phentsize 2)
        begin: here, 2 skip (handler 'e_phnum 2)
        begin: here, 2 skip (handler 'e_shentsize 2)
        begin: here, 2 skip (handler 'e_shnum 2)
        begin: here, 2 skip (handler 'e_shstrndx 2)

        (mode: _)
    ]

    program-header-rule: [
        begin: here, 4 skip (handler 'p_type 4)
        [
            :(bits = 32) [
                begin: here, 4 skip (handler 'p_offset 4)
                4 skip  ; p_vaddr
                4 skip  ; p_paddr
                begin: here, 4 skip (handler 'p_filesz 4)
                4 skip  ; p_memsz
            ]
        |
            :(bits = 64) [
                4 skip  ; p_flags, different position in 64-bit
                begin: here, 8 skip (handler 'p_offset 8)
                8 skip  ; p_vaddr
                8 skip  ; p_paddr
                begin: here, 8 skip (handler 'p_filesz 8)
                8 skip  ; p_memsz
            ]
        ]
        [
            :(bits = 32) [
                4 skip  ; p_flags, different position in 32-bit
                4 skip  ; p_align
            ]
        |
            :(bits = 64) [
                8 skip  ; p_align
            ]
        ]

        (mode: _)
    ]

    section-header-rule: [
        begin: here, 4 skip (handler 'sh_name 4)
        begin: here, 4 skip (handler 'sh_type 4)
        [
            :(bits = 32) [
                begin: here, 4 skip (handler 'sh_flags 4)
                begin: here, 4 skip (handler 'sh_addr 4)
                begin: here, 4 skip (handler 'sh_offset 4)
                begin: here, 4 skip (handler 'sh_size 4)
            ]
        |
            :(bits = 64) [
                begin: here, 8 skip (handler 'sh_flags 8)
                begin: here, 8 skip (handler 'sh_addr 8)
                begin: here, 8 skip (handler 'sh_offset 8)
                begin: here, 8 skip (handler 'sh_size 8)
            ]
        ]
        begin: here, 4 skip (handler 'sh_link 4)
        begin: here, 4 skip (handler 'sh_info 4)
        [
            :(bits = 32) [
                begin: here, 4 skip (handler 'sh_addralign 4)
                begin: here, 4 skip (handler 'sh_entsize 4)
            ]
        |
            :(bits = 64) [
                begin: here, 8 skip (handler 'sh_addralign 8)
                begin: here, 8 skip (handler 'sh_entsize 8)
            ]
        ]

        (mode: _)
    ]

    find-section: meth [
        return: [<opt> integer!]
            {The index of the section header with encap (sh_xxx vars set)}
        name [text!]
        section-headers [binary!]
        string-section [binary!]
    ][
        let index: 0
        parse section-headers [
            repeat (e_shnum) [
                (mode: 'read) section-header-rule
                (
                    let name-start: skip string-section sh_name
                    let name-end: ensure binary! find name-start #{00}
                    let section-name: to-text copy/part name-start name-end
                    if name = section-name [
                        return index  ; sh_offset, sh_size, etc. are set
                    ]
                    index: index + 1
                )
            ]
            end
        ]
        return null
    ]

    update-offsets: meth [
        {Adjust headers to account for insertion or removal of data @ offset}

        return: <none>
        executable [binary!]
        offset [integer!]
        delta [integer!]
    ][
        assert [e_phoff < offset]  ; program headers are before any changes

        parse skip executable e_phoff [
            repeat (e_phnum) [
                (mode: 'read) let pos: here, program-header-rule
                (if p_offset >= offset [p_offset: p_offset + delta])
                (mode: 'write) seek pos, program-header-rule
            ]
            to end
        ] else [
            fail "Error updating offsets in program headers"
        ]

        assert [e_shoff >= offset]  ; section headers are after any changes

        let pos
        parse skip executable e_shoff [
            repeat (e_shnum) [
                (mode: 'read) let pos: here, section-header-rule
                (if sh_offset >= offset [sh_offset: sh_offset + delta])
                (mode: 'write) seek pos, section-header-rule
            ]
            to end
        ] else [
            fail "Error updating offsets in section headers"
        ]
    ]

    update-embedding: meth [
        return: <none>
        executable [binary!]
            {Executable to be mutated to either add or update an embedding}
        embedding [binary!]
    ][
        ; Up front, let's check to see if the executable has data past the
        ; tail or not--which indicates some other app added data using the
        ; simple concatenation method of "poor man's encap"
        ;
        let section-header-tail: e_shoff + (e_shnum * e_shentsize)
        case [
            section-header-tail = length of executable [
                print "Executable has no appended data past ELF image size"
            ]
            section-header-tail > length of executable [
                print [
                    "Executable has"
                    (length of executable) - section-header-tail
                    "bytes of extra data past the formal ELF image size"
                ]
            ]
        ] else [
            fail "Section header table in ELF binary is corrupt"
        ]

        ; The string names of the sections are themselves stored in a section,
        ; (at index `e_shstrndx`)
        ;
        let string-header-offset: e_shoff + (e_shstrndx * e_shentsize)

        parse skip executable string-header-offset [
            (mode: 'read) section-header-rule to end
        ] else [
            fail "Error finding string section in ELF binary"
        ]

        let string-section-offset: sh_offset
        let string-section-size: sh_size

        ; Now that we have the string section, we can go through the
        ; section names and see if there's any match for an existing encap
        ;
        let section-index: (
            find-section
                encap-section-name
                skip executable e_shoff  ; section headers
                skip executable string-section-offset  ; section offset
        )

        if section-index [
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

            let old-size: sh_size
            let new-size: length of embedding

            ; Update the size of the embedded section in it's section header
            ;
            parse skip executable e_shoff + (section-index * e_shentsize) [
                (sh_size: new-size)
                (mode: 'write) section-header-rule
                end
            ]

            ; Adjust all the program and section header offsets that are
            ; affected by this movement
            ;
            let delta: new-size - old-size
            print ["Updating embedding by delta of" delta "bytes."]
            (update-offsets
                executable
                (sh_offset + old-size)  ; offset of change
                delta  ; amount of change
            )

            ; With offsets adjusted, delete old embedding, and insert the new
            ;
            remove/part (skip executable sh_offset) old-size
            insert (skip executable sh_offset) embedding

            ; We moved the section headers at the tail of the file, which are
            ; pointed to by the main ELF header.  Updated after branch.
            ;
            e_shoff: e_shoff + delta
        ] else [
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
                (1 + length of encap-section-name)  ; include null terminator
            )

            ; Update string table size in its corresponding header.
            ;
            parse skip executable string-header-offset [
                (mode: 'read) let pos: here, section-header-rule
                (
                    assert [sh_offset = string-section-offset]
                    sh_size: sh_size + (1 + length of encap-section-name)
                )
                (mode: 'write) seek pos, section-header-rule
                to end
            ] else [
                fail "Error updating string table size in string header"
            ]

            ; MAKE NEW SECTION TO BE THE LAST SECTION

            ; Start by cloning the string table section, and assume that its
            ; fields will be mostly okay for the platform.
            ;
            let new-section-header: (copy/part
                (skip executable string-header-offset) e_shentsize)

            ; Tweak the fields of the copy to be SHT_NOTE, which is used for
            ; miscellaneous program-specific purposes, and hence not touched
            ; by strip...it is also not mapped into memory.
            ;
            parse new-section-header [
                (
                    sh_name: string-section-size  ; w.r.t string-section-offset
                    sh_type: 7  ; SHT_NOTE
                    sh_flags: 0
                    sh_size: length of embedding
                    sh_offset: e_shoff + (1 + length of encap-section-name)
                )
                (mode: 'write) section-header-rule
                to end
            ] else [
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
                (join (to-binary encap-section-name) #{00})
            )

            ; We added a section (so another section header to account for),
            ;
            e_shnum: e_shnum + 1

            ; We expanded the string table and added the embedding, so the
            ; section header table offset has to be adjusted.
            ;
            e_shoff: (
                e_shoff
                + (length of embedding)
                + (1 + length of encap-section-name)
            )

            ; (main header write is done after the branch.)
        ]

        parse executable [
            (mode: 'write) header-rule to end
        ] else [
            fail "Error updating the ELF header"
        ]
    ]

    get-embedding: meth [
        return: [<opt> binary!]
        file [file!]
    ][
        let header-data: read/part file 64  ; 64-bit size, 32-bit is smaller

        parse header-data [(mode: 'read) header-rule to end] else [
            return null
        ]

        let section-headers-data:
            read/seek/part file e_shoff (e_shnum * e_shentsize)

        ; The string names of the sections are themselves stored in a section,
        ; (at index `e_shstrndx`)
        ;
        parse skip section-headers-data (e_shstrndx * e_shentsize) [
            (mode: 'read) section-header-rule to end
        ] else [
            fail "Error finding string section in ELF binary"
        ]

        let string-section-data: read/seek/part file sh_offset sh_size

        ; Now that we have the string section, we can go through the
        ; section names and see if there's any match for an existing encap
        ;
        let section-index: (
            find-section
                encap-section-name
                section-headers-data
                string-section-data
        ) else [
            return null
        ]

        return read/seek/part file sh_offset sh_size
    ]
]

; The Portable Executable (PE) format is a file format for executables, object
; code, DLLs, FON Font files, and others used in 32-bit and 64-bit versions of
; Windows operating systems.
;
; The official specification is at:
; https://msdn.microsoft.com/en-us/library/windows/desktop/ms680547(v=vs.85).aspx
;
pe-format: context [
    encap-section-name: ".rebolE"  ; Limited to 8 bytes

    buf: ~
    u16: u32: uintptr: ~
    err: null
    fail-at: ~

    u16-le: [copy buf 2 skip (u16: debin [LE + 2] buf)]
    u32-le: [copy buf 4 skip (u32: debin [LE + 4] buf)]

    ; Note: `uintptr-64-le` uses a signed interpretation (+/-) even though it
    ; is supposed to be an unsigned integer the size of a platform pointer in
    ; the actual format (C's <stdint.h> `uintptr_t`).  That's because Ren-C
    ; inherited R3-Alpha's INTEGER! which can hold 64-bit values but is
    ; interpreted as signed.  Hence positive values can only go up to 63 bits.
    ; If arithmetic is not needed and values are merely read and written back
    ; (as this code does), the negative interpretation when the high bit is
    ; set won't cause a problem.
    ;
    uintptr-32-le: [copy buf 4 skip (uintptr: debin [LE + 4] buf)]
    uintptr-64-le: [
        copy buf 8 skip
        (uintptr: debin [LE +/- 8] buf)  ; See note above for why +/-
    ]

    uintptr-le: uintptr-32-le  ; assume 32-bit unless discovered otherwise

    gen-rule: func [
        "Collect set-words in @rule to make into an object saved in @name"
        return: [block!]
        name [word!]
        rule [block!]
        /skip "Do not collect these words"
            [word! block!]
    ][
        let words: skip
        skip: :lib/skip

        let def: make block! 1
        let find-a-word: func [
            return: <none>
            word [any-word!]
        ][
            any [
                find words ^ to word! word
                find def ^ to set-word! word
            ] else [
                append def ^ to set-word! word
            ]
        ]

        either words [
            if word? words [
                words: reduce [words]
            ]
            if locked? words [
                words: copy words
            ]
            append words [err]
        ][
            words: [err]
        ]

        let word
        let block-rule
        let group-rule: [
            set word set-word!
            (find-a-word word)
            | ahead block! into block-rule  ; recursively look into the array
            | skip
        ]
        block-rule: [
            ahead group! into [while group-rule]
            | ahead block! into [while block-rule]
            | ['copy | 'set] set word word! (find-a-word word)
            | skip
        ]

        parse rule [while block-rule] else [fail]

        set name make object! append def [~unset~]
        bind rule get name
    ]

    DOS-header: ~
    pos: ~

    DOS-header-rule: gen-rule 'DOS-header [
        ["MZ" | fail-at: here (err: 'missing-dos-signature) fail]
        u16-le (last-size: u16)
        u16-le (n-blocks: u16)
        u16-le (n-reloc: u16)
        u16-le (header-size: u16)
        u16-le (min-alloc: u16)
        u16-le (max-alloc: u16)
        u16-le (ss: u16)
        u16-le (sp: u16)
        u16-le (checksum: u16)
        u16-le (ip: u16)
        u16-le (cs: u16)
        u16-le (reloc-pos: u16)
        u16-le (n-overlay: u16)
        copy reserved1 4 u16-le
        u16-le (oem-id: u16)
        u16-le (oem-info: u16)
        copy reserved2 10 u16-le
        u32-le (e-lfanew: u32)
    ]

    PE-header-rule: [
        "PE" #{0000} | fail-at: here, (err: 'missing-PE-signature) fail
    ]

    COFF-header: ~
    COFF-header-rule: gen-rule/skip 'COFF-header [
        and [
            #{4c01} (machine: 'i386)
            | #{6486} (machine: 'x86-64 uintptr-le: uintptr-64-le)
            | #{6201} (machine: 'MIPS-R3000)
            | #{6801} (machine: 'MIPS-R10000)
            | #{6901} (machine: 'MIPS-le-WCI-v2)
            | #{8301} (machine: 'old-alpha-AXP)
            | #{8401} (machine: 'alpha-AXP)
            | #{0002} (machine: 'IA64 uintptr-le: uintptr-64-le)
            | #{6602} (machine: 'MIPS16)
        ]
        u16-le (machine-value: u16)
        pos: here, u16-le (
            number-of-sections: u16
            number-of-sections-offset: (index of pos) - 1
        )
        u32-le (time-date-stamp: u32)
        u32-le (pointer-to-symbol-table: u32)
        u32-le (number-of-symbols: u32)
        u16-le (size-of-optional-headers: u16)
        u16-le (chracteristics: u16)
    ] 'uintptr-le

    data-directories: make block! 16
    sections: make block! 8

    PE-optional-header: ~
    PE-optional-header-rule: gen-rule 'PE-optional-header [
        and [#{0b01} (signature: 'exe-32)
             | #{0b02} (signature: 'exe-64)
             | #{0701} (signature: 'ROM)
             | fail-at: here, (err: 'missing-image-signature) fail
        ]
        u16-le (signature-value: u16)
        copy major-linker-version skip  ; 1 byte
        copy minor-linker-version skip  ; 1 byte
        u32-le (size-of-code: u32)
        u32-le (size-of-initialized-data: u32)
        u32-le (size-of-uninialized-data: u32)
        u32-le (address-of-entry-point: u32)
        u32-le (code-base: u32)
        u32-le (data-base: u32)
        u32-le (image-base: u32
            if signature = 'exe-64 [
                image-base: code-base or+ shift image-base 32
                code-base: _
            ])
        u32-le (section-alignment: u32)
        u32-le (file-alignment: u32)
        u16-le (major-OS-version: u16)
        u16-le (minor-OS-version: u16)
        u16-le (major-image-version: u16)
        u16-le (minor-image-version: u16)
        u16-le (major-subsystem-version: u16)
        u16-le (minor-subsystem-version: u16)
        u32-le (win32-version-value: u32)
        pos: here, u32-le (
            image-size: u32
            image-size-offset: (index of pos) - 1
        )
        u32-le (size-of-headers: u32)
        u32-le (checksum: u32)
        and [
            #{0000} (subsystem: 'unknown)
            | #{0100} (subsystem: 'native)
            | #{0200} (subsystem: 'Widnows-GUI)
            | #{0300} (subsystem: 'Windows-CUI)
            | #{0500} (subsystem: 'OS2-CUI)
            | #{0700} (subsystem: 'POSIX-CUI)
            | #{0900} (subsystem: 'Widnows-CE-GUI)
            | #{1000} (subsystem: 'EFI-application)
            | #{1100} (subsystem: 'EFI-boot-service-driver)
            | #{1200} (subsystem: 'EFI-runtime-driver)
            | #{1300} (subsystem: 'EFI-ROM)
            | #{1400} (subsystem: 'Xbox)
            | #{1600} (subsystem: 'Windows-Boot-application)
            | fail-at: here, (err: 'unrecoginized-subsystem), false
        ]
        u16-le (subsystem-value: u16)
        u16-le (dll-characteristics: u16)
        uintptr-le (size-of-stack-reserve: uintptr)
        uintptr-le (size-of-stack-commit: uintptr)
        uintptr-le (size-of-heap-reserve: uintptr)
        uintptr-le (size-of-heap-commit: uintptr)
        u32-le (loader-flags: u32)
        u32-le (number-of-RVA-and-sizes: u32)
    ]

    data-directory: ~
    data-directory-rule: gen-rule 'data-directory [
        u32-le (RVA: u32)
        u32-le (size: u32)
        (append data-directories copy data-directory)
    ]

    section: ~
    section-rule: gen-rule 'section [
        copy name [8 skip]  ; 8 bytes
        u32-le (virtual-size: u32)
        u32-le (virtual-offset: u32)
        u32-le (physical-size: u32)
        u32-le (physical-offset: u32)
        copy reserved [12 skip]  ; 12 bytes
        u32-le (flags: u32)
        (append sections copy section)
    ]

    garbage: ~
    start-of-section-header: ~
    end-of-section-header: ~

    exe-rule: [
        DOS-header-rule
        pos: here, (garbage: DOS-header.e-lfanew + 1 - index of pos)
        repeat (garbage) skip
        PE-header-rule
        COFF-header-rule
        PE-optional-header-rule
        repeat (PE-optional-header.number-of-RVA-and-sizes) data-directory-rule
        start-of-section-header: here
        repeat (COFF-header.number-of-sections) section-rule
        end-of-section-header: here

        ; !!! stop here, no END ?
    ]
    size-of-section-header: 40  ; Size of one entry

    to-u32-le: func [
        i [integer!]
    ][
        reverse skip (to binary! i) 4
    ]

    to-u16-le: func [
        i [integer!]
    ][
        reverse skip (to binary! i) 6
    ]

    align-to: func [
        offset [integer!]
        align [integer!]
    ][
        if zero? let rem: remainder offset align [
            offset
        ] else [
            offset + align - rem
        ]
    ]

    reset: does [
        err: null
        fail-at: ~
        start-of-section-header: ~
        end-of-section-header: ~
        garbage: ~
        comment [DOS-header: ~]
        pos: ~
        comment [PE-optional-header: ~]

        clear sections
        clear data-directories
    ]

    parse-exe: func [
        return: [logic!]
        exe-data [binary!]
    ][
        reset
        parse exe-data exe-rule
        if err = 'missing-dos-signature [
            return false  ; soft failure (just wasn't an EXE, no "MZ")
        ]
        if err [  ; hard failure (was an EXE, but something wrong with it)
            fail ["err:" err, "at:" copy/part fail-at 16]
        ]
        true
    ]

    update-section-header: func [
        return: <none>
        pos [binary!]
        section [object!]
    ][
        change pos let new-section: join-all [
            copy/part (head of insert/dup
                tail of to binary! copy section/name
                #{00}
                8
            ) 8  ; name, must be 8 bytes long

            to-u32-le section.virtual-size
            to-u32-le section.virtual-offset
            to-u32-le section.physical-size
            to-u32-le section.physical-offset

            copy/part (head of insert/dup
                tail of to binary! copy section/reserved
                #{00}
                12
            ) 12  ; reserved, must be 12 bytes long

            if binary? section/flags [
                section/flags
            ] else [
                to-u32-le section/flags
            ]
        ]

        assert [size-of-section-header = length of new-section]
    ]

    add-section: func [
        "Add a new section to the exe, modify in place"
        exe-data [binary!]
        section-name [text!]
        section-data [binary!]
    ][
        parse-exe exe-data

        ; check if there's section name conflicts
        ;
        for-each sec sections [
            if section-name = to text! trim/with sec.name #{00} [
                fail [
                    "There is already a section named" section-name |
                    mold sec
                ]
            ]
        ]

        comment [
            print ["Section headers end at:" index of end-of-section-header]
        ]
    
        sort/compare sections func [a b][a/physical-offset < b/physical-offset]

        let first-section-by-phy-offset: any [
            sections/1
            catch [
                for-each sec sections [
                    if not zero? sec.physical-offset [
                        throw sec
                    ]
                ]
            ]
        ]

        let gap: (
            first-section-by-phy-offset.physical-offset
            - (index of end-of-section-header)
        )
        if gap < size-of-section-header [
            fail "Not enough room for a new section header"
        ]

        ; Increment the "number of sections"
        ;
        change skip exe-data COFF-header.number-of-sections-offset
            to-u16-le (COFF-header.number-of-sections + 1)

        let last-section-by-phy-offset: sections/(COFF-header.number-of-sections)

        sort/compare sections func [a b][a.virtual-offset < b.virtual-offset]

        let last-section-by-virt-offset:
            sections/(COFF-header.number-of-sections)

        let last-virt-offset: align-to
            (last-section-by-virt-offset.virtual-offset
                + last-section-by-virt-offset.virtual-size)
            4096

        let new-section-size: align-to
            (length of section-data)
            PE-optional-header.file-alignment  ; physical size

        let new-section-offset:
            last-section-by-phy-offset.physical-offset
            + last-section-by-phy-offset.physical-size

        assert [zero? new-section-offset mod PE-optional-header.file-alignment]

        ; Set image size, must be a multiple of SECTION-ALIGNMENT
        ;
        change skip exe-data PE-optional-header.image-size-offset
            to-u32-le align-to
                (PE-optional-header.image-size + new-section-size)
                PE-optional-header.section-alignment

        ; add a new section header
        ;
        let new-section-header: make section [
            name: section-name
            virtual-size: length of section-data
            virtual-offset: last-virt-offset
            physical-size: new-section-size
            physical-offset: new-section-offset
            flags: #{40000040}  ; initialized read-only exe-data
        ]

        update-section-header end-of-section-header new-section-header

        print ["current exe-data length" length of exe-data]

        if new-section-offset > length of exe-data [
            print "Last section has been truncated, filling with garbage"
            insert/dup let garbage: copy #{} #{00} (
                new-section-offset - length of exe-data
            )
            print ["length of filler:" length of garbage]
            append exe-data garbage
        ]

        if new-section-size > length of section-data [
            insert/dup let garbage: copy #{} #{00} (
                new-section-size - length of section-data
            )
            section-data: join to binary! section-data garbage
        ]

        assert [
            zero? (length of section-data) mod PE-optional-header.file-alignment
        ]

        ; add the section

        case [
            new-section-offset < length of exe-data [
                print ["There's extra exe-data at the end"]
                insert (skip exe-data new-section-offset) section-data
            ]
            new-section-offset = length of exe-data [
                print ["Appending exe-data"]
                append exe-data section-data
            ]
        ] else [
            fail "Last section has been truncated"
        ]

        head of exe-data
    ]

    find-section: func [
        "Find a section to the exe"
        return: [<opt> binary!]
        exe-data [binary!]
        section-name [text!]
        /header "Return only the section header"
        /data "Return only the section data"
    ][
        trap [
            parse-exe exe-data
        ] then err -> [
            return null
        ]

        ; Check if there's section name conflicts

        let target-sec: try catch [
            for-each sec sections [
                if section-name = to text! trim/with sec.name #{00} [
                    throw sec
                ]
            ]
            return null
        ]

        case [
            header [
                target-sec
            ]
            data [
                ; !!! physical-size looks to be rounded up, e.g. if you wrote
                ; 126 bytes in the section you might get 512 bytes.  The
                ; virtual size looks like what you actually want...but the
                ; virtual offset isn't right, the physical offset is (?)
                ;
                copy/part (skip exe-data target-sec.physical-offset) target-sec.virtual-size
            ]
        ] else [
            reduce [
                target-sec
                copy/part (skip exe-data target-sec.physical-offset) target-sec.physical-size
            ]
        ]
    ]

    update-section: func [
        return: [binary!]
        exe-data [binary!]
        section-name [text!]
        section-data [binary!]
    ][
        ; FIND-SECTION will parse exe-data
        ;
        let target-sec: find-section/header exe-data section-name else [
            return add-section exe-data section-name section-data
        ]

        let new-section-size: align-to
            (length of section-data)
            PE-optional-header.file-alignment

        let section-size-diff: new-section-size - target-sec.physical-size
        if not zero? section-size-diff [
            new-image-size: to-u32-le align-to
                (PE-optional-header.image-size + section-size-diff)
                PE-optional-header.section-alignment

            if new-image-size != PE-optional-header.image-size [
                change skip exe-data PE-optional-header.image-size-offset new-image-size
            ]
        ]

        let pos: start-of-section-header
        for-each sec sections [
            if sec.physical-offset > target-sec.physical-size [
                ;
                ; Update the offset affected sections
                ;
                sec.physical-offset: sec.physical-offset + section-size-diff
                update-section-header pos sec
            ]
            pos: skip pos size-of-section-header
        ]
        remove/part pos: skip exe-data target-sec.physical-offset target-sec.physical-size

        if new-section-size > length of section-data [  ; needs pad with #{00}
            insert/dup garbage: copy #{} #{00} (
                new-section-size - length of section-data
            )
            section-data: join to binary! section-data garbage
        ]
        insert pos section-data

        return (head of exe-data, elide reset)
    ]

    remove-section: func [
        exe-data [binary!]
        section-name [text!]
    ][
        ; FIND-SECTION will parse exe-data
        ;
        let target-sec: find-section/header exe-data section-name

        ; decrement the "number of sections"
        ;
        change skip exe-data COFF-header.number-of-sections-offset
            to-u16-le (COFF-header.number-of-sections - 1)

        image-size-diff: align-to target-sec.physical-size PE-optional-header.section-alignment
        if not zero? image-size-diff [
            change skip exe-data PE-optional-header.image-size-offset
                to-u32-le (PE-optional-header.image-size - image-size-diff)
        ]

        let pos: start-of-section-header
        for-each sec sections [
            print to text! sec.name
            case [
                sec.physical-offset = target-sec.physical-offset [
                    assert [sec.name = target-sec.name]

                    ; Target section, replace with all #{00}
                    ;
                    change pos head of (
                        insert/dup copy #{} #{00} size-of-section-header
                    )

                    ; do not skip @pos, so that the next section will
                    ; overwrite this one if it's not the last section
                ]
                sec.physical-offset > target-sec.physical-offset [
                    ;
                    ; Update the offset affected sections
                    ;
                    sec.physical-offset: sec.physical-offset - target-sec.physical-size
                    update-section-header pos sec
                    pos: skip pos size-of-section-header
                ]
            ] else [  ; Unchanged
                pos: skip pos size-of-section-header
            ]
        ]

        if not (target-sec.physical-offset + 1 = index of pos) [
            ;
            ; If the section to remove is not the last section, the last section
            ; must have moved forward, so erase the old section.
            ;
            change pos head of (
                insert/dup copy #{} #{00} size-of-section-header
            )
        ]

        remove/part skip exe-data target-sec.physical-offset target-sec.physical-size

        return (head of exe-data, elide reset)
    ]

    update-embedding: specialize :update-section [
        section-name: encap-section-name
    ]

    get-embedding: func [
        return: [<opt> binary!]
        file [file!]
    ][
        ;print ["Geting embedded from" mold file]
        let exe-data: read file

        return (find-section/data exe-data encap-section-name, elide reset)
    ]
]

generic-format: context [
    signature: to-binary "ENCAP000"
    sig-length: length of signature

    update-embedding: meth [
        return: <none>
        executable "Executable to mutate to either add or update an embedding"
            [binary!]
        embedding [binary!]
    ][
        let embed-size: length of embedding

        ; The executable we're looking at is already encapped if it ends with
        ; the encapping signature.
        ;
        let sig-location: skip tail of executable (negate length of signature)
        case [
            sig-location = signature [
                print "Binary contains encap version 0 data block."

                let size-location: skip sig-location -8
                embed-size: debin [be +] copy/part size-location 8
                print ["Existing embedded data is" embed-size "bytes long."]

                print ["Trimming out existing embedded data."]
                clear skip size-location (negate embed-size)

                print ["Trimmed executable size is" length of executable]
            ]
        ] else [
            print "Binary contains no pre-existing encap data block"
        ]

        loop [0 != modulo (length of executable) 4096] [
            append executable #{00}
            true
        ] then padded -> [
            if padded [
                print [{Executable padded to} length of executable {bytes.}]
            ] else [
                print {No padding of executable length required.}
            ]
        ]

        append executable embedding

        append executable enbin [be + 8] length of embedding  ; size as binary

        append executable signature
    ]

    get-embedding: meth [
        return: [<opt> binary!]
        file [file!]
    ][
        let info: query file

        let test-sig: read/seek/part file (info/size - sig-length) sig-length

        if test-sig != signature [return null]

        let embed-size: debin [be +] (
            read/seek/part file (info/size - sig-length - 8) 8
        )

        let embed: read/seek/part file (
            info.size - sig-length - 8 - embed-size
        ) embed-size

        return embed
    ]
]


encap: func [
    return: "Path location of the resulting output"
        [file!]
    spec "Single script to embed, directory to zip with main.reb, or dialect"
        [file! block!]
    /rebol "Path to a Rebol to encap instead of using the current one"
        [any-value!]
][
    let in-rebol-path: any [rebol, system.options.boot]
    let base-name-tail: skip tail of in-rebol-path -4
    let out-rebol-path: either ".exe" = base-name-tail [
        join (copy/part in-rebol-path (index of base-name-tail) - 1) "-encap.exe"
    ][
        join in-rebol-path "-encap"
    ]

    print ["Encapping from original executable:" rebol]

    let executable: read in-rebol-path

    print ["Original executable is" length of executable "bytes long."]

    ; !!! Note: LIB. qualifier needed on ZIP due to binding dependency.  "Sea
    ; of words" resolves this problem (not committed to master yet).  Also,
    ; head tuple support is still pending...use GROUP!
    ;
    let compressed: copy #{}
    (:lib.zip)/deep/verbose compressed spec

    print ["Compressed resource is" length of compressed "bytes long."]

    case [
        parse? executable [
            (elf-format.mode: 'read) elf-format.header-rule to end
        ][
            print "ELF format found"
            elf-format.update-embedding executable compressed
        ]
        pe-format.parse-exe executable [
            print "PE format found"
            pe-format.update-embedding executable compressed
        ]
    ] else [
        print "Unidentified executable format, using naive concatenation."

        generic-format.update-embedding executable compressed
    ]

    print ["Writing executable with encap, size, signature to" out-rebol-path]

    write out-rebol-path executable

    print ["Output executable written with total size" length of executable]

    return out-rebol-path
]


get-encap: func [
    return: "NULL if no encapping found, unzipped BLOCK! results if found"
        [<opt> block!]
    rebol-path "The executable to search for the encap information in"
        [file!]
][
    trap [
        read/part rebol-path 1
    ] then e -> [
        print [e]
        print ["Can't check for embedded code in Rebol path:" rebol-path]
        return null
    ]

    let compressed-data: any [
        elf-format.get-embedding rebol-path,
        pe-format.get-embedding rebol-path,
        generic-format.get-embedding rebol-path
    ] else [
        return null
    ]

    ; !!! Note: LIB. qualifier needed on UNZIP due to binding dependency.  "Sea
    ; of words" resolves this problem (not committed to master yet).  Also,
    ; head tuple support is still pending...use GROUP!
    ;
    let block: copy []
    (:lib.unzip)/quiet block compressed-data
    return block
]
