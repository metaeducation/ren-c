REBOL [
    Title: {JPG Codec Extension}
    Name: JPG
    Type: Module
    Version: 1.0.0
    License: {Apache 2.0}
]

sys.util.register-codec* 'jpeg [%.jpg %jpeg]
    unrun :identify-jpeg?
    unrun :decode-jpeg
    null  ; currently no JPG encoder
