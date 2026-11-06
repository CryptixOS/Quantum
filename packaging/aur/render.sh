#!/usr/bin/env bash
#
# Usage: packaging/aur/render.sh <version> <output directory>
#
# Writes the PKGBUILD for release <version> (without the leading 'v') into
# the output directory, using the revisions pinned in packaging/dependencies.env
set -euo pipefail

if [ $# -ne 2 ]; then
    echo "usage: $0 <version> <output directory>" >&2
    exit 1
fi

version="$1"
outdir="$2"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../dependencies.env
source "$here/../dependencies.env"

if ! [[ "$version" =~ ^[0-9]+(\.[0-9]+)*$ ]]; then
    echo "error: '$version' is not a valid pkgver" >&2
    exit 1
fi

mkdir -p "$outdir"
sed -e "s/@PKGVER@/$version/" \
    -e "s/@PRISM_COMMIT@/$PRISM_COMMIT/" \
    -e "s/@DEMANGLER_COMMIT@/$DEMANGLER_COMMIT/" \
    "$here/PKGBUILD.in" > "$outdir/PKGBUILD"
