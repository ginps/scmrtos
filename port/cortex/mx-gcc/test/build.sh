#!/bin/sh
# build.sh OUT.elf [extra flags...]
# Builds the test image with clang for Cortex-M4 (soft float unless -mfloat-abi=hard given).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
port=${PORT_DIR:-$root/port/cortex/mx-gcc}     # PORT_DIR: port copy (mutation testing)
out=$1; shift
fl="--target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb"
case "$*" in *float-abi=hard*) ;; *) fl="$fl -mfloat-abi=soft";; esac
cxx="clang++ $fl -std=c++17 -ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics -Wall -Wextra -Werror -Wno-unused-function -I$here -I$root/core -I$port"
tmp=$(mktemp -d)
for f in $root/core/os_kernel.cpp $root/core/os_services.cpp $port/os_target.cpp $here/test.cpp $here/startup.cpp; do
    $cxx "$@" -c "$f" -o "$tmp/$(basename "$f").o"
done
clang $fl "$@" -I$here -c $port/os_target_asm.S -o "$tmp/asm.o"
ld.lld -T "$here/cm4sim.ld" --gc-sections -o "$out" "$tmp"/*.o
rm -rf "$tmp"
