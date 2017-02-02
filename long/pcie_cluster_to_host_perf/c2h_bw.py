#!/usr/bin/python

import argparse
import os
import re
import signal
import subprocess
import time


packet_sizes = [64, 128, 256, 512, 1024, 1280, 1500]

def pps_to_gbps(pps, bytes_per_pkt):
	bits_per_pkt = bytes_per_pkt * 8
	return (pps * bits_per_pkt) / 1000000000


parser = argparse.ArgumentParser(prefix_chars = '-')
parser.add_argument('--perf-file', required = True)
perf_file_name = parser.parse_args().perf_file

dir = os.path.dirname(perf_file_name)
if not os.path.exists(dir):
	os.makedirs(dir)
perff = open(perf_file_name, 'a')

ifce = "modp0.0.0.0"


for size in packet_sizes:

	test_name = "odp_pcie_cluster_to_host:" + os.environ['OS_PLAT_NAME'] +":pktsize:" + str(size) + " "
	jtag_cmd = [
		"k1-jtag-runner",
		"--multibinary=./pcie_cluster_to_host_perf_multibin.mpk",
		"--exec-multibin=IODDR0:iopcie",
		"--", "-c", "pcie_cluster_to_host_perf",
		"-a",
		"-I p0p0:nofree --srcmac fe:0f:97:c9:e0:44 --dstmac 02:de:ad:be:ef:00 --srcip 192.168.0.1 --dstip 192.168.0.2 -P " + str(size) +" -i 0"
	]
	print "#",' '.join(jtag_cmd)
	jtag_p = subprocess.Popen(jtag_cmd, stdout = subprocess.PIPE)

	try:

		for line in iter(jtag_p.stdout.readline, b""):
			o = "# " + line
			print o,
			if "[13] created mode: SEND" in line:
				break
		time.sleep(2)
		packets_src_str = os.popen("cat /proc/net/dev | grep " + ifce +" | awk '{ print $3}'").read()
		time.sleep(5)
		packets_dst_str = os.popen("cat /proc/net/dev | grep " + ifce +" | awk '{ print $3}'").read()
		packets_src = int(packets_src_str)
		packets_dst = int(packets_dst_str)
		pps = (packets_dst - packets_src) / 5
		bw_mbps = (pps * size * 8) / (1024 *1024)
		perff.write("PPS " + test_name + str(pps) + "\n")
		perff.write("MBPS " + test_name + str(bw_mbps) + "\n")

                print "Start:" + packets_src_str + " End:" + packets_dst_str + " PPS:" + \
                        str(pps) + " MBPS:" + str(bw_mbps)
		os.kill(jtag_p.pid, signal.SIGCONT)

		jtag_p.kill()
		jtag_p.wait()
	except:
		try:
			jtag_p.kill()
			jtag_p.wait()
			exit(1)
		except OSError:
			exit(1)

