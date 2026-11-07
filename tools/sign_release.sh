#!/usr/bin/env bash
# Signs a ShellWarden release so the in-app updater accepts it.
#
#   tools/sign_release.sh <folder with the release files> [version]
#
# Writes SHA256SUMS (sha256 of every file) and SHA256SUMS.minisig (minisign
# signature of that list). Upload all of them to the GitHub release.
#
# The secret key: $MINISIGN_KEY, ~/Desktop/minisign-keys/minisign.key or ~/.minisign/minisign.key.
# The matching public key is minisign.pub in the project root; builds embed it,
# so the app installs only updates signed with this key.
# minisign: apt/dnf/pacman install minisign, or https://github.com/jedisct1/minisign/releases
set -euo pipefail
dir=${1:?usage: $0 <release folder> [version]}
ver=${2:-}
key=${MINISIGN_KEY:-}
if [[ -z $key ]]; then
    for k in "$HOME/Desktop/minisign-keys/minisign.key" "$HOME/.minisign/minisign.key"; do
        [[ -f $k ]] && { key=$k; break; }
    done
fi
[[ -n $key ]] || { echo "No minisign secret key found (set MINISIGN_KEY)"; exit 1; }
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
pub=${MINISIGN_PUB:-$(dirname "$0")/../minisign.pub}
[[ -f $pub ]] && minisign -V -p "$pub" -m SHA256SUMS
echo "Upload to the release: $files SHA256SUMS SHA256SUMS.minisig"
