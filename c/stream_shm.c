#define SHM_NAME "/msg_queue"
#define MAX_FLOAT_SIZE 16
#define MAX_QUEUE_SIZE 100000
#define MSG_SIZE sizeof(Msg)
#define SHM_SIZE sizeof(Queue)

typedef struct Msg
{
    // 1: Binance-Futures_BTCUSDT, 2: Binance-Futures_ETHUSDT, 3: Binance-Futures_SOLUSDT, 4: Binance-Futures_DOGEUSDT, 5: Binance_BTCUSDT
    long instrument_id;
    // 1: L1 Bid, -1: L1 Ask, 2: L2 Bid, -2: L2 Ask, 3: Buy Trade, -3: Sell Trade
    long msg_type;
    // Transaction Time MS
    long tx_ms;
    // Event Time MS
    long event_ms;
    // Local Time NS
    long local_ns;
    // Sequence Number / Trade ID
    long sn_id;
    // Price
    char price[MAX_FLOAT_SIZE];
    // Size
    char size[MAX_FLOAT_SIZE];
} Msg;

typedef struct
{
    long sn;
    long from;
    long to;
    char msgs[MAX_QUEUE_SIZE * MSG_SIZE];
} Queue;

// main.c
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

int fd = 0;
void *ptr;
Queue *buf;

int main()
{
    // 打開共享記憶體區域
    fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (fd == -1)
    {
        perror("shm_open");
        exit(1);
    }

    // 映射共享記憶體
    ptr = mmap(0, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED)
    {
        perror("mmap");
        close(fd);
        exit(1);
    }

    // 讀取共享記憶體中的 Buf 結構體數據
    buf = (Queue *)ptr;
    long cur = buf->from;

    while (1)
    {
        while (cur != buf->to)
        {
            Msg *msg = (Msg *)&buf->msgs[cur * MSG_SIZE];
            printf("%lli: %lli, %lli, %lli, %lli, %lli, %lli, %s, %s\n", cur, msg->instrument_id, msg->msg_type, msg->sn_id, msg->tx_ms, msg->event_ms, msg->local_ns, msg->price, msg->size);
            cur = (cur + 1) % MAX_QUEUE_SIZE;
        }
    }

    // 解除映射並關閉共享記憶體
    munmap(ptr, SHM_SIZE);
    close(fd);

    return 0;
}
