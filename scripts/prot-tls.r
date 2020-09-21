REBOL [
    Title: "REBOL 3 TLS Client 1.0 - 1.2 Protocol Scheme"
    Name: tls
    Type: module
    Version: 0.7.0
    Rights: {
        Copyright 2012 Richard "Cyphre" Smolak (TLS 1.0)
        Copyright 2012-2018 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0.
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Description: {
        This is an implementation of a TLS client layer, which can be used in
        lieu of a plain TCP scheme for providing network connectivity.  e.g.
        the HTTPS scheme is the same code as the HTTP scheme, only using this
        TLS scheme instead of a TCP connection underneath.

        Only the client side of the protocol is implemented ATM.  Adapting
        it to work as a server (e.g. for use with %httpd.reb) would be more
        work.  But it would involve most of the same general methods, as the
        protocol is fairly symmetrical.

                            !!! IMPORTANT WARNING !!!

        While this code encrypts communication according to the TLS protocol
        it does not yet validate certificates.  So it's not checking a site's
        credentials against a trusted certificate chain installed on the local
        machine (the way a web browser would).  This makes it vulnerable to
        man-in-the-middle attacks:

        https://en.wikipedia.org/wiki/Man-in-the-middle_attack

        It also doesn't validate signatures, but for a different reason...many
        of the signature routines are not built into the executable.  So it
        lies and says it can check things it cannot, in order to prevent a
        server from refusing to talk to it at all.

        This is an unfortunate legacy of the code as it was inherited from
        Saphirion, which there has not been time or resources to devote to
        improving.  (Keeping it running in an ever-evolving cryptographic
        environment is nearly a full time job for someone in and of itself.)
        However, it does serve as a starting point for anyone interested in
        hacking on a better answer in usermode Rebol.
    }
    Notes: {
        At time of writing (Sept 2018), TLS 1.0 and TLS 1.1 are in the process
        of formal deprecation by the IETF.  In the meantime, the payment card
        industry (PCI) set a deadline of 30-Jun-2018 for sites to deprecate
        1.0, with strong recommendation to deprecate 1.1 as well:

        https://www.thesslstore.com/blog/june-30-to-disable-tls-1-0/

        TLS 1.2 is currently requested by the client.  Legacy TLS 1.0 support
        from Cyphre's original code, as well as 1.1 support, are retained.
        But it may be better if the client refuses to speak to servers that
        respond they only support those protocols.

        SSL-v2 and SSL-v3 were deprecated in 2011 and 2015 respectively, and
        %prot-tls.r never supported or tested SSL.  No support is planned:

        https://tools.ietf.org/html/rfc6176
        https://tools.ietf.org/html/rfc7568
    }
    Todo: {
        - cached sessions
        - automagic cert data lookup
        - add more cipher suites
        - server role support
        - cert validation
        - TLS 1.3
        - incorporate native BigNum INTEGER! math when available to do more
          modular usermode protocols that can be pulled on demand (vs. needing
          ever more native C crypto code baked in)
    }
]

; !!! There was a /SECURE refinement to RANDOM, which implemented the
; following after generating the REBI64 into a tmp variable:
;
;     REBYTE srcbuf[20], dstbuf[20];
;
;     memcpy(srcbuf, &tmp, sizeof(tmp));
;     memset(srcbuf + sizeof(tmp), *(REBYTE*)&tmp, 20 - sizeof(tmp));
;
;     SHA1(srcbuf, 20, dstbuf);
;     memcpy(&tmp, dstbuf, sizeof(tmp));
;
; It's not entirely clear how much more secure that makes it.  In any case,
; SHA1 was removed from the core.  We could do it in userspace if it were
; deemed important.
;
random-secure: function [range [integer!]] [random range]


version-to-bytes: [
    1.0 #{03 01}
    1.1 #{03 02}
    1.2 #{03 03}
]
bytes-to-version: reverse copy version-to-bytes


; CURRENTLY SUPPORTED CIPHER SUITES
;
; https://testssl.sh/openssl-rfc.mapping.html
; https://fly.io/articles/how-ciphersuites-work/
;
; If you want to get a report on what suites a particular site has:
;
; https://www.ssllabs.com/ssltest/analyze.html
;
; This list is sent to the server when negotiating which one to use.  Hence
; it should be ORDERED BY CLIENT PREFERENCE (more preferred suites first).
;
; In TLS 1.2, the cipher suite also specifies a hash used by the pseudorandom
; function (PRF) implementation, and to make the seed fed into the PRF when
; generating `verify_data` in a `Finished` message.  All cipher specs that
; are named in the RFC use SHA256, but others are possible.  Additionally, the
; length of `verify_data` is part of the cipher spec, with 12 as default for
; the specs in the RFC.  The table should probably encode these choices.
;
; If ECDHE is mentioned in the cipher suite, then that doesn't imply a
; specific elliptic curve--but rather a *category* of curves.  Hence another
; layer of negotiation is slipstreamed into TLS 1.2 via a "protocol extension"
; to narrow it further, where the client offers which curves it has.  We only
; support secp256r1 for now, as a kind of "lowest common denominator".
;
; RC4-based cipher suites present in the original Saphirion implementation are
; removed from the negotiation list based on a 2015 ruling from the IETF:
;
; https://tools.ietf.org/html/rfc7465
;
cipher-suites: compose [
    ;
    ; #{XX XX} [  ; two byte cipher suite identification code
    ;    CIPHER_SUITE_NAME
    ;    <key-exchange> @block-cipher [...] #message-authentication [...]
    ; ]

    #{C0 14} [
        TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA
        <ecdhe-rsa> @aes [size 32 block 16 iv 16] #sha1 [size 20]
    ]

    #{C0 13} [
        TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA
        <ecdhe-rsa> @aes [size 16 block 16 iv 16] #sha1 [size 20]
    ]

    ; The Discourse server on forum.rebol.info offers these choices too:
    ;
    ; TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256 (0xc02f)
    ; TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384 (0xc030)
    ; TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA384 (0xc028)  "weak"
    ; TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256 (0xc027)  "weak"
    ;
    ; We don't have "GCM" or SHA384 at this time.

    #{00 2F} [
        TLS_RSA_WITH_AES_128_CBC_SHA
        <rsa> @aes [size 16 block 16 iv 16] #sha1 [size 20]
    ]
    #{00 35} [
        TLS_RSA_WITH_AES_256_CBC_SHA  ; https://example.com will do this one
        <rsa> @aes [size 32 block 16 iv 16] #sha1 [size 20]
    ]
    #{00 32} [
        TLS_DHE_DSS_WITH_AES_128_CBC_SHA
        <dhe-dss> @aes [size 16 block 16 iv 16] #sha1 [size 20]
    ]
    #{00 38} [
        TLS_DHE_DSS_WITH_AES_256_CBC_SHA
        <dhe-dss> @aes [size 32 block 16 iv 16] #sha1 [size 20]
    ]
    #{00 33} [
        TLS_DHE_RSA_WITH_AES_128_CBC_SHA
        <dhe-rsa> @aes [size 16 block 16 iv 16] #sha1 [size 20]
    ]
    #{00 39} [
        TLS_DHE_RSA_WITH_AES_256_CBC_SHA
        <dhe-rsa> @aes [size 32 block 16 iv 16] #sha1 [size 20]
    ]
]


;
; SUPPORT FUNCTIONS
;

debug: (comment [:print] blank)

emit: function [
    {Emits binary data, optionally marking positions with SET-WORD!}

    return: [void!]
    ctx [object!]
    code [block! binary!]
    <local> result
][
    if binary? code [
        append ctx/msg code
        return
    ]

    while [code] [
        if set-word? code/1 [
            set code/1 tail ctx/msg  ; save position
            code: my next
        ]
        else [
            ; Keep evaluating so long as returned code pos is quoted, as
            ; it indicates invisible eval (`emit ctx [comment "X" ...]`)
            ;
            ; Note: this would be nicer with predicates, since UNTIL's
            ; ultimate return result after looping could be falsey:
            ;
            ;    if until .not.quoted? [[code result]: evaluate code] [
            ;         append ctx/msg ensure binary! result
            ;    ]
            ;
            until [
                not quoted? [code result]: evaluate code
            ]
            if code [
                append ctx/msg ensure binary! result
            ]
        ]
    ]
]

; Network formats generally use big-endian byte ordering.  The concept seems
; to have originated with wanting to see most significant part of a routing
; hierarchy first (in contrast with little-endian, which helps for arithmetic
; that wants to process the least-significant part first and carry upward)
;
; !!! These shorthands cover what's needed and are chosen to clearly separate
; the number of bytes from the number being encoded (both integers).
;
to-1bin: (-> enbin [be + 1])
to-2bin: (-> enbin [be + 2])
to-3bin: (-> enbin [be + 3])
to-4bin: (-> enbin [be + 4])
to-8bin: (-> enbin [be + 8])

make-tls-error: func [
    message [text! block!]
][
    if block? message [message: unspaced message]
    make error! [
        type: 'Access
        id: 'Protocol
        arg1: message
    ]
]


;
; ASN.1 FORMAT PARSER CODE
;
; ASN.1 is similar in purpose and use to protocol buffers and Apache Thrift,
; which are also interface description languages for cross-platform data
; serialization. Like those languages, it has a schema (in ASN.1, called a
; "module"), and a set of encodings, typically type-length-value encodings.
;
; https://en.wikipedia.org/wiki/Abstract_Syntax_Notation_One
;
; The only use of it here is to extract certificates, so it's rather heavy
; handed to do a complete ASN parse:
;
; https://security.stackexchange.com/a/31057
;
; Yet it's a good, short, real-world case to look at through a Rebol lens.

parse-asn: function [
    {Create a legible Rebol-structured BLOCK! from an ASN.1 BINARY! encoding}

    return: [<opt> block!]
    data [binary!]

    <static>

    universal-tags ([
        <eoc>
        <boolean>
        <integer>
        <bit-string>
        <octet-string>
        <null>
        <object-identifier>
        <object-descriptor>
        <external>
        <real>
        <enumerated>
        <embedded-pdv>
        <utf8string>
        <relative-oid>
        <undefined>
        <undefined>
        <sequence>
        <set>
        <numeric-string>
        <printable-string>
        <t61-string>
        <videotex-string>
        <ia5-string>
        <utc-time>
        <generalized-time>
        <graphic-string>
        <visible-string>
        <general-string>
        <universal-string>
        <character-string>
        <bmp-string>
    ])

    class-types ([@universal @application @context-specific @private])
][
    data-start: data  ; may not be at head
    index: does [1 + offset-of data-start data]  ; calculates effective index

    mode: #type
    class: _
    tag: _

    return collect [ iterate data [
        byte: data/1

        switch mode [
            #type [
                constructed: not zero? (byte and+ 32)
                class: pick class-types 1 + shift byte -6

                switch class [
                    @universal [
                        tag: pick universal-tags 1 + (byte and+ 31)
                    ]
                    @context-specific [
                        tag: <context-specific>
                        val: byte and+ 31
                    ]
                ]
                mode: #size
            ]

            #size [
                size: byte and+ 127
                if not zero? (byte and+ 128) [  ; long form
                    old-size: size
                    size: debin [be +] copy/part next data old-size
                    data: skip data old-size
                ]
                if zero? size [
                    keep/only/line compose/deep [
                        (tag) [
                            (either constructed ["constructed"] ["primitive"])
                            (index)
                            (size)
                            _
                        ]
                    ]
                    mode: #type
                ] else [
                    mode: #value
                ]
            ]

            #value [
                switch class [
                    @universal [
                        val: copy/part data size
                        keep/only/line compose/deep/only [
                            (tag) [
                                (either constructed ["constructed"] ["primitive"])
                                (index)
                                (size)
                                (either constructed [parse-asn val] [val])
                            ]
                        ]
                    ]

                    @context-specific [
                        keep/only/line compose/deep [(tag) [(val) (size)]]
                        parse-asn copy/part data size  ; !!! ensures valid?
                    ]
                ]

                data: skip data size - 1
                mode: #type
            ]
        ]
    ] ]
]


;
; PROTOCOL STATE HANDLING
;

update-state: function [
    return: <void>
    ctx [object!]
    new [tag! issue!] "new state, ISSUE! is a (potentially) terminal state"
    direction [word!] "READ or WRITE"
    transitions [block!] "maps from state to a BLOCK! of legal next states"
][
    old: ensure [blank! issue! tag!] ctx/mode
    debug [mold old unspaced ["=" direction "=>"] new]

    if old and [not find (legal: select transitions old) new] [
        fail ["Invalid write state transition, expected one of:" mold legal]
    ]

    ctx/mode: new
]

update-read-state: specialize 'update-state [
    direction: 'read
    transitions: [
        <client-hello> [<server-hello>]
        <server-hello> [<certificate>]
        <certificate> [#server-hello-done <server-key-exchange>]
        <server-key-exchange> [#server-hello-done]
        <finished> [<change-cipher-spec> #alert]
        <change-cipher-spec> [#encrypted-handshake]
        #encrypted-handshake [#application]
        #application [#application #alert]
        #alert []
        <close-notify> [#alert]
    ]
]

update-write-state: specialize 'update-state [
    direction: 'write
    transitions: [
        #server-hello-done [<client-key-exchange>]
        <client-key-exchange> [<change-cipher-spec>]
        <change-cipher-spec> [<finished>]
        #encrypted-handshake [#application]
        #application [#application #alert]
        #alert [<close-notify>]
        <close-notify> []
    ]
]


;
; TLS PROTOCOL CODE
;

client-hello: function [
    return: <void>
    ctx [object!]
    /version "TLS version to request (block is [lowest highest] allowed)"
        [decimal! block!]
][
    version: default '[1.0 1.2]

    set [ctx/min-version: ctx/max-version:] case [
        decimal? version [reduce [version version]]
        block? version [
            parse version [decimal! decimal!] else [
                fail "BLOCK! /VERSION must be two DECIMAL! (min ver, max ver)"
            ]
            version
        ]
    ]
    min-ver-bytes: select version-to-bytes ctx/min-version else [
        fail ["Unsupported minimum TLS version" ctx/min-version]
    ]
    max-ver-bytes: select version-to-bytes ctx/max-version else [
        fail ["Unsupported maximum TLS version" ctx/max-version]
    ]

    clear ctx/handshake-messages

    emit ctx [
      TLSPlaintext: ; https://tools.ietf.org/html/rfc5246#section-6.2.1
        #{16}                       ; protocol type (22=Handshake)
        min-ver-bytes               ; TLS version for ClientHello (min? max?)
      fragment-length:
        #{00 00}                    ; length of handshake data (updated after)
    ]

    emit ctx [
      Handshake: ; https://tools.ietf.org/html/rfc5246#appendix-A.4
        #{01}                       ; protocol message type (1=ClientHello)
      message-length:
        #{00 00 00}                 ; protocol message length (updated after)
    ]

    ; struct {
    ;     uint32 gmt_unix_time;
    ;     opaque random_bytes[28];
    ; } Random;
    ;
    ctx/client-random: to-4bin to-integer difference now/precise 1-Jan-1970
    random/seed now/time/precise
    loop 28 [append ctx/client-random (random-secure 256) - 1]

    cs-data: join-all map-each item cipher-suites [
        if binary? item [item]
    ]

    emit ctx [
      ClientHello:  ; https://tools.ietf.org/html/rfc5246#section-7.4.1.2
        max-ver-bytes               ; max supported version by client
        ctx/client-random           ; 4 bytes gmt unix time + 28 random bytes
        #{00}                       ; session ID length
        to-2bin length of cs-data   ; cipher suites length
        cs-data                     ; cipher suites list

        comment {
            "Secure clients will advertise that they do not support
            compression (by passing "null" as the only algorithm) to avoid
            the CRIME attack": https://en.wikipedia.org/wiki/CRIME
        }
        #{01}                       ; compression method length
        #{00}                       ; no compression

        comment {
            "The presence of extensions can be detected by determining whether
            there are bytes following the compression_methods at the end of
            the ClientHello.  Note that this method of detecting optional data
            differs from the normal TLS method of having a variable-length
            field, but it is used for compatibility with TLS before extensions
            were defined."
        }
      ExtensionsLength:
        #{00 00}                    ; filled in later
      Extensions:
    ]

    ; Some servers will disconnect if they don't think you can check the
    ; digital signatures in their certificates correctly.  Hence we say we
    ; have hashes that we may not actually have.  Since we don't check
    ; certificates yet, it just keeps the server from hanging up.
    ;
    emit ctx [
        #{00 0d}                    ; signature_algorithms (13)
      extension_length:
        #{00 00}                    ; total length (filled in after)
      signatures_length:
        #{00 00}                    ; just signatures (filled in after)
      signature_algorithms:
        #{06 01}                    ; rsa_pkcs1_sha512
        #{06 02}                    ; SHA512 DSA
        #{06 03}                    ; ecdsa_secp521r1_sha512
        #{05 01}                    ; rsa_pkcs1_sha384
        #{05 02}                    ; SHA384 DSA
        #{05 03}                    ; ecdsa_secp384r1_sha384
        #{04 01}                    ; rsa_pkcs1_sha256
        #{04 02}                    ; SHA256 DSA
        #{04 03}                    ; ecdsa_secp256r1_sha256
        #{03 01}                    ; SHA224 RSA
        #{03 02}                    ; SHA224 DSA
        #{03 03}                    ; SHA224 ECDSA
        #{02 01}                    ; rsa_pkcs1_sha1
        #{02 02}                    ; SHA1 DSA
        #{02 03}                    ; ecdsa_sha1
    ]
    change extension_length to-2bin (length of signatures_length)
    change signatures_length to-2bin (length of signature_algorithms)

    ; https://en.wikipedia.org/wiki/Server_Name_Indication
    ; Sending the server name you're trying to connect to allows the same host
    ; to serve certificates for multiple domains.  While not technically a
    ; required part of TLS protocol, the widespread use of this in browsers
    ; means there are https servers which will hang up on ClientHello messages
    ; that don't include this extension.
    ;
    if text? ctx/host-name [
        server-name-bin: to binary! ctx/host-name
        emit ctx [
            #{00 00}                    ; extension type (server_name=0)
          extension_length:
            #{00 00}                    ; total length (filled in after)
          list_length:
            #{00 00}                    ; name list length (filled in after)
          list_item_1:
            #{00}                       ; server name type (host_name=0)
            to-2bin (length of server-name-bin)  ; server name length
            server-name-bin             ; server name
        ]
        change extension_length to-2bin (length of list_length) 
        change list_length to-2bin (length of list_item_1)
    ]

    ; When your cipher suites mention ECDHE, that doesn't imply any particular
    ; curve.  So the "extensions" in the hello message should say which curves
    ; you support (if you don't, then the server can just pick whatever it
    ; chooses).  At time of writing, we are experimenting with secp256r1
    ; and not building in any others.
    ;
    if true [  ; should depend on if any "_ECDHE_" ciphers are in the table
        emit ctx [
            #{00 0A}                    ; extension type (supported_groups=10)
          extension_length:
            #{00 00}
        Curves:
          curves_length:
            #{00 00}
          curves_list:
            ; https://tools.ietf.org/html/rfc8422#section-5.1.1
            #{00 17}                    ; hex iana code for secp256r1=23
        ]
        change curves_length to-2bin (length of curves_list)
        change extension_length to-2bin (length of Curves)
    ]

    ; During elliptic curve (EC) cryptography the client and server will
    ; exchange information on the points selected, in either compressed or
    ; uncompressed form.  We use uncompressed form because the TLS RFC
    ; says: "The uncompressed point format is the default format in that
    ; implementations of this document MUST support it for all of their
    ; supported curves."  Compressed forms are optional.
    ;
    ; We've modified the "easy-ecc" library providing secp256r1 to accept
    ; uncompressed format keys (it initially only took X coordinate and sign).
    ;
    if true [  ; should depend on if any "_ECDHE_" ciphers are in the table
        emit ctx [
            #{00 0b}                    ; extension type (ec_points_format=11)
          extension_length:
            #{00 00}                    ; filled in later
        PointsFormat:
          formats_length:
            #{00}
          supported_formats:
            #{00}                       ; uncompressed form (server MUST do)
        ]
        change formats_length to-1bin (length of supported_formats)
        change extension_length to-2bin (length of PointsFormat)
    ]

    ; These extensions are commonly sent by OpenSSL or browsers, so turning
    ; them on might be a good first step with a server rejecting ClientHello
    ; that seems to work in curl/wget.
    ;
    comment [
        emit ctx [
            #{00 23 00 00}  ; SessionTicket TLS

            #{00 0f 00 01 01}  ; heartbeat
        ]
    ]

    ; update the embedded lengths to correct values
    ;
    change fragment-length to-2bin (length of Handshake)
    change message-length to-3bin (length of ClientHello)
    change ExtensionsLength to-2bin (length of Extensions)

    append ctx/handshake-messages Handshake
]


client-key-exchange: function [
    return: <void>
    ctx [object!]
][
    switch ctx/key-method [
        <rsa> [
            ; generate pre-master-secret
            ctx/pre-master-secret: copy ctx/ver-bytes
            random/seed now/time/precise
            loop 46 [append ctx/pre-master-secret (random-secure 256) - 1]

            ; encrypt pre-master-secret
            rsa-key: rsa-make-key
            rsa-key/e: ctx/rsa-e
            rsa-key/n: ctx/rsa-pub

            ; supply encrypted pre-master-secret to server
            key-data: rsa ctx/pre-master-secret rsa-key
            key-len: to-2bin length of key-data  ; Two bytes for this case
        ]

        <dhe-dss>
        <dhe-rsa> [
            ctx/dh-keypair: dh-generate-keypair ctx/dh-p ctx/dh-g
            ctx/pre-master-secret: dh-compute-secret ctx/dh-keypair ctx/dh-pub

            ; supply the client's public key to server
            key-data: ctx/dh-keypair/public-key
            key-len: to-2bin length of key-data  ; Two bytes for this case
        ]

        <ecdhe-rsa> [
            ctx/ecdh-keypair: ecc-generate-keypair 'secp256r1

            ctx/pre-master-secret: (
                ecdh-shared-secret 'secp256r1
                    ctx/ecdh-keypair/private-key ctx/ecdh-pub
            )

            ; we use the 65-byte uncompressed format to send our key back
            key-data: join-all [
                #{04}
                ctx/ecdh-keypair/public-key/x
                ctx/ecdh-keypair/public-key/y
            ]
            key-len: to-1bin length of key-data  ; One byte for this case
        ]
    ]

    emit ctx [
        #{16}                       ; protocol type (22=Handshake)
        ctx/ver-bytes               ; protocol version

      ssl-record-length:
        #{00 00}                    ; length of SSL record data

      ssl-record:
        #{10}                       ; message type (16=ClientKeyExchange)

      message-length:
        #{00 00 00}                 ; protocol message length

      message:
        key-len                     ; may be one or two bytes (see above)
        key-data
    ]

    ; update the embedded lengths to correct values
    ;
    change ssl-record-length to-2bin (length of ssl-record)
    change message-length to-3bin (length of message)

    ; make all secure data
    ;
    make-master-secret ctx ctx/pre-master-secret

    make-key-block ctx

    ; update keys
    ctx/client-mac-key: copy/part ctx/key-block ctx/hash-size
    ctx/server-mac-key: copy/part skip ctx/key-block ctx/hash-size ctx/hash-size
    ctx/client-crypt-key: copy/part skip ctx/key-block 2 * ctx/hash-size ctx/crypt-size
    ctx/server-crypt-key: copy/part skip ctx/key-block (2 * ctx/hash-size) + ctx/crypt-size ctx/crypt-size

    if ctx/block-size [
        if ctx/version = 1.0 [
            ;
            ; Block ciphers in TLS 1.0 used an implicit initialization vector
            ; (IV) to seed the encryption process.  This has vulnerabilities.
            ;
            ctx/client-iv: copy/part skip ctx/key-block 2 * (ctx/hash-size + ctx/crypt-size) ctx/block-size
            ctx/server-iv: copy/part skip ctx/key-block (2 * (ctx/hash-size + ctx/crypt-size)) + ctx/block-size ctx/block-size
        ] else [
            ;
            ; Each encrypted message in TLS 1.1 and above carry a plaintext
            ; initialization vector, so the ctx does not use one for the whole
            ; session.  Unset it to make sure.
            ;
            unset in ctx 'client-iv
            unset in ctx 'server-iv
        ]
    ]

    append ctx/handshake-messages ssl-record
]


change-cipher-spec: function [
    return: <void>
    ctx [object!]
][
    emit ctx [
        #{14}           ; protocol type (20=ChangeCipherSpec)
        ctx/ver-bytes   ; protocol version
        #{00 01}        ; length of SSL record data
        #{01}           ; CCS protocol type
    ]
]


encrypted-handshake-msg: function [
    return: <void>
    ctx [object!]
    unencrypted [binary!]
][
    encrypted: encrypt-data/type ctx unencrypted #{16}
    emit ctx [
        #{16}                         ; protocol type (22=Handshake)
        ctx/ver-bytes                 ; protocol version
        to-2bin length of encrypted  ; length of SSL record data
        encrypted
    ]
    append ctx/handshake-messages unencrypted
]


application-data: function [
    return: <void>
    ctx [object!]
    unencrypted [binary! text!]
][
    encrypted: encrypt-data ctx to binary! unencrypted
    emit ctx [
        #{17}                         ; protocol type (23=Application)
        ctx/ver-bytes                 ; protocol version
        to-2bin length of encrypted  ; length of SSL record data
        encrypted
    ]
]


alert-close-notify: function [
    ctx [object!]
][
    encrypted: encrypt-data ctx #{0100} ; close notify
    emit ctx [
        #{15}                         ; protocol type (21=Alert)
        ctx/ver-bytes                 ; protocol version
        to-2bin length of encrypted  ; length of SSL record data
        encrypted
    ]
]


finished: function [
    return: [binary!]
    ctx [object!]
][
    ctx/seq-num-w: 0
    who-finished: if ctx/server? ["server finished"] else ["client finished"]

    seed: if ctx/version < 1.2 [
        join-all [
            checksum 'md5 ctx/handshake-messages
            checksum 'sha1 ctx/handshake-messages
        ]
    ] else [
        ; "The Hash MUST be the Hash used as the basis for the PRF.  Any
        ; cipher suite which defines a different PRF MUST also define the
        ; Hash to use in the Finished computation."
        ;
        ; For now, assume all supported cipher suites use SHA256.
        ;
        checksum 'sha256 ctx/handshake-messages
    ]

    return join-all [
        #{14}       ; protocol message type (20=Finished)
        #{00 00 0c} ; protocol message length (12 bytes)

        applique 'prf [
            ctx: ctx
            secret: ctx/master-secret
            label: who-finished
            seed: seed
            output-length: 12
        ]
    ]
]


encrypt-data: function [
    return: [binary!]
    ctx [object!]
    content [binary!]
    /type
        [binary!] "application data is default"
][
    type: default [#{17}]  ; #application

    ; GenericBlockCipher: https://tools.ietf.org/html/rfc5246#section-6.2.3.2

    if ctx/version > 1.0 [
        ;
        ; "The Initialization Vector (IV) SHOULD be chosen at random, and
        ;  MUST be unpredictable.  Note that in versions of TLS prior to 1.1,
        ;  there was no IV field, and the last ciphertext block of the
        ;  previous record (the "CBC residue") was used as the IV.  This was
        ;  changed to prevent the attacks described in [CBCATT].  For block
        ;  ciphers, the IV length is SecurityParameters.record_iv_length,
        ;  which is equal to the SecurityParameters.block_size."
        ;
        ctx/client-iv: copy #{}
        loop ctx/block-size [append ctx/client-iv (random-secure 256) - 1]
    ]

    ; Message Authentication Code
    ; https://tools.ietf.org/html/rfc5246#section-6.2.3.1
    ;
    MAC: checksum/method/key join-all [
        to-8bin ctx/seq-num-w               ; sequence number (64-bit int)
        type                                ; msg type
        ctx/ver-bytes                       ; version
        to-2bin length of content           ; msg content length
        content                             ; msg content
    ] (to word! ctx/hash-method) ctx/client-mac-key

    data: join-all [content MAC]

    if ctx/block-size [
        ; add the padding data in CBC mode
        padding: ctx/block-size - (
            remainder (1 + (length of data)) ctx/block-size
        )
        len: 1 + padding
        append data head of insert/dup make binary! len to-1bin padding len
    ]

    switch ctx/crypt-method [
        @aes [
            ctx/encrypt-stream: default [
                aes-key ctx/client-crypt-key ctx/client-iv
            ]
            data: aes-stream ctx/encrypt-stream data

            if ctx/version > 1.0 [
                ; encrypt-stream must be reinitialized each time with the
                ; new initialization vector.
                ;
                ctx/encrypt-stream: _
            ]
        ]
    ] else [
        fail ["Unsupported TLS crypt-method:" ctx/crypt-method]
    ]

    ; TLS versions 1.1 and above include the client-iv in plaintext.
    ;
    if ctx/version > 1.0 [
        insert data ctx/client-iv
        unset in ctx 'client-iv  ; avoid accidental reuse
    ]

    return data
]


decrypt-data: function [
    return: [binary!]
    ctx [object!]
    data [binary!]
][
    switch ctx/crypt-method [
        @aes [
            ctx/decrypt-stream: default [
                aes-key/decrypt ctx/server-crypt-key ctx/server-iv
            ]
            data: aes-stream ctx/decrypt-stream data

            ; TLS 1.1 and above must use a new initialization vector each time
            ; so the decrypt stream has to get GC'd.
            ;
            if ctx/version > 1.0 [
                ctx/decrypt-stream: _
            ]
        ]
    ] else [
        fail ["Unsupported TLS crypt-method:" ctx/crypt-method]
    ]

    return data
]


parse-protocol: function [
    return: [object!]
    data [binary!]

    <static>

    protocol-types ([
        20 <change-cipher-spec>
        21 #alert
        22 #handshake
        23 #application
    ])
][
    return make object! [
        type: select protocol-types data/1 else [
            fail ["unknown/invalid protocol type:" data/1]
        ]
        version: select bytes-to-version copy/part at data 2 2
        size: debin [be +] copy/part at data 4 2
        messages: copy/part at data 6 size
    ]
]


grab: enfixed func [
    {Extracts N bytes from a BINARY!, and also updates its position}

    return: "BINARY! (or INTEGER! if GRAB-INT enclosure is used)"
        [binary! integer!]
    :left "Needs variable name for assignment (to deliver errors)"
        [set-word!]
    var "Variable containing the BINARY! to be extracted and advanced"
        [word!]
    n "Number of bytes to extract (errors if not enough bytes available)"
        [integer!]
][
    let data: ensure binary! get var
    let result: copy/part data n
    let actual: length of result  ; /PART accepts truncated data
    if n != actual  [
        fail ["Expected" n "bytes for" as word! left "but received" actual]
    ]
    set var skip data n  ; update variable to point past what was taken
    return set left result  ; must manually assign if SET-WORD! overridden
]

grab-int: enfixed enclose 'grab func [f [frame!]] [
    return set f/left (debin [be +] do copy f)
]


parse-messages: function [
    ctx [object!]
    proto [object!]

    <static>

    message-types ([
        0 #hello-request
        1 <client-hello>
        2 <server-hello>
        11 <certificate>
        12 <server-key-exchange>
        13 @certificate-request  ; not yet implemented
        14 #server-hello-done
        15 @certificate-verify  ; not yet implemented
        16 <client-key-exchange>
        20 <finished>
    ])

    alert-descriptions ([
        0 "Close notify"
        10 "Unexpected message"
        20 "Bad record MAC"
        21 "Decryption failed"
        22 "Record overflow"
        30 "Decompression failure"
        40 "Handshake failure - no supported cipher suite available on server"
        41 "No certificate"
        42 "Bad certificate"
        43 "Unsupported certificate"
        44 "Certificate revoked"
        45 "Certificate expired"
        46 "Certificate unknown"
        47 "Illegal parameter"
        48 "Unknown CA"
        49 "Access denied"
        50 "Decode error"
        51 "Decrypt error"
        60 "Export restriction"
        70 "Protocol version"
        71 "Insufficient security"
        80 "Internal error"
        90 "User cancelled"
       100 "No renegotiation"
       110 "Unsupported extension"
    ])

    ; The structure has a field called LENGTH, so when an ACTION! is used
    ; that field is picked up.
    ;
    <with> length
][
    result: make block! 8
    data: proto/messages

    if ctx/encrypted? [
        if ctx/block-size and [ctx/version > 1.0] [
            ;
            ; Grab the server's initialization vector, which will be new for
            ; each message.
            ;
            ctx/server-iv: take/part data ctx/block-size
        ]

        change data decrypt-data ctx data
        debug ["decrypting..."]

        if ctx/block-size [
            ; deal with padding in CBC mode
            data: copy/part data (
                ((length of data) - 1) - (last data)
            )
            debug ["depadding..."]
            if ctx/version > 1.0 [
                unset in ctx 'server-iv  ; avoid reuse in TLS 1.1 and above
            ]
        ]
        debug ["data:" data]
    ]
    debug [ctx/seq-num-r ctx/seq-num-w "READ <--" proto/type]

    if proto/type <> #handshake [
        if proto/type = #alert [
            if proto/messages/1 > 1 [
                ; fatal alert level
                fail [select alert-descriptions data/2 else ["unknown"]]
            ]
        ]
        update-read-state ctx proto/type
    ]

    switch proto/type [
        #alert [
            append result reduce [
                context [
                    level: pick [warning fatal] data/1 else ['unknown]
                    description: select alert-descriptions data/2 else [
                        "unknown"
                    ]
                ]
            ]
        ]

        #handshake [
            while [not tail? data] [
                msg-type: try select message-types data/1  ; 1 byte

                update-read-state ctx (
                    if ctx/encrypted? [#encrypted-handshake] else [msg-type]
                )

                len: debin [be +] copy/part (skip data 1) 3

                ; We don't mess with the data pointer itself as we use it, so
                ; make a copy of the data.  Skip the 4 bytes we used.
                ;
                bin: copy/part (skip data 4) len

                append result switch msg-type [
                    <server-hello> [
                        ; https://tools.ietf.org/html/rfc5246#section-7.4.1.3

                        server-version: grab 'bin 2
                        server-version: select bytes-to-version server-version
                        if server-version < ctx/min-version [
                            fail [
                                "Requested minimum TLS version" ctx/min-version
                                "with maximum TLS version" ctx/max-version
                                "but server gave back" server-version
                            ]
                        ]
                        ctx/version: server-version

                        msg-obj: context [
                            type: msg-type
                            length: len

                            version: ctx/version

                            server-random: grab 'bin 32

                            session-id-len: grab-int 'bin 1
                            session-id: grab 'bin session-id-len

                            suite-id: grab 'bin 2

                            compression-method-length: grab-int 'bin 1
                            if compression-method-length != 0 [
                                comment [
                                    compression-method:
                                        grab 'bin compression-method-length
                                ]
                                fail ["TLS compression disabled (CRIME)"]
                            ]

                            ; !!! After this point is responses based on the
                            ; extensions we asked for.  We should check to be
                            ; sure only extensions we asked for come back in
                            ; this list...but punt on that check for now.

                            extensions-list-length: grab-int 'bin 2
                            check-length: 0

                            curve-list: _
                            while [not tail? bin] [
                                extension-id: grab 'bin 2
                                extension-length: grab-int 'bin 2
                                check-length: me + 2 + 2 + extension-length

                                switch extension-id [
                                    #{00 0A} [  ; elliptic curve groups
                                        curve-list-length: grab-int 'bin 2
                                        curve-list: grab 'bin curve-list-length
                                    ]
                                    default [
                                        dummy: grab 'bin extension-length
                                    ]
                                ]
                            ]
                            assert [check-length = extensions-list-length]
                        ]

                        ctx/suite: select cipher-suites msg-obj/suite-id else [
                            fail [
                                "This TLS scheme doesn't support ciphersuite:"
                                (mold suite)
                            ]
                        ]

                        ctx/server-random: msg-obj/server-random
                        msg-obj
                    ]

                    <certificate> [
                        msg-obj: context [
                            type: msg-type
                            length: len
                            certificate-list-length: grab-int 'bin 3
                            certificate-list: make block! 4
                            while [not tail? bin] [
                                certificate-length: grab-int 'bin 3
                                certificate: grab 'bin certificate-length
                                append certificate-list certificate
                            ]
                        ]

                        ; !!! This is where the "S" for "Secure" is basically
                        ; thrown out.  We read the certificates but don't
                        ; actually verify that they match some known chain
                        ; of trust...we just use the first one sent back.
                        ; Makers of web browsers (Mozilla, Google, etc.) have
                        ; lists built into them and rules for accepting or
                        ; denying them.  We need something similar.
                        ;
                        ctx/certificate: parse-asn msg-obj/certificate-list/1

                        switch ctx/key-method [
                            <rsa> [
                                ; get the public key and exponent (hardcoded for now)
                                temp: parse-asn (next
                                    comment [ctx/certificate/1/<sequence>/4/1/<sequence>/4/6/<sequence>/4/2/<bit-string>/4]
                                    ctx/certificate/1/<sequence>/4/1/<sequence>/4/7/<sequence>/4/2/<bit-string>/4
                                )
                                ctx/rsa-e: temp/1/<sequence>/4/2/<integer>/4
                                ctx/rsa-pub: next temp/1/<sequence>/4/1/<integer>/4
                            ]
                            <dhe-dss>
                            <dhe-rsa>
                            <ecdhe-rsa> [
                                ; for DH cipher suites the certificate is used
                                ; just for signing the key exchange data
                            ]
                        ] else [
                            fail "Unsupported TLS key-method"
                        ]
                        msg-obj
                    ]

                    <server-key-exchange> [
                        switch ctx/key-method [
                            <dhe-dss>
                            <dhe-rsa> [
                                msg-obj: context [
                                    type: msg-type
                                    length: len
                                    p-length: grab-int 'bin 2
                                    p: grab 'bin p-length
                                    g-length: grab-int 'bin 2
                                    g: grab 'bin g-length
                                    ys-length: grab-int 'bin 2
                                    ys: grab 'bin ys-length

                                    ; RFC 5246 Section 7.4.3 "Note that the
                                    ; introduction of the algorithm field is a
                                    ; change from previous versions"
                                    ;
                                    algorithm: _
                                    if ctx/version >= 1.2 [
                                        algorithm: grab 'bin 2
                                    ]

                                    signature-length: grab-int 'bin 2
                                    signature: grab 'bin signature-length
                                ]

                                ctx/dh-p: msg-obj/p  ; modulus
                                ctx/dh-g: msg-obj/g  ; generator
                                ctx/dh-pub: msg-obj/ys

                                ; TODO: the signature sent by server should be
                                ; verified using DSA or RSA algorithm to be
                                ; sure the dh-key params are safe

                                msg-obj
                            ]

                            <ecdhe-rsa> [
                                msg-obj: context [
                                    type: msg-type
                                    length: len

                                    curve-info: grab 'bin 3
                                    if curve-info <> #{03 00 17} [
                                        fail [
                                            "ECDHE only works for secp256r1"
                                        ]
                                    ]

                                    ; Public key format in secp256r1 is two
                                    ; 32-byte numbers.
                                    ; https://superuser.com/q/1465455/
                                    ;
                                    server-public-length: grab-int 'bin 1
                                    assert [server-public-length = 65]
                                    prefix: grab 'bin 1
                                    assert [prefix = #{04}]
                                    ctx/ecdh-pub: copy/part bin 64
                                    x: grab 'bin 32
                                    y: grab 'bin 32

                                    ; https://crypto.stackexchange.com/a/26355
                                    ;
                                    hash-algorithm: grab-int 'bin 1
                                    hash-algorithm: select [
                                        0 <none>
                                        1 <md5>
                                        2 <sha1>
                                        3 <sha224>
                                        4 <sha256>
                                        5 <sha384>
                                        6 <sha512>
                                    ] hash-algorithm else [
                                        fail "Unknown hash algorithm"
                                    ]

                                    ; https://crypto.stackexchange.com/a/26355
                                    ;
                                    signature-algorithm: grab-int 'bin 1
                                    signature-algorithm: select [
                                        0 #anonymous
                                        1 #rsa
                                        2 #dsa
                                        3 #ecsda
                                    ] signature-algorithm else [
                                        fail "Unkown signature algorithm"
                                    ]
                                    signature-length: grab-int 'bin 2
                                    signature: grab 'bin signature-length

                                    ; The signature is supposed to be, e.g.
                                    ;
                                    ; SHA256(
                                    ;     client_hello_random
                                    ;     + server_hello_random
                                    ;     + curve_info
                                    ;     + public_key)
                                    ;
                                    ; Which we should calculate and check.
                                    ; But we don't currently have all the
                                    ; algorithms, and having connectivity is
                                    ; driving the patching of this for now. :(
                                ]

                                assert [tail? bin]
                                msg-obj
                            ]

                            fail "Server-key-exchange message sent illegally."
                        ]
                    ]

                    #server-hello-done [
                        context [
                            type: msg-type
                            length: len
                        ]
                    ]

                    <client-hello> [
                        context [
                            type: msg-type
                            version: grab 'bin 2
                            length: len
                            content: bin
                        ]
                    ]

                    <finished> [
                        ctx/seq-num-r: 0
                        who-finished: either ctx/server? [
                            "client finished"
                        ][
                            "server finished"
                        ]
                        seed: if ctx/version < 1.2 [
                            join-all [
                                checksum 'md5 ctx/handshake-messages
                                checksum 'sha1 ctx/handshake-messages
                            ]
                        ] else [
                            checksum 'sha256 ctx/handshake-messages
                        ]
                        if (
                            bin <> applique 'prf [
                                ctx: ctx
                                secret: ctx/master-secret
                                label: who-finished
                                seed: seed
                                output-length: 12
                            ]
                        )[
                            fail "Bad 'finished' MAC"
                        ]

                        debug "FINISHED MAC verify: OK"

                        context [
                            type: msg-type
                            length: len
                            content: bin
                        ]
                    ]
                ]

                append ctx/handshake-messages copy/part data len + 4

                skip-amount: either ctx/encrypted? [
                    mac: copy/part skip data len + 4 ctx/hash-size

                    mac-check: checksum/method/key join-all [
                        to-8bin ctx/seq-num-r   ; 64-bit sequence number
                        #{16}                   ; msg type
                        ctx/ver-bytes           ; version
                        to-2bin len + 4         ; msg content length
                        copy/part data len + 4
                    ] (to word! ctx/hash-method) ctx/server-mac-key

                    if mac <> mac-check [
                        fail "Bad handshake record MAC"
                    ]

                    4 + ctx/hash-size
                ][
                    4
                ]

                data: skip data (len + skip-amount)
            ]
        ]

        <change-cipher-spec> [
            ctx/encrypted?: true
            append result context [
                type: 'ccs-message-type
            ]
        ]

        #application [
            append result msg-obj: context [
                type: 'app-data
                content: copy/part data (length of data) - ctx/hash-size
            ]
            len: length of msg-obj/content
            mac: copy/part skip data len ctx/hash-size
            mac-check: checksum/method/key join-all [
                to-8bin ctx/seq-num-r   ; sequence number (64-bit int in R3)
                #{17}                   ; msg type
                ctx/ver-bytes           ; version
                to-2bin len             ; msg content length
                msg-obj/content         ; content
            ] (to word! ctx/hash-method) ctx/server-mac-key

            if mac <> mac-check [
                fail "Bad application record MAC"
            ]
        ]
    ]

    ctx/seq-num-r: ctx/seq-num-r + 1
    return result
]


parse-response: function [
    return: [object!]
    ctx [object!]
    msg [binary!]
][
    proto: parse-protocol msg
    messages: parse-messages ctx proto

    if empty? messages [
        fail "unknown/invalid protocol message"
    ]

    proto/messages: messages

    debug [
        "processed protocol type:" proto/type
        "messages:" length of proto/messages
    ]

    if not tail? skip msg proto/size + 5 [
        fail "invalid length of response fragment"
    ]

    return proto
]


prf: function [
    {(P)suedo-(R)andom (F)unction, generates arbitrarily long binaries}

    return: [binary!]
    ctx [object!]  ; needed for ctx/version, prf changed in TLS 1.2
    secret [binary!]
    label [text! binary!]
    seed [binary!]
    output-length [integer!]
][
    ; The seed for the underlying P_<hash> is the PRF's seed appended to the
    ; label.  The label is hashed as-is, so no null terminator.
    ;
    ; PRF(secret, label, seed) = P_<hash>(secret, label + seed)
    ;
    seed: join-all [#{} label seed]

    if ctx/version < 1.2 [
        ;
        ; Prior to TLS 1.2, the pseudo-random function was driven by a strange
        ; mixed method that's half MD5 and half SHA-1 hashing, regardless of
        ; cipher suite used: https://tools.ietf.org/html/rfc4346#section-5

        len: length of secret
        mid: to integer! (0.5 * (len + either odd? len [1] [0]))

        s-1: copy/part secret mid
        s-2: copy at secret mid + either odd? len [0] [1]

        p-md5: copy #{}
        a: seed  ; A(0)
        while [output-length > length of p-md5] [
            a: checksum/key 'md5 a s-1 ; A(n)
            append p-md5 checksum/key 'md5 join-all [a seed] 'md5 s-1
        ]

        p-sha1: copy #{}
        a: seed  ; A(0)
        while [output-length > length of p-sha1] [
            a: checksum/key 'sha1 a s-2 ; A(n)
            append p-sha1 checksum/key 'sha1 join-all [a seed] s-2
        ]
        return (
            (copy/part p-md5 output-length)
            xor+ (copy/part p-sha1 output-length)
        )
    ]

    ; TLS 1.2 includes the pseudorandom function as part of its cipher
    ; suite definition.  No cipher suites assume the md5/sha1 combination
    ; used above by TLS 1.0 and 1.1.  All cipher suites listed in the
    ; TLS 1.2 spec use `P_SHA256`, which is driven by the single SHA256
    ; hash function: https://tools.ietf.org/html/rfc5246#section-5

    p-sha256: copy #{}
    a: seed  ; A(0)
    while [output-length > length of p-sha256] [
        a: checksum/key 'sha256 a secret
        append p-sha256 checksum/key 'sha256 join-all [a seed] secret
    ]
    take/last/part p-sha256 ((length of p-sha256) - output-length)
    return p-sha256
]


make-key-block: function [
    return: [binary!]
    ctx [object!]
][
    ctx/key-block: applique 'prf [
        ctx: ctx
        secret: ctx/master-secret
        label: "key expansion"
        seed: join-all [ctx/server-random ctx/client-random]
        output-length: (
            (ctx/hash-size + ctx/crypt-size)
            + (either ctx/block-size [ctx/iv-size] [0])
        ) * 2
    ]
]


make-master-secret: function [
    return: [binary!]
    ctx [object!]
    pre-master-secret [binary!]
][
    ctx/master-secret: applique 'prf [
        ctx: ctx
        secret: pre-master-secret
        label: "master secret"
        seed: join-all [ctx/client-random ctx/server-random]
        output-length: 48
    ]
]


do-commands: function [
    return: <void>
    ctx [object!]
    commands [block!]
    /no-wait
][
    clear ctx/msg
    parse commands [
        some [
            set cmd: [
                <client-hello> (
                    client-hello/version ctx [1.0 1.2]  ; min/max versioning
                )
                | <client-key-exchange> (
                    client-key-exchange ctx
                )
                | <change-cipher-spec> (
                    change-cipher-spec ctx
                )
                | <finished> (
                    encrypted-handshake-msg ctx finished ctx
                )
                | #application set arg: [text! | binary!] (
                    application-data ctx arg
                )
                | <close-notify> (
                    alert-close-notify ctx
                )
            ] (
                debug [ctx/seq-num-r ctx/seq-num-w "WRITE -->" cmd]
                ctx/seq-num-w: ctx/seq-num-w + 1
                update-write-state ctx cmd
            )
        ]
        end
    ]
    debug ["writing bytes:" length of ctx/msg]
    ctx/resp: copy []
    write ctx/connection ctx/msg

    any [
        no-wait
        port? wait [ctx/connection 30]
    ] else [
        fail "port timeout"
    ]
]


;
; TLS SCHEME
;


tls-init: function [
    return: <void>
    ctx [object!]
][
    ctx/seq-num-r: 0
    ctx/seq-num-w: 0
    ctx/mode: _
    ctx/encrypted?: false
    assert [not ctx/suite]
]


tls-read-data: function [
    return: [logic!]
    ctx [object!]
    port-data [binary!]
][
    debug ["tls-read-data:" length of port-data "bytes"]
    data: append ctx/data-buffer port-data
    clear port-data

    ; !!! Why is this making a copy (5 = length of copy...) when just trying
    ; to test a size?
    ;
    while [5 = length of copy/part data 5] [
        len: 5 + debin [be +] copy/part at data 4 2

        debug ["reading bytes:" len]

        fragment: copy/part data len

        if len > length of fragment [
            debug [
                "incomplete fragment:"
                "read" length of fragment "of" len "bytes"
            ]
            break
        ]

        debug ["received bytes:" length of fragment | "parsing response..."]

        append ctx/resp parse-response ctx fragment

        data: skip data len

        if (tail? data) and [issue? ctx/mode] [
            debug [
                "READING FINISHED"
                length of head of ctx/data-buffer
                index of data
                same? tail of ctx/data-buffer data
            ]
            clear ctx/data-buffer
            return true
        ]
    ]

    debug ["CONTINUE READING..."]
    clear change ctx/data-buffer data
    return false
]


tls-awake: function [
    return: [logic!]
    event [event!]
][
    debug ["TLS Awake-event:" event/type]
    port: event/port
    tls-port: port/locals
    tls-awake: :tls-port/awake

    all [
        tls-port/state/mode = #application
        not port/data
    ] then [
        ; reset the data field when interleaving port r/w states
        tls-port/data: _
    ]

    switch event/type [
        'lookup [
            open port
            tls-init tls-port/state
            insert system/ports/system make event! [
                type: 'lookup
                port: tls-port
            ]
            return false
        ]

        'connect [
            do-commands tls-port/state [<client-hello>]

            if tls-port/state/resp/1/type = #handshake [
                do-commands tls-port/state [
                    <client-key-exchange>
                    <change-cipher-spec>
                    <finished>
                ]
            ]
            insert system/ports/system make event! [
                type: 'connect
                port: tls-port
            ]
            return false
        ]

        'wrote [
            switch tls-port/state/mode [
                <close-notify> [
                    return true
                ]
                #application [
                    insert system/ports/system make event! [
                        type: 'wrote
                        port: tls-port
                    ]
                    return false
                ]
            ]
            read port
            return false
        ]

        'read [
            debug [
                "Read" length of port/data
                "bytes in mode:" tls-port/state/mode
            ]

            complete?: tls-read-data tls-port/state port/data
            application?: false

            for-each proto tls-port/state/resp [
                switch proto/type [
                    #application [
                        for-each msg proto/messages [
                            if msg/type = 'app-data [
                                tls-port/data: default [
                                    clear tls-port/state/port-data
                                ]
                                append tls-port/data msg/content
                                application?: true
                                msg/type: _
                            ]
                        ]
                    ]
                    #alert [
                        for-each msg proto/messages [
                            if msg/description = "Close notify" [
                                do-commands tls-port/state [<close-notify>]
                                insert system/ports/system make event! [
                                    type: 'read
                                    port: tls-port
                                ]
                                return true
                            ]
                        ]
                    ]
                ]
            ]

            debug ["data complete?:" complete? "application?:" application?]

            if application? [
                insert system/ports/system make event! [
                    type: 'read
                    port: tls-port
                ]
            ] else [
                read port
            ]
            return complete?
        ]

        'close [
            insert system/ports/system make event! [
                type: 'close
                port: tls-port
            ]
            return true
        ]
    ]

    close port
    fail ["Unexpected TLS event:" (event/type)]
]


sys/make-scheme [
    name: 'tls
    title: "TLS protocol v1.0"
    spec: make system/standard/port-spec-net []
    actor: [
        read: func [
            port [port!]
            <local>
                resp data msg
        ][
            debug ["READ" open? port/state/connection]
            read port/state/connection
            return port
        ]

        write: func [port [port!] value [<opt> any-value!]] [
            if find [#encrypted-handshake #application] port/state/mode [
                do-commands/no-wait port/state compose [
                    #application (value)
                ]
                return port
            ]
            return blank
        ]

        open: func [port [port!] <local> conn] [
            if port/state [return port]

            if not port/spec/host [
                fail make-tls-error "Missing host address"
            ]

            ; This creates the `ctx:` object mentioned throughout the code
            ; (the port state is passed in as a parameter with that name)
            ;
            port/state: context [
                data-buffer: make binary! 32000
                port-data: make binary! 32000
                resp: _

                min-version: _
                max-version: _
                version: _
                ver-bytes: does [
                    select version-to-bytes version else [
                        fail ["version has no byte sequence:" version]
                    ]
                ]

                server?: false ; !!! server role of protocol not yet written

                ; Used by https://en.wikipedia.org/wiki/Server_Name_Indication
                host-name: port/spec/host

                mode: _

                suite: _

                cipher-suite: does [first find suite word!]

                key-method: does [first find suite tag!]

                hash-method: does [first find suite issue!]
                hash-size: does [
                    select (ensure block! second find suite issue!) 'size
                ]

                crypt-method: does [first find suite sym-word!]
                crypt-size: does [
                    select (ensure block! second find suite sym-word!) 'size
                ]
                block-size: does [
                    try select (
                        ensure block! second find suite sym-word!
                    ) 'block
                ]
                iv-size: does [
                    try select (ensure block! second find suite sym-word!) 'iv
                ]

                client-crypt-key: _
                client-mac-key: _
                client-iv: _
                server-crypt-key: _
                server-mac-key: _
                server-iv: _

                seq-num-r: 0
                seq-num-w: 0

                msg: make binary! 4096

                ; all messages from Handshake records except "HelloRequest"
                ;
                handshake-messages: make binary! 4096

                encrypted?: false

                client-random: _
                server-random: _
                pre-master-secret: _
                master-secret: _

                key-block: _

                certificate: _
                rsa-e: _
                rsa-pub: _

                dh-g: _  ; generator
                dh-p: _  ; modulus
                dh-keypair: _
                dh-pub: _

                ; secp256r1 currently assumed for ECDH
                ecdh-keypair: _
                ecdh-pub: _

                encrypt-stream: _
                decrypt-stream: _

                connection: _
            ]

            port/state/connection: conn: make port! [
                scheme: 'tcp
                host: port/spec/host
                port-id: port/spec/port-id
                ref: join-all [tcp:// host ":" port-id]
            ]

            port/data: port/state/port-data

            conn/awake: :tls-awake
            conn/locals: port
            open conn
            port
        ]

        reflect: func [port [port!] property [word!]] [
            switch property [
                'open? [
                    port/state and [open? port/state/connection]
                ]

                'length [
                    ; actor is not an object!, so this isn't a recursive call
                    ;
                    either port/data [length of port/data] [0]
                ]
            ]
        ]

        close: func [port [port!] <local> ctx] [
            if not port/state [return port]

            close port/state/connection

            ; The symmetric ciphers used by TLS are able to encrypt chunks of
            ; data one at a time.  It keeps the progressive state of the
            ; encryption process in the -stream variables, which under the
            ; hood are memory-allocated items stored as a HANDLE!.
            ;
            ; Calling the encryption functions with BLANK! as the data to
            ; input will assume you are done, and will free the handle.
            ;
            ; !!! Is there a good reason for not doing this with an ordinary
            ; OBJECT! containing a BINARY! ?
            ;
            if port/state/suite [
                switch port/state/crypt-method [
                    @aes [
                        if port/state/encrypt-stream [
                            port/state/encrypt-stream: _  ; will be GC'd
                        ]
                        if port/state/decrypt-stream [
                            port/state/decrypt-stream: _  ; will be GC'd
                        ]
                    ]
                ] else [
                    fail ["Unknown TLS crypt-method" port/state/crypt-method]
                ]
            ]

            debug "TLS/TCP port closed"
            port/state/connection/awake: blank
            port/state: blank
            port
        ]

        copy: func [port [port!]] [
            if port/data [copy port/data]
        ]

        query: func [port [port!]] [
            all [port/state query port/state/connection]
        ]
    ]
]
