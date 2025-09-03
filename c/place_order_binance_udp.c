/*
 * ===================================================================
 * REQUEST FORMATS
 * ===================================================================
 *
 * IMPORTANT: All requests MUST be UTF-8 encoded. Invalid UTF-8 encoding
 * will result in NON_UTF8_FORMAT error. Only include UTF-8 compatible
 * characters in all fields (api keys, order IDs, etc.).
 *
 * 1. CONNECT (Mode 0) - Multi-Account Management
 *    Format:  idx,0,api_key,api_secret[,passphrase][,account_index]
 *    
 *    PURPOSE: The UDP server supports multiple Binance accounts simultaneously.
 *    Each account gets an index (0, 1, 2, ...) that you use in subsequent place/cancel orders.
 *    This allows trading on multiple accounts through a single UDP connection.
 *    
 *    PASSPHRASE FIELD REQUIREMENT (IMPORTANT):
 *    While Binance doesn't use a passphrase, the UDP protocol maintains a uniform format
 *    across all exchanges (Gate.io requires user_id in this field). Therefore:
 *    
 *    • If using account_index parameter: MUST include empty passphrase field
 *    • Format with account_index: "idx,0,api_key,api_secret,,account_index"
 *    • Format without account_index: "idx,0,api_key,api_secret" (passphrase omitted)
 *    
 *    This ensures protocol consistency while allowing Binance to ignore the passphrase.
 *    
 *    USAGE PATTERN:
 *    1. Connect Account A → Assigned index 0
 *    2. Connect Account B → Assigned index 1
 *    3. Place order using account 0: "100,1,0,BTCUSDT,..."
 *    4. Place order using account 1: "101,1,1,ETHUSDT,..."
 *    5. Cancel order on account 0: "102,-1,0,BTCUSDT,..."
 *    
 *    Examples:
 *    "0,0,API_KEY_A,SECRET_A"                    → "0:k:0" (first account, index 0)
 *    "1,0,API_KEY_B,SECRET_B"                    → "1:k:1" (second account, index 1)
 *    "2,0,API_KEY_C,SECRET_C,,0"                 → "2:k:0" (replace account at index 0, note empty passphrase)
 *    "3,0,API_KEY_D,SECRET_D,,2"                 → "3:k:2" (assign to index 2, note empty passphrase)
 *    
 *    Response Success: "idx:k:account_index"     (returns assigned/confirmed index)
 *    Response Error:   "idx:e:ERROR_TYPE-description"
 *
 *    ACCOUNT INDEX ASSIGNMENT RULES:
 *    - account_index parameter is optional - omit to auto-assign next available index
 *    - If account_index < current_account_count: REPLACES existing account at that index
 *    - If account_index >= current_account_count: assigns to next available index (NOT the requested index)
 *    - Example: 2 accounts exist (indices 0,1), request index 5 → assigned to index 2
 *    
 *    REPLACEMENT FAILURE BEHAVIOR (CRITICAL):
 *    - If replacement fails (connection timeout, auth failure): OLD account is PRESERVED
 *    - Only successful new connections replace existing accounts
 *    - Failed replacement attempts do NOT destroy existing working accounts
 *    - Client receives error response, existing account continues operating normally
 *    
 *    CONNECTION LIFECYCLE:
 *    - When account is successfully replaced: old connections are automatically closed
 *    - WebSocket connections, auth streams are properly cleaned up
 *    - Server maintains order placement connections for all accounts simultaneously
 *    - passphrase field: IGNORE this field (Binance doesn't use it, but field must be present for account_index)
 *
 * 2. PLACE ORDER (Mode 1)
 *    Format:  idx,1,account_index,symbol,client_order_id,pos_side,side,order_type,size,price
 *    Example: "1,1,0,BTCUSDT,order-12345,0,1,1,0.001,75000.0"  (use account 0)
 *    
 *    Parameters:
 *    - symbol: Binance format without underscore (BTCUSDT, ETHUSDT)
 *    - client_order_id: Any unique string
 *    - pos_side: LONG=1, SHORT=-1, BOTH=0
 *    - side: 1=BUY, 2=SELL
 *    - order_type: 0=IOC, 1=PostOnly, 2=GTC, 3=FOK, 4=MARKET
 *    - size: Supports decimal values (0.001, 0.5, etc.)
 *    - price: Order price (ignored for MARKET orders - use 0)
 *
 * 3. CANCEL ORDER (Mode -1)
 *    Format:  idx,-1,account_index,symbol,client_order_id
 *    Example: "2,-1,0,BTCUSDT,order-12345"  (cancel on account 0)
 *
 * ===================================================================
 * POSITION SIDE MAPPING (HEDGE MODE SUPPORT)
 * ===================================================================
 *
 * Binance supports native positionSide parameter for hedge mode trading.
 * The pos_side parameter maps directly to Binance's positionSide:
 *
 * pos_side=0 (One-way mode):
 *   - Maps to positionSide="BOTH"
 *   - All positions are netted together
 *   - Most common for spot and simple futures trading
 *
 * pos_side=1 (Long position in hedge mode):
 *   - Maps to positionSide="LONG"
 *   - BUY orders: Open or increase long position
 *   - SELL orders: Close or reduce long position
 *
 * pos_side=-1 (Short position in hedge mode):
 *   - Maps to positionSide="SHORT"
 *   - BUY orders: Close or reduce short position
 *   - SELL orders: Open or increase short position
 *
 * ===================================================================
 * ORDER TYPES REFERENCE
 * ===================================================================
 *
 * 0 = IOC (Immediate or Cancel): Execute immediately, cancel remainder
 * 1 = PostOnly: Only place as maker, reject if would execute immediately  
 * 2 = GTC (Good Till Cancel): Remain active until filled or cancelled
 * 3 = FOK (Fill or Kill): Execute completely or cancel entirely
 * 4 = MARKET: Execute at best available price (price parameter ignored)
 *
 * ===================================================================
 * RESPONSE FORMAT GUIDE
 * ===================================================================
 * 
 * RESPONSE FORMAT: 
 * 
 * 1. Indexed response: "<idx>:<type>:<payload>"
 * 2. Auth stream update: "a:<account_index>:<payload>"
 *
 * Note: Auth stream updates are not initiated by an indexed user request
 * 
 * INDEXED RESPONSE:
 * 
 * 1. ACKNOWLEDGMENT (type='k'):
 *    Format: "idx:k:account_index"
 *    Example: "2:k:0" (connect request idx 2, assigned account 0)
 *    Example: "10:k:1" (connect request idx 10, assigned account 1)
 *    Note: account_index returned should match the account_index field used in mode -1/1 requests
 * 
 * 2. EXCHANGE RESPONSE (type='r'):
 *    Format: "idx:r:json_response"
 *    Example: "1:r:{\"orderId\":123,\"status\":\"NEW\"}" (order placed)
 *    Example: "2:r:{\"orderId\":123,\"status\":\"CANCELED\"}" (order cancelled)
 *    Note: Parse JSON from Binance exchange response
 * 
 * 3. ERROR RESPONSE (type='e'):
 *    Format: "idx:e:ERROR_TYPE-description"
 *    Example: "1:e:INVALID_FORMAT-missing required fields"
 *    Example: "2:e:NOT_CONNECTED-please connect first"
 *    Note: Parse as "ERROR_TYPE-description" format
 * 
 * AUTH STREAM UPDATE:
 *    Format: "a:account_index:json_response"
 *    Example: "a:0:{\"e\":\"executionReport\",\"s\":\"BTCUSDT\"}"
 *
 * AUTH STREAM UPDATE SOURCES:
 *
 * 1. ORDER EXECUTION UPDATES:
 *    - Real-time order state changes (NEW, FILLED, CANCELED, etc.)
 *    - Trade execution notifications
 *    - Order updates from any source (API, web interface, mobile app)
 *
 * 2. ACCOUNT STATE CHANGES:
 *    - Balance updates after trades
 *    - Position changes in futures trading
 *    - Account-level notifications
 *
 * RESPONSE DESTINATION:
 * 1. All indexed responses are sent to the <ip>:<port> from which the request originated.
 * 2. Auth stream updates for accounts are sent to the <ip>:<port> from which that account's login request originated.
 *
 * COMMON USAGE PATTERNS
 * ===================================================================
 *
 * Pattern 1: Basic Single Account Trading
 * "0,0,API_KEY,API_SECRET"                     → "0:k:0" (assigned account 0)
 * "1,1,0,BTCUSDT,order-123,0,1,1,0.001,75000" → Place limit buy order
 *
 * Pattern 2: Hedge Mode Trading
 * "2,0,API_KEY,API_SECRET"                     → "2:k:0" (assigned account 0)
 * "3,1,0,BTCUSDT,long-1,1,1,2,0.001,75000"    → Open LONG position
 * "4,1,0,BTCUSDT,short-1,-1,2,2,0.001,74000"  → Open SHORT position
 * "5,1,0,BTCUSDT,close-long,1,2,2,0.001,76000" → Close LONG position
 *
 * Pattern 3: Market Orders
 * "6,1,0,ETHUSDT,market-buy,0,1,4,0.1,0"      → Market BUY (price ignored)
 *
 * Pattern 4: Multi-Account Management
 * "7,0,API_KEY1,SECRET1"                       → "7:k:0" (first account)
 * "8,0,API_KEY2,SECRET2"                       → "8:k:1" (second account)
 * "9,1,0,BTCUSDT,acc0-order,0,1,2,0.001,75000" → Trade on account 0
 * "10,1,1,ETHUSDT,acc1-order,0,2,2,0.1,3000"  → Trade on account 1
 *
 * Pattern 5: Cancel Orders
 * "11,-1,0,BTCUSDT,order-123"                 → Cancel order on account 0
 *
 * Pattern 6: Replace Existing Account (Success)
 * "12,0,NEW_API_KEY,NEW_SECRET,,0"            → "12:k:0" (replaced account 0)
 * 
 * Pattern 7: Replace Existing Account (Failure - Old Account Preserved)
 * "13,0,INVALID_KEY,INVALID_SECRET,,0"        → "13:e:TIMEOUT" (account 0 unchanged)
 * 
 * Pattern 8: Out-of-Bounds Index Assignment
 * Current accounts: [0, 1] (2 accounts exist)
 * "14,0,API_KEY,API_SECRET,,5"                → "14:k:2" (assigned to index 2, not 5)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>
#include <sys/select.h>
#include <fcntl.h>

// Server connection settings
#define SERVER_IP "10.11.4.97"
#define SERVER_PORT 6671   // Update this if server uses different port. Confirm with the server administrator.

// Client local binding port (Should be ports that allow UDP traffic and not occupied by other services)
#define LOCAL_BIND_PORT 6672

// Binance API credentials (REPLACE WITH YOUR ACTUAL CREDENTIALS)
#define API_KEY "YOUR_API_KEY"
#define API_SECRET "YOUR_API_SECRET"

#define BUFFER_SIZE 65536  // Large buffer for JSON responses
#define RECV_TIMEOUT_SEC 5 // Response timeout in seconds

// Response types (single character for network efficiency)
#define RESP_ACK "k"
#define RESP_ERR "e"
#define RESP_EXC "r"
#define RESP_AUTH "a"

// Response structure
typedef struct {
    int idx;
    char response_type[16];
    char payload[BUFFER_SIZE - 32];
    int is_valid;
} Response;

// Get UNIX timestamp
long unix_time() {
    return (long)time(NULL);
}

// Get UNIX timestamp in milliseconds
long long unix_time_millis() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

// Parse response into structured format
Response parse_response(const char *raw_response) {
    Response resp = {0};
    resp.is_valid = 0;
    
    // Find first colon
    const char *first_colon = strchr(raw_response, ':');
    if (!first_colon) return resp;
    
    // Find second colon
    const char *second_colon = strchr(first_colon + 1, ':');
    if (!second_colon) return resp;
    
    // Parse idx
    char idx_str[16] = {0};
    size_t idx_len = first_colon - raw_response;
    if (idx_len >= sizeof(idx_str)) return resp;
    strncpy(idx_str, raw_response, idx_len);
    resp.idx = atoi(idx_str);
    
    // Parse response type
    size_t type_len = second_colon - first_colon - 1;
    if (type_len >= sizeof(resp.response_type)) return resp;
    strncpy(resp.response_type, first_colon + 1, type_len);
    
    // Copy payload
    strncpy(resp.payload, second_colon + 1, sizeof(resp.payload) - 1);
    
    resp.is_valid = 1;
    return resp;
}

// Send UDP message and wait for single response with timeout
int send_and_receive(int sock, struct sockaddr_in *server_addr, 
                     const char *message, Response *response) {
    char buffer[BUFFER_SIZE];
    
    // Send message
    if (sendto(sock, message, strlen(message), 0, 
               (struct sockaddr *)server_addr, sizeof(*server_addr)) < 0) {
        perror("sendto failed");
        return -1;
    }
    
    // Set up timeout
    fd_set readfds;
    struct timeval tv;
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);
    tv.tv_sec = RECV_TIMEOUT_SEC;
    tv.tv_usec = 0;
    
    // Wait for response with timeout
    int rv = select(sock + 1, &readfds, NULL, NULL, &tv);
    if (rv == -1) {
        perror("select failed");
        return -1;
    } else if (rv == 0) {
        printf("Timeout: No response received within %d seconds\n", RECV_TIMEOUT_SEC);
        return -2;
    }
    
    // Receive response
    socklen_t addr_len = sizeof(*server_addr);
    ssize_t received_bytes = recvfrom(sock, buffer, BUFFER_SIZE - 1, 0, 
                                      (struct sockaddr *)server_addr, &addr_len);
    
    if (received_bytes > 0) {
        buffer[received_bytes] = '\0';
        printf("Raw response: %s\n", buffer);
        
        // Parse response
        *response = parse_response(buffer);
        if (!response->is_valid) {
            printf("Failed to parse response\n");
            return -3;
        }
        
        return 0;
    } else {
        perror("recvfrom failed");
        return -1;
    }
}

// Handle response based on type
void handle_response(const Response *resp) {
    printf("\n=== Response Analysis ===\n");
    printf("Index: %d\n", resp->idx);
    printf("Type: %s\n", resp->response_type);
    
    if (strcmp(resp->response_type, RESP_ACK) == 0) {
        printf("Status: SUCCESS\n");
        printf("Message: %s\n", resp->payload);
        
    } else if (strcmp(resp->response_type, RESP_ERR) == 0) {
        printf("Status: ERROR\n");
        printf("Error: %s\n", resp->payload);
        
    } else if (strcmp(resp->response_type, RESP_EXC) == 0) {
        printf("Status: EXCHANGE RESPONSE\n");
        printf("JSON: %.200s%s\n", resp->payload, 
               strlen(resp->payload) > 200 ? "..." : "");
    } else if (strcmp(resp->response_type, RESP_AUTH) == 0) {
        printf("Status: AUTH STREAM UPDATE\n");
        printf("Account: %d\n", resp->idx); // idx represents account_index for auth messages
        printf("JSON: %.200s%s\n", resp->payload, 
               strlen(resp->payload) > 200 ? "..." : "");
    }
    
    printf("========================\n\n");
}

int main() {
    int sock = -1;
    int ret = 0;
    
    // Create UDP socket
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return EXIT_FAILURE;
    }
    
    // Set socket to non-blocking for select()
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    
    // Set local binding address (any interface, configurable port)
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(LOCAL_BIND_PORT);
    
    // Bind local port
    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        perror("Bind failed");
        close(sock);
        return EXIT_FAILURE;
    }
    
    printf("Binance UDP Client connecting to %s:%d...\n\n", SERVER_IP, SERVER_PORT);
    
    // Set target (server) address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    server_addr.sin_port = htons(SERVER_PORT);
    
    // Use timestamp for unique client order IDs
    long long timestamp = unix_time_millis();
    Response response;
    
    // ========================================
    // 1. CONNECT TO BINANCE (Create new account)
    // ========================================
    printf("=== STEP 1: Connecting to Binance ===\n");
    char connect_msg[BUFFER_SIZE];
    int account_index = -1;
    
    // Connect to Binance (passphrase not used by Binance, but protocol field maintained)
    // Using auto-assigned account index (no account_index parameter)
    snprintf(connect_msg, sizeof(connect_msg), 
             "0,0,%s,%s", API_KEY, API_SECRET);
    
    // Alternative format to specify account index 0 explicitly:
    // snprintf(connect_msg, sizeof(connect_msg), 
    //          "0,0,%s,%s,,0", API_KEY, API_SECRET);
    // Note the empty passphrase field (double comma) when using account_index
    
    printf("Request: %s\n", connect_msg);
    
    ret = send_and_receive(sock, &server_addr, connect_msg, &response);
    if (ret == 0) {
        handle_response(&response);
        
        // Check if connected successfully and get account index
        if (strcmp(response.response_type, RESP_ACK) == 0) {
            account_index = atoi(response.payload);
            printf("Successfully connected! Assigned account index: %d\n", account_index);
        } else {
            printf("Failed to connect. Exiting.\n");
            close(sock);
            return EXIT_FAILURE;
        }
    } else {
        printf("Failed to get connect response\n");
        close(sock);
        return EXIT_FAILURE;
    }
    
    // Wait a bit before next request
    usleep(100000); // 100ms
    
    // ========================================
    // 2. CANCEL NON-EXISTENT ORDER (Demonstrates Protocol)
    // ========================================
    printf("=== STEP 2: Canceling Non-Existent Order ===\n");
    char cancel_msg[BUFFER_SIZE];
    snprintf(cancel_msg, sizeof(cancel_msg), 
             "1,-1,%d,BTCUSDT,nonexistent-order-%lld", account_index, timestamp);
    printf("Request: %s\n", cancel_msg);
    printf("(Canceling non-existent order to test error handling)\n");
    
    ret = send_and_receive(sock, &server_addr, cancel_msg, &response);
    if (ret == 0) {
        handle_response(&response);
    }
    
    // Close socket
    close(sock);
    
    return EXIT_SUCCESS;
}