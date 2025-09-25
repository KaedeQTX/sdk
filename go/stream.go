package main

import (
	"bytes"
	"context"
	"encoding/binary"
	"fmt"
	"net"
	"os"
	"os/signal"
	"sync"
	"syscall"
	"time"
)

// Constants
const (
	UDPSize                 = 65536
	MaxSymbols              = 100
	MaxSymbolLen            = 64
	SubscriptionManagerAddr = "10.11.4.97"
	SubscriptionManagerPort = 9080
	LocalBindingPort        = 9088
)

// Message structures
type Msg struct {
	MsgType int32   // 1: L1 Bid, -1: L1 Ask, 2: L2 Bid, -2: L2 Ask, 3: Buy Trade, -3: Sell Trade
	Index   int32   // Symbol index
	TxMs    int64   // Transaction time in milliseconds
	EventMs int64   // Event time in milliseconds
	LocalNs int64   // Local time in nanoseconds
	SnId    int64   // Sequence number / Trade ID
	Price   float64 // Price
	Size    float64 // Size
}

type Msg2 struct {
	MsgType int32 // Message type
	Index   int32 // Symbol index
	TxMs    int64 // Transaction time in milliseconds
	EventMs int64 // Event time in milliseconds
	LocalNs int64 // Local time in nanoseconds
	SnId    int64 // Sequence number / Trade ID
	AsksIdx int32 // Unused
	AsksLen int32 // Number of asks
	BidsIdx int32 // Unused
	BidsLen int32 // Number of bids
}

type Msg2Level struct {
	Price float64 // Price
	Size  float64 // Size
}

type Subscription struct {
	Symbol string
	Index  uint32
}

type SubscriptionManager struct {
	conn            *net.UDPConn
	subscriptions   []Subscription
	subscriptionMap map[uint32]string
	mutex           sync.RWMutex
	running         bool
	ctx             context.Context
	cancel          context.CancelFunc
}

// NewSubscriptionManager creates a new subscription manager
func NewSubscriptionManager() *SubscriptionManager {
	ctx, cancel := context.WithCancel(context.Background())
	return &SubscriptionManager{
		subscriptions:   make([]Subscription, 0, MaxSymbols),
		subscriptionMap: make(map[uint32]string),
		running:         true,
		ctx:             ctx,
		cancel:          cancel,
	}
}

// InitSubscriptionManager initializes the subscription manager
func (sm *SubscriptionManager) InitSubscriptionManager() error {
	// Create UDP connection
	addr, err := net.ResolveUDPAddr("udp", fmt.Sprintf(":%d", LocalBindingPort))
	if err != nil {
		return fmt.Errorf("failed to resolve UDP address: %v", err)
	}

	sm.conn, err = net.ListenUDP("udp", addr)
	if err != nil {
		return fmt.Errorf("failed to listen on UDP: %v", err)
	}

	fmt.Printf("UDP server listening on port %d\n", LocalBindingPort)
	return nil
}

// Subscribe subscribes to a symbol
func (sm *SubscriptionManager) Subscribe(symbol string) error {
	fmt.Printf("Subscribing to symbol: %s\n", symbol)

	// Send subscription request
	serverAddr, err := net.ResolveUDPAddr("udp", fmt.Sprintf("%s:%d", SubscriptionManagerAddr, SubscriptionManagerPort))
	if err != nil {
		return fmt.Errorf("failed to resolve server address: %v", err)
	}

	_, err = sm.conn.WriteToUDP([]byte(symbol), serverAddr)
	if err != nil {
		return fmt.Errorf("failed to send subscription request: %v", err)
	}

	return nil
}

// AddSubscription adds a subscription from response
func (sm *SubscriptionManager) AddSubscription(data []byte) error {
	sm.mutex.Lock()
	defer sm.mutex.Unlock()

	message := string(data)
	var index uint32
	var symbol string

	n, err := fmt.Sscanf(message, "%d:%s", &index, &symbol)
	if err != nil || n != 2 {
		return fmt.Errorf("failed to parse subscription response: %s", message)
	}

	// Check if already subscribed
	if _, exists := sm.subscriptionMap[index]; exists {
		return nil // Already subscribed
	}

	// Add new subscription
	if len(sm.subscriptions) < MaxSymbols {
		subscription := Subscription{
			Symbol: symbol,
			Index:  index,
		}
		sm.subscriptions = append(sm.subscriptions, subscription)
		sm.subscriptionMap[index] = symbol
		fmt.Printf("Successfully subscribed to %s with index %d\n", symbol, index)
	}

	return nil
}

// Unsubscribe unsubscribes from a symbol
func (sm *SubscriptionManager) Unsubscribe(symbol string) error {
	sm.mutex.Lock()
	defer sm.mutex.Unlock()

	fmt.Printf("Unsubscribing from symbol: %s\n", symbol)

	// Find subscription
	pos := -1
	var targetIndex uint32
	for i, sub := range sm.subscriptions {
		if sub.Symbol == symbol {
			pos = i
			targetIndex = sub.Index
			break
		}
	}

	if pos >= 0 {
		// Send unsubscribe request
		unsubscribeMsg := fmt.Sprintf("-%s", symbol)
		serverAddr, err := net.ResolveUDPAddr("udp", fmt.Sprintf("%s:%d", SubscriptionManagerAddr, SubscriptionManagerPort))
		if err != nil {
			return fmt.Errorf("failed to resolve server address: %v", err)
		}

		_, err = sm.conn.WriteToUDP([]byte(unsubscribeMsg), serverAddr)
		if err != nil {
			return fmt.Errorf("failed to send unsubscribe request: %v", err)
		}

		// Remove subscription
		sm.subscriptions = append(sm.subscriptions[:pos], sm.subscriptions[pos+1:]...)
		delete(sm.subscriptionMap, targetIndex)
	} else {
		fmt.Printf("Symbol %s not found in subscriptions\n", symbol)
	}

	return nil
}

// UnsubscribeAll unsubscribes from all symbols
func (sm *SubscriptionManager) UnsubscribeAll() error {
	sm.mutex.RLock()
	symbols := make([]string, len(sm.subscriptions))
	for i, sub := range sm.subscriptions {
		symbols[i] = sub.Symbol
	}
	sm.mutex.RUnlock()

	// Iterate backwards to avoid slice modification issues
	for i := len(symbols) - 1; i >= 0; i-- {
		if err := sm.Unsubscribe(symbols[i]); err != nil {
			fmt.Printf("Failed to unsubscribe from %s: %v\n", symbols[i], err)
		}
	}

	return nil
}

// PrintStatus prints current subscription status
func (sm *SubscriptionManager) PrintStatus() {
	sm.mutex.RLock()
	defer sm.mutex.RUnlock()

	fmt.Println("=== Current Status ===")
	fmt.Printf("Total symbols: %d\n", len(sm.subscriptions))
	for _, sub := range sm.subscriptions {
		fmt.Printf("Symbol: %s (index: %d)\n", sub.Symbol, sub.Index)
	}
	fmt.Println("====================")
}

// FindSymbolByIndex finds symbol by index
func (sm *SubscriptionManager) FindSymbolByIndex(index uint32) (string, bool) {
	sm.mutex.RLock()
	defer sm.mutex.RUnlock()
	symbol, exists := sm.subscriptionMap[index]
	return symbol, exists
}

// Close closes the subscription manager
func (sm *SubscriptionManager) Close() {
	sm.running = false
	sm.cancel()
	if sm.conn != nil {
		sm.conn.Close()
	}
}

// GetCurrentTimestampNs gets current timestamp in nanoseconds
func GetCurrentTimestampNs() int64 {
	return time.Now().UnixNano()
}

// ParseMsg parses regular message (56 bytes)
func ParseMsg(data []byte) (*Msg, error) {
	if len(data) < 56 {
		return nil, fmt.Errorf("insufficient data length, need 56 bytes, got %d bytes", len(data))
	}

	buf := bytes.NewReader(data[:56])
	msg := &Msg{}

	// Read in little endian format: int32*2, int64*4, float64*2
	if err := binary.Read(buf, binary.LittleEndian, &msg.MsgType); err != nil {
		return nil, err
	}
	if err := binary.Read(buf, binary.LittleEndian, &msg.Index); err != nil {
		return nil, err
	}
	if err := binary.Read(buf, binary.LittleEndian, &msg.TxMs); err != nil {
		return nil, err
	}
	if err := binary.Read(buf, binary.LittleEndian, &msg.EventMs); err != nil {
		return nil, err
	}
	if err := binary.Read(buf, binary.LittleEndian, &msg.LocalNs); err != nil {
		return nil, err
	}
	if err := binary.Read(buf, binary.LittleEndian, &msg.SnId); err != nil {
		return nil, err
	}
	if err := binary.Read(buf, binary.LittleEndian, &msg.Price); err != nil {
		return nil, err
	}
	if err := binary.Read(buf, binary.LittleEndian, &msg.Size); err != nil {
		return nil, err
	}

	return msg, nil
}

// ParseMsg2 parses depth message (56 bytes header + level data)
func ParseMsg2(data []byte) (*Msg2, []Msg2Level, error) {
	if len(data) < 56 {
		return nil, nil, fmt.Errorf("insufficient depth message data length, need at least 56 bytes, got %d bytes", len(data))
	}

	buf := bytes.NewReader(data[:56])
	msg2 := &Msg2{}

	// Read header in little endian format: int32*2, int64*4, int32*4
	if err := binary.Read(buf, binary.LittleEndian, &msg2.MsgType); err != nil {
		return nil, nil, err
	}
	if err := binary.Read(buf, binary.LittleEndian, &msg2.Index); err != nil {
		return nil, nil, err
	}
	if err := binary.Read(buf, binary.LittleEndian, &msg2.TxMs); err != nil {
		return nil, nil, err
	}
	if err := binary.Read(buf, binary.LittleEndian, &msg2.EventMs); err != nil {
		return nil, nil, err
	}
	if err := binary.Read(buf, binary.LittleEndian, &msg2.LocalNs); err != nil {
		return nil, nil, err
	}
	if err := binary.Read(buf, binary.LittleEndian, &msg2.SnId); err != nil {
		return nil, nil, err
	}
	if err := binary.Read(buf, binary.LittleEndian, &msg2.AsksIdx); err != nil {
		return nil, nil, err
	}
	if err := binary.Read(buf, binary.LittleEndian, &msg2.AsksLen); err != nil {
		return nil, nil, err
	}
	if err := binary.Read(buf, binary.LittleEndian, &msg2.BidsIdx); err != nil {
		return nil, nil, err
	}
	if err := binary.Read(buf, binary.LittleEndian, &msg2.BidsLen); err != nil {
		return nil, nil, err
	}

	// Parse level data
	totalLevels := int(msg2.AsksLen + msg2.BidsLen)
	levelSize := 16 // float64*2 = 16 bytes
	expectedSize := 56 + totalLevels*levelSize

	if len(data) < expectedSize {
		return nil, nil, fmt.Errorf("insufficient depth message data length, need %d bytes, got %d bytes", expectedSize, len(data))
	}

	levels := make([]Msg2Level, totalLevels)
	offset := 56

	for i := 0; i < totalLevels; i++ {
		levelBuf := bytes.NewReader(data[offset : offset+levelSize])
		if err := binary.Read(levelBuf, binary.LittleEndian, &levels[i].Price); err != nil {
			return nil, nil, err
		}
		if err := binary.Read(levelBuf, binary.LittleEndian, &levels[i].Size); err != nil {
			return nil, nil, err
		}
		offset += levelSize
	}

	return msg2, levels, nil
}

// HandleSignal handles termination signals
func HandleSignal(sm *SubscriptionManager) {
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)

	sig := <-sigChan
	fmt.Printf("\nReceived signal %v, shutting down...\n", sig)
	sm.UnsubscribeAll()
	sm.Close()
}

func main() {
	manager := NewSubscriptionManager()

	if err := manager.InitSubscriptionManager(); err != nil {
		fmt.Printf("Failed to initialize subscription manager: %v\n", err)
		os.Exit(1)
	}

	// Subscribe to default symbols
	defaultSymbols := []string{
		"binance-futures:btcusdt",
		"binance:btcusdt",
		"okx-swap:BTC-USDT-SWAP",
		"okx-spot:BTC-USDT",
		"bybit:BTCUSDT",
		"gate-io-futures:BTC_USDT",
		"kucoin-futures:XBTUSDTM",
		"kucoin:BTC-USDT",
		"bitget-futures:BTCUSDT",
		"bitget:BTCUSDT",
	}

	for _, symbol := range defaultSymbols {
		if err := manager.Subscribe(symbol); err != nil {
			fmt.Printf("Failed to subscribe to %s: %v\n", symbol, err)
		}
	}

	manager.PrintStatus()

	// Set up signal handling
	go HandleSignal(manager)

	fmt.Println("Starting to receive data...")

	// Main loop
	buffer := make([]byte, UDPSize)
	for manager.running {
		select {
		case <-manager.ctx.Done():
			break
		default:
			// Set read timeout
			manager.conn.SetReadDeadline(time.Now().Add(100 * time.Millisecond))

			n, addr, err := manager.conn.ReadFromUDP(buffer)
			if err != nil {
				if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
					continue // Timeout is expected, continue loop
				}
				if manager.running {
					fmt.Printf("Failed to receive data: %v\n", err)
				}
				continue
			}

			data := buffer[:n]

			// Subscription response
			if addr.Port == SubscriptionManagerPort {
				if err := manager.AddSubscription(data); err != nil {
					fmt.Printf("Failed to add subscription: %v\n", err)
				}
				continue
			}

			// Process market data
			if len(data) < 8 {
				continue // Need at least message type and index
			}

			// First read message header to determine message type
			headerBuf := bytes.NewReader(data[:8])
			var msgType, index int32
			if err := binary.Read(headerBuf, binary.LittleEndian, &msgType); err != nil {
				fmt.Printf("Failed to parse message type: %v\n", err)
				continue
			}
			if err := binary.Read(headerBuf, binary.LittleEndian, &index); err != nil {
				fmt.Printf("Failed to parse index: %v\n", err)
				continue
			}

			symbol, exists := manager.FindSymbolByIndex(uint32(index))
			if !exists {
				continue // Unknown symbol
			}

			if msgType == 2 {
				// Depth data, parse with Msg2 format
				msg2, levels, err := ParseMsg2(data)
				if err != nil {
					fmt.Printf("Failed to parse depth message: %v\n", err)
					continue
				}

				now := GetCurrentTimestampNs()
				latency := now - msg2.LocalNs

				fmt.Printf("%s: depth data, asks:%d, bids:%d, latency:%dns\n",
					symbol, msg2.AsksLen, msg2.BidsLen, latency)

				fmt.Print("asks: ")
				for i := 0; i < int(msg2.AsksLen); i++ {
					level := levels[i]
					fmt.Printf("%.8g:%.8g, ", level.Price, level.Size)
				}

				fmt.Print("\nbids: ")
				for i := int(msg2.AsksLen); i < int(msg2.AsksLen+msg2.BidsLen); i++ {
					level := levels[i]
					fmt.Printf("%.8g:%.8g, ", level.Price, level.Size)
				}
				fmt.Println()

			} else if abs(msgType) == 1 || abs(msgType) == 3 {
				// Ticker or trade data, parse with regular Msg format
				msg, err := ParseMsg(data)
				if err != nil {
					fmt.Printf("Failed to parse message: %v\n", err)
					continue
				}

				now := GetCurrentTimestampNs()
				latency := now - msg.LocalNs

				if abs(msg.MsgType) == 1 {
					// Handle ticker data
					side := "bid"
					if msg.MsgType < 0 {
						side = "ask"
					}
					fmt.Printf("%s: ticker, %s, %.8g, %.8g, latency:%dns\n",
						symbol, side, msg.Price, msg.Size, latency)

				} else if abs(msg.MsgType) == 3 {
					// Handle trade data
					side := "buy"
					if msg.MsgType < 0 {
						side = "sell"
					}
					fmt.Printf("%s: trade, %s, %.8g, %.8g, latency:%dns\n",
						symbol, side, msg.Price, msg.Size, latency)
				}
			}
		}
	}

	// Clean up resources
	manager.Close()
	fmt.Println("Program terminated gracefully")
}

// Helper function for absolute value
func abs(x int32) int32 {
	if x < 0 {
		return -x
	}
	return x
}
