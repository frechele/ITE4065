#! /bin/bash

for gc_interval in 10 20 50 100 500 1000
do
	for ratio in 0.2 0.5 0.8 1.0
	do
		echo "Now running (${gc_interval} - ${ratio})..."
		./batched_run.sh ${gc_interval} ${ratio} > "INT_${gc_interval}_${ratio}"
	done
done
