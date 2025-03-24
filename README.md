# QTX Lightning Stream Lite Market Data SDK

这是一个用于订阅和接收市场数据的 C 语言 SDK。它支持订阅多个交易对的行情数据，包括 Ticker、深度和成交信息。

## 功能特点

- 支持多个交易对的订阅管理
- 使用 UDP 协议进行高效的数据传输
- 支持以下类型的市场数据：
  - Ticker 数据（买一/卖一）
  - 深度数据（多档深度）
  - 成交数据

## 消息类型

```c
// Ticker 和成交消息格式
typedef struct {
    int msg_type;      // 1: 买一, -1: 卖一, 3: 买成交, -3: 卖成交
    unsigned int index; // 交易对索引
    long long tx_ms;   // 发送时间（毫秒）
    long long event_ms;// 事件时间（毫秒）
    long long local_ns;// 本地时间（纳秒）
    long long sn_id;   // 序列号/成交ID
    double price;      // 价格
    double size;       // 数量
} Msg;

// 深度消息格式
typedef struct {
    int msg_type;      // 2: 深度数据
    unsigned int index; // 交易对索引
    long long tx_ms;   // 发送时间（毫秒）
    long long event_ms;// 事件时间（毫秒）
    long long local_ns;// 本地时间（纳秒）
    long long sn_id;   // 序列号
    size_t asks_len;   // 卖盘档数
    size_t bids_len;   // 买盘档数
} Msg2;
```

## 使用方法

### 编译

```bash
gcc -o stream_lite stream_lite.c
```

### 运行

```bash
./stream_lite
```

### 订阅示例

```c
// 初始化订阅管理器
SubscriptionManager manager;
init_subscription_manager(&manager);

// 订阅交易对
subscribe(&manager, "binance-futures:btcusdt");
subscribe(&manager, "binance:btcusdt");

// 接收和处理数据
while (running) {
    // 接收数据...
}

// 程序结束前取消所有订阅
unsubscribe_all(&manager);
```

## 配置项

- `UDP_SIZE`: UDP 缓冲区大小（默认 65536 字节）
- `MAX_SYMBOLS`: 最大支持的交易对数量（默认 100）
- `MAX_SYMBOL_LEN`: 交易对名称最大长度（默认 64）

## 数据格式说明

### Ticker 数据
```
symbol: ticker, bid/ask, price, size, latency
```

### 深度数据
```
symbol: depth, asks_count, bids_count, latency
asks: price:size, price:size, ...
bids: price:size, price:size, ...
```

### 成交数据
```
symbol: trade, buy/sell, price, size, latency
```

## 注意事项

1. 确保有足够的网络权限
2. 服务器地址和端口在代码中硬编码为 "172.30.2.221:8080"
3. 程序会自动处理 SIGINT 和 SIGTERM 信号
4. 退出时会自动取消所有订阅并清理资源

## 性能考虑

- 使用数组而非链表存储订阅信息，适合小规模订阅场景
- UDP 通信保证最低延迟
- 使用 CLOCK_REALTIME 计算精确的延迟时间 
