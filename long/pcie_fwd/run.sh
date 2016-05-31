#!/bin/bash -x

CUR_DIR=$(readlink -e $(dirname $0))
cd $CUR_DIR

#Save initial freq
INIT_FREQ=$(cat /mppa/board0/mppa0/chip_freq)

#Set it at 400 for the test
echo 400 > /mppa/board0/mppa0/chip_freq
echo 1 > /mppa/board0/mppa0/reset

#Start jtag runner in the BG
k1-jtag-runner --multibinary=pcie_fwd_multibin.mpk --exec-multibin=IODDR0:iopcie --exec-multibin=IODDR1:iopcie -- -cpcie_fwd -a "-i p0p0:tags=60,p1p0:tags=60" -a "-m 0" -a "-s 0" -a "-c 10" &
sleep 5

#11s for 10 pings (allow 1 drop)
ping6 -c 10 -w 11 -I modp0.0.0.0 fe80::de:adff:febe:ef80
res=$?

# Kill jtag and join it
kill %
wait

#Restore frequency
echo 600 > /mppa/board0/mppa0/chip_freq
echo 1 > /mppa/board0/mppa0/reset

exit $res
