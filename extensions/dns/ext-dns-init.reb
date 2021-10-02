REBOL [
    Title: "Domain Name Lookup / Reverse-Lookup Extension"
    Name: DNS
    Type: Module
    Version: 1.0.0
    License: {Apache 2.0}
]

sys/make-scheme [
    title: "DNS Lookup"
    name: 'dns
    actor: get-dns-actor-handle
    spec: system/standard/port-spec-net
]
