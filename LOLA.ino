#include <Arduino.h>
#include "esp32_e220900t22s_jp_lib_v2.h"
#include "LolaStruct.h"

// ==========================================
// 送受信する任意の構造体を定義
// ==========================================
struct MySensorData {
    float temperature;
    float humidity;
};

struct MyCommand {
    int cmd_type;
    bool enable;
};

// ==========================================
// インスタンス作成
// ==========================================
CLoRa lora;
LolaStruct lola(&lora);

// ==========================================
// 受信時のコールバック関数
// ==========================================
void onPacketReceived(uint8_t packet_type, uint8_t* payload, uint16_t payload_len, int rssi) {
    SerialMon.printf("=== パケット受信 (ID: %d, RSSI: %d) ===\n", packet_type, rssi);

    if (packet_type == 1 && payload_len == sizeof(MySensorData)) {
        MySensorData* data = (MySensorData*)payload;
        SerialMon.printf("  [Sensor] Temp: %.2f, Hum: %.2f\n", data->temperature, data->humidity);
    } 
    else if (packet_type == 2 && payload_len == sizeof(MyCommand)) {
        MyCommand* data = (MyCommand*)payload;
        SerialMon.printf("  [Command] Type: %d, Enable: %d\n", data->cmd_type, data->enable);
    }
}

// ==========================================
// 初期設定 (Setup)
// ==========================================
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

    // LoRaモジュールの初期化
    if (lora.InitLoRaModule(lora.config)) {
        SerialMon.println("LoRaモジュールの初期化に失敗しました。");
        while(1) { delay(100); }
    }
    SerialMon.println("LoRaモジュール初期化 成功");

    // ノーマルモード(M0=0,M1=0)へ移行
    lora.SwitchToNormalMode();

    // LolaStruct のコールバック関数を登録
    lola.setCallback(onPacketReceived);
}
void loop() {
    lola.receive();

    // ----------------------------------------
    // 5秒に1回、センサーデータを送信するテスト
    // ----------------------------------------
    static uint32_t last_send_time = 0;
    if (millis() - last_send_time > 5000) {
        last_send_time = millis();

        MySensorData s_data = { 25.5, 60.0 };
        MySensorData s_data2 = { 30.0, 55.0 };
        SerialMon.println("\n[送信テスト] SensorData を送信してACKを待機します...");
        
        if (lola.send_ack(1, s_data, 2000)) {
            SerialMon.println("  -> 送信成功 (相手からACKを受信しました)");
        } else {
            SerialMon.println("  -> 送信失敗 (タイムアウト: 相手からの応答がありません)");
        }

        if(lola.send(2, s_data2)) {
            SerialMon.println("  -> 送信成功 (ACK待ちなし)");
        } else {
            SerialMon.println("  -> 送信失敗");
        }
    }
}
