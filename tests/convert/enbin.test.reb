; Tests for the ENBIN and DEBIN functions
;
; !!! The design of these dialects is under construction at time of writing
; writing, but they are to replace most BINARY! <=> INTEGER! conversions

(#{00000020} = enbin [BE + 4] 32)
(#{20000000} = enbin [LE + 4] 32)
(32 = debin [BE + 4] #{00000020})
(32 = debin [LE + 4] #{20000000})
(32 = debin [BE +] #{00000020})
(32 = debin [LE +] #{20000000})

(
    random/seed {Reproducible Permutations!}
    repeat 1000 [
        endian: random/only [BE LE]
        signedness: random/only [+ +/-]
        num-bytes: random 8

        settings: reduce [endian signedness num-bytes]
        r: random power 256 num-bytes
        either signedness = '+ [
            if num-bytes = 8 [r: r / 2 - 1]  ; 63-bit limit
        ][
            r: r - ((power 256 num-bytes) / 2)
        ]
        value: to integer! r
        bin: enbin settings value
        check: debin settings bin
        if value != check [
            panic [value "didn't round trip ENBIN/DEBIN:" settings]
        ]
        comment [
            print [mold settings value "=>" mold bin "=>" value]
        ]
    ]
    okay
)
