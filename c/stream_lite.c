#include "sdk.c"

typedef struct Msg
{
    // 1: L1 Bid, -1: L1 Ask, 2: L2 Bid, -2: L2 Ask, 3: Buy Trade, -3: Sell Trade
    int msg_type;
    // Index of symbol
    int index;
    // Transaction Time MS
    long tx_ms;
    // Event Time MS
    long event_ms;
    // Local Time NS
    long local_ns;
    // Sequence Number / Trade ID
    long sn_id;
    // Price
    double price;
    // Size
    double size;
} Msg;

typedef struct Msg2
{
    // 1: L1 Bid, -1: L1 Ask, 2: L2 Bid, -2: L2 Ask, 3: Buy Trade, -3: Sell Trade
    int msg_type;
    // Index of symbol
    int index;
    // Transaction Time MS
    long tx_ms;
    // Event Time MS
    long event_ms;
    // Local Time NS
    long local_ns;
    // Sequence Number / Trade ID
    long sn_id;
    // Number of asks
    size_t asks_len;
    // Number of bids
    size_t bids_len;
} Msg2;

typedef struct
{
    double price;
    double size;
} Msg2Level;

long long get_current_timestamp_ns()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

int main()
{
    if (init_subscription_manager() < 0)
    {
        return 1;
    }

    // 订阅默认的 symbols
    const char *default_symbols[] = {
        "binance-futures:btcusdt",
        "binance:btcusdt",
        "okx-swap:BTC-USDT-SWAP",
        "okx-spot:BTC-USDT",
        "bybit:BTCUSDT",
        "gate-io-futures:BTC_USDT",
        "bitget-futures:BTCUSDT",
        "bitget:BTCUSDT",
    };
    for (int i = 0; i < sizeof(default_symbols) / sizeof(default_symbols[0]); i++)
    {
        if (subscribe(default_symbols[i]) < 0)
        {
            fprintf(stderr, "Failed to subscribe to %s\n", default_symbols[i]);
        }
    }
    print_status();

    // 设置信号处理
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

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
            long long now = get_current_timestamp_ns();
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
    unsubscribe_all();

    // 清理资源
    close(manager.socket);
    printf("Gracefully shut down\n");
    return 0;
}
