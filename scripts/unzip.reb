REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Zip and Unzip Services"
    Rights: {
        Copyright 2009 Vincent Ecuyer
        Copyright 2009-2019 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies

        See README.md and CREDITS.md for more information.
    }
    License: {
        Original code from %rebzip.r from www.rebol.org
        Public Domain License
    }
    Notes: {
        Only DEFLATE and STORE methods are supported.

        == archiving: zip ==

        you can zip a single file:
            zip %new-zip.zip %my-file

        a block of files:
            zip %new-zip.zip [%file-1.txt %file-2.exe]

        a block of data (binary!/text!) and files:
            zip %new-zip.zip [%my-file "my data"]

        a entire directory:
            zip/deep %new-zip.zip %my-directory/

        from a url:
            zip %new-zip.zip ftp://192.168.1.10/my-file.txt

        any combination of these:
            zip/deep %new-zip.zip  [
                %readme.txt "An example"
                ftp://192.168.1.10/my-file.txt
                %my-directory
            ]

        == unarchiving: unzip ==

        you can uncompress to a directory (created if it does not exist):
            unzip %my-new-dir %my-zip-file.zip

        or a block:
            unzip my-block %my-zip-file.zip

            my-block == [%file-1.txt #{...} %file-2.exe #{...}]
    }
]

ctx-zip: context [
    local-file-sig: #{504B0304}
    central-file-sig: #{504B0102}
    end-of-central-sig: #{504B0506}
    data-descriptor-sig: #{504B0708}

    to-ilong: (<- enbin [LE + 4])  ; Little endian 4-byte positive integer
    get-ilong: (<- debin [LE + 4])

    to-ishort: (<- enbin [LE + 2])  ; Little endian 2-byte positive integer
    get-ishort: (<- debin [LE + 2])

    to-long: (<- enbin [BE + 4])  ; Big endian 4-byte positive integer

    to-msdos-time: func [
        "Converts to a msdos time."
        value [time!] "AnyValue to convert"
    ][
        to-ishort (value/hour * 2048)
            or+ (value/minute * 32)
            or+ to integer! value/second / 2
    ]

    to-msdos-date: func [
        "Converts to a msdos date."
        value [date!]
    ][
        to-ishort 512 * (max 0 value/year - 1980)
            or+ (value/month * 32) or+ value/day
    ]

    get-msdos-time: func [
        "Converts from a msdos time."
        value [binary! port!]
    ][
        value: get-ishort value
        to time! reduce [
            63488 and+ value / 2048
            2016 and+ value / 32
            31 and+ value * 2
        ]
    ]

    get-msdos-date: func [
        "Converts from a msdos date."
        value [binary! port!]
    ][
        value: get-ishort value
        to date! reduce [
            65024 and+ value / 512 + 1980
            480 and+ value / 32
            31 and+ value
        ]
    ]

    zip-entry: func [
        {Compresses a file}

        return: "local header"
            [binary!]
        central-dir-entry: "Central Directory entry"
            [binary!]

        name "Name of file"
            [file!]
        date "Modification date of file"
            [date!]
        data "Data to compress"
            [binary!]
        offset "Offset where the compressed entry will be stored in the file"
            [integer!]
    ][
        ; info on data before compression
        let crc: checksum-core 'crc32 data

        let uncompressed-size: to-ilong length of data

        let compressed-data: deflate data

        let method
        if (length of compressed-data) < (length of data) [
            method: 'deflate
        ] else [
            method: 'store  ; deflating didn't help

            clear compressed-data  ; !!! doesn't reclaim memory (...FREE ?)
            compressed-data: data
        ]

        let compressed-size: to-ilong length of compressed-data

        ; central-dir file entry.  note that the file attributes are
        ; interpreted based on the OS of origin--can't say Amiga :-(
        ;
        assert [central-dir-entry]  ; currently expects you request it
        set central-dir-entry join-all [
            central-file-sig
            #{1E}  ; version of zip spec this encoder speaks (#{1E}=3.0)
            #{03}  ; OS of origin: 0=DOS, 3=Unix, 7=Mac, 1=Amiga...
            #{0A00}  ; minimum spec version for decoder (#{0A00}=1.0)
            #{0000}  ; flags
            switch method ['store [#{0000}] 'deflate [#{0800}] fail]
            to-msdos-time date/time
            to-msdos-date date/date
            crc  ; crc-32
            compressed-size
            uncompressed-size
            to-ishort length of name  ; filename length
            #{0000}  ; extrafield length
            #{0000}  ; filecomment length
            #{0000}  ; disknumber start
            #{0100}  ; internal attributes (Mac puts #{0100} vs. #{0000})
            #{0000A481}  ; external attributes, this is `-rw-r--r--`
            to-ilong offset  ; header offset
            name  ; filename
            comment <extrafield>  ; not used
            comment <filecomment>  ; not used
        ]

        ; local file entry
        ;
        return join-all [
            local-file-sig
            #{0A00}  ; version (both Mac OS Zip and Linux Zip put #{0A00})
            #{0000}  ; flags
            switch method ['store [#{0000}] 'deflate [#{0800}] fail]
            to-msdos-time date/time
            to-msdos-date date/date
            crc  ; crc-32
            compressed-size
            uncompressed-size
            to-ishort length of name  ; filename length
            #{0000}  ; extrafield length
            name  ; filename
            comment <extrafield>  ; not used
            compressed-data
        ]
    ]

    to-path-file: func [
        {Converts url! to file! and removes heading "/"}
        value [file! url!] "AnyValue to convert"
    ][
        if file? value [
            if #"/" = first value [value: copy next value]
            return value
        ]
        value: decode-url value
        join %"" [
            value/host "/"
            any [value/path ""]
            any [value/target ""]
        ]
    ]

    zip: func [
        {Build zip archive from a file or dialected data specification block}

        return: "Number of entries in archive"
            [integer!]
        where "Where to build the archive (allows series in-memory)"
            [file! url! binary! text!]
        source "Files to archive (only STORE and DEFLATE supported)"
            [file! url! block!]
        /deep "Includes files in subdirectories"
        /verbose "Lists files while compressing"
        /only "Include the root source directory"
    ][
        let info: if not verbose [:elide] else [:print]

        if match [file! url!] where [
            where: open/write where
        ]

        let out: func [value] [append where value]

        let offset: 0
        let num-entries: 0
        let central-directory: copy #{}

        let root
        all [not only, file? source, dir? source] then [
            root: source
            source: read source
        ] else [
            root: %./
        ]

        source: to block! source
        iterate source [
            let name: source/1
            let root+name: if find "\/" name/1 [
                info ["Warning: absolute path" name]
                name
            ] else [%% (root)/(name)]

            let no-modes: did any [url? root+name, dir? root+name]

            all [deep, dir? name] then [
                name: dirize name
                let files: ensure block! read root+name
                for-each file files [
                    append source %% (name)/(file)
                ]
                continue
            ]

            num-entries: num-entries + 1

            ; !!! Zip builds on NOW, however this function comes from an
            ; extension.  Yet the Unzip which lives in this same module is
            ; needed to unpack encapping, and may be required before the
            ; Time extension has been loaded.  A simple reference to NOW
            ; won't find the function that didn't exist at module load time,
            ; so use LIB/NOW to force lookup through LIB.
            ;
            let date: lib/now  ; !!! Each file has slightly later date?

            let data: if match [binary! text!] :source/2 [  ; next is data
                first (source: next source)
            ] else [  ; otherwise data comes from reading the location itself
                if dir? name [
                    copy #{}
                ] else [
                    if not no-modes [
                        date: modified? root+name
                    ]
                    read root+name
                ]
            ]

            if not binary? data [data: to binary! data]

            name: to-path-file name
            info [name]

            let [file-entry dir-entry]: zip-entry name date data offset

            append central-directory dir-entry

            append where file-entry
            offset: me + length of file-entry
        ]

        append where join-all [
            central-directory
            end-of-central-sig
            #{0000}  ; disk num
            #{0000}  ; disk central dir
            to-ishort num-entries  ; num entries disk
            to-ishort num-entries  ; num entries
            to-ilong length of central-directory
            to-ilong offset  ; offset of the central directory
            #{0000}  ; zip file comment length
            comment <zipfilecomment>  ; not used
        ]
        if port? where [close where]
        return num-entries
    ]

    unzip: function [
        {Decompresses a zip archive to a directory or a block}

        return: "If `where` was a block, then position after archive insertion"
            [void! block!]
        where "Where to decompress it"
            [file! block!]
        source "Archive to decompress (only STORE and DEFLATE supported)"
            [file! url! binary!]
        /verbose "Lists files while decompressing (default)"
        /quiet "Don't lists files while decompressing"
    ][
        num-errors: 0
        info: all [quiet, not verbose] then [:elide] else [:print]

        if not block? where [
            where: my dirize
            if not exists? where [make-dir/deep where]
        ]
        if match [file! url!] source [
            source: read source
        ]

        num-entries: 0
        parse source [some [
            to central-file-sig 4 skip
            central-header: here
            [
                ; check coerence between central file header
                ; and local file header
                24 skip
                copy name-length: 2 skip
                (name-length: get-ishort name-length)
                12 skip
                copy local-header-offset: 4 skip
                (flag: (0 <= local-header-offset: get-ilong local-header-offset))
                flag
                (local-header: at source local-header-offset + 1)
                seek local-header
                copy tmp: 4 skip
                (flag: (tmp = local-file-sig))
                flag
                22 skip
                copy tmp: 2 skip
                (tmp: get-ishort tmp)
                (flag: (tmp = name-length))
                flag
                2 skip
                copy name: name-length skip
                seek central-header 42 skip
                copy tmp: name-length skip
                (flag: (name = tmp))
                flag
                ; check successfull
                (
                    name: to-file name
                    info [name]
                )
                seek central-header
                (num-entries: me + 1)
                2 skip ; version made by
                copy version: 2 skip ; version to extract
                copy flags: 2 skip
                (if not zero? flags/1 and+ 1 [return false])
                copy method-number: 2 skip (
                    method-number: get-ishort method-number
                    method: select [0 store 8 deflate] method-number else [
                        method-number
                    ]
                )
                copy time: 2 skip (time: get-msdos-time time)
                copy date: 2 skip (
                    date: get-msdos-date date
                    date/time: time
                    date: date - lib/now/zone  ; see notes RE: LIB/NOW above
                )
                copy crc: 4 skip ; crc-32
                    ; already little endian; int form is `crc: get-ilong crc`
                copy compressed-size: 4 skip
                    (compressed-size: get-ilong compressed-size)
                copy uncompressed-size-raw: 4 skip
                    (uncompressed-size: get-ilong uncompressed-size-raw)
                seek local-header
                local-file-sig
                2 skip ; version
                copy tmp: 2 skip
                (assert [tmp = flags])
                copy tmp: 2 skip
                (assert [method-number = get-ishort tmp])
                copy tmp: 2 skip
                (assert [time = get-msdos-time tmp])
                2 skip ; date
                4 skip ; crc-32
                4 skip ; compressed-size
                4 skip ; uncompressed-size
                copy tmp: 2 skip
                (assert [name-length = get-ishort tmp])
                copy extrafield-length: 2 skip
                    (extrafield-length: get-ishort extrafield-length)
                copy tmp: name-length skip
                (assert [name = to-file tmp])
                extrafield-length skip
                data: here, compressed-size skip
                (
                    uncompressed-data: catch [

                        ; STORE(0) and DEFLATE(8) are the only widespread
                        ; methods used for .ZIP compression in the wild today

                        if method = 'store [
                            throw copy/part data compressed-size
                        ]

                        if method <> 'deflate [
                            info ["-> failed" #"[" "method" method #"]"]
                            throw blank
                        ]
                        data: copy/part data compressed-size
                        trap [
                            data: inflate/max data uncompressed-size
                        ] then [
                            info "-> failed [deflate]"
                            throw blank
                        ]

                        if uncompressed-size != length of data [
                            info "-> failed [wrong output size]"
                            throw blank
                        ]

                        check: checksum-core 'crc32 data
                        if crc != check [
                            info "-> failed [bad crc32]"
                            print [
                                "expected crc:" crc LF
                                "actual crc:" check
                            ]
                            throw data
                        ]

                        throw data
                    ]

                    either uncompressed-data [
                        info ["-> ok" _ #"[" method #"]"]
                    ][
                        num-errors: me + 1
                    ]

                    either any-array? where [
                        where: insert where name
                        where: insert where all [
                            #"/" = last name
                            empty? uncompressed-data
                        ] then [blank] else [uncompressed-data]
                    ][
                        ; make directory and/or write file
                        either #"/" = last name [
                            if not exists? %% (where)/(name) [
                                make-dir/deep %%(where)/(name)
                            ]
                        ][
                            set [path: file:] split-path name
                            if not exists? %% (where)/(path) [
                                make-dir/deep %% (where)/(path)
                            ]
                            if uncompressed-data [
                                write %% (where)/(name) uncompressed-data

                                ; !!! R3-Alpha didn't support SET-MODES
                                comment [
                                    set-modes %% (where)/(name) [
                                        modification-date: date
                                    ]
                                ]
                            ]
                        ]
                    ]
                )
            |   (?? "FAILED")
            ]
            seek central-header
        ]]

        if block? where [return where]
        return
    ]
]

append lib compose [
    zip: (:ctx-zip/zip)
    unzip: (:ctx-zip/unzip)
]
