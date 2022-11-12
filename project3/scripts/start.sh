#! /bin/bash

#SBATCH --nodes=1
#SBATCH --job-name="BWTREE"
#SBATCH --output="OUTPUT.out"
#SBATCH --error="ERROR.out"

./run_tests.sh
