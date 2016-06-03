CUR_DIR=$(readlink -e $(dirname $0))
cd $CUR_DIR

FIRMWARE=firmware.kelf

exec k1-jtag-runner --progress --multibinary=l2fwd-pcie.mpk --exec-multibin=IODDR0:${FIRMWARE} --exec-multibin=IODDR1:${FIRMWARE} -- "$@"
