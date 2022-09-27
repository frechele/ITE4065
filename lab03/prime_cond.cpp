#include <stdio.h>
#include <pthread.h>
#include <math.h>

#define NUM_THREAD  10

int thread_ret[NUM_THREAD];

int range_start;
int range_end;

pthread_cond_t cond;
pthread_mutex_t mutex;

int canGo;
int runFlag;
int waitCount;
pthread_cond_t waitCond;

bool IsPrime(int n) {
    if (n < 2) {
        return false;
    }

    for (int i = 2; i <= sqrt(n); i++) {
        if (n % i == 0) {
            return false;
        }
    }
    return true;
}

void* ThreadFunc(void* arg) {
    long tid = (long)arg;

	pthread_mutex_lock(&mutex);
	--waitCount;

	if (waitCount == 0)
		pthread_cond_broadcast(&waitCond);
	pthread_mutex_unlock(&mutex);

	while (true) {
		pthread_mutex_lock(&mutex);
		while (!canGo)
			pthread_cond_wait(&cond, &mutex);
		const int shouldBreak = !runFlag;
		pthread_mutex_unlock(&mutex);

		if (shouldBreak)
			break;
		
		// Split range for this thread
		int start = range_start + ((range_end - range_start + 1) / NUM_THREAD) * tid;
		int end = range_start + ((range_end - range_start + 1) / NUM_THREAD) * (tid+1);
		if (tid == NUM_THREAD - 1) {
			end = range_end + 1;
		}
		
		long cnt_prime = 0;
		for (int i = start; i < end; i++) {
			if (IsPrime(i)) {
				cnt_prime++;
			}
		}

		thread_ret[tid] = cnt_prime;

		pthread_mutex_lock(&mutex);
		--waitCount;

		if (waitCount == 0)
		{
			pthread_cond_signal(&waitCond);
		}
		pthread_mutex_unlock(&mutex);
	}
        
    return NULL;
}

int main(void) {
    pthread_t threads[NUM_THREAD];

	pthread_cond_init(&cond, NULL);
	pthread_mutex_init(&mutex, NULL);

	pthread_cond_init(&waitCond, NULL);

	runFlag = 1;
	waitCount = NUM_THREAD;

	// Create threads to work
	for (long i = 0; i < NUM_THREAD; i++) {
		if (pthread_create(&threads[i], 0, ThreadFunc, (void*)i) < 0) {
			printf("pthread_create error!\n");
			return 0;
		}
	}

	canGo = 0;

	pthread_mutex_lock(&mutex);
	while (waitCount > 0)
		pthread_cond_wait(&waitCond, &mutex);
	pthread_mutex_unlock(&mutex);
    
    while (1) {
        // Input range
        scanf("%d", &range_start);
        if (range_start == -1) {
            break;
        }
        scanf("%d", &range_end); 

		pthread_mutex_lock(&mutex);
		waitCount = NUM_THREAD;
		canGo = 1;

		pthread_cond_broadcast(&cond);
		pthread_mutex_unlock(&mutex);

		pthread_mutex_lock(&mutex);
		while (waitCount > 0)
			pthread_cond_wait(&waitCond, &mutex);
		canGo = 0;
		pthread_mutex_unlock(&mutex);

        // Collect results
        int cnt_prime = 0;
        for (int i = 0; i < NUM_THREAD; i++) {
            cnt_prime += thread_ret[i];
        }
        printf("number of prime: %d\n", cnt_prime);
    }

	pthread_mutex_lock(&mutex);
	runFlag = 0;
	canGo = 1;
	pthread_cond_broadcast(&cond);
	pthread_mutex_unlock(&mutex);

	// Wait threads end
	for (int i = 0; i < NUM_THREAD; i++) {
		pthread_join(threads[i], NULL);
	}

	pthread_cond_destroy(&cond);
	pthread_cond_destroy(&waitCond);

	pthread_mutex_destroy(&mutex);

    return 0;
}

