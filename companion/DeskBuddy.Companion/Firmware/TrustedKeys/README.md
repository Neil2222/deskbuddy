# Trusted firmware signing keys

Each `*.pub.pem` file in this folder is the public half of an ECDSA P-256 key. It is embedded in the Companion at
build time (`FirmwareTrust.Production`), and the Companion accepts a firmware package only if one of these keys
signed it.

- **Empty for now.** No production key exists yet, so this Companion refuses every firmware package. That is
  intended.
- **Public keys only.** The build fails if a file here contains a private key, and so does the Companion when it
  starts.
- **Adding a key** (docs/firmware-packages.md, "Signing"):
  1. Create the key offline.
  2. Put only `deskbuddy-firmware-<year>.pub.pem` here.
  3. Commit it, and record its SHA-256 fingerprint in the docs.
  4. Rebuild and publish the Companion.
- **Never trusted any other way.** A key is not trusted from a file next to the exe, from an environment variable,
  or because it signed the package being opened.
