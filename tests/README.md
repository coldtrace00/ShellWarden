# Tests

```
cmake -S . -B build -DSHELLWARDEN_BUILD_TESTS=ON
cmake --build build
./build/tests/unit_tests                     # no network, any OS
QT_QPA_PLATFORM=offscreen ./build/tests/qml_safety_tests   # no window needed
python3 tests/check_qml_text.py              # every QML text is plain text
sudo tests/integration/setup.sh              # Linux: test servers on 127.0.0.1
sudo ./build/tests/integration_tests
```

- `unit_tests.cpp` — vault, protected secrets, encrypted journal, file names
  from servers, paste guard, command history filters, known_hosts hashing,
  minisign, update checks (downgrade), Shadowsocks / BLAKE3 vectors, ss:// links,
  master password change, text from the other side, background threads.
- `qml_safety_tests.cpp` — images, http in rich text and tooltips cannot load
  anything from the network or from file://server/… (a control run shows the test
  sees loads). It also checks the known limit: an image inside RichText/StyledText
  from file://server/… is opened by Qt past the interceptor — that is why all
  text must be plain (see src/app/QmlSafety.h).
- `integration/https_ip.py` — a test-only HTTPS "what is my IP" service (self-signed
  certificate for localhost made by setup.sh) for the IP check through Shadowsocks.
- `check_qml_text.py` — no Text/Label without textFormat: PlainText, no raw
  Button/MenuItem/CheckBox… (their labels show HTML; use the App* wrappers).
- `integration_tests.cpp` — real OpenSSH (sshd on 2222, ssh-keygen, ssh-agent),
  puttygen, SOCKS5 (`integration/socks5.py`), tinyproxy, shadowsocks-libev and
  shadowsocks-rust. All passwords and keys there are test-only.
- `fuzz/` — libFuzzer targets (clang):
  ```
  cmake -S . -B fuzz -G Ninja -DSHELLWARDEN_BUILD_TESTS=ON -DSHELLWARDEN_FUZZ=ON \
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
        "-DCMAKE_C_FLAGS=-fsanitize=address,undefined,fuzzer-no-link" \
        "-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined,fuzzer-no-link"
  cmake --build fuzz --target fuzz_ss_link && ./fuzz/tests/fuzz_ss_link tests/fuzz/corpus/ss_link
  ```

CI runs all of this: `.github/workflows/ci.yml`.
