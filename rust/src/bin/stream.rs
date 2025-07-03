use std::net::UdpSocket;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

#[repr(C)]
#[derive(Debug, Copy, Clone)]
struct Msg {
    // 1: L1 Bid, -1: L1 Ask, 3: Buy Trade, -3: Sell Trade
    msg_type: i32,
    // Instrument Index
    index: u32,
    // Transaction Time MS
    tx_ms: i64,
    // Event Time MS
    event_ms: i64,
    // Local Time NS
    local_ns: i64,
    // Sequence Number / Trade ID
    sn_id: i64,
    // Price
    price: f64,
    // Size
    size: f64,
}

#[repr(C)]
#[derive(Debug, Clone)]
struct Msg2 {
    // 2: L2 Depth
    msg_type: i32,
    // Instrument Index
    index: u32,
    // Transaction Time MS
    tx_ms: i64,
    // Event Time MS
    event_ms: i64,
    // Local Time NS
    local_ns: i64,
    // Sequence Number / Trade ID
    sn_id: i64,
    // Index of asks (unused)
    asks_idx: u32,
    // Number of asks
    asks_len: u32,
    // Index of bids (unused)
    bids_idx: u32,
    // Number of bids
    bids_len: u32,
}

#[repr(C)]
#[derive(Debug, Clone)]
struct Msg2Level {
    price: f64,
    size: f64,
}

const UDP_SIZE: usize = 65536;
const MSG_SIZE: usize = std::mem::size_of::<Msg>();

#[derive(Debug, Clone)]
struct Subscription {
    symbol: String,
    index: u32,
}

struct SubscriptionManager {
    socket: UdpSocket,
    buf: [u8; UDP_SIZE],
    subscriptions: Vec<Subscription>,
}

impl SubscriptionManager {
    fn new() -> Result<Self, Box<dyn std::error::Error>> {
        let socket = UdpSocket::bind("0.0.0.0:0")?;
        Ok(Self {
            socket,
            buf: [0u8; UDP_SIZE],
            subscriptions: Vec::new(),
        })
    }

    fn subscribe(&mut self, symbol: &str) -> Result<(), Box<dyn std::error::Error>> {
        println!("Subscribing to symbol: {}", symbol);
        self.socket.send_to(symbol.as_bytes(), "10.1.0.2:9080")?;

        let (len, _) = self.socket.recv_from(&mut self.buf)?;
        let response = String::from_utf8_lossy(&self.buf[..len]).to_string();

        if let Ok(index) = response.parse::<u32>() {
            if !self.subscriptions.iter().any(|s| s.symbol == symbol) {
                self.subscriptions.push(Subscription {
                    symbol: symbol.to_string(),
                    index,
                });
                println!("Successfully subscribed to {} with index {}", symbol, index);
            }
        } else {
            println!("Failed to subscribe to {}: {}", symbol, response);
        }
        Ok(())
    }

    fn unsubscribe(&mut self, symbol: &str) -> Result<(), Box<dyn std::error::Error>> {
        let unsubscribe_msg = format!("-{}", symbol);
        println!("Unsubscribing from symbol: {}", symbol);

        if let Some(pos) = self.subscriptions.iter().position(|s| s.symbol == symbol) {
            self.socket
                .send_to(unsubscribe_msg.as_bytes(), "10.1.0.2:9080")?;

            let (len, _) = self.socket.recv_from(&mut self.buf)?;
            let response = String::from_utf8_lossy(&self.buf[..len]).to_string();
            println!("Unsubscribe response for {}: {}", symbol, response);

            self.subscriptions.remove(pos);
        } else {
            println!("Symbol {} not found in subscriptions", symbol);
        }
        Ok(())
    }

    fn unsubscribe_all(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        let symbols: Vec<String> = self
            .subscriptions
            .iter()
            .map(|s| s.symbol.clone())
            .collect();
        for symbol in symbols {
            self.unsubscribe(&symbol)?;
        }
        Ok(())
    }

    fn receive(
        &mut self,
        mut f: impl FnMut(&str, &Msg),
        mut f2: impl FnMut(&str, &Msg2, &[Msg2Level], &[Msg2Level]),
    ) -> Result<(), Box<dyn std::error::Error>> {
        let (_len, _) = self.socket.recv_from(&mut self.buf)?;

        unsafe {
            let msg = &*(self.buf.as_ptr() as *mut Msg);
            if let Some(subscription) = self.subscriptions.iter().find(|s| s.index == msg.index) {
                if msg.msg_type == 2 {
                    let msg2 = &*(self.buf.as_ptr() as *mut Msg2);
                    let level: *mut Msg2Level =
                        self.buf.as_mut_ptr().add(MSG_SIZE) as *mut Msg2Level;
                    let asks = std::slice::from_raw_parts(level, msg2.asks_len as usize);
                    let bids = std::slice::from_raw_parts(
                        level.add(msg2.asks_len as usize),
                        msg2.bids_len as usize,
                    );

                    f2(&subscription.symbol, msg2, asks, bids);
                } else {
                    f(&subscription.symbol, msg);
                }
            }
        }

        Ok(())
    }

    fn get_symbol_by_index(&self, index: u32) -> Option<&str> {
        self.subscriptions
            .iter()
            .find(|s| s.index == index)
            .map(|s| s.symbol.as_str())
    }

    fn print_status(&self) {
        println!("=== Current Status ===");
        println!("Total symbols: {}", self.subscriptions.len());
        for subscription in &self.subscriptions {
            println!(
                "Symbol: {} (index: {})",
                subscription.symbol, subscription.index
            );
        }
        println!("==================");
    }
}

impl Drop for SubscriptionManager {
    fn drop(&mut self) {
        if let Err(e) = self.unsubscribe_all() {
            eprintln!("Error during unsubscribe: {}", e);
        }
        println!("Gracefully shut down");
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut manager = SubscriptionManager::new()?;

    for symbol in [
        "binance-futures:btcusdt",
        "binance:btcusdt",
        // "okx-swap:BTC-USDT-SWAP",
        // "bybit:BTCUSDT",
    ] {
        if let Err(e) = manager.subscribe(symbol) {
            eprintln!("Failed to subscribe to {}: {}", symbol, e);
        }
    }
    manager.print_status();

    let running = Arc::new(AtomicBool::new(true));
    let r = running.clone();
    ctrlc::set_handler(move || {
        r.store(false, Ordering::SeqCst);
    })?;

    while running.load(Ordering::SeqCst) {
        manager.receive(
            |symbol, msg| {
                let now = std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap()
                    .as_nanos() as i64;
                let latency = now - msg.local_ns;
                if msg.msg_type.abs() == 1 {
                    println!(
                        "{}: ticker, {}, {}, {}, {}",
                        symbol,
                        if msg.msg_type > 0 { "bid" } else { "ask" },
                        msg.price,
                        msg.size,
                        latency
                    );
                } else if msg.msg_type.abs() == 3 {
                    println!(
                        "{}: trade, {}, {}, {}, {}",
                        symbol,
                        if msg.msg_type > 0 { "buy" } else { "sell" },
                        msg.price,
                        msg.size,
                        latency
                    );
                }
            },
            |symbol, msg2, asks, bids| {
                let now = std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap()
                    .as_nanos() as i64;
                let latency = now - msg2.local_ns;
                println!(
                    "{}: depth, {}, {}, {}",
                    symbol, msg2.asks_len, msg2.bids_len, latency
                );
                print!("asks: ");
                for ask in asks {
                    print!("{}:{}, ", ask.price, ask.size);
                }
                print!("\nbids: ");
                for bid in bids {
                    print!("{}:{}, ", bid.price, bid.size);
                }
                print!("\n");
            },
        )?;
    }

    Ok(())
}
