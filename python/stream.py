#!/usr/bin/env python3
import socket
import struct
import signal
import time
from dataclasses import dataclass
from typing import List, Optional, Dict
import sys

# Constants definition
UDP_SIZE = 65536
MAX_SYMBOLS = 100
MAX_SYMBOL_LEN = 64
SUBSCRIPTION_MANAGER = "10.11.4.97"
SUBSCRIPTION_MANAGER_PORT = 9080
LOCAL_BINDING_PORT = 9088

@dataclass
class Msg:
    """Message structure
    msg_type: 1: L1 Bid, -1: L1 Ask, 2: L2 Bid, -2: L2 Ask, 3: Buy Trade, -3: Sell Trade
    index: symbol index
    tx_ms: transaction time in milliseconds
    event_ms: event time in milliseconds  
    local_ns: local time in nanoseconds
    sn_id: sequence number / trade ID
    price: price
    size: size
    """
    msg_type: int  # int32
    index: int     # int32  
    tx_ms: int     # int64 (C long)
    event_ms: int  # int64 (C long)
    local_ns: int  # int64 (C long)
    sn_id: int     # int64 (C long)
    price: float   # double
    size: float    # double

@dataclass  
class Msg2:
    """Depth message structure"""
    msg_type: int  # int32
    index: int     # int32
    tx_ms: int     # int64 (C long)
    event_ms: int  # int64 (C long)
    local_ns: int  # int64 (C long)
    sn_id: int     # int64 (C long)
    asks_idx: int  # int32 (unused)
    asks_len: int  # int32 (number of asks)
    bids_idx: int  # int32 (unused)
    bids_len: int  # int32 (number of bids)

@dataclass
class Msg2Level:
    """Depth level data"""
    price: float
    size: float

@dataclass
class Subscription:
    """Subscription information"""
    symbol: str
    index: int

class SubscriptionManager:
    """Subscription manager"""
    
    def __init__(self):
        self.socket: Optional[socket.socket] = None
        self.subscriptions: List[Subscription] = []
        self.subscription_count = 0
        self.running = True
        
    def init_subscription_manager(self) -> int:
        """Initialize subscription manager"""
        try:
            # Create UDP socket
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            
            # Bind to local port
            self.socket.bind(('0.0.0.0', LOCAL_BINDING_PORT))
            
            self.subscription_count = 0
            return 0
        except Exception as e:
            print(f"Socket creation failed: {e}", file=sys.stderr)
            return -1
    
    def subscribe(self, symbol: str) -> int:
        """Subscribe to symbol"""
        print(f"Subscribing to symbol: {symbol}")
        
        try:
            # Send subscription request
            server_addr = (SUBSCRIPTION_MANAGER, SUBSCRIPTION_MANAGER_PORT)
            self.socket.sendto(symbol.encode('utf-8'), server_addr)
            return 0
        except Exception as e:
            print(f"Failed to send subscription request: {e}", file=sys.stderr)
            return -1
    
    def add_subscription(self, data: bytes) -> int:
        """Add subscription"""
        try:
            message = data.decode('utf-8')
            parts = message.split(':', 1)
            
            if len(parts) == 2:
                index = int(parts[0])
                symbol = parts[1]
                
                # Check if already subscribed
                for sub in self.subscriptions:
                    if sub.symbol == symbol:
                        return 0  # Already subscribed
                
                # Add new subscription
                if self.subscription_count < MAX_SYMBOLS:
                    subscription = Subscription(symbol=symbol, index=index)
                    self.subscriptions.append(subscription)
                    self.subscription_count += 1
                    print(f"Successfully subscribed to {symbol} with index {index}")
            
            return 0
        except Exception as e:
            print(f"Failed to add subscription: {e}", file=sys.stderr)
            return -1
    
    def unsubscribe(self, symbol: str) -> int:
        """Unsubscribe from symbol"""
        unsubscribe_msg = f"-{symbol}"
        print(f"Unsubscribing from symbol: {symbol}")
        
        try:
            # Find subscription
            pos = -1
            for i, sub in enumerate(self.subscriptions):
                if sub.symbol == symbol:
                    pos = i
                    break
            
            if pos >= 0:
                # Send unsubscribe request
                server_addr = (SUBSCRIPTION_MANAGER, SUBSCRIPTION_MANAGER_PORT)
                self.socket.sendto(unsubscribe_msg.encode('utf-8'), server_addr)
                
                # Remove subscription
                self.subscriptions.pop(pos)
                self.subscription_count -= 1
            else:
                print(f"Symbol {symbol} not found in subscriptions")
            
            return 0
        except Exception as e:
            print(f"Failed to unsubscribe: {e}", file=sys.stderr)
            return -1
    
    def unsubscribe_all(self) -> int:
        """Unsubscribe from all symbols"""
        success = True
        
        # Iterate backwards to avoid list modification issues
        for i in range(len(self.subscriptions) - 1, -1, -1):
            if self.unsubscribe(self.subscriptions[i].symbol) != 0:
                success = False
        
        return 0 if success else -1
    
    def print_status(self):
        """Print current status"""
        print("=== Current Status ===")
        print(f"Total symbols: {self.subscription_count}")
        for sub in self.subscriptions:
            print(f"Symbol: {sub.symbol} (index: {sub.index})")
        print("====================")
    
    def find_symbol_by_index(self, index: int) -> Optional[str]:
        """Find symbol by index"""
        for sub in self.subscriptions:
            if sub.index == index:
                return sub.symbol
        return None
    
    def close(self):
        """Close socket"""
        if self.socket:
            self.socket.close()

def get_current_timestamp_ns() -> int:
    """Get current timestamp in nanoseconds"""
    return int(time.time_ns())

MSG_FORMAT = '<iiqqqqdd'     # Little endian: int32*2, int64*4, double*2
MSG2_FORMAT = '<iiqqqqiiii'  # Little endian: int32*2, int64*4, int32*4

def parse_msg(data: bytes) -> Msg:
    """Parse regular message"""
    # Force use of correct 56-byte format
    if len(data) < 56:
        raise ValueError(f"Insufficient data length, need 56 bytes, got {len(data)} bytes")
    
    # Correct format: int32*2, int64*4, double*2 (8 fields)
    unpacked = struct.unpack(MSG_FORMAT, data[:56])
    # print(f"Debug Msg unpack: {len(unpacked)} elements, values={unpacked}")
    
    return Msg(
        msg_type=unpacked[0],
        index=unpacked[1], 
        tx_ms=unpacked[2],
        event_ms=unpacked[3],
        local_ns=unpacked[4],
        sn_id=unpacked[5],
        price=unpacked[6],
        size=unpacked[7]
    )

def parse_msg2(data: bytes) -> tuple[Msg2, List[Msg2Level]]:
    """Parse depth message"""
    # Force use of correct 56-byte format
    if len(data) < 56:
        raise ValueError(f"Insufficient depth message data length, need at least 56 bytes, got {len(data)} bytes")
    
    # Correct format: int32*2, int64*4, int32*4 (10 fields)
    header = struct.unpack(MSG2_FORMAT, data[:56])
    # print(f"Debug Msg2 unpack: {len(header)} elements, values={header}")
    
    msg2 = Msg2(
        msg_type=header[0],
        index=header[1],
        tx_ms=header[2], 
        event_ms=header[3],
        local_ns=header[4],
        sn_id=header[5],
        asks_idx=header[6],
        asks_len=header[7],
        bids_idx=header[8],
        bids_len=header[9]
    )
    
    # Parse level data
    levels = []
    offset = 56  # Header fixed at 56 bytes
    total_levels = msg2.asks_len + msg2.bids_len
    level_size = 16  # double*2 = 16 bytes, don't use calcsize
    expected_size = 56 + total_levels * level_size
    
    # print(f"Debug: total_levels={total_levels}, expected_size={expected_size}, actual_size={len(data)}")
    
    if len(data) < expected_size:
        raise ValueError(f"Insufficient depth message data length, need {expected_size} bytes, got {len(data)} bytes")
    
    for i in range(total_levels):
        price, size = struct.unpack('<dd', data[offset:offset+level_size])
        levels.append(Msg2Level(price=price, size=size))
        offset += level_size
    
    return msg2, levels

def handle_signal(signum, frame):
    """Signal handler function"""
    global manager
    print(f"\nReceived signal {signum}, shutting down...")
    manager.running = False
    manager.unsubscribe_all()

def main():
    global manager
    manager = SubscriptionManager()
    
    if manager.init_subscription_manager() < 0:
        return 1
    
    # Subscribe to default symbols
    default_symbols = [
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
    ]
    
    for symbol in default_symbols:
        if manager.subscribe(symbol) < 0:
            print(f"Failed to subscribe to {symbol}", file=sys.stderr)
    
    manager.print_status()
    
    # Set up signal handling
    signal.signal(signal.SIGINT, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)
    
    print("Starting to receive data...")
    
    # Main loop
    while manager.running:
        try:
            data, addr = manager.socket.recvfrom(UDP_SIZE)
            
            # Subscription response
            if addr[1] == SUBSCRIPTION_MANAGER_PORT:
                manager.add_subscription(data)
                continue
                
            try:
                # First read message header to determine message type
                header_format = '<ii'  # Little endian: int32, int32
                header_size = 8  # int32*2 = 8 bytes, don't use calcsize
                if len(data) < header_size:
                    print(f"Data too short, skipping: {len(data)} bytes")
                    continue
                
                msg_type, index = struct.unpack(header_format, data[:header_size])
                symbol = manager.find_symbol_by_index(index)
                
                if symbol is not None:
                    if msg_type == 2:
                        # Depth data, need to parse with Msg2 format
                        try:
                            msg2, levels = parse_msg2(data)
                            now = get_current_timestamp_ns()
                            latency = now - msg2.local_ns
                            
                            print(f"{symbol}: depth data, asks:{msg2.asks_len}, bids:{msg2.bids_len}, latency:{latency}ns")
                            
                            print("asks: ", end="")
                            for i in range(msg2.asks_len):
                                level = levels[i]
                                print(f"{level.price:.8g}:{level.size:.8g}, ", end="")
                            
                            print("\nbids: ", end="")
                            for i in range(msg2.asks_len, msg2.asks_len + msg2.bids_len):
                                level = levels[i]
                                print(f"{level.price:.8g}:{level.size:.8g}, ", end="")
                            print()
                        except ValueError as e:
                            print(f"Failed to parse depth message: {e}", file=sys.stderr)
                            continue
                            
                    elif abs(msg_type) == 1 or abs(msg_type) == 3:
                        # Ticker or trade data, parse with regular Msg format
                        try:
                            msg = parse_msg(data)
                            now = get_current_timestamp_ns()
                            latency = now - msg.local_ns
                            
                            if abs(msg.msg_type) == 1:
                                # Handle ticker data
                                side = "bid" if msg.msg_type > 0 else "ask"
                                print(f"{symbol}: ticker, {side}, {msg.price:.8g}, {msg.size:.8g}, latency:{latency}ns")
                                
                            elif abs(msg.msg_type) == 3:
                                # Handle trade data
                                side = "buy" if msg.msg_type > 0 else "sell"
                                print(f"{symbol}: trade, {side}, {msg.price:.8g}, {msg.size:.8g}, latency:{latency}ns")
                        except ValueError as e:
                            print(f"Failed to parse message: {e}", file=sys.stderr)
                            continue
                            
            except struct.error as e:
                print(f"Failed to parse message header: {e}, data length: {len(data)}", file=sys.stderr)
                continue
                
        except socket.error as e:
            if manager.running:
                print(f"Failed to receive data: {e}", file=sys.stderr)
            continue
        except KeyboardInterrupt:
            break
    
    # Clean up resources
    manager.close()
    print("Program terminated gracefully")
    return 0

if __name__ == "__main__":
    sys.exit(main()) 