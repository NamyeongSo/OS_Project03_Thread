#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#define NPROCS 4

// 공유 데이터 구조
struct shared_data {
    int results[NPROCS];
    int ready[NPROCS];
};

int main(void) {
    int pid, i, fd;
    struct shared_data *data;
    char *filename = "shared_data.dat";

    // 공유 파일 생성 및 초기화
    fd = open(filename, O_CREATE | O_RDWR);
    if (fd < 0) {
        printf(1, "Failed to create shared file\n");
        exit();
    }

    data = malloc(sizeof(struct shared_data));
    memset(data, 0, sizeof(struct shared_data));
    write(fd, data, sizeof(struct shared_data));
    close(fd);

    for (i = 0; i < NPROCS; i++) {
        pid = fork();
        if (pid < 0) {
            printf(1, "Fork failed\n");
            exit();
        }

        if (pid == 0) {  // 자식 프로세스
            fd = open(filename, O_RDWR);
            read(fd, data, sizeof(struct shared_data));

            int base = (getpid() % 5) + 1;
            data->results[i] = base * base;
            data->ready[i] = 1;

            write(fd, data, sizeof(struct shared_data));
            close(fd);
            exit();
        }
    }

    // 부모 프로세스가 모든 자식 프로세스의 준비 완료를 기다림
    fd = open(filename, O_RDONLY);
    do {
        read(fd, data, sizeof(struct shared_data));
        close(fd);

        fd = open(filename, O_RDONLY);
        int all_ready = 1;
        for (i = 0; i < NPROCS; i++) {
            if (data->ready[i] == 0) {
                all_ready = 0;
                break;
            }
        }

        if (all_ready) {
            break;
        }
    } while (1);
    close(fd);

    // 결과 출력
    int total = 0;
    for (i = 0; i < NPROCS; i++) {
        total += data->results[i];
        printf(1, "Result from child %d: %d\n", i, data->results[i]);
    }
    printf(1, "Total result: %d\n", total);

    while (wait() > 0);
    free(data);
    unlink(filename);
    exit();
}
