#!/bin/bash
ELF=$1
shift
extension="${ELF##*.}"

KTEST=$(readlink -e $0)
export TARGET_RUNNER=$KTEST

if [ "$extension" != "kelf" ]; then
	exec ${ELF} $*
fi

case "$RUN_TARGET" in
    "k1-jtag")
	BOARD_TYPE=$(cat /mppa/board0/type)
	FIRMWARES=""
	case "$BOARD_TYPE" in
	    "ab01")
		FIRMWARES="--exec-file IODDR0:${ODP_TOOLCHAIN_DIR}/share/odp/firmware/developer/k1b/iounified.kelf --exec-file IODDR1:${ODP_TOOLCHAIN_DIR}/share/odp/firmware/developer/k1b/iounified.kelf "
		;;
	    "konic80")
		FIRMWARES="--exec-file IODDR0:${ODP_TOOLCHAIN_DIR}/share/odp/firmware/konic80/k1b/iounified.kelf --exec-file IODDR1:${ODP_TOOLCHAIN_DIR}/share/odp/firmware/konic80/k1b/iounified.kelf "
		;;
	    "ab04")
		FIRMWARES="--exec-file IODDR0:${ODP_TOOLCHAIN_DIR}/share/odp/firmware/ab04/k1b/iounified.kelf --exec-file IODDR1:${ODP_TOOLCHAIN_DIR}/share/odp/firmware/ab04/k1b/iounified.kelf "
		;;
	    "emb01")
		TMP=$(mktemp -d -p $(pwd) remote-runer.XXXX)
		cp -R ${ODP_TOOLCHAIN_DIR}/share/odp/firmware/emb01/k1b/iounified.kelf $ELF ${TMP}
		cd ${TMP}
		echo k1-remote-runner -H -M  -u iounified.kelf,$(basename $ELF) -- \
		     k1-jtag-runner --exec-file IODDR0:iounified.kelf --exec-file Cluster0:$(basename $ELF) -- $*
		exec k1-remote-runner -H -M  -u iounified.kelf,$(basename $ELF) -- \
		     k1-jtag-runner --exec-file IODDR0:iounified.kelf --exec-file Cluster0:$(basename $ELF) -- $*
		;;
	    "explorer")
		FIRMWARES="--exec-file IOETH1:${ODP_TOOLCHAIN_DIR}/share/odp/firmware/explorer/k1b/ioeth.kelf"
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
	echo k1-cluster   --functional --dcache-no-check  --mboard=developer --march=bostan \
	     --user-syscall=${ODP_TOOLCHAIN_DIR}/lib64/libodp_syscall.so -- $ELF $*
	exec k1-cluster   --functional --dcache-no-check  --mboard=developer --march=bostan \
	     --user-syscall=${ODP_TOOLCHAIN_DIR}/lib64/libodp_syscall.so -- $ELF $*
	;;
    *)
	echo TARGET_RUNNER=$KTEST ${ELF} $*
	exec ${ELF} $*
esac

