#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>

#define SERVER_IP "10.100.0.2"
#define SERVER_PORT 6666
#define BUFFER_SIZE 1500
#define API_KEY "API_KEY"
#define API_SECRET "API_SECRET"
#define API_PASS "API_PASS"

// 獲取 UNIX 時間戳
long unix_time()
{
    return (long)time(NULL);
}

// 發送 UDP 訊息並等待回應
void send_udp_message(int sock, struct sockaddr_in *server_addr, const char *message)
{
    char buffer[BUFFER_SIZE];

    // 發送消息
    sendto(sock, message, strlen(message), 0, (struct sockaddr *)server_addr, sizeof(*server_addr));

    // 接收回應
    socklen_t addr_len = sizeof(*server_addr);
    ssize_t received_bytes = recvfrom(sock, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr *)server_addr, &addr_len);

    if (received_bytes > 0)
    {
        buffer[received_bytes] = '\0'; // 確保字符串結尾
        printf("Received: %s\n", buffer);
    }
}

int main()
{
    // 創建 UDP 套接字
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // 設定本地監聽地址（10.100.0.1:6666）
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = inet_addr("10.100.0.1");
    local_addr.sin_port = htons(6666);

    // 綁定本地端口
    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0)
    {
        perror("Bind failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Listening on UDP port 6666...\n");

    // 設定目標（伺服器）地址
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    server_addr.sin_port = htons(SERVER_PORT);

    // 取得 UNIX 時間作為 `client_order_id`
    long client_order_id = unix_time();

    // // 1. 發送 `connect` 訊息
    char msg1[BUFFER_SIZE];
    snprintf(msg1, sizeof(msg1), "0,0,%s,%s,%s", API_KEY, API_SECRET, API_PASS);
    send_udp_message(sock, &server_addr, msg1);

    // 2. 發送 `create` 訂單訊息
    char msg2[BUFFER_SIZE];
    snprintf(msg2, sizeof(msg2), "1,1,BTC-USDT,%ld,1,1,0.02,80000.0", client_order_id);
    send_udp_message(sock, &server_addr, msg2);

    // 3. 發送 `cancel` 訂單訊息
    char msg3[BUFFER_SIZE];
    snprintf(msg3, sizeof(msg3), "2,-1,BTC-USDT,%ld", client_order_id);
    send_udp_message(sock, &server_addr, msg3);

    // 關閉套接字
    close(sock);

    return 0;
}
