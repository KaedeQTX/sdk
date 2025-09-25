import java.io.IOException;
import java.net.*;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.time.Instant;
import java.util.Arrays;

public class SubscriptionManager {

    private static final int UDP_SIZE = 65536;
    private static final int MAX_SYMBOLS = 100;

    private DatagramSocket socket;
    private byte[] buf = new byte[UDP_SIZE];
    private Subscription[] subscriptions = new Subscription[MAX_SYMBOLS];
    private int subscriptionCount = 0;

    public static class Msg {
        int msgType;
        int index;
        long txMs;
        long eventMs;
        long localNs;
        long snId;
        double price;
        double size;

        static Msg fromByteBuffer(ByteBuffer buffer) {
            Msg m = new Msg();
            m.msgType = buffer.getInt();
            m.index = buffer.getInt();
            m.txMs = buffer.getLong();
            m.eventMs = buffer.getLong();
            m.localNs = buffer.getLong();
            m.snId = buffer.getLong();
            m.price = buffer.getDouble();
            m.size = buffer.getDouble();
            return m;
        }
    }

    public static class Msg2 {
        int msgType;
        int index;
        long txMs;
        long eventMs;
        long localNs;
        long snId;
        int asksIdx;  // unused
        int asksLen;  // number of asks
        int bidsIdx;  // unused
        int bidsLen;  // number of bids

        static Msg2 fromByteBuffer(ByteBuffer buffer) {
            Msg2 m = new Msg2();
            m.msgType = buffer.getInt();
            m.index = buffer.getInt();
            m.txMs = buffer.getLong();
            m.eventMs = buffer.getLong();
            m.localNs = buffer.getLong();
            m.snId = buffer.getLong();
            m.asksIdx = buffer.getInt();
            m.asksLen = buffer.getInt();
            m.bidsIdx = buffer.getInt();
            m.bidsLen = buffer.getInt();
            return m;
        }
    }

    public static class Msg2Level {
        double price;
        double size;

        static Msg2Level fromByteBuffer(ByteBuffer buffer) {
            Msg2Level m = new Msg2Level();
            m.price = buffer.getDouble();
            m.size = buffer.getDouble();
            return m;
        }
    }

    public static class Subscription {
        String symbol;
        int index;

        public Subscription(String symbol, int index) {
            this.symbol = symbol;
            this.index = index;
        }
    }

    public boolean init() {
        try {
            socket = new DatagramSocket(9088);  // Bind to LOCAL_BINDING_PORT
            return true;
        } catch (SocketException e) {
            e.printStackTrace();
            return false;
        }
    }

    public boolean subscribe(String symbol) {
        try {
            System.out.println("Subscribing to symbol: " + symbol);

            InetAddress serverAddress = InetAddress.getByName("10.11.4.97");
            int port = 9080;
            byte[] sendData = symbol.getBytes(StandardCharsets.UTF_8);
            DatagramPacket sendPacket = new DatagramPacket(sendData, sendData.length, serverAddress, port);
            socket.send(sendPacket);

            DatagramPacket recvPacket = new DatagramPacket(buf, buf.length);
            socket.receive(recvPacket);
            String response = new String(recvPacket.getData(), 0, recvPacket.getLength(), StandardCharsets.UTF_8);

            int index = Integer.parseInt(response.trim());

            for (int i = 0; i < subscriptionCount; i++) {
                if (subscriptions[i].symbol.equals(symbol)) {
                    return true;
                }
            }

            if (subscriptionCount < MAX_SYMBOLS) {
                subscriptions[subscriptionCount++] = new Subscription(symbol, index);
                System.out.printf("Successfully subscribed to %s with index %d\n", symbol, index);
                return true;
            }

        } catch (IOException | NumberFormatException e) {
            System.err.println("Failed to subscribe to " + symbol + ": " + e.getMessage());
        }
        return false;
    }

    public boolean unsubscribe(String symbol) {
        try {
            System.out.println("Unsubscribing from symbol: " + symbol);

            int pos = -1;
            for (int i = 0; i < subscriptionCount; i++) {
                if (subscriptions[i].symbol.equals(symbol)) {
                    pos = i;
                    break;
                }
            }

            if (pos >= 0) {
                String unsubscribeMsg = "-" + symbol;
                InetAddress serverAddress = InetAddress.getByName("10.11.4.97");
                int port = 9080;
                byte[] sendData = unsubscribeMsg.getBytes(StandardCharsets.UTF_8);
                DatagramPacket sendPacket = new DatagramPacket(sendData, sendData.length, serverAddress, port);
                socket.send(sendPacket);

                DatagramPacket recvPacket = new DatagramPacket(buf, buf.length);
                socket.receive(recvPacket);
                String response = new String(recvPacket.getData(), 0, recvPacket.getLength(), StandardCharsets.UTF_8);

                System.out.printf("Unsubscribe response for %s: %s\n", symbol, response);

                System.arraycopy(subscriptions, pos + 1, subscriptions, pos, subscriptionCount - pos - 1);
                subscriptionCount--;
                return true;
            } else {
                System.out.printf("Symbol %s not found in subscriptions\n", symbol);
            }

        } catch (IOException e) {
            e.printStackTrace();
        }
        return false;
    }

    public void unsubscribeAll() {
        for (int i = subscriptionCount - 1; i >= 0; i--) {
            unsubscribe(subscriptions[i].symbol);
        }
    }

    public void printStatus() {
        System.out.println("=== Current Status ===");
        System.out.println("Total symbols: " + subscriptionCount);
        for (int i = 0; i < subscriptionCount; i++) {
            System.out.printf("Symbol: %s (index: %d)\n", subscriptions[i].symbol, subscriptions[i].index);
        }
        System.out.println("======================");
    }

    private long currentNanos() {
        return Instant.now().toEpochMilli() * 1_000_000;
    }

    public void run() {
        while (true) {
            try {
                DatagramPacket packet = new DatagramPacket(buf, buf.length);
                socket.receive(packet);

                // ByteBuffer buffer = ByteBuffer.wrap(buf);
                ByteBuffer buffer = ByteBuffer.wrap(buf).order(ByteOrder.LITTLE_ENDIAN);
                Msg msg = Msg.fromByteBuffer(buffer);

                String symbol = null;
                for (int i = 0; i < subscriptionCount; i++) {
                    if (subscriptions[i].index == msg.index) {
                        symbol = subscriptions[i].symbol;
                        break;
                    }
                }

                if (symbol != null) {
                    long latency = currentNanos() - msg.localNs;

                    if (msg.msgType == 2) {
                        buffer.rewind();
                        Msg2 msg2 = Msg2.fromByteBuffer(buffer);
                        Msg2Level[] levels = new Msg2Level[(int) (msg2.asksLen + msg2.bidsLen)];

                        for (int i = 0; i < levels.length; i++) {
                            levels[i] = Msg2Level.fromByteBuffer(buffer);
                        }

                        System.out.printf("%s: depth, %d, %d, %d\n", symbol, msg2.asksLen, msg2.bidsLen, latency);
                        System.out.print("asks: ");
                        for (int i = 0; i < msg2.asksLen; i++) {
                            System.out.printf("%.8g:%.8g, ", levels[i].price, levels[i].size);
                        }
                        System.out.print("\nbids: ");
                        for (int i = 0; i < msg2.bidsLen; i++) {
                            System.out.printf("%.8g:%.8g, ", levels[(int) msg2.asksLen + i].price,
                                    levels[(int) msg2.asksLen + i].size);
                        }
                        System.out.println();
                    } else if (Math.abs(msg.msgType) == 1) {
                        System.out.printf("%s: ticker, %s, %.8g, %.8g, %d\n", symbol,
                                msg.msgType > 0 ? "bid" : "ask", msg.price, msg.size, latency);
                    } else if (Math.abs(msg.msgType) == 3) {
                        System.out.printf("%s: trade, %s, %.8g, %.8g, %d\n", symbol,
                                msg.msgType > 0 ? "buy" : "sell", msg.price, msg.size, latency);
                    }
                }

            } catch (IOException e) {
                e.printStackTrace();
            }
        }
    }

    // 主程序
    public static void main(String[] args) {
        SubscriptionManager manager = new SubscriptionManager();
        if (!manager.init()) {
            System.err.println("Failed to init manager");
            return;
        }

        String[] defaultSymbols = {
                "binance-futures:btcusdt",
                "binance:btcusdt",
                "okx-swap:BTC-USDT-SWAP",
                "okx-spot:BTC-USDT",
                "bybit:BTCUSDT",
                "gate-io-futures:BTC_USDT",
                "kucoin-futures:XBTUSDTM",
                "kucoin:BTC-USDT",
                "bitget-futures:BTCUSDT",
                "bitget:BTCUSDT"
        };

        Arrays.stream(defaultSymbols).forEach(symbol -> {
            if (!manager.subscribe(symbol)) {
                System.err.println("Failed to subscribe to " + symbol);
            }
        });

        manager.printStatus();

        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            System.out.println("Unsubscribing all symbols...");
            manager.unsubscribeAll();
            manager.socket.close();
            System.out.println("Gracefully shut down");
        }));

        manager.run();
    }
}