# third_party/tbb

`life/` and `primes/` both link against Intel Threading Building Blocks (TBB) —
`tbb::task_scheduler_init`, `tbb::parallel_for`, `tbb::concurrent_hash_map`, etc.
This directory pins the exact vintage of TBB these programs were written
against in 2011, rather than whatever a package manager happens to install
today.

## What's here

- `tbb40_20111130oss_src.tgz` — the official open-source release tarball for
  TBB 4.0's 2011-11-30 update (`tbb40_20111130oss`), the last TBB release that
  actually shipped within 2011. Originally distributed from
  threadingbuildingblocks.org; that site is long gone, so this copy is
  bundled directly rather than fetched at build time to avoid depending on
  link rot in an archival mirror.
- `tbb-cxx17-compat.patch` — a one-line patch to `build/linux.gcc.inc`
  (`CPLUS_FLAGS += -std=gnu++98`). TBB 4.0's `tbbmalloc` proxy
  (`src/tbbmalloc/proxy.cpp`) uses pre-C++17 dynamic exception specifications
  (`throw(std::bad_alloc)`) — legal, if deprecated, in 2011. A modern GCC's
  default dialect is `gnu++17`, which removed that syntax entirely and turns
  it into a hard compile error. The patch forces the build to use the
  dialect the code actually assumed; it touches only the build's own
  compiler invocation, not any of the vendored TBB source itself, which
  stays byte-for-byte the original 2011 release.

## Building it

`scripts/build-tbb.sh` extracts the tarball, applies the patch, and builds
`tbb` + `tbbmalloc` + `tbbmalloc_proxy` with the system's `make`/`g++`,
leaving stable symlinks at `third_party/tbb/{include,lib}` for `life/` and
`primes/`'s Makefiles to reference. It's idempotent (skips rebuilding if
already built; pass `--force` to redo it) and is wired as a Makefile
prerequisite in both projects, so a plain `make` in `life/` or `primes/`
triggers it automatically the first time.

Everything under `third_party/tbb/` (the extracted source and build output)
is generated and gitignored — only the tarball, the patch, and this README
are checked in.
