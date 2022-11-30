REBOL [
    Title: "PNG Codec Extension"
    Name: PNG
    Type: Module
    Version: 1.0.0
    License: {Apache 2.0}
]

sys.util.register-codec* 'png %.png
    unrun :identify-png?
    unrun :decode-png
    unrun :encode-png
