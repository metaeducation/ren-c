#!/usr/bin/env r3

; Modular Cryptography Demo
; Shows how AES-GCM is built from independent components

print "=== MODULAR CRYPTO ENGINE DEMO ==="
print ""

; === THE COMPONENTS ===

key: #{00112233445566778899AABBCCDDEEFF}  ; 128-bit AES key
block: #{DEADBEEFCAFEBABE0123456789ABCDEF}  ; 16-byte block

print ["1. PURE AES BLOCK CIPHER"]
print ["   Input: " mold block]
encrypted-block: aes-encrypt-block key block
print ["   AES(key, block): " mold encrypted-block]
decrypted-block: aes-decrypt-block key encrypted-block
print ["   AES⁻¹(key, encrypted): " mold decrypted-block]
print ["   Round-trip success:" block = decrypted-block]
print ""

print ["2. COUNTER MODE (AES → Stream Cipher)"]
plaintext: "Hello, modular crypto world!"
plaintext-blob: as blob! plaintext
nonce: #{00000000000000000000000000000001}  ; 16-byte counter
print ["   Plaintext: " plaintext]
print ["   CTR nonce: " mold nonce]
ciphertext: ctr-encrypt key nonce plaintext-blob
print ["   CTR ciphertext: " mold ciphertext]
decrypted-ctr: ctr-encrypt key nonce ciphertext  ; CTR: decrypt = encrypt
print ["   CTR decrypt: " to text! decrypted-ctr]
print ["   Round-trip success:" plaintext-blob = decrypted-ctr]
print ""

print ["3. GHASH AUTHENTICATION"]
aad: "This is additional authenticated data"
aad-blob: as blob! aad
auth-key: aes-encrypt-block key #{00000000000000000000000000000000}
print ["   AAD: " aad]
print ["   Auth key: AES(key, 0) = " mold auth-key]
tag: ghash-auth auth-key aad-blob ciphertext
print ["   GHASH tag: " mold tag]
print ""

print ["4. COMPOSED AES-GCM (All Components Together)"]
gcm-nonce: #{000000000000000000000001}  ; 12-byte nonce
print ["   GCM encrypting: " plaintext]
print ["   With AAD: " aad]
gcm-encrypted: aes-gcm-encrypt key gcm-nonce plaintext-blob aad-blob
print ["   GCM result: " mold gcm-encrypted]
gcm-decrypted: aes-gcm-decrypt key gcm-nonce gcm-encrypted aad-blob
print ["   GCM decrypted: " to text! gcm-decrypted]
print ["   Round-trip success:" plaintext-blob = gcm-decrypted]
print ""

print "=== CLASSIC CAR SHOW COMPLETE ==="
print "Each component is visible and usable independently!"
