#include <Arduino.h>
#include "esp32_e220900t22s_jp_lib_v2.h"
#include "LolaStruct.h"

// ==========================================
// 送受信する任意の構造体を定義
// ==========================================
struct MySensorData {
    static const uint8_t ID = 1;
    float temperature;
    float humidity;
};

struct MyCommand {
    static const uint8_t ID = 2;
    int cmd_type;
    bool enable;
};

// ==========================================
// インスタンス作成
// ==========================================
CLoRa lora;
LolaStruct lola(&lora);

void setup() {
    // シリアルモニタの初期化
    SerialMon.begin(9600);
    // 起動タイミングをランダムでずらすために少し待機
    delay(random(100, 500));
    SerialMon.println("\n--- Seeed Studio XIAO ESP32C3 - LolaStruct Booting ---");

    // LoRaの設定ファイル読み込み (SPIFFS等に保存されている場合)
    if (lora.LoadConfigSetting(CONFIG_FILENAME, lora.config)) {
        SerialMon.println("Configの読み込みに失敗しました。デフォルト値を使用します。");
    }
    if (lora.InitLoRaModule(lora.config)) {
        SerialMon.println("LoRaモジュールの初期化に失敗しました。");
        while(1) { delay(100); }
    }
    SerialMon.println("LoRaモジュール初期化 成功");

    lora.SwitchToNormalMode();

    // 受信時のコールバック関数
    lola.onPacket<MySensorData>([](const MySensorData& data, int rssi) {
        SerialMon.printf("=== Sensor受信 (RSSI: %d) ===\n", rssi);
        SerialMon.printf("  Temp: %.2f, Hum: %.2f\n", data.temperature, data.humidity);
    });

    lola.onPacket<MyCommand>([](const MyCommand& data, int rssi) {
        SerialMon.printf("=== Command受信 (RSSI: %d) ===\n", rssi);
        SerialMon.printf("  Type: %d, Enable: %d\n", data.cmd_type, data.enable);
    });
}

void loop() {
    lola.receive();
    if (!lola.maintainConnection(10000, 12000)) return;

    static uint32_t last_send_time = 0;
    static uint32_t interval = 5000;

    if (millis() - last_send_time > interval) {
        last_send_time = millis();
        interval = 5000 + random(0, 1000); 

        MySensorData s_data = { 25.5, 60.0 };
        SerialMon.println("\n[センサー送信] SensorData を送信します...");
        
        // 構造体の中のIDを自動で読み取って送信し、タイムアウト2000msでACKを待つ
        if (lola.send_ack(s_data, 2000)) {
            SerialMon.println("  -> センサー送信完了");
        } else {
            SerialMon.println("  -> センサー送信失敗");
        }
    }
}
