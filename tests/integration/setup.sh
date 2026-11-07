#!/bin/bash
# Prepares the servers tests/integration_tests.cpp talks to (Linux, as root).
# Everything listens on 127.0.0.1 only and uses test-only passwords and keys.
#
#   sudo apt-get install -y openssh-server tinyproxy shadowsocks-libev putty-tools python3
#   sudo tests/integration/setup.sh
#   ./build/tests/integration_tests
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/sw-test
mkdir -p "$W" "$W/www"

# test user for SSH logins
id tester >/dev/null 2>&1 || useradd -m -s /bin/bash tester
echo 'tester:Secret-Pass-123' | chpasswd

# sshd on 127.0.0.1:2222 (system host keys; the tests read the ed25519 one)
ssh-keygen -A >/dev/null
mkdir -p /run/sshd
cat > "$W/sshd_config" <<CFG
Port 2222
ListenAddress 127.0.0.1
HostKey /etc/ssh/ssh_host_ed25519_key
HostKey /etc/ssh/ssh_host_rsa_key
PasswordAuthentication yes
KbdInteractiveAuthentication no
PubkeyAuthentication yes
UsePAM yes
PidFile $W/sshd.pid
AllowTcpForwarding yes
AllowAgentForwarding yes
Subsystem sftp internal-sftp
CFG
/usr/sbin/sshd -f "$W/sshd_config" -E "$W/sshd.log"

# SOCKS5 with user/password on 1080 (logs what it was asked to connect to)
nohup python3 "$HERE/socks5.py" 1080 puser ppass "$W/socks.log" >/dev/null 2>&1 &

# HTTP CONNECT on 8888
cat > "$W/tinyproxy.conf" <<CFG
Port 8888
Listen 127.0.0.1
Timeout 600
Allow 127.0.0.1
ConnectPort 2222
LogFile "$W/tinyproxy.log"
PidFile "$W/tinyproxy.pid"
CFG
tinyproxy -c "$W/tinyproxy.conf"

# web server with a 2 MB file (through-the-tunnel transfers)
head -c 2000000 /dev/urandom > "$W/www/big.bin"
(cd "$W/www" && nohup python3 -m http.server 8766 --bind 127.0.0.1 >/dev/null 2>&1 &)

# Shadowsocks AEAD-2017 (shadowsocks-libev)
for spec in "8388 chacha20-ietf-poly1305" "8389 aes-256-gcm" "8390 aes-128-gcm"; do
    set -- $spec
    nohup ss-server -s 127.0.0.1 -p "$1" -k Test-SS-Pass-1 -m "$2" -u >/dev/null 2>&1 &
done

# Shadowsocks 2022 (shadowsocks-rust, pinned release, checksum verified)
SSR_VER=1.21.2
SSR_SHA=6f88280ab5aec46ceaed41f8882dc889061b8d334ce34bee98be1fc11eb32558
if [ ! -x "$W/ssr/ssserver" ]; then
    curl -sSL -o "$W/ssr.tar.xz" \
        "https://github.com/shadowsocks/shadowsocks-rust/releases/download/v$SSR_VER/shadowsocks-v$SSR_VER.x86_64-unknown-linux-gnu.tar.xz"
    echo "$SSR_SHA  $W/ssr.tar.xz" | sha256sum -c -
    mkdir -p "$W/ssr" && tar -xJf "$W/ssr.tar.xz" -C "$W/ssr"
fi
cat > "$W/ss2022.json" <<CFG
{"servers":[
 {"server":"127.0.0.1","server_port":8391,"method":"2022-blake3-aes-256-gcm","password":"c3dTZXN0S2V5LTMyLWJ5dGVzLWFlczI1Ni0wMDAwMDA="},
 {"server":"127.0.0.1","server_port":8392,"method":"2022-blake3-aes-128-gcm","password":"c3dUZXN0S2V5LWFlczEyOA=="},
 {"server":"127.0.0.1","server_port":8393,"method":"2022-blake3-chacha20-poly1305","password":"c3dUZXN0S2V5LTMyLWJ5dGVzLWNoYWNoYTIwLTAwMDA="},
 {"server":"127.0.0.1","server_port":8394,"method":"2022-blake3-aes-256-gcm","password":"c3dUZXN0LWlkZW50aXR5LWtleS0zMi1ieXRlcy0wMDA=",
  "users":[{"name":"tester","password":"c3dUZXN0LXVzZXIta2V5LTMyLWJ5dGVzLTAwMDAwMDA="}]}
]}
CFG
nohup "$W/ssr/ssserver" -c "$W/ss2022.json" > "$W/ssserver.log" 2>&1 &

sleep 2
echo "test servers are up (logs in $W)"
