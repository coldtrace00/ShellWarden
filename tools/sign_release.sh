#!/usr/bin/env bash
# Signs a ShellWarden release so the in-app updater accepts it.
#
#   tools/sign_release.sh <folder with the release files> [version]
#
# Writes SHA256SUMS (sha256 of every file) and SHA256SUMS.minisig (minisign
# signature of that list). Upload all of them to the GitHub release.
#
# Once, before the first release:
#   minisign -G -p minisign.pub -s ~/.minisign/minisign.key
#   - keep minisign.key private and backed up (it has its own password)
#   - build with:  cmake -DAPP_UPDATE_PUBKEY="RWQ…(2nd line of minisign.pub)" …
#     from then on the app installs only updates signed with this key.
# minisign: apt/dnf/pacman install minisign, or https://github.com/jedisct1/minisign/releases
set -euo pipefail
dir=${1:?usage: $0 <release folder> [version]}
ver=${2:-}
key=${MINISIGN_KEY:-$HOME/.minisign/minisign.key}
command -v minisign >/dev/null || { echo "minisign is not installed"; exit 1; }
[[ -f $key ]] || { echo "No secret key at $key (set MINISIGN_KEY)"; exit 1; }

cd "$dir"
rm -f SHA256SUMS SHA256SUMS.minisig
files=$(ls -1 | grep -vE '^(SHA256SUMS|SHA256SUMS\.minisig)$' || true)
[[ -n $files ]] || { echo "No files in $dir"; exit 1; }
sha256sum $files > SHA256SUMS
minisign -S -s "$key" -m SHA256SUMS -t "ShellWarden ${ver:-release} $(date -u +%Y-%m-%d)"
echo
cat SHA256SUMS
[[ -f ${MINISIGN_PUB:-} ]] && minisign -V -p "$MINISIGN_PUB" -m SHA256SUMS
echo "Upload to the release: $files SHA256SUMS SHA256SUMS.minisig"
