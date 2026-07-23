#!/bin/bash
# Builds the 2011-vintage Intel TBB bundled in third_party/ into
# third_party/tbb/{include,lib} so life/ and primes/ can link against it.
#
# Source: tbb40_20111130oss (TBB 4.0, the 2011-11-30 update — the last TBB
# release that actually shipped within 2011). See third_party/README.md for
# provenance and why this exact build needs a small compatibility patch.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
VENDOR_DIR="$REPO_ROOT/third_party"
TARBALL="$VENDOR_DIR/tbb40_20111130oss_src.tgz"
PATCH="$VENDOR_DIR/tbb-cxx17-compat.patch"
SRC_DIR="$VENDOR_DIR/tbb40_20111130oss"
STABLE_LINK="$VENDOR_DIR/tbb"
EXPECTED_SHA256="540609f21d256b1da0bf5688933d7165676ac6aed59dc734ae7aa3efed8d9c67"

force=0
if [[ "${1:-}" == "--force" ]]; then
    force=1
fi

if [[ -e "$STABLE_LINK/lib/libtbb.so.2" && "$force" -eq 0 ]]; then
    echo "build-tbb.sh: already built at $STABLE_LINK (pass --force to rebuild)"
    exit 0
fi

echo "build-tbb.sh: verifying bundled tarball checksum..."
actual_sha256="$(sha256sum "$TARBALL" | awk '{print $1}')"
if [[ "$actual_sha256" != "$EXPECTED_SHA256" ]]; then
    echo "build-tbb.sh: error: $TARBALL checksum mismatch" >&2
    echo "  expected: $EXPECTED_SHA256" >&2
    echo "  actual:   $actual_sha256" >&2
    exit 1
fi

echo "build-tbb.sh: extracting $(basename "$TARBALL")..."
rm -rf "$SRC_DIR"
tar xzf "$TARBALL" -C "$VENDOR_DIR"

echo "build-tbb.sh: applying C++17 compatibility patch..."
(cd "$SRC_DIR" && patch -p1 < "$PATCH")

echo "build-tbb.sh: building TBB (tbb + tbbmalloc + tbbmalloc_proxy)..."
(cd "$SRC_DIR" && make tbb tbbmalloc tbbproxy)

release_dir="$(cd "$SRC_DIR" && ls -d build/linux_intel64_gcc_cc*_release 2>/dev/null | head -1)"
if [[ -z "$release_dir" ]]; then
    echo "build-tbb.sh: error: no release build dir found under $SRC_DIR/build" >&2
    exit 1
fi

echo "build-tbb.sh: linking stable paths..."
ln -sfn "$release_dir" "$SRC_DIR/lib"
ln -sfn "$(basename "$SRC_DIR")" "$STABLE_LINK"

echo "build-tbb.sh: done. headers: $STABLE_LINK/include  libs: $STABLE_LINK/lib"
