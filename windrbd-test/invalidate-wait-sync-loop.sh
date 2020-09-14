#!/bin/bash

RES=${RES:-vamp-2nodes}
i=0
while true
do
	i=$[ $i+1 ]
	echo Invalidate $i
	drbdadm invalidate $RES
	drbdadm status $RES
	drbdadm wait-sync $RES
	drbdadm status $RES
	sleep 1
done
