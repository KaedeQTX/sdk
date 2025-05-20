#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>

#define SERVER_IP "172.30.2.221"
#define SERVER_PORT 6666
#define LOCAL_PORT 6667
#define BUFFER_SIZE 1500
#define API_KEY "API_KEY"
#define API_SECRET "API_SECRET"
#define API_KEY_2 "API_KEY_2"
#define API_SECRET_2 "API_SECRET_2"

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

    // 設定本地監聽地址
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(LOCAL_PORT);

    // 綁定本地端口
    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0)
    {
        perror("Bind failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Listening on UDP port %d...\n", LOCAL_PORT);

    // 設定目標（伺服器）地址
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    server_addr.sin_port = htons(SERVER_PORT);

    // 取得 UNIX 時間作為 `client_order_id`
    long client_order_id = unix_time();

    char msg[BUFFER_SIZE];

    // 1. 登入第一個帳號
    // idx, mode, api_key, api_secret
    snprintf(msg, sizeof(msg), "0,0,%s,%s", API_KEY, API_SECRET);
    send_udp_message(sock, &server_addr, msg);

    // 2. 第一個帳號下單
    // idx, mode, account_idx, symbol, client_order_id, pos_side, side, order_type, size, price
    snprintf(msg, sizeof(msg), "1,1,0,BTCUSDT,%ld,0,1,1,0.02,80000.0", client_order_id);
    send_udp_message(sock, &server_addr, msg);

    // 3. 第一個帳號取消訂單
    // idx, mode, account_idx, symbol, client_order_id
    snprintf(msg, sizeof(msg), "2,-1,0,BTCUSDT,%ld", client_order_id);
    send_udp_message(sock, &server_addr, msg);

    // 4. 登入第二個帳號
    // idx, mode, api_key, api_secret
    snprintf(msg, sizeof(msg), "0,0,%s,%s", API_KEY_2, API_SECRET_2);
    send_udp_message(sock, &server_addr, msg);

    // 5. 第二個帳號下單
    // idx, mode, account_idx, symbol, client_order_id, pos_side, side, order_type, size, price
    snprintf(msg, sizeof(msg), "1,1,1,BTCUSDT,%ld,0,1,1,0.02,80000.0", client_order_id);
    send_udp_message(sock, &server_addr, msg);

    // 6. 第二個帳號取消訂單
    // idx, mode, account_idx, symbol, client_order_id
    snprintf(msg, sizeof(msg), "2,-1,1,BTCUSDT,%ld", client_order_id);
    send_udp_message(sock, &server_addr, msg);

    // 關閉套接字
    close(sock);

    return 0;
}