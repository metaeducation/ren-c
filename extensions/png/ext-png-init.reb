REBOL [
    Title: "PNG Codec Extension"
    Name: PNG
    Type: Module
    Version: 1.0.0
    License: {Apache 2.0}
]

sys.util.register-codec* 'png %.png
    reify :identify-png?
    reify :decode-png
    reify :encode-png
