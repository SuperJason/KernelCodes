#!/bin/bash
vv="ck_150414"
outdir="${PWD}"
kbuild="kernelheaders_build"
ksource="kernelheaders_source"
usage()
{
echo "Usage: $0 [options] <path to kernel source or kernel build>"
echo ""
echo "Possible options:"
echo "   -c   Automatically collect compiler used for kernel build, if found"
echo "   -f   Collect full kernel directory instead of headers"
echo "   -o <path to output directory>   Changes the directory where the resulting archive will be placed"
echo ""
exit 0
}
findocmd()
{
if [ -f $tardir/fs/.inode.o.cmd ]; then
	addocmd="$tardir/fs/.inode.o.cmd"
else
	addocmd="`find $tardir -name '*\.o\.cmd' -type f | sed -n 1p`"
fi

# compdir="`head -1 $addocmd | grep -P -o '(?<=\-isystem ).*?(?= )'`"
compdir="`head -1 $addocmd | grep -E -o '\-isystem .*' | awk '{print $2}'`"
if [ -n "$compdir" ] && [ -d "$compdir" ]; then
	cd "$compdir"
	while [ ! -n "`ls -A bin/*\-gcc 2>/dev/null`" ]; do
		cd ..
		compdir="${PWD}"
		[ "$compdir" = "/" ] && break
	done
	if [ ! "$compdir" = "/" ]; then
		compfound="1"
		if [ -n "$colcomp" ]; then
			echo "Compiler used for kernel build was found in \"$compdir\" directory. Collecting required files.."
			echo "$vv" > ".collected_with"
			date +%F_%H-%M-%S >> ".collected_with"
			compdir="${compdir##*/}"
			cd ..
			[ "$compdir" = "usr" ] && compdir="${PWD##*/}" && cd ..
			tar -cjf $outdir/compiler.tar.bz2 $compdir
			rm -f "$compdir/.collected_with"
			cd $outdir
			md5sum compiler.tar.bz2 > compiler.md5
			echo "Created \"compiler.tar.bz2\" and \"compiler.md5\" in \"$outdir\""
		else
			echo "Compiler used for kernel build was found in \"$compdir\" directory. Make sure to use \"collect_toolchain.sh\" script on it."
		fi
	fi
fi
if [ -z "$compfound" ]; then
	echo "Failed to locate compiler used for kernel build. Please, use \"collect_toolchain.sh\" script on the correct directory or provide a compiler you are using manually." >&2
fi


if [ -n "$addocmd" ]; then
	addocmd=" $addocmd"
else
	addocmd=""
fi
cd "$kerneldir"
cd ..
}
if [ -n "$1" ]; then
	while getopts cfo: opt
	do
		case $opt in
			c)
				colcomp="1";;
			f)
				kbuild="kernel_build"
				ksource="kernel_source"
				full="1";;
			o)
				outdir="$OPTARG"
				if [ ! -d "$outdir" ]; then
					echo "$outdir is not a directory" >&2
					usage
				fi;;
			?)
				usage;;
		esac
	done
	shift $((OPTIND-1))
	if [ -d "$1" ]; then
		cd -P "$1"
		kerneldir="$PWD"
		if [ -d arch ] && [ -d include ] && [ -d scripts ] && [ -f Makefile ] && [ -f Module.symvers ] && [ -f .config ]; then
			tardir=${PWD##*/}
			echo "$vv" > ".collected_with"
			date +%F_%H-%M-%S >> ".collected_with"
			cd ..
			if [ -n "$full" ]; then
				findocmd "$1"
				echo "\"$1\" is a kernel build directory. Collecting required files.."
				tar -cjf $outdir/${kbuild}.tar.bz2 $tardir
			else
				findocmd "$1"
				echo "\"$1\" is a kernel build directory. Collecting required files.."
				longcalls="`find $tardir -type f -name '*\.o\.cmd' -exec head -1 -v '{}' \; | grep -B 1 '\-mlong\-calls' | sed -n 1p`"
				if [ -n "$longcalls" ]; then
					echo "!!! ADD -mlong-calls TO CFLAGS !!!" >> $tardir/MLONGCALLS
					longcalls=" $tardir/MLONGCALLS"
				else
					longcalls=""
				fi
				addko="`find $tardir -type f -name '*\.ko' | sed -n 1p`"
				if [ -n "$addko" ]; then
					modinfo $addko >> $tardir/MODINFO
					addko=" $tardir/MODINFO"
				else
					addko=""
				fi
				tar -cjf $outdir/${kbuild}.tar.bz2 $tardir/.collected_with $tardir/arch $tardir/include $tardir/scripts $tardir/Makefile $tardir/Module.symvers $tardir/.config$addocmd$addko$longcalls
				[ -f $tardir/MLONGCALLS ] && rm -f $tardir/MLONGCALLS
				[ -f $tardir/MODINFO ] && rm -f $tardir/MODINFO
			fi
			rm -f "$tardir/.collected_with"
			cd $outdir
			md5sum "${kbuild}.tar.bz2" > "${kbuild}.md5"
			echo "Created \"${kbuild}.tar.bz2\" and \"${kbuild}.md5\" in \"$outdir\""
			echo "Make sure you also use this script on kernel source directory."
		elif [ -d arch ] && [ -d include ] && [ -d scripts ] && [ -f Makefile ]; then
			echo "$vv" > ".collected_with"
			echo "\"$1\" is a kernel source directory. Collecting required files.."
			tardir=${PWD##*/}
			cd ..
			if [ -n "$full" ]; then
				tar -cjf $outdir/${ksource}.tar.bz2 $tardir
			else
				tar -cjf $outdir/${ksource}.tar.bz2 $tardir/.collected_with $tardir/arch $tardir/include $tardir/scripts $tardir/Makefile
			fi
			rm -f "$tardir/.collected_with"
			cd $outdir
			md5sum "${ksource}.tar.bz2" > "${ksource}.md5"
			echo "Created \"${ksource}.tar.bz2\" and \"${ksource}.md5\" in \"$outdir\""
			echo "Make sure you also use this script on kernel build (already compiled kernel) directory."
		else
			echo "Failed to locate required files in \"$1\"" >&2
		fi
	else
		echo "\"$1\" is not a directory." >&2
		usage
	fi
else
	usage
fi
