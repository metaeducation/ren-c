REBOL [
    Title: {JPG Codec Extension}
    Name: JPG
    Type: Module
    Version: 1.0.0
    License: {Apache 2.0}
]

sys.util.register-codec* 'jpeg [%.jpg %jpeg]
    reify :identify-jpeg?
    reify :decode-jpeg
    null  ; currently no JPG encoder
