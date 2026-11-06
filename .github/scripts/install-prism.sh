#!/usr/bin/env bash
#
# Builds Prism at the revision pinned in packaging/dependencies.env and
# installs it into PREFIX (default: /usr), so that dependency('prism') can be
# found through pkg-config
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=../../packaging/dependencies.env
source "$root/packaging/dependencies.env"

prefix="${PREFIX:-/usr}"
workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT

git init -q "$workdir/prism"
git -C "$workdir/prism" fetch -q --depth 1 "$PRISM_URL" "$PRISM_COMMIT"
git -C "$workdir/prism" checkout -q FETCH_HEAD

git clone -q "$DEMANGLER_URL" "$workdir/prism/subprojects/demangler"
git -C "$workdir/prism/subprojects/demangler" checkout -q "$DEMANGLER_COMMIT"
# Meson only applies the wrap's patch overlay when it downloads the
# subproject itself
tar -xf "$workdir/prism/subprojects/packagefiles/demangler.tar" \
    -C "$workdir/prism/subprojects"

meson setup "$workdir/build" "$workdir/prism" \
    --prefix="$prefix" --buildtype=release -Dwerror=false
meson compile -C "$workdir/build"
meson install -C "$workdir/build" --quiet

echo "Installed Prism ${PRISM_COMMIT:0:12} into $prefix"
