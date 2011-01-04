#!/bin/sh
ARCH=x86
SUBLIST=KDE



NODE=/nodes/$ARCH/$SUBLIST
if [ "$ARCH" = "x86" ] ; then
	REPO=file:///core32-mirror/repository/
else
	REPO=file:///core64-mirror/repository/
fi
LIST=${SUBLIST}.list

ARCH=$ARCH LIST=$LIST NODE=$NODE REPO=$REPO ./install_virtual_machine.sh

