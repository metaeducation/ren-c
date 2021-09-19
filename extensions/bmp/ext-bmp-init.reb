REBOL [
    Title: "BMP Codec Extension"
    Name: BMP
    Type: Module
    Version: 1.0.0
    License: {Apache 2.0}
]

sys.register-codec* 'bmp %.bmp
    :identify-bmp?
    :decode-bmp
    :encode-bmp
