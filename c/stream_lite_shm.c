#include "sdk.c"

#define SHM_NAME "/stream_lite"
#define L2_SHM_NAME "/stream_lite_l2"
#define MAX_QUEUE_SIZE 100000
#define MSG_SIZE sizeof(Msg)
#define SHM_SIZE sizeof(Queue)
#define LEVEL_SIZE sizeof(Level)
#define L2_SHM_SIZE sizeof(LevelQueue)

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
    // Index of asks
    int asks_idx;
    // Number of asks
    int asks_len;
    // Index of bids
    int bids_idx;
    // Number of bids
    int bids_len;
} Msg2;

typedef struct
{
    long from;
    long to;
    Msg msgs[MAX_QUEUE_SIZE];
} Queue;

typedef struct
{
    double price;
    double size;
} Level;

typedef struct
{
    long from;
    long to;
    Level levels[MAX_QUEUE_SIZE];
} LevelQueue;

int fd = 0;
void *ptr;
Queue *buf;
int fd2 = 0;
void *ptr2;
LevelQueue *buf2;

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
        "binance:btcusdt"};
    for (int i = 0; i < sizeof(default_symbols) / sizeof(default_symbols[0]); i++)
    {
        if (subscribe(default_symbols[i]) < 0)
        {
            fprintf(stderr, "Failed to subscribe to %s\n", default_symbols[i]);
        }
    }
    print_status();

    fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (fd == -1)
    {
        perror("shm_open");
        exit(1);
    }
    ptr = mmap(0, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED)
    {
        perror("mmap");
        close(fd);
        exit(1);
    }
    buf = (Queue *)ptr;

    fd2 = shm_open(L2_SHM_NAME, O_RDWR, 0666);
    if (fd2 == -1)
    {
        perror("shm_open");
        exit(1);
    }
    ptr2 = mmap(0, L2_SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);
    if (ptr2 == MAP_FAILED)
    {
        perror("mmap");
        close(fd2);
        exit(1);
    }
    buf2 = (LevelQueue *)ptr2;

    long cur = buf->to;

    // 设置信号处理
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // 主循环
    while (running)
    {
        while (cur != buf->to)
        {
            Msg *msg = (Msg *)&buf->msgs[cur];
            long now = get_current_timestamp_ns();
            if (msg->msg_type == 2)
            {
                Msg2 *msg = (Msg2 *)&buf->msgs[cur];
                printf("%lli: %i, %i, %lli, %lli, %lli, %lli, %i, %i, %i, %i\n", cur, msg->index, msg->msg_type, msg->sn_id, msg->tx_ms, msg->event_ms, msg->local_ns, msg->asks_idx, msg->asks_len, msg->bids_idx, msg->bids_len);
                printf("asks:");
                for (int i = 0; i < msg->asks_len; i++)
                {
                    Level *level = (Level *)&buf2->levels[msg->asks_idx + i];
                    printf(" %.8g:%.8g,", level->price, level->size);
                }
                printf("\nbids:");
                for (int i = 0; i < msg->bids_len; i++)
                {
                    Level *level = (Level *)&buf2->levels[msg->bids_idx + i];
                    printf(" %.8g:%.8g,", level->price, level->size);
                }
                printf("\n");
            }
            else
            {
                printf("%lli: %i, %i, %lli, %lli, %lli, %lli, %.8g, %.8g\n", cur, msg->index, msg->msg_type, msg->sn_id, msg->tx_ms, msg->event_ms, msg->local_ns, msg->price, msg->size);
            }
            printf("latency: %lli ns\n", now - msg->local_ns);
            cur = (cur + 1) % MAX_QUEUE_SIZE;
        }
    }

    munmap(ptr, SHM_SIZE);
    close(fd);

    return 0;
}
