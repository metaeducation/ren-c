REBOL [
    Title: "BMP Codec Extension"
    Name: BMP
    Type: Module
    Version: 1.0.0
    License: {Apache 2.0}
]

sys.util.register-codec* 'bmp %.bmp
    reify :identify-bmp?
    reify :decode-bmp
    reify :encode-bmp
