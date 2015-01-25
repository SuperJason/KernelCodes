#!/bin/sh

file_push()
{
    if [ -f "$1" ]; then
	echo "push $1"
	adb push $1 /data/ko/
    else
	echo "### "
	echo "### Error: Cannot find $1!"
	echo "### "
    fi
}

file_push hello.ko;
file_push ablock.ko;
file_push achar.ko;
file_push abus.ko;

