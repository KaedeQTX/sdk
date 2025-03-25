package main

import (
	"fmt"
	"os"
	"syscall"
	"unsafe"
)

const (
	SHM_NAME       = "/dev/shm/msg_queue"
	MAX_FLOAT_SIZE = 16
	MAX_QUEUE_SIZE = 100000
	MSG_SIZE       = int(unsafe.Sizeof(Msg{}))
	SHM_SIZE       = int(unsafe.Sizeof(Queue{}))
)

type Msg struct {
	// 1: Binance-Futures_BTCUSDT, 2: Binance-Futures_ETHUSDT, 3: Binance-Futures_SOLUSDT, 4: Binance-Futures_DOGEUSDT, 5: Binance_BTCUSDT
	InstrumentID int64
	// 1: L1 Bid, -1: L1 Ask, 2: L2 Bid, -2: L2 Ask, 3: Buy Trade, -3: Sell Trade
	MsgType int64
	// Transaction Time MS
	TxMs int64
	// Event Time MS
	EventMs int64
	// Local Time NS
	LocalNs int64
	// Sequence Number / Trade ID
	SnID int64
	// Price in String
	Price [MAX_FLOAT_SIZE]byte
	// Size in String
	Size [MAX_FLOAT_SIZE]byte
}

type Queue struct {
	Sn   int64
	From int64
	To   int64
	Msgs [MAX_QUEUE_SIZE * MSG_SIZE]byte
}

func main() {
	// 打開共享記憶體區域
	fd, err := syscall.Open(SHM_NAME, syscall.O_RDWR, 0666)
	if err != nil {
		fmt.Fprintf(os.Stderr, "shm_open error: %v\n", err)
		os.Exit(1)
	}
	defer syscall.Close(fd)

	// 映射共享記憶體
	ptr, err := syscall.Mmap(fd, 0, SHM_SIZE, syscall.PROT_READ|syscall.PROT_WRITE, syscall.MAP_SHARED)
	if err != nil {
		fmt.Fprintf(os.Stderr, "mmap error: %v\n", err)
		os.Exit(1)
	}
	defer syscall.Munmap(ptr)

	// 將共享記憶體的指針轉換為 Queue 結構
	buf := (*Queue)(unsafe.Pointer(&ptr[0]))
	cur := buf.From

	// 循環讀取共享記憶體中的消息
	for {
		for cur != buf.To {
			// 計算當前消息的起始地址
			msgPtr := (*Msg)(unsafe.Pointer(&buf.Msgs[cur*int64(MSG_SIZE)]))

			// 打印消息內容
			fmt.Printf("%d: %d, %d, %d, %d, %d, %d, %s, %s\n",
				cur,
				msgPtr.InstrumentID,
				msgPtr.MsgType,
				msgPtr.SnID,
				msgPtr.TxMs,
				msgPtr.EventMs,
				msgPtr.LocalNs,
				string(msgPtr.Price[:]),
				string(msgPtr.Size[:]))

			// 更新讀取位置
			cur = (cur + 1) % MAX_QUEUE_SIZE
		}
	}
}
