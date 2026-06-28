# LolaStruct.h

`LolaStruct` は、ESP32用 LoRaモジュール（E22-900T22S(JP)等）を使った通信において、C++の構造体（Struct）を簡単かつ型安全に送受信するためのラッパーライブラリです。パケットの自動ACK返信、専用コールバックによるスマートな受信処理、生存確認（Ping）機能を備えています。

## 主な機能
- **構造体の直接送受信**: バイト配列の組み立てやパースを意識せず、C++の構造体をそのまま送受信できます。
- **型安全な受信コールバック**: 受信したデータ構造体ごとに専用の関数（コールバック）を割り当てて処理できます。
- **自動ACK機能**: データを受信すると自動で送信元にACK（確認応答）を返します。
- **送信到達確認**: `send_ack()` を使うことで、相手が確実に受信したか（ACKが返ってきたか）を確認できます。
- **スマートPing（生存確認）**: 最後に通信してから一定時間経過した場合のみPingを送信し、通信相手が生きているか確認できます。

## 依存関係
- `Arduino.h`
- `esp32_e220900t22s_jp_lib_v2.h` (ベースとなるLoRa通信ライブラリ)

---

## プロトコル仕様

パケットは以下の構造で送信されます（ヘッダ2バイト ＋ ペイロード）。

| バイト位置 | 説明 |
| :---: | :--- |
| `0` | **パケットID (Type)** <br> `0`: ACKパケット用<br> `1〜254`: ユーザー定義のデータパケット<br> `255`: Pingパケット用 |
| `1` | **シーケンス番号** <br> `0〜255` 送信のたびにインクリメントされます。 |
| `2〜` | **データ本体 (Payload)** <br> ユーザー定義構造体のデータ、またはACK/Ping用の付加情報 |

---

## 使い方 (Usage)

### 1. 構造体の定義
送受信したいデータ構造体を定義します。構造体の中には、一意のパケットIDを示す `ID` を `static const uint8_t` として定義しておくのが推奨です（自動ID機能が使えます）。

```cpp
// 送受信するデータ構造体の例
struct SensorData {
    static const uint8_t ID = 1; // この構造体のパケットID (1〜254)
    float temperature;
    float humidity;
};

struct CommandData {
    static const uint8_t ID = 2;
    int commandCode;
};
```

### 2. 初期化とコールバックの登録
`CLoRa` インスタンスを渡して `LolaStruct` を初期化し、受信時のコールバック関数を登録します。

```cpp
CLoRa lora; // 既存のLoRaインスタンス
LolaStruct lola(&lora);

void setup() {
    // LoRaの初期化処理...
    // lora.Init(); 等
    
    // 型安全なコールバックの登録
    lola.onPacket<SensorData>([](const SensorData& data, int rssi) {
        Serial.printf("温度: %.2f, 湿度: %.2f (RSSI: %d)\n", data.temperature, data.humidity, rssi);
    });

    lola.onPacket<CommandData>([](const CommandData& data, int rssi) {
        Serial.printf("コマンド受信: %d\n", data.commandCode);
    });
}
```

### 3. メインループでの受信処理と生存確認
`loop()` 内で常に `receive()` を呼び出してください。

```cpp
void loop() {
    // 常にパケットの受信待ちを行う
    lola.receive();

    // 通信が生きているか（5秒以上通信がなければPingを飛ばす）
    if (lola.isConnected(5000)) {
        // 通信OKの処理
    } else {
        // 通信切断時の処理
    }
}
```

### 4. データの送信
任意のタイミングで構造体のインスタンスを作成し、送信します。

**A. 通常送信 (ACKを待たない)**
```cpp
SensorData sData = {25.5, 60.0};
lola.send(sData); // IDを自動取得して送信
```

**B. ACK待ち送信 (到達を保証したい場合)**
```cpp
CommandData cmd = {100};
if (lola.send_ack(cmd, 2000)) { // 最大2000ms待機
    Serial.println("コマンド送信成功（相手が受け取りました）");
} else {
    Serial.println("コマンド送信失敗（タイムアウト）");
}
```

---

## API リファレンス

### コンストラクタ
- `LolaStruct(CLoRa* lora_instance)`
  - 既存の `CLoRa` インスタンスのポインタを渡して初期化します。

### 受信・コールバック系
- `template<typename T> void onPacket(void (*cb)(const T& data, int rssi))`
  - 指定した構造体 `T` が受信された際に呼ばれる関数を登録します。`T::ID` がパケットIDとして自動認識されます。
- `void setCallback(LolaReceiveCallback cb)`
  - 従来の汎用コールバックを登録します（専用コールバックがないTypeを受信した際のフォールバックとして機能します）。
- `void receive()`
  - パケットの受信処理およびACK自動返信を行います。`loop()` 内で定期的に呼び出してください。

### 送信系
- `template<typename T> bool send(const T& data)`
  - 構造体 `T` を送信します。内部で `T::ID` を使用します。
- `template<typename T> bool send(uint8_t packet_type, const T& data)`
  - 任意のパケットIDを指定して構造体を送信します。
- `template<typename T> bool send_ack(const T& data, uint32_t timeout_ms = 2000)`
  - 構造体 `T` を送信し、相手からのACKを待機します。成功すれば `true`、タイムアウトすれば `false` を返します。
- `template<typename T> bool send_ack(uint8_t packet_type, const T& data, uint32_t timeout_ms = 2000)`
  - パケットIDとタイムアウト時間を指定してACK付き送信を行います。

### ユーティリティ
- `bool isConnected(uint32_t interval_ms = 5000)`
  - 相手との通信が確立しているかを確認します。最後に通信してから `interval_ms` 経過していない場合はそのまま `true` を返し、超過している場合はPingを送信して応答を確かめます。
