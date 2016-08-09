#!/bin/bash -x

CUR_DIR=$(readlink -e $(dirname $0))
cd $CUR_DIR

#Start jtag runner in the BG
res=1

for i in $(seq 1 5); do
    k1-jtag-runner --multibinary=pcie_fwd_multibin.mpk --exec-multibin=IODDR0:iopcie \
		   --exec-multibin=IODDR1:iopcie --chip-freq=400 -- -cpcie_fwd \
		   -a "-i p0p0:tags=60,p1p0:tags=60" -a "-m 0" -a "-s 0" -a "-c 8" &
    sleep 5

    #11s for 10 pings (allow 1 drop)
    ping6 -c 10 -w 11 -I modp0.0.0.0 fe80::de:adff:febe:ef80
    res=$?

    # Kill jtag and join it
    kill %
    wait

    if [ $res -eq 0 ]; then break; fi
done


exit $res
