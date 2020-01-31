#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test latency spikes caused by FIN/ACK handling race.

set +x
set -e

tmpfile=$(mktemp /tmp/fin_ack_latency.XXXX.log)

kill_accept() {
	kill $ACCEPT_PID
}

cleanup() {
	kill_accept
	rm -f $tmpfile
}

trap cleanup EXIT

do_test() {
	RUNTIME=$1

	./fin_ack_lat_accept &
	ACCEPT_PID=$!
	sleep 1

	./fin_ack_lat_connect | tee $tmpfile &
	sleep $RUNTIME
	NR_SPIKES=$(wc -l $tmpfile | awk '{print $1}')
	rm $tmpfile
	if [ $NR_SPIKES -gt 0 ]
	then
		echo "FAIL: $NR_SPIKES spikes detected"
		return 1
	fi
	return 0
}

do_test "30"
echo "test done"
