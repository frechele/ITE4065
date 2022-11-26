TRY_COUNT=10

for i in `seq 1 ${TRY_COUNT}`
do
	./bwtreeplus $1 $2 $3 $4 $5
	echo "=============================="
done

