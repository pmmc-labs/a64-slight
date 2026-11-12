#!/bin/sh
# Builds Swish (Erlang's processes on Chez Scheme) for the benchmarks: a
# non-threaded Chez Scheme first, beside any other (Homebrew's is threaded,
# which Swish doesn't support), then Swish on it. Everything goes under
# PREFIX (default ~/opt/swish); put PREFIX/bin on PATH, and bench/run.py
# finds swish and swish-build there.
#
#     bench/build-swish.sh [PREFIX]
#
# macOS needs Xcode's command-line tools and, from Homebrew, cmake,
# coreutils and make (GNU make 4.4 or later, as gmake): brew install cmake
# coreutils make. Ubuntu needs build-essential, cmake, uuid-dev and
# libsystemd-dev. Takes a few minutes.
set -eu

PREFIX=${1:-$HOME/opt/swish}
CHEZ_TAG=v10.4.1    # the release Swish 3.0.0's own CI builds on
SWISH_TAG=v3.0.0
SRC=$PREFIX/src

MAKE=make
command -v gmake > /dev/null && MAKE=gmake
case $($MAKE --version | head -1) in
    "GNU Make 4."[4-9]*|"GNU Make "[5-9]*) ;;
    *) echo "warning: Swish asks for GNU make 4.4 or later; $MAKE is $($MAKE --version | head -1)" >&2 ;;
esac

mkdir -p "$SRC"
cd "$SRC"
[ -d ChezScheme ] || git clone --depth 1 --branch "$CHEZ_TAG" https://github.com/cisco/ChezScheme
[ -d swish ] || git clone --depth 1 --branch "$SWISH_TAG" --recurse-submodules --shallow-submodules https://github.com/becls/swish

# Chez, non-threaded, its machine type found by configure (arm64osx on
# the M2 Max, a6le on x86-64 Linux).
cd "$SRC/ChezScheme"
./configure --nothreads --disable-x11 --installprefix="$PREFIX/chez"
$MAKE
$MAKE install

# Swish on that Chez: just its binaries and the boot files they need, no
# documentation (which needs LaTeX).
cd "$SRC/swish"
./configure --scheme="$PREFIX/chez/bin/scheme"
$MAKE
$MAKE -C src/swish install-bin install-batteries INSTALLROOT="$PREFIX"

echo
"$PREFIX/bin/swish" --version
echo "Now put $PREFIX/bin on PATH: export PATH=\"$PREFIX/bin:\$PATH\""
