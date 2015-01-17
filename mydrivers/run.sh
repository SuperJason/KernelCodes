#!/bin/sh

file_push()
{
    if [ -f "$1" ]; then
	echo "push $1"
	adb push $1 /data
    else
	echo "### "
	echo "### Error: Cannot find $1!"
	echo "### "
    fi
}

#TARGET=hello.ko
file_push hello.ko;

#TARGET=ablock.ko
file_push ablock.ko;

#TARGET=script.sh
file_push script.sh;

