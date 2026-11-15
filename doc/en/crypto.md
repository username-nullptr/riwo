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
| `<riwo/crypto/base64.h>` | Base64 encoding for text and byte spans |
| `<riwo/crypto/secure_random.h>` | Operating-system secure random bytes |
| `<riwo/crypto/sha1.h>` | Incremental SHA-1 and hex/Base64 output |
| `<riwo/crypto/uuid.h>` | UUID parsing and versions 4, 5, 6, and 7 generation |

The public functions and types are in namespace `riwo::crypto`. Link `riwo.crypto`
or `Riwo::crypto` whenever one of these headers is used directly.

SHA-1 remains available for compatibility protocols such as the WebSocket
handshake and UUID version 5. It must not be selected for new collision-resistant
security designs.

Percent encoding and wildcard matching are text primitives in
[Core](core.md), not Crypto facilities.
