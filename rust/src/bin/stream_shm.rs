use libc::{close, mmap, munmap, shm_open};
use libc::{MAP_FAILED, MAP_SHARED, O_RDWR, PROT_READ, PROT_WRITE};
use std::ffi::CString;
use std::mem::size_of;
use std::ptr;
use std::thread::sleep;
use std::time::Duration;

// 常量定義
const SHM_NAME: &str = "/msg_queue";
const MAX_FLOAT_SIZE: usize = 16;
const MAX_QUEUE_SIZE: usize = 100000;

// 消息結構體定義
#[repr(C)]
// #[derive(Debug, Copy, Clone)]
struct Msg {
    // 1: Binance-Futures_BTCUSDT, 2: Binance-Futures_ETHUSDT, 3: Binance-Futures_SOLUSDT, 4: Binance-Futures_DOGEUSDT, 5: Binance_BTCUSDT
    instrument_id: i64,
    // 1: L1 Bid, -1: L1 Ask, 2: L2 Bid, -2: L2 Ask, 3: Buy Trade, -3: Sell Trade
    msg_type: i64,
    // Transaction Time MS
    tx_ms: i64,
    // Event Time MS
    event_ms: i64,
    // Local Time NS
    local_ns: i64,
    // Sequence Number / Trade ID
    sn_id: i64,
    // Price
    price: [u8; MAX_FLOAT_SIZE],
    // Size
    size: [u8; MAX_FLOAT_SIZE],
}

#[repr(C)]
// #[derive(Debug, Copy, Clone)]
struct Queue {
    sn: i64,
    from: usize,
    to: usize,
    msgs: [Msg; MAX_QUEUE_SIZE],
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // 打開共享記憶體區域
    let shm_name = CString::new(SHM_NAME)?;
    let fd = unsafe { shm_open(shm_name.as_ptr(), O_RDWR, 0o666) };
    if fd == -1 {
        return Err("Failed to open shared memory".into());
    }

    println!("Shared memory opened successfully");

    // 映射共享記憶體
    let ptr = unsafe {
        mmap(
            ptr::null_mut(),
            size_of::<Queue>(),
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            fd,
            0,
        )
    };

    if ptr == MAP_FAILED {
        unsafe { close(fd) };
        return Err("Failed to map shared memory".into());
    }

    println!("Memory mapped successfully");

    // 讀取共享記憶體中的 Queue 結構體數據
    let buf = ptr as *mut Queue;
    let mut cur;

    unsafe {
        cur = (*buf).from;
    }

    // 循環讀取消息
    loop {
        unsafe {
            while cur != (*buf).to {
                // 計算消息位置
                let msg = &(*buf).msgs[cur];

                // 將字符數組轉換為字符串
                let price_str = String::from_utf8_lossy(&msg.price);
                let size_str = String::from_utf8_lossy(&msg.size);

                // 打印消息
                println!(
                    "{}: {}, {}, {}, {}, {}, {}, {}, {}",
                    cur,
                    msg.instrument_id,
                    msg.msg_type,
                    msg.sn_id,
                    msg.tx_ms,
                    msg.event_ms,
                    msg.local_ns,
                    price_str,
                    size_str
                );

                // 更新指針
                cur = (cur + 1) % MAX_QUEUE_SIZE;
            }
        }

        // 短暫休眠以減少CPU使用率
        sleep(Duration::from_micros(1));
    }

    // 注意：這段代碼實際上永遠不會執行到，因為上面的循環是無限的
    // 但為了代碼完整性，保留以下清理代碼
    // unsafe {
    //     munmap(ptr, size_of::<Queue>());
    //     close(fd);
    // }

    Ok(())
}
