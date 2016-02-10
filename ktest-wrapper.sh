#!/bin/bash
ELF=$1
shift
extension="${ELF##*.}"
if [ "$extension" != "kelf" ]; then
	exec ${ELF} $*
fi

case "$RUN_TARGET" in
	"k1-jtag")
		BOARD_TYPE=$(cat /mppa/board0/type)
		FIRMWARES=""
		case "$BOARD_TYPE" in
			"ab01")
				FIRMWARES="--exec-file IODDR0:${TOP_BUILDDIR}/../../firmware/iounified/k1b-kalray-iounified/iounified.kelf --exec-file IODDR1:${TOP_BUILDDIR}/../../firmware/iounified/k1b-kalray-iounified/iounified.kelf "
				;;
			"konic80")
				FIRMWARES="--exec-file IODDR0:${TOP_BUILDDIR}/../../firmware/iounified/k1b-kalray-iounified_konic80/iounified.kelf --exec-file IODDR1:${TOP_BUILDDIR}/../../firmware/iounified/k1b-kalray-iounified_konic80/iounified.kelf "
				;;
			"explorer")
				FIRMWARES="--exec-file IOETH1:${TOP_BUILDDIR}/../../firmware/ioeth/k1b-kalray-ioeth530/ioeth.kelf"
				;;
			"")
				;;
			*)
				;;
		esac

		echo k1-jtag-runner ${FIRMWARES} --exec-file "Cluster0:$ELF" -- $*
		exec k1-jtag-runner ${FIRMWARES} --exec-file "Cluster0:$ELF" -- $*
		;;
	"k1-cluster")
		echo k1-cluster   --functional --dcache-no-check  --mboard=developer --march=bostan --user-syscall=${TOP_SRCDIR}/syscall/build_x86_64/libodp_syscall.so -- $ELF $*
		exec k1-cluster   --functional --dcache-no-check  --mboard=developer --march=bostan  --user-syscall=${TOP_SRCDIR}/syscall/build_x86_64/libodp_syscall.so -- $ELF $*
		;;
	*)
		KTEST=$(readlink -e $0)
		export TARGET_RUNNER=$KTEST
		echo TARGET_RUNNER=$KTEST ${ELF} $*
		exec ${ELF} $*
esac

