import mmap
import os
import struct

# 定義常量
SHM_NAME = "/dev/shm/msg_queue"
MAX_FLOAT_SIZE = 16
MAX_QUEUE_SIZE = 100000
MSG_SIZE = 80  # 每個 Msg 的大小
SHM_SIZE = 8 + 8 + 8 + (MAX_QUEUE_SIZE * MSG_SIZE)  # Queue 結構的大小

# 定義 Msg 結構的格式 (與 C 的 struct 對應)
MSG_FORMAT = f"qqqqqq{MAX_FLOAT_SIZE}s{MAX_FLOAT_SIZE}s"

# Queue 偏移量和格式
QUEUE_HEADER_SIZE = 8 + 8 + 8  # sn, from, to 三個 long 的大小

# 開啟共享記憶體
fd = os.open(SHM_NAME, os.O_RDWR)
shm = mmap.mmap(fd, SHM_SIZE, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)

# 讀取 Queue 結構
def read_queue_header():
    shm.seek(0)
    header_data = shm.read(QUEUE_HEADER_SIZE)
    sn, from_index, to_index = struct.unpack("qqq", header_data)
    return sn, from_index, to_index

# 主循環
def main():
    _, from_index, to_index = read_queue_header()
    cur = from_index

    while True:
        # 更新 Queue Header
        _, from_index, to_index = read_queue_header()

        while cur != to_index:
            msg_offset = QUEUE_HEADER_SIZE + (cur * MSG_SIZE)
            shm.seek(msg_offset)
            raw_msg = shm.read(MSG_SIZE)

            # 確保有足夠的數據
            if len(raw_msg) < MSG_SIZE:
                raise ValueError(f"Buffer size too small ({len(raw_msg)} instead of at least {MSG_SIZE} bytes)")

            # 解構 msg
            (
                # 1: Binance-Futures_BTCUSDT, 2: Binance-Futures_ETHUSDT, 3: Binance-Futures_SOLUSDT, 4: Binance-Futures_DOGEUSDT, 5: Binance_BTCUSDT
                instrument_id,
                # 1: L1 Bid, -1: L1 Ask, 2: L2 Bid, -2: L2 Ask, 3: Buy Trade, -3: Sell Trade
                msg_type,
                # Transaction Time MS
                tx_ms,
                # Event Time MS
                event_ms,
                # Local Time NS
                local_ns,
                # Sequence Number / Trade ID
                sn_id,
                # Price
                price,
                # Size
                size
            ) = struct.unpack(MSG_FORMAT, raw_msg)

            # 打印消息
            print(
                f"{cur}: {instrument_id}, {msg_type}, {sn_id}, {tx_ms}, {event_ms}, {local_ns}, {price.decode().strip()}, {size.decode().strip()}"
            )

            # 更新指針
            cur = (cur + 1) % MAX_QUEUE_SIZE

if __name__ == "__main__":
    try:
        main()
    finally:
        shm.close()
        os.close(fd)
