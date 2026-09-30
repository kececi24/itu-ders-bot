# Dependency provenance

`cmake/dependencies.lock.json` pins official source archives and published
SHA-256 values. `scripts/bootstrap.py` verifies each archive before extraction,
builds under `.deps/<target>`, and copies the complete upstream license texts
from those archives into the local install. Packaging copies only those named
licenses plus the license for the vendored nlohmann JSON header.

macOS links Apple's system libcurl; its library is not redistributed. Windows
uses static libcurl/nghttp2/zlib and native Schannel. Linux uses static
libcurl/nghttp2/zlib/OpenSSL while retaining dynamic system C/C++ runtime linkage.

Upstream source/checksum references are recorded beside every lock entry.
Updating an entry requires reviewing the upstream release, replacing its exact
URL/version/hash together, rebuilding on each native target, and passing the
offline and archive checks. Do not replace hashes automatically on mismatch.
