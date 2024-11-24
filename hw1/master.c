#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>

#define MAX_WORKERS 10
#define DISCOVER_PORT 8081
#define TASK_PORT 8082
#define TIMEOUT_SEC 2

typedef struct {
    double start;
    double end;
    double step;
    bool complete
} Task;

typedef struct {
    struct sockaddr_in addr;
    bool active;
} Worker;

typedef struct {
    Worker* worker;
    Task* task;
    double result;
    bool complete;
} TaskArgs;

typedef struct {
    Worker workers[MAX_WORKERS];
    int count;
    pthread_mutex_t mutex;
} WorkerList;

typedef struct {
    pthread_t thread;
    bool in_progress;
} Thread;

WorkerList workerList = {.count = 0, .mutex = PTHREAD_MUTEX_INITIALIZER};

void addWorker(WorkerList* list, const Worker* worker) {
    pthread_mutex_lock(&list->mutex);
    
    for (int i = 0; i < list->count; ++i) {
        if (list->workers[i].addr.sin_addr.s_addr == worker->addr.sin_addr.s_addr) {
            list->workers[i].active = true;
            pthread_mutex_unlock(&list->mutex);
            return;
        }
    }

    if (list->count < MAX_WORKERS) {
        list->workers[list->count] = *worker;
        list->count++;
        printf("Add worker: %s:%d\n", inet_ntoa(worker->addr.sin_addr), ntohs(worker->addr.sin_port));
    }

    pthread_mutex_unlock(&list->mutex);
}

void printActiveWorkers(const WorkerList* list) {
    pthread_mutex_lock(&list->mutex);
    printf("\n=== Active workers ===\n");
    for (int i = 0; i < list->count; ++i) {
        if (!list->workers[i].active) {
            continue;
        }
        printf("%d: %s:%d\n", i, inet_ntoa(list->workers[i].addr.sin_addr),
               ntohs(list->workers[i].addr.sin_port));
    }
    printf("=====================\n\n");
    pthread_mutex_unlock(&list->mutex);
}

void discoverWorkers() {
    printf("\nSearch for workers\n");

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    int broadcast = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        perror("SO_BROADCAST setting error");
        close(sock);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in broadcastAddr;
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(DISCOVER_PORT);
    broadcastAddr.sin_addr.s_addr = INADDR_BROADCAST;

    const char* msg = "DISCOVER";
    if (sendto(sock, msg, strlen(msg), 0, (struct sockaddr*)&broadcastAddr, sizeof(broadcastAddr)) < 0) {
        perror("Error sending broadcast");
        close(sock);
        exit(EXIT_FAILURE);
    }

    struct timeval tv = {TIMEOUT_SEC, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char buffer[256];
    struct sockaddr_in serverAddr;
    socklen_t addrLen = sizeof(serverAddr);

    while (1) {
        int n = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&serverAddr, &addrLen);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            perror("Response reception error");
            continue;
        }

        serverAddr.sin_port = htons(DISCOVER_PORT);
        Worker worker = {serverAddr, true};
        addWorker(&workerList, &worker);
    }

    close(sock);
    printActiveWorkers(&workerList);
}

void* processTask(void* arg) {

    TaskArgs* args = (TaskArgs*)arg;

    Worker* worker = args->worker;
    Task* task = args->task;

    printf("Send task %s (range: %lf - %lf, step: %lf)\n", inet_ntoa(worker->addr.sin_addr), task->start, task->end, task->step);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Error creating socket");
        worker->active = false;
        args->complete = false;
        return;
    }

    struct timeval tv = {TIMEOUT_SEC, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in taskAddr = worker->addr;
    taskAddr.sin_port = htons(TASK_PORT);

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    int conn_result = connect(sock, (struct sockaddr*)&taskAddr, sizeof(taskAddr));

    if (conn_result < 0 && errno != EINPROGRESS) {
        perror("Connection error");
        worker->active = false;
        close(sock);
        args->complete = false;
        return;
    }

    fd_set set;
    FD_ZERO(&set);
    FD_SET(sock, &set);

    conn_result = select(sock + 1, NULL, &set, NULL, &tv);

    if (conn_result <= 0) {
        perror("Connection error");
        worker->active = false;
        close(sock);
        args->complete = false;
        return;
    }

    int so_error;
    socklen_t len = sizeof(so_error);
    getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
    if (so_error != 0) {
        perror("Connection error");
        worker->active = false;
        close(sock);
        args->complete = false;
        return;
    }

    fcntl(sock, F_SETFL, flags);

    if (send(sock, task, sizeof(Task), 0) < 0) {
        perror("Sending error");
        worker->active = false;
        close(sock);
        args->complete = false;
        return;
    }

    double result;
    if (recv(sock, &result, sizeof(result), 0) < 0) {
        perror("Receive error");
        worker->active = false;
        close(sock);
        args->complete = false;
        return;
    }

    printf("Result from worker %s: %lf\n", inet_ntoa(worker->addr.sin_addr), result);

    close(sock);
    args->result = result;
    args->complete = true;
}

double distributeTasks(Task* tasks, size_t task_count) {
    double total_result = 0.0;
    size_t pending_count = task_count;
    TaskArgs* task_args = malloc(MAX_WORKERS * sizeof(TaskArgs));
    Thread* threads = malloc(MAX_WORKERS * sizeof(Thread));

    printf("\n Distribute %lu tasks \n", task_count);

    while (pending_count > 0) {
        for (int i = 0; i < workerList.count; ++i) {
            Worker* worker = &workerList.workers[i];
            if (!worker->active) continue;

            for (int j = 0; j < task_count; ++j) {
                if (!tasks[j].complete) {
                    task_args[i].worker = worker;
                    task_args[i].task =  &tasks[j];
                    task_args[i].result = 0.0;
                    task_args[i].complete = false;
                    threads[i].in_progress = true;

                    tasks[j].complete = true;
                    pthread_create(&threads[i].thread, NULL, processTask, &task_args[i]);
                    break;
                }
            }

        }

        for (int i = 0; i < workerList.count; ++i) {
            if (!threads[i].in_progress) {
                continue;
            }
            pthread_join(threads[i].thread, NULL);
            if (task_args[i].complete) {
                total_result += task_args[i].result;
                pending_count--;
                task_args[i].complete = false;
            } else {
                task_args[i].task->complete = false;
            }
            threads[i].in_progress = false;
        }

        if (pending_count > 0) {
            printf("Search for workers...\n");
            discoverWorkers();
        }
    }

    free(task_args);
    free(threads);

    return total_result;
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <start> <end> <step>\n", argv[0]);
        return 1;
    }

    double start = atof(argv[1]);
    double end = atof(argv[2]);
    double step = atof(argv[3]);

    size_t num_tasks = (size_t)((end - start) + 1.0);
    Task* tasks = malloc(num_tasks * sizeof(Task));

    for (size_t i = 0; i < num_tasks; ++i) {
        tasks[i].start = start + i;
        tasks[i].end = (start + i + 1.0 < end) ? start + i + 1.0 : end;
        tasks[i].step = step;
        tasks[i].complete = false;
    }

    discoverWorkers();

    double result = distributeTasks(tasks, num_tasks);

    printf("\nResult: %lf\n", result);

    free(tasks);
    return 0;
}