#!/bin/bash -x

CUR_DIR=$(readlink -e $(dirname $0))
cd $CUR_DIR

#Start jtag runner in the BG
k1-jtag-runner --multibinary=pcie_fwd_multiq_multibin.mpk --exec-multibin=IODDR0:firmware.kelf \
			   --exec-multibin=IODDR1:firmware.kelf --chip-freq=400 -- > /dev/null &
sleep 20

(sudo ping6 -I modp0.0.0.0 -f fe80::de:adff:febe:ef80 -c 8192 -w 10 > /dev/null) && \
    ping6 -c 10 -w 11 -I modp0.0.0.0 fe80::de:adff:febe:ef80
res=$?

# Kill jtag and join it
kill %
wait

exit $res

