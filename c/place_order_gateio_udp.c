/*
 * Gate.io UDP Order Placement Client SDK (FUTURES ONLY)
 *
 * This client demonstrates how to interact with the Gate.io UDP server
 * for futures trading account connection, order placement, and order cancellation.
 * 
 * NOTE: Currently only FUTURES trading is supported. Spot trading is not available.
 *
 * SERVER PORT: Confirm with the server administrator.
 *
 * REQUEST MESSAGE FORMATS:
 * 1. Connect:      idx,0,api_key,api_secret[,user_id]
 * 2. Place Order:  idx,1,symbol,client_order_id,pos_side,side,order_type,size,price
 *    - pos_side: Position side parameter that controls reduce_only mapping:
 *      • 0 = Both (one-way mode): reduce_only=false, user manages exact position
 *      • 1 = Long (hedge mode): BUY → reduce_only=false (open), SELL → reduce_only=true (close)
 *      • -1 = Short (hedge mode): BUY → reduce_only=true (close), SELL → reduce_only=false (open)
 *    - side: 1=BUY, 2=SELL
 *    - size: MUST be positive integer (float like 1.5 would have QTX_ERR:INVALID_FORMAT!)
 *    - Extra arguments beyond required fields are ignored for latency optimization
 * 3. Cancel Order: idx,-1,symbol,client_order_id
 *
 * RESPONSE FORMAT:
 * All responses follow a unified 3-field format: idx:response_type:payload
 * 
 * Response Types:
 * - QTX_ACK: Service acknowledgment (e.g., "connected")
 * - QTX_ERR: Service error with format ERROR_TYPE-description
 * - EXC_RES: Raw JSON response from Gate.io exchange (both success and error)
 *
 * Example Responses:
 * - Success connect: "0:QTX_ACK:connected"
 * - Error connect:   "0:QTX_ERR:LOGIN_FAILED-check credentials"
 * - Order success:   "1:EXC_RES:{\"header\":{\"status\":\"200\"},...}"
 * - Order error:     "1:EXC_RES:{\"header\":{\"status\":\"400\"},\"label\":\"INVALID_PARAM\",...}"
 * - Service error:   "1:QTX_ERR:NOT_CONNECTED-please connect first"
 * - Format error:    "1:QTX_ERR:INVALID_FORMAT-incorrect message format"
 * - Timeout error:   "1:QTX_ERR:TIMEOUT-no response received within 30s"
 *
 * GATE.IO DUAL RESPONSE PATTERN:
 * - PLACE ORDER operations receive TWO responses:
 *   1. Immediate ACK: "idx:EXC_RES:{\"ack\":true,...}" (order accepted)
 *   2. Final result: "idx:EXC_RES:{\"header\":{\"status\":\"200\"},...}" (order processed)
 * - Both responses have the same idx but different content
 * - CANCEL ORDER and CONNECT operations have single response only
 *
 * COMMON ERROR FLOWS:
 * - If you place/cancel orders without connecting first: NOT_CONNECTED error
 * - If your credentials are invalid: LOGIN_FAILED error after connect attempt
 * - The server handles WebSocket authentication internally, so you won't see 
 *   intermediate states - just success (connected) or failure (LOGIN_FAILED)
 *
 * GATE.IO SPECIFIC REQUIREMENTS:
 * - Symbol format uses underscore (BTC_USDT) not concatenation (BTCUSDT)
 * - Gate.io WebSocket authentication only requires API key/secret
 * - Optional user_id parameter for private channel subscriptions (planned for future)
 * - Client order ID must start with "t-" prefix
 * - Futures size must be an integer (number of contracts)
 * - Market orders are identified by price=0 (not a separate order type)
 *   - MARKET order type (4) automatically sets price=0 and uses IOC
 *   - Advanced: FOK market orders using order_type=3 (FOK) with price=0
 *
 * CRITICAL SIZE VALIDATION:
 * - size parameter MUST be positive integer (e.g. 1, 2, 100)
 * - Float sizes (e.g. 1.5, 2.7) are rejected immediately with QTX_ERR:INVALID_FORMAT
 * - Gate.io uses signed size for BUY/SELL direction mapping, qtx uses side parameter for direction
 *
 * PROTOCOL OPTIMIZATIONS:
 * - Extra arguments beyond required fields are ignored for ultra-low latency
 * - Invalid formats are rejected immediately without network round-trips
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

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 6670   // Update this if server uses different port. Confirm with the server administrator.
#define BUFFER_SIZE 65536  // Large buffer for JSON responses
#define RECV_TIMEOUT_SEC 5 // Response timeout in seconds

// Response types
#define RESP_QTX_ACK "QTX_ACK"
#define RESP_QTX_ERR "QTX_ERR"
#define RESP_EXC_RES "EXC_RES"

// Error types (from QTX_ERR responses)
#define ERR_INVALID_FORMAT "INVALID_FORMAT"
#define ERR_INVALID_INDEX "INVALID_INDEX"
#define ERR_INVALID_MODE "INVALID_MODE"
#define ERR_INVALID_CREDENTIALS "INVALID_CREDENTIALS"
#define ERR_NOT_CONNECTED "NOT_CONNECTED"
#define ERR_NOT_LOGGED_IN "NOT_LOGGED_IN"  // Rarely seen by clients
#define ERR_INVALID_ORDER_TYPE "INVALID_ORDER_TYPE"
#define ERR_API_SERVICE_ERROR "API_SERVICE_ERROR"
#define ERR_TIMEOUT "TIMEOUT"
#define ERR_LOGIN_FAILED "LOGIN_FAILED"
#define ERR_UNKNOWN_MODE "UNKNOWN_MODE"
#define ERR_WS_SERVICE_ERROR "WS_SERVICE_ERROR"

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

// Parse QTX_ERR payload format: ERROR_TYPE-description
void parse_qtx_error(const char *payload, char *error_type, char *description) {
    const char *dash = strchr(payload, '-');
    if (dash) {
        size_t type_len = dash - payload;
        strncpy(error_type, payload, type_len);
        error_type[type_len] = '\0';
        strcpy(description, dash + 1);
    } else {
        strcpy(error_type, payload);
        description[0] = '\0';
    }
}

// Send UDP message and wait for response with timeout
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
    
    if (strcmp(resp->response_type, RESP_QTX_ACK) == 0) {
        printf("Status: SUCCESS (Service Acknowledgment)\n");
        printf("Message: %s\n", resp->payload);
        
    } else if (strcmp(resp->response_type, RESP_QTX_ERR) == 0) {
        char error_type[64] = {0};
        char description[256] = {0};
        parse_qtx_error(resp->payload, error_type, description);
        
        printf("Status: ERROR (Service Error)\n");
        printf("Error Type: %s\n", error_type);
        printf("Description: %s\n", description);
        
        // Provide specific guidance based on common error types
        if (strcmp(error_type, ERR_NOT_CONNECTED) == 0) {
            printf("Action: Send connect message (mode 0) first\n");
        } else if (strcmp(error_type, ERR_LOGIN_FAILED) == 0) {
            printf("Action: Authentication failed - verify credentials and permissions\n");
        } else if (strcmp(error_type, ERR_INVALID_CREDENTIALS) == 0) {
            printf("Action: Check API key and secret format\n");
        } else if (strcmp(error_type, ERR_TIMEOUT) == 0) {
            printf("Action: Exchange did not respond within 30s, retry if needed\n");
        } else if (strcmp(error_type, ERR_WS_SERVICE_ERROR) == 0) {
            printf("Action: WebSocket communication error - connection may be unstable\n");
        }
        
    } else if (strcmp(resp->response_type, RESP_EXC_RES) == 0) {
        printf("Status: EXCHANGE RESPONSE\n");
        
        // Basic JSON parsing to check status
        // In production, use a proper JSON parser
        if (strstr(resp->payload, "\"status\":\"200\"")) {
            printf("Exchange Status: SUCCESS\n");
            
            // Extract some key fields if present
            char *order_id = strstr(resp->payload, "\"id\":");
            if (order_id) {
                printf("Order ID: %s...\n", order_id);
            }
        } else {
            printf("Exchange Status: ERROR\n");
            
            // Try to extract error info
            char *label = strstr(resp->payload, "\"label\":\"");
            if (label) {
                label += 9;
                char *end = strchr(label, '"');
                if (end) {
                    *end = '\0';
                    printf("Error Code: %s\n", label);
                }
            }
        }
        
        // For debugging, show first 200 chars of JSON
        printf("JSON Preview: %.200s%s\n", resp->payload, 
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
    
    // Set local listening address (0.0.0.0:0 for any available port)
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = 0; // Let system assign port
    
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
    // 1. CONNECT TO GATE.IO
    // ========================================
    printf("=== STEP 1: Connecting to Gate.io ===\n");
    char connect_msg[BUFFER_SIZE];
    
    snprintf(connect_msg, sizeof(connect_msg), 
             "0,0,YOUR_API_KEY,YOUR_API_SECRET");
    
    printf("Request: %s\n", connect_msg);
    
    ret = send_and_receive(sock, &server_addr, connect_msg, &response);
    if (ret == 0) {
        handle_response(&response);
        
        // Check if connected successfully
        if (strcmp(response.response_type, RESP_QTX_ACK) != 0 ||
            strcmp(response.payload, "connected") != 0) {
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
    // 2. PLACE LIMIT ORDER (One-Way Mode)
    // ========================================
    printf("=== STEP 2: Placing Limit Order (One-Way Mode) ===\n");
    char limit_order_msg[BUFFER_SIZE];
    snprintf(limit_order_msg, sizeof(limit_order_msg), 
             "1,1,BTC_USDT,t-%lld,0,1,1,1,75000.0", timestamp);
    printf("Request: %s\n", limit_order_msg);
    printf("(PostOnly BUY 1 BTC contract at $75,000 - One-way mode)\n");
    
    ret = send_and_receive(sock, &server_addr, limit_order_msg, &response);
    if (ret == 0) {
        handle_response(&response);
    }
    
    usleep(100000);
    
    // ========================================
    // 3. PLACE MARKET ORDER (Hedge Mode - Open SHORT)
    // ========================================
    printf("=== STEP 3: Placing Market Order (Hedge Mode - Open SHORT) ===\n");
    char market_order_msg[BUFFER_SIZE];
    snprintf(market_order_msg, sizeof(market_order_msg), 
             "2,1,ETH_USDT,t-%lld-mkt,-1,2,4,2,0", timestamp);
    printf("Request: %s\n", market_order_msg);
    printf("(Market SELL 2 ETH contracts to open SHORT position - price ignored)\n");
    
    ret = send_and_receive(sock, &server_addr, market_order_msg, &response);
    if (ret == 0) {
        handle_response(&response);
    }
    
    usleep(100000);
    
    // ========================================
    // 4. CLOSE POSITION (Hedge Mode - Close LONG)
    // ========================================
    printf("=== STEP 4: Closing Position (Hedge Mode - Close LONG) ===\n");
    char close_position_msg[BUFFER_SIZE];
    snprintf(close_position_msg, sizeof(close_position_msg), 
             "4,1,BTC_USDT,t-%lld-close,1,2,2,1,50000.0", timestamp);
    printf("Request: %s\n", close_position_msg);
    printf("(GTC SELL 1 BTC contract to close LONG position at $50,000)\n");
    
    ret = send_and_receive(sock, &server_addr, close_position_msg, &response);
    if (ret == 0) {
        handle_response(&response);
    }
    
    usleep(100000);
    
    // ========================================
    // 5. CANCEL ORDER
    // ========================================
    printf("=== STEP 5: Canceling Order ===\n");
    char cancel_msg[BUFFER_SIZE];
    snprintf(cancel_msg, sizeof(cancel_msg), 
             "5,-1,BTC_USDT,t-%lld", timestamp);
    printf("Request: %s\n", cancel_msg);
    
    ret = send_and_receive(sock, &server_addr, cancel_msg, &response);
    if (ret == 0) {
        handle_response(&response);
    }
    
    // ========================================
    // 6. DEMONSTRATE ERROR HANDLING
    // ========================================
    printf("=== STEP 6: Error Handling Examples ===\n");
    
    // Example: Invalid order type
    printf("\n--- Testing invalid order type ---\n");
    char invalid_order[BUFFER_SIZE];
    snprintf(invalid_order, sizeof(invalid_order), 
             "6,1,BTC_USDT,t-%lld-err,0,1,99,1,50000.0", timestamp);
    printf("Request: %s\n", invalid_order);
    
    ret = send_and_receive(sock, &server_addr, invalid_order, &response);
    if (ret == 0) {
        handle_response(&response);
    }
    
    // Example: Float size (critical validation error)
    printf("\n--- Testing float size (integer required) ---\n");
    char float_size_order[BUFFER_SIZE];
    snprintf(float_size_order, sizeof(float_size_order), 
             "7,1,BTC_USDT,t-%lld-float,0,1,1,1.5,50000.0", timestamp);
    printf("Request: %s\n", float_size_order);
    printf("(This should fail with INVALID_FORMAT due to float size)\n");
    
    ret = send_and_receive(sock, &server_addr, float_size_order, &response);
    if (ret == 0) {
        handle_response(&response);
    }
    
    // Example: Negative size (critical validation error)  
    printf("\n--- Testing negative size (integer parsing error) ---\n");
    char negative_size_order[BUFFER_SIZE];
    snprintf(negative_size_order, sizeof(negative_size_order), 
             "8,1,BTC_USDT,t-%lld-neg,0,1,1,-1,50000.0", timestamp);
    printf("Request: %s\n", negative_size_order);
    printf("(This should fail with INVALID_FORMAT due to negative size)\n");
    
    ret = send_and_receive(sock, &server_addr, negative_size_order, &response);
    if (ret == 0) {
        handle_response(&response);
    }
    
    // Close socket
    close(sock);
    
    printf("\nGate.io UDP Client example completed.\n");
    printf("\nIMPORTANT NOTES:\n");
    printf("1. Replace YOUR_API_KEY, YOUR_API_SECRET with real credentials\n");
    printf("2. Update SERVER_PORT to match your server's port (confirm with server administrator)\n");
    printf("3. This example uses safe prices far from market to avoid execution\n");
    printf("4. In production, implement proper JSON parsing for exchange responses\n");
    printf("5. Always handle timeouts and implement retry logic\n");
    printf("6. Monitor rate limits in exchange responses\n");
    
    return EXIT_SUCCESS;
}