# Crypto

Language: English | [简体中文](../zh_CN/crypto.md)

| Property | Value |
| --- | --- |
| Build option | `RIWO_BUILD_CRYPTO` (ON by default) |
| Source-tree target | `riwo.crypto` |
| Installed target | `Riwo::crypto` |
| Umbrella header | `<riwo/crypto.h>` |
| Depends on | Core; Windows BCrypt |

Crypto owns reusable cryptographic and identifier primitives that are shared by
HTTP and WebSocket without making them part of the Core runtime boundary.

## Header map

| Header | Facilities |
| --- | --- |
| `<riwo/crypto/base64.h>` | Base64 and Base64URL encoding and decoding |
| `<riwo/crypto/secure_random.h>` | Operating-system secure random bytes |
| `<riwo/crypto/sha1.h>` | Incremental SHA-1 and hex/Base64 output |
| `<riwo/crypto/sha256.h>` | Incremental SHA-256 with raw bytes, hex, and Base64 output |
| `<riwo/crypto/hmac_sha256.h>` | HMAC-SHA256 tag generation and verification |
| `<riwo/crypto/uuid.h>` | UUID parsing and versions 4, 5, 6, and 7 generation |

The public functions and types are in namespace `riwo::crypto`. Link `riwo.crypto`
or `Riwo::crypto` whenever one of these headers is used directly.

`base64_decode` and `base64url_decode` return decoded bytes in an
`optional<std::string>`. Valid empty input produces an empty string. Invalid
input produces a disengaged optional. Both decoders reject whitespace and ignore
unused bits in the final encoded digit. Standard Base64 requires padding for
partial groups. Base64URL decoding accepts padded and unpadded input.
Call `base64url_encode(input, true)` to include padding.
[RFC 4648](https://www.rfc-editor.org/rfc/rfc4648.html)

`sha256` accepts chunks through `append`. Call `digest()`, `hex()`, or
`base64()` to read the current digest and keep appending. Calls to `finalize()`
are idempotent. Appending after finalization throws `riwo::logic_error`.

`hmac_sha256(key, message)` computes a 32-byte tag in one call.
`hmac_sha256_verify(key, message, tag)` takes the full 32-byte tag and compares
its contents in constant time. The default build includes SHA-256 and HMAC-SHA256
with the dependencies listed above. [RFC 6234](https://www.rfc-editor.org/rfc/rfc6234.html)

SHA-1 remains available for compatibility protocols such as the WebSocket
handshake and UUID version 5. It must not be selected for new collision-resistant
security designs.

Percent encoding and wildcard matching are text primitives in
[Core](core.md), not Crypto facilities.
