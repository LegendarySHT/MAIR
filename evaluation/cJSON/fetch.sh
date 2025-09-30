# #!/bin/bash
URL="https://github.com/DaveGamble/cJSON.git"


# Use depth 1 to avoid cloning the history
git clone $URL repo --depth 1

mkdir -p corpus/cJSON@fuzz_main

cp repo/fuzzing/inputs/* corpus/cJSON@fuzz_main

