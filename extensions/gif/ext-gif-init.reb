REBOL [
    Title: "GIF Codec Extension"
    Name: GIF
    Type: Module
    Version: 1.0.0
    License: {Apache 2.0}
]

sys.util.register-codec* 'gif %.gif
    unrun :identify-gif?
    unrun :decode-gif
    null  ; currently no GIF encoder
