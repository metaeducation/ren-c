; Tests for the ENBIN and DEBIN functions
;
; !!! The design of these dialects is under construction at time of writing
; writing, but they are to replace most BLOB! <=> INTEGER! conversions

(0 == decode [BE +/-] #{00})
(1 == decode [BE +/-] #{01})
<32bit>
(-1 == decode [BE +/-] #{ffffffff})
<64bit>
(-1 == decode [BE +/-] #{ffffffffffffffff})
<64bit>

(#{00000020} = encode [BE + 4] 32)
(#{20000000} = encode [LE + 4] 32)
(32 = decode [BE + 4] #{00000020})
(32 = decode [LE + 4] #{20000000})
(32 = decode [BE +] #{00000020})
(32 = decode [LE +] #{20000000})

(
    random:seed "Reproducible Permutations!"
    repeat 1000 wrap [
        endian: random:only [be le]
        signedness: random:only [+ +/-]
        num-bytes: random 8

        settings: reduce [endian signedness num-bytes]
        r: random power 256 num-bytes
        either signedness = '+ [
            if num-bytes = 8 [r: r / 2 - 1]  ; 63-bit limit
        ][
            r: r - ((power 256 num-bytes) / 2)
        ]
        value: to integer! r
        bin: encode settings value
        check: decode settings bin
        if value != check [
            fail [value "didn't round trip ENBIN/DEBIN:" settings]
        ]
        comment [
            print [mold settings value "=>" mold bin "=>" value]
        ]
    ]
    ok
)
