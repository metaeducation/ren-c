REBOL [
    Title: "Domain Name Lookup / Reverse-Lookup Extension"
    Name: DNS
    Type: Module
    Version: 1.0.0
    License: "Apache 2.0"
]

sys.util/make-scheme [
    title: "DNS Lookup"
    name: 'dns
    actor: dns-actor/
    spec: system.standard.port-spec-net
]
