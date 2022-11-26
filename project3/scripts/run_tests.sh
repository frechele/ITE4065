#! /bin/bash

for datasize in 10
do
	for write_skew in 0 1
	do
		for read_skew in 0 1
		do
			for gc_interval in 0
			do
				for ratio in 0.2 0.5 0.8 1.0
				do
					echo "Now running (${gc_interval}  ${ratio} ${datasize} ${write_skew} ${read_skew})..."
					./batched_run.sh ${gc_interval} ${ratio} ${datasize} ${write_skew} ${read_skew} > "INT_${gc_interval}_${ratio}"
				done
			done

			python3 summary.py
			dir_name="${datasize}M_${write_skew}${read_skew}"

			mkdir ${dir_name}
			mv INT_* ${dir_name}
			mv result.pkl ${dir_name}
		done
	done
done

echo "DONE"

