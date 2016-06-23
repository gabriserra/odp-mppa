CUR_DIR=$(readlink -e $(dirname $0))
cd $CUR_DIR

FIRMWARE=firmware.kelf

exec k1-jtag-runner --chip-freq=600 --progress --multibinary=l2fwd-auto.mpk --exec-multibin=IODDR0:${FIRMWARE} --exec-multibin=IODDR1:${FIRMWARE} -- "$@"
