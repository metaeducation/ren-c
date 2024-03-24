REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Zip and Unzip Services"
    Rights: {
        Copyright 2009 Vincent Ecuyer
        Copyright 2009-2020 Rebol Open Source Contributors
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

        if (length of compressed-data) < (length of data) [
            method: 'deflate
        ] else [
            method: 'store  ; deflating didn't help

            clear compressed-data  ; !!! doesn't reclaim memory (...FREE ?)
            compressed-data: data
        ]

        compressed-size: to-ilong length of compressed-data

        return reduce [
            ; local file entry
            to binary! reduce [
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

            ; central-dir file entry.  note that the file attributes are
            ; interpreted based on the OS of origin--can't say Amiga :-(
            ;
            to binary! reduce [
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
        join %"" unspaced [
            value/host "/"
            any [value/path ""]
            any [value/target ""]
        ]
    ]

    zip: function [
        {Builds a ZIP archive from a file or block of files.}
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

        if (not only) and [all [file? source | dir? source]] [
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
            date: now  ; !!! Each file gets a slightly later compression date?

            ; is next one data or filename?
            data: if match [file! url!] :source/2 [
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

        append where to binary! reduce [
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
        {Decompresses a ZIP archive with to a directory or a block.}
        where [file! url! any-array!]
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
            func [value][prin form value]
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
                to local-file-sig repeat 4 one
                (num-entries: me + 1)
                repeat 2 one  ; version
                flags: across repeat 2 one
                    (if not zero? flags/1 and+ 1 [return false])
                method-number: across repeat 2 one (
                    method-number: get-ishort method-number
                    method: select [0 store 8 deflate] method-number else [
                        method-number
                    ]
                )
                time: across repeat 2 one (time: get-msdos-time time)
                date: across repeat 2 one (
                    date: get-msdos-date date
                    date/time: time
                    date: date - now/zone
                )
                crc: across repeat 4 one (   ; crc-32
                    crc: get-ilong crc
                )
                compressed-size: across repeat 4 one
                    (compressed-size: get-ilong compressed-size)
                uncompressed-size-raw: across repeat 4 one
                    (uncompressed-size: get-ilong uncompressed-size-raw)
                name-length: across repeat 2 one
                    (name-length: get-ishort name-length)
                extrafield-length: across repeat 2 one
                    (extrafield-length: get-ishort extrafield-length)
                name: across repeat (name-length) one (
                    name: to-file name
                    info name
                )
                repeat (extrafield-length) one
                data: <here> repeat (compressed-size) one
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
                        trap [
                            data: inflate/max data uncompressed-size
                        ] then [
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
                            path: split-path/file name the file:
                            if not exists? where/:path [
                                make-dir/deep where/:path
                            ]
                            if uncompressed-data [
                                write where/:name uncompressed-data

                                ; !!! R3-Alpha didn't support SET-MODES
                                comment [
                                    set-modes where/:name [
                                        modification-date: date
                                    ]
                                ]
                            ]
                        ]
                    ]
                )
            ]
            to <end>
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
