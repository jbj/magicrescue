#!/bin/bash

if [ "x$1" = "x--devel" ]; then 
    DEVEL=1
    VERSION=`date +%Y%m%d`
    cp new_NEWS new_NEWS~
    cp NEWS NEWS~
else
    DEVEL=
    VERSION="$1"
fi

APP=magicrescue
RELEASE=$APP-$VERSION

if [ "" = "$VERSION" ] || [ "x${VERSION#[0-9]}" = "x$VERSION" ]; then
    cat << EOF
Usage: $0 [ VERSION | --devel ]"

Remember to update new_NEWS first. This script is only meant to be run by the
maintainer.
EOF
    exit 1
fi

./configure
make docs-clean
make docs RELEASE=$VERSION || exit 1

if grep -q . new_NEWS; then
    mv NEWS old_NEWS

    if [ "$DEVEL" ]; then
	echo -n "Nightly build: " > NEWS
    else
	echo -n "Version $VERSION: " > NEWS
    fi

    date         >> NEWS
    cat new_NEWS >> NEWS
    echo         >> NEWS
    cat old_NEWS >> NEWS

    rm -f old_NEWS
    echo > new_NEWS

else
    if [ -z "$DEVEL" ]; then 
	echo "$0: no new_NEWS entries"
	exit 1
    fi
fi


echo "For less detailed change information, see the NEWS file" > ChangeLog
echo               >> ChangeLog
svn log -vr HEAD:1 >> ChangeLog

ln -s . $RELEASE

(echo ChangeLog; ls doc/*.1; svn ls -R) \
    |sed "s%^%$RELEASE/%"|egrep -v 'new_NEWS|release.sh|tests/'|grep -v '/$' \
    |tar -T- -czvf release/$RELEASE.tar.gz

rm -f ChangeLog $RELEASE

if [ "$DEVEL" ]; then
    mv new_NEWS~ new_NEWS
    mv NEWS~ NEWS
else
    url=`svn info|grep URL:|cut -d" " -f2-`
    cat << EOF

All done, now run:

svn commit -m "Release $VERSION"
svn cp -m "Tag $VERSION" . ${url%/trunk}/release/$VERSION
scp release/$RELEASE.tar.gz itu:public_html/magicrescue/release/
EOF
fi
