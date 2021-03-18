REBOL [
    Title: {JPG Codec Extension}
    Name: JPG
    Type: Module
    Version: 1.0.0
    License: {Apache 2.0}
]

sys/register-codec* 'jpeg [%.jpg %jpeg]
    :identify-jpeg?
    :decode-jpeg
    _  ; currently no JPG encoder
