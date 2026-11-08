#!/bin/bash
# Prepares the servers tests/integration_tests.cpp talks to (Linux, as root).
# Everything listens on 127.0.0.1 only and uses test-only passwords and keys.
# The servers keep none of this script's input/output open, so
# `setup.sh | tail` returns as soon as they are up.
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
/usr/sbin/sshd -f "$W/sshd_config" -E "$W/sshd.log" </dev/null >/dev/null 2>&1

# SOCKS5 with user/password on 1080 (logs what it was asked to connect to)
nohup python3 "$HERE/socks5.py" 1080 puser ppass "$W/socks.log" </dev/null >/dev/null 2>&1 &

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
tinyproxy -c "$W/tinyproxy.conf" </dev/null >/dev/null 2>&1

# web server with a 2 MB file (through-the-tunnel transfers)
head -c 2000000 /dev/urandom > "$W/www/big.bin"
(cd "$W/www" && exec nohup python3 -m http.server 8766 --bind 127.0.0.1) </dev/null >/dev/null 2>&1 &

# HTTPS "what is my IP" service with a test-only certificate for "localhost"
# (the IP check through Shadowsocks speaks TLS inside the tunnel)
mkdir -p "$W/tls"
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -days 2 \
    -subj /CN=localhost -addext subjectAltName=DNS:localhost \
    -keyout "$W/tls/key.pem" -out "$W/tls/cert.pem" >/dev/null 2>&1
nohup python3 "$HERE/https_ip.py" 8767 "$W/tls/cert.pem" "$W/tls/key.pem" </dev/null >/dev/null 2>&1 &

# Shadowsocks AEAD-2017 (shadowsocks-libev). The Debian/Ubuntu package starts
# its own service on 8388 with a random password; stop it so ours gets the port.
if command -v systemctl >/dev/null 2>&1; then
    systemctl stop shadowsocks-libev.service 2>/dev/null || true
fi
pkill -x ss-server 2>/dev/null || true
sleep 0.5
for spec in "8388 chacha20-ietf-poly1305" "8389 aes-256-gcm" "8390 aes-128-gcm"; do
    set -- $spec
    nohup ss-server -s 127.0.0.1 -p "$1" -k Test-SS-Pass-1 -m "$2" -u </dev/null >/dev/null 2>&1 &
done

# Shadowsocks 2022 (shadowsocks-rust, pinned release, checksum verified)
SSR_VER=1.21.2
SSR_SHA=6f88280ab5aec46ceaed41f8882dc889061b8d334ce34bee98be1fc11eb32558
if [ ! -x "$W/ssr/ssserver" ]; then
    curl -sSLf --retry 5 --retry-all-errors --retry-delay 5 -o "$W/ssr.tar.xz" \
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
nohup "$W/ssr/ssserver" -c "$W/ss2022.json" </dev/null > "$W/ssserver.log" 2>&1 &

sleep 2
# every test port must be ours and listening
for p in 2222 1080 8888 8766 8767 8388 8389 8390 8391 8392 8393 8394; do
    ok=0
    for _ in $(seq 20); do
        if (exec 3<>/dev/tcp/127.0.0.1/$p) 2>/dev/null; then ok=1; break; fi
        sleep 0.25
    done
    [ $ok = 1 ] || { echo "port $p is not listening" >&2; exit 1; }
done
ours=$(ps -eo args= | grep -c '^ss-server .*-k Test-SS-Pass-1' || true)
other=$(ps -eo args= | grep '^ss-server' | grep -vc 'Test-SS-Pass-1' || true)
if [ "$ours" != 3 ] || [ "$other" != 0 ]; then
    echo "ss-server: $ours test processes, $other others (want 3 and 0)" >&2
    ps -eo pid,args | grep '[s]s-server' >&2
    exit 1
fi
echo "test servers are up (logs in $W)"
