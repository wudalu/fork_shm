#!/bin/bash
for i in `seq 10`
do
	echo $i
	telnet localhost 8080 &
	sleep 3
done
