#!/usr/bin/python

import argparse
import os
import re
import signal
import subprocess
import time


packet_sizes = [64, 64, 128, 256, 512, 1024, 1280, 1500]

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

jtag_cmd = [
	"k1-jtag-runner",
	"--multibinary=./pcie_host_to_cluster_perf_multibin.mpk",
	"--exec-multibin=IODDR0:iopcie",
	"--exec-multibin=IODDR1:iopcie",
	"--",
	"-c",
	"pcie_host_to_cluster_perf",
	"-a",
	"-i p0p0,drop"
]

print "#",' '.join(jtag_cmd)

try:
	jtag_p = subprocess.Popen(jtag_cmd, stdout = subprocess.PIPE)

	for line in iter(jtag_p.stdout.readline, b""):
		o = "# " + line
		print o,
		if "[13] srcif" in line:
			os.kill(jtag_p.pid, signal.SIGSTOP)
			break
	time.sleep(1)
	first = 1
	for size in packet_sizes:
		trafgen_cmd = [
			"./pktgen_sample01_simple.sh", "-i", ifce, "-s", str(size), "-d",
			"127.0.0.9", "-m", "02:de:ad:be:ef:01", "-t", "1", "-c", "1" ]
		print "#",' '.join(trafgen_cmd)
		trafgen_p = subprocess.Popen(trafgen_cmd, stdout = subprocess.PIPE, stderr = subprocess.STDOUT)
		test_name = "odp_pcie_host_to_cluster:" + os.environ['OS_PLAT_NAME'] +":pktsize:" + str(size) + " "
		try:
			for line in iter(trafgen_p.stdout.readline, b""):
				o = "# " + line
				print o,
				if "errors:" in line:
					match = re.search("(\d+)pps (\d+)Mb/sec \((\d+)bps\) errors: (\d+)", line)
					if match is not None and first != 1:
						perff.write("PPS " + test_name + match.group(1) + "\n")
						perff.write("MBPS " + test_name + match.group(2) + "\n")
					try:
				 		trafgen_p.terminate()
						trafgen_p.wait()
					except OSError:
				 		pass
		except KeyboardInterrupt:
			trafgen_p.kill()
			trafgen_p.wait()
			pass
		first = 0

	os.kill(jtag_p.pid, signal.SIGCONT)

	jtag_p.terminate()
	jtag_p.wait()
except KeyboardInterrupt:
	try:
		jtag_p.kill()
		jtag_p.wait()
	except OSError:
		pass

