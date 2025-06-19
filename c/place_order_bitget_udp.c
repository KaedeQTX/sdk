/*
 * Bitget UDP Order Placement Client SDK
 *
 * This client demonstrates how to interact with the Bitget UDP server
 * for account connection, order placement, and order cancellation.
 * 
 * SUPPORTED TRADING MODES:
 * - FUTURES (default): Use server without --spot flag
 * - SPOT: Use server with --spot flag
 *
 * SERVER CONFIGURATION:
 * Default port: 6666 (confirm with server administrator)
 * Example: cargo run --bin place_order_bitget_udp -- --port 6669
 *
 * REQUEST MESSAGE FORMATS:
 * 1. Connect:      idx,0,api_key,api_secret,api_pass
 * 2. Place Order:  idx,1,symbol,client_order_id,side,order_type,size,price
 *    - side: 1=BUY, other values=SELL
 *    - order_type: 0=IOC, 1=PostOnly (Note: GTC and FOK not yet implemented)
 *    - client_order_id: Will be prefixed with "t-" by server
 * 3. Cancel Order: idx,-1,symbol,client_order_id
 *
 * RESPONSE FORMAT:
 * All responses follow format: idx:message
 * 
 * Response Types:
 * - Connect success: "0:connected"
 * - Order/Cancel response: "idx:response_message" (JSON response from exchange)
 *
 * Example Responses:
 * - "0:connected" - Successful connection
 * - "1:{\"code\":\"00000\",\"msg\":\"success\",\"data\":{...}}" - Order success
 * - "2:{\"code\":\"40018\",\"msg\":\"Order does not exist\"}" - Cancel error
 *
 * BITGET SPECIFIC REQUIREMENTS:
 * - API passphrase is required in addition to key/secret
 * - Symbol format: BTCUSDT (no separator)
 * - Client order ID must start with "t-" prefix (server handles this)
 * - Size can be decimal for spot trading
 * - Currently only IOC and PostOnly order types are implemented
 *
 * COMMON ERROR SCENARIOS:
 * - Missing passphrase: Connection will fail
 * - Invalid credentials: Server will report connection error
 * - Unsupported order type (2 or 3): Server will panic (avoid!)
 * - Invalid symbol format: Exchange will return error
 *
 * EXAMPLE USAGE:
 * 1. Update SERVER_IP and SERVER_PORT to match your server
 * 2. Replace API_KEY, API_SECRET, API_PASS with real credentials
 * 3. Compile: gcc -o bitget_client place_order_bitget_udp.c
 * 4. Run: ./bitget_client
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>

// Client configuration
#define CLIENT_IP "0.0.0.0"        // Bind to any available interface
#define CLIENT_PORT 6668           // Local port (0 for OS-assigned)

// Server configuration - UPDATE THESE
#define SERVER_IP "172.30.2.221"   // Your server IP
#define SERVER_PORT 6669           // Your server port (default: 6666)

// Protocol constants
#define BUFFER_SIZE 1500          // UDP buffer size
#define API_KEY "API_KEY"         // Replace with your API key
#define API_SECRET "API_SECRET"   // Replace with your API secret
#define API_PASS "API_PASS"       // Replace with your API passphrase

// Get UNIX timestamp for client_order_id
long unix_time()
{
    return (long)time(NULL);
}

// Send UDP message and wait for response
void send_udp_message(int sock, struct sockaddr_in *server_addr, const char *message)
{
    char buffer[BUFFER_SIZE];

    // Send message to server
    printf("Sending: %s\n", message);
    sendto(sock, message, strlen(message), 0, (struct sockaddr *)server_addr, sizeof(*server_addr));

    // Receive response from server
    socklen_t addr_len = sizeof(*server_addr);
    ssize_t received_bytes = recvfrom(sock, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr *)server_addr, &addr_len);

    if (received_bytes > 0)
    {
        buffer[received_bytes] = '\0'; // Null-terminate the string
        printf("Received: %s\n\n", buffer);
    }
    else
    {
        printf("Error: No response received\n\n");
    }
}

int main()
{
    // Create UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Configure local address for binding
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = inet_addr(CLIENT_IP);
    local_addr.sin_port = htons(CLIENT_PORT);

    // Bind to local port
    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0)
    {
        perror("Bind failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Bitget UDP Client started\n");
    printf("Server: %s:%d\n\n", SERVER_IP, SERVER_PORT);

    // Configure server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    server_addr.sin_port = htons(SERVER_PORT);

    // Generate unique client_order_id using timestamp
    long client_order_id = unix_time();

    // 1. CONNECT - Establish WebSocket connection with credentials
    printf("=== Step 1: Connect to Bitget ===\n");
    char connect_msg[BUFFER_SIZE];
    snprintf(connect_msg, sizeof(connect_msg), "0,0,%s,%s,%s", API_KEY, API_SECRET, API_PASS);
    send_udp_message(sock, &server_addr, connect_msg);

    // Wait briefly between operations
    sleep(1);

    // 2. PLACE ORDER - Create a PostOnly buy order
    printf("=== Step 2: Place Order ===\n");
    printf("Order: PostOnly BUY 0.02 BTC at $80,000\n");
    char place_order_msg[BUFFER_SIZE];
    snprintf(place_order_msg, sizeof(place_order_msg), 
             "1,1,BTCUSDT,%ld,1,1,0.02,80000.0", client_order_id);
    send_udp_message(sock, &server_addr, place_order_msg);

    sleep(1);

    // 3. CANCEL ORDER - Cancel the previously placed order
    printf("=== Step 3: Cancel Order ===\n");
    char cancel_order_msg[BUFFER_SIZE];
    snprintf(cancel_order_msg, sizeof(cancel_order_msg), 
             "2,-1,BTCUSDT,%ld", client_order_id);
    send_udp_message(sock, &server_addr, cancel_order_msg);

    // Close socket
    close(sock);

    return 0;
}
