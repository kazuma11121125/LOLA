#ifndef LOLA_STRUCT_H
#define LOLA_STRUCT_H

#include <Arduino.h>
#include "esp32_e220900t22s_jp_lib_v2.h"

// 受信コールバックの型定義
// packet_type: 受信したパケットのID (1〜255)
// payload: 受信した構造体のデータポインタ
// payload_len: データの長さ
// rssi: 電波強度
typedef void (*LolaReceiveCallback)(uint8_t packet_type, uint8_t* payload, uint16_t payload_len, int rssi);

class LolaStruct {
private:
    CLoRa* lora;
    uint8_t seq_num;
    LolaReceiveCallback rx_cb;
    
    // ACK待ち用フラグ
    bool waiting_ack;
    uint8_t ack_expected_seq;
    uint8_t ack_expected_type;
    bool ack_received;

    // ACKを送信する内部関数
    bool send_ack_reply(uint8_t seq, uint8_t type) {
        uint8_t buffer[3];
        buffer[0] = 0; // Type 0 は ACK専用
        buffer[1] = seq;
        buffer[2] = type;
        return lora->SendFrame(lora->config, buffer, 3) == 0;
    }

public:
    LolaStruct(CLoRa* lora_instance) {
        lora = lora_instance;
        seq_num = 0;
        rx_cb = nullptr;
        waiting_ack = false;
        ack_received = false;
    }

    // 受信時のコールバック関数を登録
    void setCallback(LolaReceiveCallback cb) {
        rx_cb = cb;
    }

    // 任意の構造体を送信 (ACKを待たない)
    template<typename T>
    bool send(uint8_t packet_type, const T& data) {
        if (!lora || packet_type == 0) return false; // packet_type 0はACK用として予約
        
        uint16_t size = sizeof(T);
        uint8_t buffer[size + 2];
        
        buffer[0] = packet_type;
        buffer[1] = seq_num++;
        memcpy(&buffer[2], &data, size);
        
        return lora->SendFrame(lora->config, buffer, size + 2) == 0;
    }

    // [自動ID版] 構造体内に定義された T::ID を使って送信する
    template<typename T>
    bool send(const T& data) {
        return send(T::ID, data);
    }

    // 任意の構造体を送信し、相手からのACKを待機する
    template<typename T>
    bool send_ack(uint8_t packet_type, const T& data, uint32_t timeout_ms = 2000) {
        if (!lora || packet_type == 0) return false;
        
        uint8_t current_seq = seq_num;
        if (!send(packet_type, data)) {
            return false;
        }
        
        waiting_ack = true;
        ack_expected_seq = current_seq;
        ack_expected_type = packet_type;
        ack_received = false;
        
        uint32_t start_time = millis();
        while (millis() - start_time < timeout_ms) {
            // ACK待ちの間も他のパケットを受信処理する
            receive(); 
            
            if (ack_received) {
                waiting_ack = false;
                return true; // ACKを受信成功
            }
            delay(10);
        }
        
        waiting_ack = false;
        return false; // タイムアウト
    }

    // [自動ID版] 構造体内に定義された T::ID を使って送信し、ACKを待つ
    template<typename T>
    bool send_ack(const T& data, uint32_t timeout_ms = 2000) {
        return send_ack(T::ID, data, timeout_ms);
    }

    // パケットを受信する (loop関数内で定期的に呼び出す)
    void receive() {
        if (!lora) return;
        
        if (lora->ReceiveFrame(&lora->data) == 0) {
            if (lora->data.recv_data_len >= 2) {
                uint8_t p_type = lora->data.recv_data[0];
                uint8_t p_seq = lora->data.recv_data[1];
                
                if (p_type == 0) { 
                    // ACKパケットを受信した場合
                    if (lora->data.recv_data_len >= 3) {
                        uint8_t ack_p_type = lora->data.recv_data[2];
                        if (waiting_ack && p_seq == ack_expected_seq && ack_p_type == ack_expected_type) {
                            ack_received = true;
                        }
                    }
                } else {
                    // 通常のデータパケットを受信した場合 -> 自動でACKを返信
                    send_ack_reply(p_seq, p_type);
                    
                    // コールバック関数を呼び出してユーザーに処理を任せる
                    if (rx_cb) {
                        rx_cb(p_type, &lora->data.recv_data[2], lora->data.recv_data_len - 2, lora->data.rssi);
                    }
                }
            }
        }
    }
};

#endif // LOLA_STRUCT_H
