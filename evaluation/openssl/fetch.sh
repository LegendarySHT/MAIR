# #!/bin/bash
URL="https://github.com/openssl/openssl.git"

git clone $URL repo --depth 1

dd if=/dev/random of=corpus/openssl/test.txt bs=1K count=1
