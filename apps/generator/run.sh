CUR_DIR=$(readlink -e $(dirname $0))
cd $CUR_DIR

FIRMWARE=firmware.kelf

if [ $(find . -name "*.mpk" | wc -l) != "1" ]; then
	echo "ERROR: No or multiple mpk available in the directory"
fi
MPK=$(find . -name "*.mpk")
exec k1-jtag-runner --progress --multibinary=$MPK --exec-multibin=IODDR0:${FIRMWARE} --exec-multibin=IODDR1:${FIRMWARE} -- "$@"
