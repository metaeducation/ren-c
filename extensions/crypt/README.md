## Cryptography Extension

Copyright (C) 2012 Saphirion AG
Copyright (C) 2012-2025 Ren-C Open Source Contributors

Distributed under the Apache 2.0 License
mbedTLS is distributed under the Apache 2.0 License

### Purpose

The Cryptography extension currently includes an eclectic set of ciphers, key
generators, and hashes written in C.  They're sufficient to write a TLS 1.2
module that can support the cipher suites spoken by example.com in 2025:

    TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384 (secp256r1)
    TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256 (secp256r1)
    TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256 (secp256r1)

These are the standard "Authenticated Encryption with Associated Data" (AEAD)
ciphers that are advocated for at the moment.

C code for the handshaking protocol of TLS itself is not included.  Instead,
that is implemented in usermode in Rebol by a file called %prot-tls.r.
So the extension only has the basic crypto primitives.

### Built With mbedTLS version 3 (for now)...

R3-Alpha originally had a few hand-picked routines for hashing picked from
OpenSSL.  Saphirion added support for the AES streaming cipher and Diffie
Hellman keys in order to do Transport Layer Security 1.1 (TLS, e.g. the "S" in
the "Secure" of HTTPS).  Much of the code for this cryptography was extracted
from the "axTLS" library by Cameron Rich, but there were other sources.

The patchwork method of pulling non-audited C files off the internet was
haphazard and insecure.  So in 2020, the Crypt module was shifted to use
the "mbedTLS" library.  At that time it was at version 2.0, and was an
admirably factored codebase from which ciphers and hashes could be picked
a-la carte:

  https://github.com/Mbed-TLS/mbedtls/tree/21522a49aa0d1e8b76e9b4d5d289f95cd85f2782/include/mbedtls

There was a master configuration file which allowed fine-grained control over
every setting you might wish:

  https://github.com/Mbed-TLS/mbedtls/blob/21522a49aa0d1e8b76e9b4d5d289f95cd85f2782/include/mbedtls/config.h

Unfortunately, good design did not last.  It began to atrophy in version 3.0,
and collapsed completely in 4.0 to become a blight known as "PSA Crypto":

  https://github.com/Mbed-TLS/TF-PSA-Crypto/tree/19edaa785dd71ec8f0c9f72235243314c3d895fa/include/psa

Transparency and simplicity gave way to the desire to abstract, with these
abstractions allowing embedded systems to swap in their hardware acceleration
for various mechanisms.  There's no good reason why this necessitated wiping
out a clear and easy-to-grok layer of organized and configurable code for
those who wanted it.  :-(

In any case, at time of writing the files have been sync'd to 3.6:

  "Mbed TLS 3.6 is a long-term support (LTS) branch. It will be supported
  with bug-fixes and security fixes until at least March 2027."

Hopefully by 2027, AI will be competent enough to help maintain a curated
and properly factored set of cryptography primitives that haven't taken
on insane dependencies and obfuscation, in the false dichotomy of clarity
and "performance/flexibility".

### Future Work

Due to the divergence in design philosophy of mbedTLS 4, it is likely that
alternative implementation strategies will be pursued.

There's not yet a "BigNum" implementation settled upon for INTEGER! in the
language as a whole.  Ideally, whatever BigNum implementation the cryptography
used would run common code.

https://forum.rebol.info/t/planning-ahead-for-bignum-integer/623

No "streaming" hashes have been implemented yet (even though mbedTLS has
support for them).  One could imagine a hash "PORT!" to which data could be
written a piece at a time, calculating SHA hashes on multi-gigabyte files...
digesting the information as it is read in chunks and then discarded.

Being able to break the ciphers into smaller modules might be appealing,
where they could be loaded on an as-needed basis.  This could run against
the "single file" aesthetic of historical Rebol, but having many tiny DLLs
may be a benefit as a build option for some.
