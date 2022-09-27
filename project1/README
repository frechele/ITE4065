# Project1 : Parallel Join Processing

In this project, we need to parallelize the joining algorithms to speed up the program's performance.
There are some main operations to optimize such as `Scan`, `FilterScan`, `Join`, `SelfJoin`, and `Checksum`.
Sections below describe how each algorithm was optimized.

## Thread Pool

Because my program needs frequent thread creation and deletion, I made `ThreadPool` class.
It provides two services: `Submit` and `ParallelFor`.
If one call the `Submit` service, a task is enqueued to the task queue of the thread pool.
The `ParallelFor` service split entire loop into some sub-blocks and execute them with many CPU-cores.
To prevent frequent locking, `ParallelFor` locks mutex once while enqueuing multiple splitted tasks.

There are two thread pools in my program. One is for join operation. The other is for query executing.
Details of the use of each thread pool are discussed below.

## Scan

There is no any loop, so we don't need parallelize the Scan operation.

## FilterScan

![selfjoin figure](resource/filterscan_selfjoin.png)

Like the figure above, I split the FilterScan operation two phases.

In the first phase, find rows that satisfy condition of filter.
The first phase was parallelized by making sub-result buffers for each thread.
If a thread finds a row that satisfies the condition, that row ID is appended to the sub-result buffer rather than the main result buffer.

In the second phase, merge sub-result buffers into the main result buffer.
Unfortunately, the second phase is not parallelizable.
Therefore, in the second phase, I don't use the thread pool.

## Join

Vanilla version of Join operation has three stages: processing input, build, and probe.

Parallelized processing input is not good, because thread can execute limited tasks simultaneously.
If `left->run()` and `right->run()` are executed at the same time,
limited thread pool resource must be used separately, it is not effective.
So I decide to execute processing input in single thread.

Building hash table phase is not parallelizable. So I decide to execute build phase in single thread too.

However, I can parallelize probe phase like FilterScan method.
In vanilla version, probe phase executes `equal_range` of the hash table, and then executes `copy2Result` method.
I split the probe phase into three sub-phases.
First sub-phase execute `equal_range` and calculate the number of matched rows.
First sub-phase can be parallelized.
Second sub-phase accumulate the match countes to get the start offset used in third sub-phase.
Parallelizing this sub-phase is impossible.
Third sub-phase build total result buffer from results of `equal_range` that built in first sub-phase.
Thanks to second sub-phase, I can parallelize this sub-phase.

The figure below shows how to my join algorithm works.

![join figure](resource/join.png)

## SelfJoin

![selfjoin figure](resource/filterscan_selfjoin.png)

Overall, it is similar to the `FilterScan` algorithm.
Like the figure above, I split the SelfJoin operation two phases.

In the first phase, find rows where `leftCol == rightCol`.
The first phase was parallelized by making sub-result buffers for each thread.
If a thread finds a row that satisfies the condition, that row ID is appended to the sub-result buffer rather than the main result buffer.

In the second phase, merge sub-result buffers into the main result buffer.
Unfortunately, the second phase is not parallelizable.
Therefore, in the second phase, I don't use the thread pool.

## Checksum

Parallelizing of the Checksum operation is not powerful in terms of execution time.
Presumably, caching has a greater effect than parallelism.

## Query Batching Parallelism

In this project, input queries can be splitted into batches.
Each query in the same batch can be executed simultaneously.
This parallelization is performed using a thread pool that is different from the algorithms listed above.
Query executing thread pool has one fewer worker than the number of CPU cores (one is for main thread).

Multiple queries in a batch are enqueued query executing thread pool's task queue.
Using future-promise pattern in C++ STL, future object for each query is stored to buffer ordered to original execution order.
At the same time, the main thread wait future object to print of query output.

Although both thread pools have more workers than CPU cores,
workers in query execution thread pool mostly wait and don't use CPU.
So, I think my two thread pool design is not bad.
Rather, I think it is a resonable design,
because it can indirectly parallelize the parts of not parallelized algorithms.
