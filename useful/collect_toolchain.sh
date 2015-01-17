#!/bin/bash
vv="ct_150414"
outdir="${PWD}"
collfunc()
{
echo "\"$1\" is a compiler(toolchain) directory. Collecting required files.."
tardir="${PWD##*/}"
cd ..
[ "${tardir}" = "usr" ] && tardir="${PWD##*/}" && cd ..
echo "$vv" > "$tardir/.collected_with"
date +%F_%H-%M-%S >> "$tardir/.collected_with"
tar -cjf "$outdir/compiler.tar.bz2" "$tardir"
rm -f "$tardir/.collected_with"
cd "$outdir"
md5sum "compiler.tar.bz2" > "compiler.md5"
echo "Created \"compiler.tar.bz2\" and \"compiler.md5\" in \"$outdir\""
}
if [ -n "$1" ] ; then
	if [ -d "$1" ] ; then
		cd -P "$1"
		if [ -n "`ls -A *\-gcc 2>/dev/null`" ]; then
			cd ..
			collfunc "$1"
		elif [ -n "`ls -A bin/*\-gcc 2>/dev/null`" ]; then
			collfunc "$1"
		elif [ -n "`ls -A usr/bin/*\-gcc 2>/dev/null`" ]; then
			collfunc "$1"
		else
			echo "Failed to locate required files in \"$1\"" >&2
		fi
	else
		echo "\"$1\" is not a directory." >&2
	fi
else
	echo "Usage: $0 <path to compiler (toolchain)>"
fi
