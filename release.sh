#!/bin/bash

set -e

VERSION="$1"

if [ "" = "$VERSION" ] || [ "x${VERSION#[0-9]}" = "x$VERSION" ]; then
    cat << EOF
Usage: $0 VERSION

Remember to update NEWS first. This script is only meant to be run by the
maintainer.
EOF
    exit 1
fi

if ! head -n1 NEWS | grep -q '^-'; then
    echo "$0: no entries at the top of NEWS"
    exit 1
fi


./configure
make docs-clean
make docs RELEASE="$VERSION" || exit 1

mv NEWS old_NEWS
echo -n "Version $VERSION: " > NEWS
date         >> NEWS
cat old_NEWS >> NEWS
rm -f old_NEWS

git add NEWS doc

cat << EOF

All done, now run:

git commit -m "Release $VERSION"
git tag v$VERSION
git push origin v$VERSION
EOF
