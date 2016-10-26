#!/bin/bash -x

CUR_DIR=$(readlink -e $(dirname $0))
cd $CUR_DIR

#Start jtag runner in the BG
k1-jtag-runner --multibinary=pcie_fwd_jumbo_multibin.mpk --exec-multibin=IODDR0:firmware \
			   --exec-multibin=IODDR1:firmware --chip-freq=400 -- \
			   -c 0  &
sleep 4
sudo ifconfig modp0.0.0.0 mtu 9000
if [ $? -eq 0 ]; then
    kill %
    wait
    exit 1
fi

sudo ifconfig modp0.0.0.0 mtu 8000
sudo ifconfig modp0.0.1.0 mtu 8000
#11s for 10 pings (allow 1 drop)
ping6 -c 10 -w 11 -s 7000 -I modp0.0.0.0 fe80::de:adff:febe:ef80
res=$?

# Kill jtag and join it
kill %
wait

exit $res
