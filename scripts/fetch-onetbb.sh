#!/bin/bash
# Fetches a stable modern oneTBB release into third_party/onetbb/{include,lib}
# so life/'s greedy and beam can link against it.
#
# Unlike third_party/tbb (the 2011-vintage TBB 4.0, bundled directly — see
# third_party/README.md), this is downloaded at build time: oneTBB is still
# actively distributed from GitHub releases, so there's no link-rot risk to
# work around, and vendoring a second multi-megabyte tarball into the repo
# isn't worth it.
#
# Source: oneTBB v2023.1.0 (github.com/uxlfoundation/oneTBB), the Linux
# binary release tarball (prebuilt with gcc4.8-ABI-compatible binaries that
# work with any newer system gcc).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
VENDOR_DIR="$REPO_ROOT/third_party"

ONETBB_VERSION=2023.1.0
TARBALL_NAME="oneapi-tbb-${ONETBB_VERSION}-lin.tgz"
URL="https://github.com/uxlfoundation/oneTBB/releases/download/v${ONETBB_VERSION}/${TARBALL_NAME}"
TARBALL="$VENDOR_DIR/$TARBALL_NAME"
SRC_DIR="$VENDOR_DIR/oneapi-tbb-${ONETBB_VERSION}"
STABLE_LINK="$VENDOR_DIR/onetbb"
EXPECTED_SHA256="349d0e8b08cae4a5ab2668d54ff4e90b0fa012a332de6fb156961ddc119cd617"

force=0
if [[ "${1:-}" == "--force" ]]; then
    force=1
fi

if [[ -e "$STABLE_LINK/lib/libtbb.so.12" && "$force" -eq 0 ]]; then
    echo "fetch-onetbb.sh: already fetched at $STABLE_LINK (pass --force to redo)"
    exit 0
fi

echo "fetch-onetbb.sh: downloading $TARBALL_NAME..."
curl -fL --retry 3 -o "$TARBALL" "$URL"

echo "fetch-onetbb.sh: verifying checksum..."
actual_sha256="$(sha256sum "$TARBALL" | awk '{print $1}')"
if [[ "$actual_sha256" != "$EXPECTED_SHA256" ]]; then
    echo "fetch-onetbb.sh: error: $TARBALL checksum mismatch" >&2
    echo "  expected: $EXPECTED_SHA256" >&2
    echo "  actual:   $actual_sha256" >&2
    exit 1
fi

echo "fetch-onetbb.sh: extracting $TARBALL_NAME..."
rm -rf "$SRC_DIR"
tar xzf "$TARBALL" -C "$VENDOR_DIR"

echo "fetch-onetbb.sh: linking stable paths..."
mkdir -p "$STABLE_LINK"
ln -sfn "../$(basename "$SRC_DIR")/include" "$STABLE_LINK/include"
ln -sfn "../$(basename "$SRC_DIR")/lib/intel64/gcc4.8" "$STABLE_LINK/lib"

echo "fetch-onetbb.sh: done. headers: $STABLE_LINK/include  libs: $STABLE_LINK/lib"
