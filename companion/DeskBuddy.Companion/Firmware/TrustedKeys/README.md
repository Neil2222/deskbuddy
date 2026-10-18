# Trusted firmware signing keys

Each `*.pub.pem` file in this folder is the public half of an ECDSA P-256 key. It is embedded in the Companion at
build time (`FirmwareTrust.Production`), and the Companion accepts a firmware package only if one of these keys
signed it.

| Key | SHA-256 fingerprint (of the SubjectPublicKeyInfo DER) | Created |
|---|---|---|
| `deskbuddy-firmware-2026.pub.pem` | `00ecd57e1e562ee38a99595b5795f85b92c28281a84cea2987edf40c37e34c44` | 2026-10-18 |

Check it with `openssl pkey -pubin -in deskbuddy-firmware-2026.pub.pem -outform DER | openssl dgst -sha256`
(`deskbuddy-firmware-tool verify` prints it too). The private half is not in any repository. It is stored under
the developer's Windows profile (`%USERPROFILE%\DeskBuddy-signing\`), in a folder only that user and SYSTEM can
open.

- **Public keys only.** The build fails if a file here contains a private key, and so does the Companion when it
  starts.
- **Adding a key** (docs/firmware-packages.md, "Signing"):
  1. Create the key offline.
  2. Put only `deskbuddy-firmware-<year>.pub.pem` here.
  3. Commit it, and record its SHA-256 fingerprint in the docs.
  4. Rebuild and publish the Companion.
- **Never trusted any other way.** A key is not trusted from a file next to the exe, from an environment variable,
  or because it signed the package being opened.
