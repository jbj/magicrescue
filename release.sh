#!/bin/bash

VERSION="$1"
APP=magicrescue
RELEASE=$APP-$VERSION

mv NEWS old_NEWS

echo -n "Version $VERSION: " > NEWS
date         >> NEWS
cat new_NEWS >> NEWS
echo         >> NEWS
cat old_NEWS >> NEWS

rm -f old_NEWS
echo > new_NEWS

echo "For less detailed change information, see the NEWS file" > ChangeLog
echo               >> ChangeLog
svn log -vr HEAD:1 >> ChangeLog

ln -s . $RELEASE

(echo ChangeLog; svn ls -R)|sed "s%^%$RELEASE/%"|\
    grep -v new_NEWS|grep -v '/$'|\
    tar -T- -czvf release/$RELEASE.tar.gz

rm -f ChangeLog $RELEASE
