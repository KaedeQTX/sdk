#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <time.h>

#define UDP_SIZE 65536
#define MAX_SYMBOLS 100
#define MAX_SYMBOL_LEN 64

typedef struct
{
    int msg_type;
    unsigned int index;
    long long tx_ms;
    long long event_ms;
    long long local_ns;
    long long sn_id;
    double price;
    double size;
} Msg;

typedef struct
{
    int msg_type;
    unsigned int index;
    long long tx_ms;
    long long event_ms;
    long long local_ns;
    long long sn_id;
    size_t asks_len;
    size_t bids_len;
} Msg2;

typedef struct
{
    double price;
    double size;
} Msg2Level;

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

volatile sig_atomic_t running = 1;

void handle_signal(int sig)
{
    running = 0;
}

long long get_current_nanos()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

int init_subscription_manager(SubscriptionManager *manager)
{
    // 创建 UDP socket
    manager->socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (manager->socket < 0)
    {
        perror("socket creation failed");
        return -1;
    }

    // 绑定到任意端口
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0; // 让系统自动分配端口

    if (bind(manager->socket, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind failed");
        close(manager->socket);
        return -1;
    }

    manager->subscription_count = 0;
    return 0;
}

int subscribe(SubscriptionManager *manager, const char *symbol)
{
    printf("Subscribing to symbol: %s\n", symbol);

    // 发送订阅请求
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    inet_pton(AF_INET, "172.30.2.221", &server_addr.sin_addr);

    if (sendto(manager->socket, symbol, strlen(symbol), 0,
               (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("sendto failed");
        return -1;
    }

    // 接收响应
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    int len = recvfrom(manager->socket, manager->buf, UDP_SIZE, 0,
                       (struct sockaddr *)&from_addr, &from_len);
    if (len < 0)
    {
        perror("recvfrom failed");
        return -1;
    }

    manager->buf[len] = '\0';
    unsigned int index;
    if (sscanf(manager->buf, "%u", &index) == 1)
    {
        // 检查是否已经订阅
        for (int i = 0; i < manager->subscription_count; i++)
        {
            if (strcmp(manager->subscriptions[i].symbol, symbol) == 0)
            {
                return 0; // 已经订阅过了
            }
        }

        // 添加新订阅
        if (manager->subscription_count < MAX_SYMBOLS)
        {
            strncpy(manager->subscriptions[manager->subscription_count].symbol,
                    symbol, MAX_SYMBOL_LEN - 1);
            manager->subscriptions[manager->subscription_count].index = index;
            manager->subscription_count++;
            printf("Successfully subscribed to %s with index %u\n", symbol, index);
        }
    }
    else
    {
        printf("Failed to subscribe to %s: %s\n", symbol, manager->buf);
        return -1;
    }

    return 0;
}

int unsubscribe(SubscriptionManager *manager, const char *symbol)
{
    char unsubscribe_msg[MAX_SYMBOL_LEN + 1];
    snprintf(unsubscribe_msg, sizeof(unsubscribe_msg), "-%s", symbol);
    printf("Unsubscribing from symbol: %s\n", symbol);

    // 查找订阅
    int pos = -1;
    for (int i = 0; i < manager->subscription_count; i++)
    {
        if (strcmp(manager->subscriptions[i].symbol, symbol) == 0)
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
        inet_pton(AF_INET, "172.30.2.221", &server_addr.sin_addr);

        if (sendto(manager->socket, unsubscribe_msg, strlen(unsubscribe_msg), 0,
                   (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        {
            perror("sendto failed");
            return -1;
        }

        // 接收响应
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        int len = recvfrom(manager->socket, manager->buf, UDP_SIZE, 0,
                           (struct sockaddr *)&from_addr, &from_len);
        if (len < 0)
        {
            perror("recvfrom failed");
            return -1;
        }

        manager->buf[len] = '\0';
        printf("Unsubscribe response for %s: %s\n", symbol, manager->buf);

        // 移除订阅
        for (int i = pos; i < manager->subscription_count - 1; i++)
        {
            manager->subscriptions[i] = manager->subscriptions[i + 1];
        }
        manager->subscription_count--;
    }
    else
    {
        printf("Symbol %s not found in subscriptions\n", symbol);
    }

    return 0;
}

// 取消所有订阅
int unsubscribe_all(SubscriptionManager *manager)
{
    int success = 1;
    // 保存当前订阅数量，因为 unsubscribe 会修改 subscription_count
    size_t count = manager->subscription_count;

    // 从后向前遍历以避免数组重排的影响
    for (int i = count - 1; i >= 0; i--)
    {
        if (unsubscribe(manager, manager->subscriptions[i].symbol) != 0)
        {
            success = 0;
        }
    }

    return success ? 0 : -1;
}

void print_status(SubscriptionManager *manager)
{
    printf("=== Current Status ===\n");
    printf("Total symbols: %d\n", manager->subscription_count);
    for (int i = 0; i < manager->subscription_count; i++)
    {
        printf("Symbol: %s (index: %u)\n",
               manager->subscriptions[i].symbol,
               manager->subscriptions[i].index);
    }
    printf("==================\n");
}

int main()
{
    SubscriptionManager manager;
    if (init_subscription_manager(&manager) < 0)
    {
        return 1;
    }

    // 设置信号处理
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // 订阅默认的 symbols
    const char *default_symbols[] = {
        "binance-futures:btcusdt",
        "binance:btcusdt"};
    for (int i = 0; i < sizeof(default_symbols) / sizeof(default_symbols[0]); i++)
    {
        if (subscribe(&manager, default_symbols[i]) < 0)
        {
            fprintf(stderr, "Failed to subscribe to %s\n", default_symbols[i]);
        }
    }
    print_status(&manager);

    // 主循环
    while (running)
    {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        int len = recvfrom(manager.socket, manager.buf, UDP_SIZE, 0,
                           (struct sockaddr *)&from_addr, &from_len);
        if (len < 0)
        {
            perror("recvfrom failed");
            continue;
        }

        Msg *msg = (Msg *)manager.buf;
        // 查找对应的订阅
        const char *symbol = NULL;
        for (int i = 0; i < manager.subscription_count; i++)
        {
            if (manager.subscriptions[i].index == msg->index)
            {
                symbol = manager.subscriptions[i].symbol;
                break;
            }
        }

        if (symbol != NULL)
        {
            long long now = get_current_nanos();
            long long latency = now - msg->local_ns;

            if (msg->msg_type == 2)
            {
                // 处理深度数据
                Msg2 *msg2 = (Msg2 *)manager.buf;
                Msg2Level *levels = (Msg2Level *)(manager.buf + sizeof(Msg2));
                printf("%s: depth, %zu, %zu, %lld\n",
                       symbol, msg2->asks_len, msg2->bids_len, latency);

                printf("asks: ");
                for (size_t i = 0; i < msg2->asks_len; i++)
                {
                    printf("%.8g:%.8g, ", levels[i].price, levels[i].size);
                }
                printf("\nbids: ");
                for (size_t i = 0; i < msg2->bids_len; i++)
                {
                    printf("%.8g:%.8g, ", levels[msg2->asks_len + i].price,
                           levels[msg2->asks_len + i].size);
                }
                printf("\n");
            }
            else if (abs(msg->msg_type) == 1)
            {
                // 处理 ticker 数据
                printf("%s: ticker, %s, %.8g, %.8g, %lld\n",
                       symbol,
                       msg->msg_type > 0 ? "bid" : "ask",
                       msg->price,
                       msg->size,
                       latency);
            }
            else if (abs(msg->msg_type) == 3)
            {
                // 处理交易数据
                printf("%s: trade, %s, %.8g, %.8g, %lld\n",
                       symbol,
                       msg->msg_type > 0 ? "buy" : "sell",
                       msg->price,
                       msg->size,
                       latency);
            }
        }
    }

    // 清理资源前先取消订阅所有符号
    printf("Unsubscribing all symbols...\n");
    unsubscribe_all(&manager);

    // 清理资源
    close(manager.socket);
    printf("Gracefully shut down\n");
    return 0;
}