/*
 * ===================================================================
 * PROTOCOL OVERVIEW
 * ===================================================================
 * 
 * The UDP protocol uses CSV format with 3-field responses:
 * Request:  idx,mode,param1,param2,...
 * Response: idx:response_type:payload
 *
 * RESPONSE TYPES (Single Character for Network Efficiency):
 * - 'k': Service acknowledgment (successful operations)
 * - 'e': Protocol/service errors (immediate rejection)  
 * - 'r': Exchange responses (raw JSON from Gate.io)
 *
 * ===================================================================
 * REQUEST FORMATS (COMPLETE REFERENCE)
 * ===================================================================
 *
 * 1. CONNECT (Mode 0)
 *    Format:  idx,0,api_key,api_secret[,user_id][,account_idx]
 *    Example: "0,0,YOUR_API_KEY,YOUR_API_SECRET"                  (creates new account)
 *             "0,0,YOUR_API_KEY,YOUR_API_SECRET,110284739"        (with user_id)
 *             "0,0,YOUR_API_KEY,YOUR_API_SECRET,,2"               (replace account 2)
 *             "0,0,YOUR_API_KEY,YOUR_API_SECRET,110284739,1"      (replace account 1 with user_id)
 *    
 *    Response Success: "0:k:0"   (returns actual account index)
 *                     "0:k:2"   (if account_idx=2 was specified and exists)
 *                     "0:k:3"   (if account_idx=100 was out of bounds, assigned next available)
 *    Response Error:   "0:e:LOGIN_FAILED-check credentials"
 *                     "0:e:INVALID_CREDENTIALS-credentials appear invalid"
 *                     "0:e:INVALID_ACCOUNT_INDEX-account index must be numeric and positive"
 *                     "0:e:ACCOUNT_LIMIT_EXCEEDED-account limit exceeded"
 *
 *    Notes:
 *    - user_id is optional (only needed for private channel subscriptions)
 *    - account_idx is optional (if provided, replaces existing account or appends if out of bounds)
 *    - Empty user_id field: use double comma (e.g., "0,0,key,secret,,5")
 *    - Server waits up to 5 seconds for WebSocket authentication
 *
 * 2. PLACE ORDER (Mode 1)
 *    Format:  idx,1,account_idx,symbol,client_order_id,pos_side,side,order_type,size,price
 *    Example: "1,1,0,BTC_USDT,t-12345,0,1,1,1,75000.0"  (use account 0)
 *    
 *    Parameters:
 *    - symbol: Gate.io format with underscore (BTC_USDT, ETH_USDT)
 *    - client_order_id: Must start with "t-" prefix, <30 characters total
 *    - pos_side: Position intent mapping (see POSITION SIDE MAPPING below)
 *    - side: 1=BUY, 2=SELL (only these values are valid)
 *    - order_type: 0=IOC, 1=PostOnly, 2=GTC, 3=FOK, 4=MARKET
 *    - size: MUST be positive integer (contracts), NO floats allowed
 *    - price: Order price (ignored for MARKET orders - use 0)
 *
 *    Response: Single response (server uses simplified response handling)
 *
 * 3. CANCEL ORDER (Mode -1)
 *    Format:  idx,-1,account_idx,symbol,client_order_id
 *    Example: "2,-1,0,BTC_USDT,t-12345"  (cancel on account 0)
 *    
 *    Response Success: "2:r:{\"header\":{\"status\":\"200\"},...}"
 *    Response Error:   "2:e:INVALID_ACCOUNT_INDEX-account index must be numeric and positive"
 *                     "2:e:ACCOUNT_NOT_FOUND-no account at specified index"
 *
 * ===================================================================
 * POSITION SIDE MAPPING (CRITICAL FOR HEDGE MODE)
 * ===================================================================
 *
 * Gate.io doesn't support Binance-style positionSide parameter.
 * The pos_side parameter controls reduce_only flag for position intent:
 *
 * pos_side=0 (One-way mode):
 *   - Always reduce_only=false
 *   - User manages exact position size
 *   - All positions are netted together
 *
 * pos_side=1 (Long position in hedge mode):
 *   - BUY orders: reduce_only=false (open long position)
 *   - SELL orders: reduce_only=true (close long position)
 *
 * pos_side=-1 (Short position in hedge mode):
 *   - BUY orders: reduce_only=true (close short position)  
 *   - SELL orders: reduce_only=false (open short position)
 *
 * This mapping solves hedge mode ambiguity where BUY could mean 
 * "open long" or "close short" depending on user intent.
 *
 * ===================================================================
 * CRITICAL VALIDATION RULES
 * ===================================================================
 *
 * 1. SIZE MUST BE POSITIVE INTEGER:
 *    Valid:   "1", "10", "100"
 *    Invalid: "1.5", "-1", "0", "1.0"
 *    Result:  e:INVALID_FORMAT for non-integers
 *
 * 2. CLIENT ORDER ID REQUIREMENTS:
 *    - Must start with "t-" prefix
 *    - Total length < 30 characters
 *    - Example: "t-12345", "t-timestamp-1"
 *
 * 3. SYMBOL FORMAT:
 *    - Use underscore: "BTC_USDT", "ETH_USDT"
 *    - NOT concatenated: "BTCUSDT" is wrong
 *
 * 4. SIDE VALUES:
 *    - Only 1 (BUY) and 2 (SELL) are valid
 *    - Other values → e:INVALID_FORMAT
 *
 * 5. MARKET ORDERS:
 *    - Use order_type=4 (MARKET) with any price (ignored)
 *    - OR use order_type=0/3 (IOC/FOK) with price=0
 *    - Gate.io API requires price=0 for market orders
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
 * AUTH STREAM RESPONSE FORMAT
 * ===================================================================
 *
 * In addition to direct request/response communication, the server also sends
 * account state updates using the auth stream format:
 *
 * FORMAT: "a:account_index:response_json"
 * - 'a': Indicates auth stream message
 * - account_index: Account identifier
 * - response_json: JSON data containing account updates
 *
 * AUTH STREAM SOURCES:
 *
 * 1. REAL-TIME ACCOUNT UPDATES (Private WebSocket Stream):
 *    - Order executions, fills, cancellations from other sources
 *    - Position changes, balance updates
 *    - User trade notifications
 *    - Pushed automatically by Gate.io's private WebSocket channels
 *
 * 2. ADDITIONAL ORDER UPDATES (Order WebSocket Stream):
 *    - Follow-up order status changes after initial order placement response
 *    - Late execution updates, partial fills
 *    - Final order completion notifications
 *
 * Both sources use the same "a:" format to provide a unified interface for
 * all account state changes. Clients should process these messages alongside
 * direct responses to maintain complete account state visibility.
 *
 * Example auth stream messages:
 * "a:0:{\"channel\":\"futures.orders\",\"result\":[{\"status\":\"filled\",...}]}"
 * "a:0:{\"channel\":\"futures.usertrades\",\"result\":[{\"price\":\"50000\",...}]}"
 *
 * ===================================================================
 * RESPONSE PARSING GUIDE
 * ===================================================================
 *
 * 1. Parse Response Format: "idx:type:payload"
 * 2. Check Response Type:
 *    - 'k': Operation successful (e.g., connected)
 *    - 'e': Parse as "ERROR_TYPE-description"
 *    - 'r': Parse JSON from Gate.io
 *    - 'a': Auth stream message (account state updates)
 *
 * ===================================================================
 * COMMON USAGE PATTERNS
 * ===================================================================
 *
 * Pattern 1: Connect and Place Limit Order (One-way mode)
 * "0,0,API_KEY,API_SECRET"                    → "0:k:0" (assigned account 0)
 * "1,1,0,BTC_USDT,t-123,0,1,1,1,75000.0"      → Single exchange response
 *
 * Pattern 2: Open Short Position (Hedge mode) on Second Account
 * "2,0,API_KEY2,API_SECRET2"                  → "2:k:1" (assigned account 1)
 * "3,1,1,ETH_USDT,t-456,-1,2,4,2,0"          → Market SELL to open SHORT
 *
 * Pattern 3: Close Long Position (Hedge mode)  
 * "4,1,0,BTC_USDT,t-789,1,2,2,1,50000.0"     → GTC SELL to close LONG on account 0
 *
 * Pattern 4: Cancel Order
 * "5,-1,0,BTC_USDT,t-123"                     → Cancel on account 0
 *
 * Pattern 5: Replace Existing Account
 * "6,0,NEW_API_KEY,NEW_API_SECRET,,0"         → "6:k:0" (replaced account 0)
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
#define SERVER_PORT 6670   // Update this if server uses different port. Confirm with the server administrator.

// Client local binding port (Should be ports that allow UDP traffic and not occupied by other services)
#define LOCAL_BIND_PORT 6671

// Gate.io API credentials (REPLACE WITH YOUR ACTUAL CREDENTIALS)
#define API_KEY "YOUR_API_KEY"
#define API_SECRET "YOUR_API_SECRET"
#define USER_ID "YOUR_USER_ID" // If you need to use private channel, you need to provide user_id (not yet implemented in the udp server)

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

// Simple error response format: "ERROR_TYPE-description"
// Example: "INVALID_FORMAT-missing required fields"
// Example: "NOT_CONNECTED-please connect first"

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
    
    printf("Gate.io UDP Client connecting to %s:%d...\n\n", SERVER_IP, SERVER_PORT);
    
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
    // 1. CONNECT TO GATE.IO (Create new account)
    // ========================================
    printf("=== STEP 1: Connecting to Gate.io ===\n");
    char connect_msg[BUFFER_SIZE];
    int account_idx = -1;
    
    // Connect without specifying account_idx (will create new account)
    snprintf(connect_msg, sizeof(connect_msg), 
             "0,0,%s,%s", API_KEY, API_SECRET);
    
    printf("Request: %s\n", connect_msg);
    
    ret = send_and_receive(sock, &server_addr, connect_msg, &response);
    if (ret == 0) {
        handle_response(&response);
        
        // Check if connected successfully and get account index
        if (strcmp(response.response_type, RESP_ACK) == 0) {
            account_idx = atoi(response.payload);
            printf("Successfully connected! Assigned account index: %d\n", account_idx);
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
             "1,-1,%d,BTC_USDT,t-nonexistent-%lld", account_idx, timestamp);
    printf("Request: %s\n", cancel_msg);
    printf("(This will demonstrate exchange error response for non-existent order)\n");
    
    ret = send_and_receive(sock, &server_addr, cancel_msg, &response);
    if (ret == 0) {
        handle_response(&response);
    }
    
    // Close socket
    close(sock);
    
    return EXIT_SUCCESS;
}