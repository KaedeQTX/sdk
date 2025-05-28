#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <arpa/inet.h>
#include <time.h>

#define UDP_SIZE 65536
#define MAX_SYMBOLS 100
#define MAX_SYMBOL_LEN 64
#define SYMBOL_MANAGER "172.30.2.221"
#define MY_PORT 8088

typedef struct
{
    char symbol[MAX_SYMBOL_LEN];
    unsigned int index;
} Subscription;

typedef struct
{
    int socket;
    char buf[UDP_SIZE];
    Subscription subscriptions[MAX_SYMBOLS];
    int subscription_count;
} SubscriptionManager;

SubscriptionManager manager;
volatile sig_atomic_t running = 1;

int init_subscription_manager()
{
    // 创建 UDP socket
    manager.socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (manager.socket < 0)
    {
        perror("socket creation failed");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(MY_PORT);

    if (bind(manager.socket, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind failed");
        close(manager.socket);
        return -1;
    }

    manager.subscription_count = 0;
    return 0;
}

int subscribe(const char *symbol)
{
    printf("Subscribing to symbol: %s\n", symbol);

    // 发送订阅请求
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    inet_pton(AF_INET, SYMBOL_MANAGER, &server_addr.sin_addr);

    if (sendto(manager.socket, symbol, strlen(symbol), 0,
               (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("sendto failed");
        return -1;
    }

    return 0;
}

int add_subscripton(int len) {
    manager.buf[len] = '\0';
    unsigned int index;
    char subscripted_symbol[MAX_SYMBOL_LEN] = {0};
    if (sscanf(manager.buf, "%u:%63s", &index, subscripted_symbol) == 2)
    {
        // 检查是否已经订阅
        for (int i = 0; i < manager.subscription_count; i++)
        {
            if (strcmp(manager.subscriptions[i].symbol, subscripted_symbol) == 0)
            {
                return 0; // 已经订阅过了
            }
        }

        // 添加新订阅
        if (manager.subscription_count < MAX_SYMBOLS)
        {
            strncpy(manager.subscriptions[manager.subscription_count].symbol,
                    subscripted_symbol, MAX_SYMBOL_LEN - 1);
            manager.subscriptions[manager.subscription_count].symbol[MAX_SYMBOL_LEN-1] = '\0';
            manager.subscriptions[manager.subscription_count].index = index;
            manager.subscription_count++;
            printf("Successfully subscribed to %s with index %u\n", subscripted_symbol, index);
        }
    }

    return 0;
}

int unsubscribe(const char *symbol)
{
    char unsubscribe_msg[MAX_SYMBOL_LEN + 1];
    snprintf(unsubscribe_msg, sizeof(unsubscribe_msg), "-%s", symbol);
    printf("Unsubscribing from symbol: %s\n", symbol);

    // 查找订阅
    int pos = -1;
    for (int i = 0; i < manager.subscription_count; i++)
    {
        if (strcmp(manager.subscriptions[i].symbol, symbol) == 0)
        {
            pos = i;
            break;
        }
    }

    if (pos >= 0)
    {
        // 发送取消订阅请求
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(8080);
        inet_pton(AF_INET, SYMBOL_MANAGER, &server_addr.sin_addr);

        if (sendto(manager.socket, unsubscribe_msg, strlen(unsubscribe_msg), 0,
                   (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        {
            perror("sendto failed");
            return -1;
        }

        // // 接收响应
        // struct sockaddr_in from_addr;
        // socklen_t from_len = sizeof(from_addr);
        // int len = recvfrom(manager.socket, manager.buf, UDP_SIZE, 0,
        //                    (struct sockaddr *)&from_addr, &from_len);
        // if (len < 0)
        // {
        //     perror("recvfrom failed");
        //     return -1;
        // }

        // manager.buf[len] = '\0';
        // printf("Unsubscribe response for %s: %s\n", symbol, manager.buf);

        // 移除订阅
        for (int i = pos; i < manager.subscription_count - 1; i++)
        {
            manager.subscriptions[i] = manager.subscriptions[i + 1];
        }
        manager.subscription_count--;
    }
    else
    {
        printf("Symbol %s not found in subscriptions\n", symbol);
    }

    return 0;
}

// 取消所有订阅
int unsubscribe_all()
{
    int success = 1;
    // 保存当前订阅数量，因为 unsubscribe 会修改 subscription_count
    size_t count = manager.subscription_count;

    // 从后向前遍历以避免数组重排的影响
    for (int i = count - 1; i >= 0; i--)
    {
        if (unsubscribe(manager.subscriptions[i].symbol) != 0)
        {
            success = 0;
        }
    }

    return success ? 0 : -1;
}

void print_status()
{
    printf("=== Current Status ===\n");
    printf("Total symbols: %d\n", manager.subscription_count);
    for (int i = 0; i < manager.subscription_count; i++)
    {
        printf("Symbol: %s (index: %u)\n",
               manager.subscriptions[i].symbol,
               manager.subscriptions[i].index);
    }
    printf("==================\n");
}

void handle_signal(int sig)
{
    unsubscribe_all();
    running = 0;
}

