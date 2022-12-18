# Project4 - implement Border-Collie algorithm

In this project, our goal is implementation of the Border-Collie algorithm (Border Collie: a wait-free, read-optimal Algorithm for Database Logging on Multicore Hardware) at PostgreSQL.

## 1. Overall design

Actually, PostgreSQL has already implemented a WAL (Write Ahead Logging) algorithm that is similar to the Border-Collie algorithm.
Almost of variables that are needed to implement the Border-Collie manner are already declared.
Hence, it is only necessary to make PostgreSQL WAL writing policy more efficient.
Following subsections, I will describe specific information about my implementation.

### 1-1) Border-Collie API

In the Border-Collie algorithm, there are two status for each transaction execution workers (hereafter referred to as workers): color and timestamp.
So I defined `Flag` structure and `BorderCollieFlagColor` enumeration as follows.

```c++
typedef struct 
{
    XLogRecPtr timestamp;
    unsigned char color;
} Flag;

typedef enum
{
    BLACK,  // running 
    WHITE   // complete 
} BorderCollieFlagColor;
```

For workers, one function API that informs flag of worker to the Border-Collie thread is provided.
Function prototype is described below.

```c++
void SetBorderCollieFlag(int id, XLogRecPtr lsn, unsigned char color);
```

### 1-2) Worker process

It seems that `XLogInsertRecord` function is called for each WAL log.
In that function, LSN is calculated using latch protection, and copy the log record to the buffer.
Therefore, I think that is is needed to modify `XLogInsertRecord` function to implement the Border-Collie algorithm.

To call the `SetBorderCollieFlag` function, we need three information: worker id, LSN and LSN + logsize.
worker id is provided as `MyProc->pgprocno`, LSN is provided as `StartPos`, and LSN + logsize is provided as `EndPos`.

### 1-3) Border-Collie process

The Border-Collie process performs checking every second.
Firstly, it calculates minimum recoverable logging bound (RLB) according to the Border-Collie algorithm.
And then, execute log flushing to the RLB. This can be performed with `XLogFlush` function.

## 2. Performance

All experiments are conducted under the environment as follows:

- OS: Ubuntu Server 20.04 LTS
- CPU: Intel(R) Core(TM) i9-10940X CPU (3.30GHz)
  - hyperthreading enabled (14 cores, 28 threads)
- RAM: DDR4 128GB
- g++ 9.4.0
- Docker: 20.10.18

For each test, time limit of sysbench is 60 seconds, and thread counts are 1, 4, 8, and 12.
Throughput results are as follows.

#### Original version

- 1 Thread: 7487.93 (eps)
- 4 Threads: 28344.88 (eps)
- 8 Threads: 54963.11 (eps)
- 12 Threads: 77086.79 (eps)

#### My implementation

- 1 Thread: 7478.67 (eps) [-0.12%]
- 4 Threads: 28529.82 (eps) [+0.65%]
- 8 Threads: 55084.55 (eps) [+0.22%]
- 12 Threads: 78531.76 (eps) [+1.87%]

## 3. Non-trivial problems

### 3-1) Environment setup

Actually, I have already been using PostgreSQL on my server machine.
Hence, I need a way to protect my running database from experiments of this project.

To use docker, I solve this problem. Docker is virtual container system. It provides virtual machine that not affect the current system. My Dockerfile is as follows:

```Dockerfile
FROM ubuntu:20.04

SHELL ["/bin/bash", "-c"]

ARG USER=postgres
ARG GROUP=postgres
ARG UID=1000
ARG GID=1000

RUN groupadd -g ${GID} ${GROUP}
RUN adduser --home /home/${USER} --uid ${UID} --gid ${GID} ${USER}

RUN apt-get update && apt-get upgrade -y

RUN DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC \
		apt-get install -y tzdata

RUN apt-get install -y \
		build-essential \
		libreadline-dev \
		zlib1g-dev \
		flex \
		bison \
		libxml2-dev \
		libxslt-dev \
		libssl-dev \
		libxml2-utils \
		xsltproc \
		ccache \
		vim

RUN apt-get install -y \
		make \
		automake \
		libtool \
		pkg-config \
		libaio-dev \
		libpq-dev

RUN apt-get install -y \
		locales

RUN localedef -i en_US -f UTF-8 en_US.UTF-8

USER ${USER}

COPY --chown=${USER} . /app

WORKDIR /app
```

### 3-2) WALwriter in the origial PostgreSQL

In the original PostgreSQL, WALwriter process performs works similar to the Border-Collie process.
Therefore, the same operation may be repeated more than twice.
In my final version, the operation of WALwriter is disabled.

Here's an ablation study result about deactiation of WALwriter.

#### With deactivation

- 1 Thread: 7478.67 (eps)
- 4 Threads: 28529.82 (eps)
- 8 Threads: 55084.55 (eps)
- 12 Threads: 78531.76 (eps)

#### Without deactivation

- 1 Thread: 7504.20 (eps) [+0.34%]
- 4 Threads: 28002.78 (eps) [-1.85%]
- 8 Threads: 54307.78 (eps) [-1.41%]
- 12 Threads: 77530.67 (eps) [-1.27%]

There is considerable performance drop. Therefore, I decide to adapt WALwriter deactivation.

## 4. Discussion

Although I implemented a fancy algorithm, there is no big performance improvement.
The original Border-Collie algorithm is wait-free manner by using atomic fetch-add instruction to manage LSN.
However, PostgreSQL uses locking mechanism for managing LSN.
Because I didn't remove this, my implementation as some gap compared to the original algorithm.
If locking is eliminated, I expect that there will be a huge performance up.
