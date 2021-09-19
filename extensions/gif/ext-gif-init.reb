REBOL [
    Title: "GIF Codec Extension"
    Name: GIF
    Type: Module
    Version: 1.0.0
    License: {Apache 2.0}
]

sys.register-codec* 'gif %.gif
    :identify-gif?
    :decode-gif
    _  ; currently no GIF encoder
