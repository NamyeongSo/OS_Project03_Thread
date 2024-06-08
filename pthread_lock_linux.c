#include <stdio.h>
#include <pthread.h>

#define NUM_ITERS 10
#define NUM_THREADS 10

int shared_resource = 0;
volatile int number[NUM_THREADS];
volatile int choosing[NUM_THREADS];  //true면 1, false면 0

void lock(int tid);
void unlock(int tid);
int max(volatile int *array, int size);

void memory_barrier() {
    asm volatile("" ::: "memory");
}

void lock(int tid) {
    choosing[tid] = 1; // true
    memory_barrier();

    number[tid] = 1 + max(number, NUM_THREADS);
    memory_barrier();

    choosing[tid] = 0; // false
    memory_barrier();

    for (int j = 0; j < NUM_THREADS; j++) {
        while (choosing[j])
            ; // Busy wait
        while (number[j] != 0 && (number[j] < number[tid] || (number[j] == number[tid] && j < tid)))
            ; // Busy wait
    }
}

void unlock(int tid) {
    number[tid] = 0;
    memory_barrier();
}

int max(volatile int *array, int size) {
    int max_value = array[0];
    for (int i = 1; i < size; i++) {
        if (array[i] > max_value) {
            max_value = array[i];
        }
    }
    return max_value;
}


void* thread_func(void* arg) {
    int tid = *(int*)arg;
    
    lock(tid);
    
        for(int i = 0; i < NUM_ITERS; i++)    shared_resource++;
    
    unlock(tid);
    
    pthread_exit(NULL);
}

int main() {
    pthread_t threads[NUM_THREADS];
    int tids[NUM_THREADS];
    
    for (int i = 0; i < NUM_THREADS; i++) {
        tids[i] = i;
        pthread_create(&threads[i], NULL, thread_func, &tids[i]);
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("shared: %d\n", shared_resource);
    
    return 0;
}