REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Zip and Unzip Services"
    Rights: {
        Copyright 2009 Vincent Ecuyer
        Copyright 2009-2018 Rebol Open Source Contributors
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
    crc-32: func [
        "Returns a CRC32 checksum."
        data [text! binary!] "Data to checksum"
    ][
        copy skip to binary! checksum/method data 'crc32 4
    ]

    ;signatures
    local-file-sig: #{504B0304}
    central-file-sig: #{504B0102}
    end-of-central-sig: #{504B0506}
    data-descriptor-sig: #{504B0708}

    to-ilong: func [
        "Converts an integer to a little-endian long."
        value [integer!] "AnyValue to convert"
    ][
        copy reverse skip to binary! value 4
    ]

    to-ishort: func [
        "Converts an integer to a little-endian short."
        value [integer!] "AnyValue to convert"
    ][
        copy/part reverse skip to binary! value 4 2
    ]

    to-long: func [
        "Converts an integer to a big-endian long."
        value [integer!] "AnyValue to convert"
    ][
        copy skip to binary! value 4
    ]

    get-ishort: func [
        "Converts a little-endian short to an integer."
        value [binary! port!] "AnyValue to convert"
    ][
        to integer! reverse copy/part value 2
    ]

    get-ilong: func [
        "Converts a little-endian long to an integer."
        value [binary! port!] "AnyValue to convert"
    ][
        to integer! reverse copy/part value 4
    ]

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

    zip-entry: function [
        {Compresses a file}
        return: [block!]
            {[local file header + compressed file, central directory entry]}
        name [file!]
            "Name of file"
        date [date!]
            "Modification date of file"
        data [binary!]
            "Data to compress"
        offset [integer!]
            "Offset where the compressed entry will be stored in the file"
    ][
        ; info on data before compression
        crc: head of reverse crc-32 data

        uncompressed-size: to-ilong length of data

        compressed-data: deflate data

        if length of compressed-data < length of data [
            method: 'deflate
        ] else [
            method: 'store ;-- deflating didn't help

            clear compressed-data ;-- !!! doesn't reclaim memory (...FREE ?)
            compressed-data: data
        ]

        compressed-size: to-ilong length of compressed-data

        return reduce [
            ; local file entry
            join-all [
                local-file-sig
                #{0000} ; version
                #{0000} ; flags
                really switch method ['store [#{0000}] 'deflate [#{0800}]]
                to-msdos-time date/time
                to-msdos-date date/date
                crc     ; crc-32
                compressed-size
                uncompressed-size
                to-ishort length of name ; filename length
                #{0000} ; extrafield length
                name    ; filename
                        ; no extrafield
                compressed-data
            ]

            ; central-dir file entry
            join-all [
                central-file-sig
                #{0000} ; version source
                #{0000} ; version min
                #{0000} ; flags
                really switch method ['store [#{0000}] 'deflate [#{0800}]]
                to-msdos-time date/time
                to-msdos-date date/date
                crc     ; crc-32
                compressed-size
                uncompressed-size
                to-ishort length of name ; filename length
                #{0000} ; extrafield length
                #{0000} ; filecomment length
                #{0000} ; disknumber start
                #{0000} ; internal attributes
                #{00000000} ; external attributes
                to-ilong offset ; header offset
                name    ; filename
                        ; extrafield
                        ; comment
            ]
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
        join-of %"" [
            value/host "/"
            any [value/path ""]
            any [value/target ""]
        ]
    ]

    zip: function [
        {Builds a zip archive from a file or block of files.}
        return: [integer!]
            {Number of entries in archive.}
        where [file! url! binary! text!]
            "Where to build it"
        source [file! url! block!]
            "Files to include in archive"
        /deep
            "Includes files in subdirectories"
        /verbose
            "Lists files while compressing"
        /only
            "Include the root source directory"
    ][
        if match [file! url!] where [
            where: open/write where
        ]

        out: func [value] [append where value]

        offset: num-entries: 0
        central-directory: copy #{}

        if not only and [all [file? source | dir? source]] [
            root: source
            source: read source
        ] else [
            root: %./
        ]

        source: to block! source
        iterate source [
            name: source/1
            root+name: if find "\/" name/1 [
                if verbose [print ["Warning: absolute path" name]]
                name
            ] else [root/:name]

            no-modes: (url? root+name) or [dir? root+name]

            if deep and [dir? name] [
                name: dirize name
                files: ensure block! read root+name
                for-each file files [
                    append source name/:file
                ]
                continue
            ]

            num-entries: num-entries + 1
            date: now ;; !!! Each file gets a slightly later compression date?

            ; is next one data or filename?
            data: if match [file! url! blank!] try :source/2 [
                if dir? name [
                    copy #{}
                ] else [
                    if not no-modes [
                        date: modified? root+name
                    ]
                    read root+name
                ]
            ] else [
                first (source: next source)
            ]

            if not binary? data [data: to binary! data]

            name: to-path-file name
            if verbose [print name]

            set [file-entry: dir-entry:] zip-entry name date data offset

            append central-directory dir-entry

            append where file-entry
            offset: me + length of file-entry
        ]

        append where join-all [
            central-directory
            end-of-central-sig
            #{0000} ; disk num
            #{0000} ; disk central dir
            to-ishort num-entries ; num entries disk
            to-ishort num-entries ; num entries
            to-ilong length of central-directory
            to-ilong offset ; offset of the central directory
            #{0000} ; zip file comment length
                    ; zip file comment
        ]
        if port? where [close where]
        return num-entries
    ]

    unzip: function [
        {Decompresses a zip archive with to a directory or a block.}
        where  [file! url! any-array!]
            "Where to decompress it"
        source [file! url! binary!]
            "Archive to decompress (only STORE and DEFLATE methods supported)"
        /verbose
            "Lists files while decompressing (default)"
        /quiet
            "Don't lists files while decompressing"
    ][
        num-errors: 0
        info: either all [quiet | not verbose] [
            func [value] []
        ][
            func [value][prin join-of "" value]
        ]
        if not block? where [
            where: my dirize
            if not exists? where [make-dir/deep where]
        ]
        if match [file! url!] source [
            source: read source
        ]

        num-entries: 0
        parse source [
            to local-file-sig
            some [
                to local-file-sig 4 skip
                (num-entries: me + 1)
                2 skip ; version
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
                    date: date - now/zone
                )
                copy crc: 4 skip (   ; crc-32
                    crc: get-ilong crc
                )
                copy compressed-size: 4 skip
                    (compressed-size: get-ilong compressed-size)
                copy uncompressed-size-raw: 4 skip
                    (uncompressed-size: get-ilong uncompressed-size-raw)
                copy name-length: 2 skip
                    (name-length: get-ishort name-length)
                copy extrafield-length: 2 skip
                    (extrafield-length: get-ishort extrafield-length)
                copy name: name-length skip (
                    name: to-file name
                    info name
                )
                extrafield-length skip
                data: compressed-size skip
                (
                    uncompressed-data: catch [

                        ; STORE(0) and DEFLATE(8) are the only widespread
                        ; methods used for .ZIP compression in the wild today

                        if method = 'store [
                            throw copy/part data compressed-size
                        ]

                        if method <> 'deflate [
                            info ["^- -> failed [method " method "]^/"]
                            throw blank
                        ]

                        data: copy/part data compressed-size
                        if error? trap [
                            data: inflate/max data uncompressed-size
                        ][
                            info "^- -> failed [deflate]^/"
                            throw blank
                        ]

                        if uncompressed-size != length of data [
                            info "^- -> failed [wrong output size]^/"
                            throw blank
                        ]

                        if crc != checksum/method data 'crc32 [
                            info "^- -> failed [bad crc32]^/"
                            print [
                                "expected crc:" crc LF
                                "actual crc:" checksum/method data 'crc32
                            ]
                            throw data
                        ]

                        throw data
                    ]

                    either uncompressed-data [
                        info unspaced ["^- -> ok [" method "]^/"]
                    ][
                        num-errors: me + 1
                    ]

                    either any-array? where [
                        where: insert where name
                        where: insert where either all [
                            #"/" = last name
                            empty? uncompressed-data
                        ][blank][uncompressed-data]
                    ][
                        ; make directory and/or write file
                        either #"/" = last name [
                            if not exists? where/:name [
                                make-dir/deep where/:name
                            ]
                        ][
                            set [path: file:] split-path name
                            if not exists? where/:path [
                                make-dir/deep where/:path
                            ]
                            if uncompressed-data [
                                write where/:name
                                    uncompressed-data
;not supported in R3 yet :-/
;                                set-modes where/:name [
;                                    modification-date: date
;                                ]
                            ]
                        ]
                    ]
                )
            ]
            to end
        ]
        info ["^/"
            "Files/Dirs unarchived: " num-entries "^/"
            "Decompression errors: " num-errors "^/"
        ]
        return zero? num-errors
    ]
]

append lib compose [
    zip: (:ctx-zip/zip)
    unzip: (:ctx-zip/unzip)
]
