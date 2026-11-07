# BLAKE3 (portable C)

From https://github.com/BLAKE3-team/BLAKE3, tag `1.5.4`
(commit 95e42b84fc4709974c7b23c7ae885989ab36c31e), folder `c/`.
Only the portable implementation is used (no SIMD files): ShellWarden hashes
a few dozen bytes per connection (Shadowsocks 2022 key derivation).

License: CC0 1.0 or Apache 2.0 (see LICENSE_CC0, LICENSE_A2), both compatible
with ShellWarden's MIT license. The files are unchanged.
