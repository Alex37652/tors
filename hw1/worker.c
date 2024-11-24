#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <stdbool.h>

#define DISCOVER_PORT 8081
#define TASK_PORT 8082

struct Task {
    double start;
    double end;
    double step;
};

double computeFunction(double x) {
    return 3 * x * x;
}

double computeIntegral(const struct Task* task) {
    double result = 0.0;
    for (double x = task->start; x < task->end; x += task->step) {
        result += computeFunction(x) * task->step;
    }
    return result;
}

void* handleDiscovery(void* arg) {
    int discover_port = *(int*)arg;

    int discover_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (discover_sock < 0) {
        perror("Error creating socket");
        return NULL;
    }

    int reuse = 1;
    if (setsockopt(discover_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("SO_REUSEADDR setting error");
        close(discover_sock);
        return NULL;
    }

    struct sockaddr_in discover_addr;
    discover_addr.sin_family = AF_INET;
    discover_addr.sin_addr.s_addr = INADDR_ANY;
    discover_addr.sin_port = htons(discover_port);

    if (bind(discover_sock, (struct sockaddr*)&discover_addr, sizeof(discover_addr)) < 0) {
        perror("UDP socket bind error");
        close(discover_sock);
        return NULL;
    }

    char buffer[256];
    while (true) {
        struct sockaddr_in master_addr;
        socklen_t addr_len = sizeof(master_addr);
        printf("Wait message...\n");
        ssize_t n = recvfrom(discover_sock, buffer, sizeof(buffer) - 1, 0,
        (struct sockaddr*)&master_addr, &addr_len);
        if (n > 0) {
            buffer[n] = '\0';
            printf("Receive message: %s\n", buffer);
            if (strcmp(buffer, "DISCOVER") == 0) {
                const char* response = "READY";
                sendto(discover_sock, response, strlen(response), 0,
                (struct sockaddr*)&master_addr, addr_len);
            }
        }
    }
    
    close(discover_sock);
    return NULL;
}

void* handleTasks(void* arg) {
    int task_port = *(int*)arg;

    int task_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (task_sock < 0) {
        perror("Error creating socket");
        return NULL;
    }

    int reuse = 1;
    if (setsockopt(task_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("SO_REUSEADDR setting error");
        close(task_sock);
        return NULL;
    }

    struct sockaddr_in task_addr;
    task_addr.sin_family = AF_INET;
    task_addr.sin_addr.s_addr = INADDR_ANY;
    task_addr.sin_port = htons(task_port);

    if (bind(task_sock, (struct sockaddr*)&task_addr, sizeof(task_addr)) < 0) {
        perror("TCP socket bind error");
        close(task_sock);
        return NULL;
    }

    if (listen(task_sock, 5) < 0) {
        perror("listen error");
        close(task_sock);
        return NULL;
    }

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        int client_sock = accept(task_sock, (struct sockaddr*)&client_addr, &addr_len);
        if (client_sock < 0) {
            perror("accept error");
            continue;
        }

        struct Task task;
        ssize_t n = recv(client_sock, &task, sizeof(task), 0);
        
        if (n == sizeof(struct Task)) {
            printf("Start task: start=%lf, end=%lf, step=%lf\n", task.start, task.end, task.step);
            
            double result = computeIntegral(&task);
            
            send(client_sock, &result, sizeof(result), 0);
            printf("Send result\n");
        }
        
        close(client_sock);
    }
    
    close(task_sock);
    return NULL;
}

void runWorker(int discover_port, int task_port) {
    pthread_t discovery_thread, task_thread;
    
    pthread_create(&discovery_thread, NULL, handleDiscovery, &discover_port);
    pthread_create(&task_thread, NULL, handleTasks, &task_port);

    pthread_join(discovery_thread, NULL);
    pthread_join(task_thread, NULL);
}

int main() {

    printf("Start worker %d и %d\n", DISCOVER_PORT, TASK_PORT);
    runWorker(DISCOVER_PORT, TASK_PORT);
    return 0;
}