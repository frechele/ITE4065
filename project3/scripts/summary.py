import glob
import numpy as np
import pickle

file_list = glob.glob("INT_*")
print(file_list)
print("file count:", len(file_list))

keys = []
values = []

for filename in file_list:
    with open(filename, "rt") as f:
        results = f.read()

    blocks = results.split("="*30)[:-1]

    succ_write_throughputs = []
    read_throughputs = []
    garbage_lengths = []
    for block in blocks:
        block = block.strip().splitlines()

        garbage_length = float(block[-1].split()[-1])
        read_throughput = float(block[-2].split()[2])
        succ_write_throughput = float(block[-3].split()[3])
        
        succ_write_throughputs.append(succ_write_throughput)
        read_throughputs.append(read_throughput)
        garbage_lengths.append(garbage_length)

    succ_write_throughputs = np.array(succ_write_throughputs)
    read_throughputs = np.array(read_throughputs)
    garbage_lengths = np.array(garbage_lengths)

    gc_interval = int(filename.split("_")[1])
    writer_ratio = float(filename.split("_")[2])

    keys.append((gc_interval, writer_ratio))
    values.append([succ_write_throughputs, read_throughputs, garbage_lengths])

with open("result.pkl", "wb") as f:
    pickle.dump([keys, values], f)

