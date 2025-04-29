Rebol [
    title: "Domain Name Lookup / Reverse-Lookup Extension"
    name: DNS
    type: module
    version: 1.0.0
    license: "Apache 2.0"
]

sys.util/make-scheme [
    title: "DNS Lookup"
    name: 'dns
    actor: dns-actor/
    spec: system.standard.port-spec-net
]
