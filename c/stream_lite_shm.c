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
    // 1: Binance-Futures_BTCUSDT, 2: Binance-Futures_ETHUSDT, 3: Binance-Futures_SOLUSDT, 4: Binance-Futures_DOGEUSDT, 5: Binance_BTCUSDT
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
    // 1: Binance-Futures_BTCUSDT, 2: Binance-Futures_ETHUSDT, 3: Binance-Futures_SOLUSDT, 4: Binance-Futures_DOGEUSDT, 5: Binance_BTCUSDT
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

// main.c
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <arpa/inet.h>
#include <time.h>

#define PORT 5000
#define SERVER "127.0.0.1"

long long get_current_timestamp_ns()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);                      // 獲取當前時間
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec; // 轉換為納秒
}

void subscribe_sigusr1()
{
    int sock;
    struct sockaddr_in server_addr;
    pid_t pid = getpid(); // 取得當前進程的 PID

    // 創建 UDP socket
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // 設定目標地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER, &server_addr.sin_addr);

    // 傳送 PID
    if (sendto(sock, &pid, sizeof(pid), 0, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("sendto failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Sent PID %d to %s:%d\n", pid, SERVER, PORT);
    close(sock);
}

int fd = 0;
void *ptr;
Queue *buf;
int fd2 = 0;
void *ptr2;
LevelQueue *buf2;

int main()
{
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
    while (1)
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
