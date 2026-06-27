#ifndef TCP_H
#define TCP_H

#include "esp32_e220900t22s_jp_lib_v2.h"

struct Data_t {
    char id[9]; // make_id_stringで生成したIDを格納する文字列
    uint16_t is_Sender; // 送信者なら1、受信者なら0
    uint16_t length; // payloadの長さ
    char payload[100]; // 送受信するデータを格納する文字列
};

class ReliableLoRaLink {
public:
    explicit ReliableLoRaLink(HardwareSerial &serial, HardwareSerial &mon);

    void begin();
    void poll();
    bool sendText(const char *text);

private:
    void make_id_string(char *id_str, size_t size);
    void fill_data(Data_t &d, const char *text, uint16_t senderFlag, const char *idOverride = nullptr);
    bool send_data_struct(const Data_t &data);
    bool parse_one_data(Data_t &out);
    void send_ack(const char *id);
    bool is_valid_data(const Data_t &d) const;
    bool is_ack_of_waiting_packet(const Data_t &d) const;
    void handle_data(const Data_t &data);
    void pump_receive();
    void poll_serial_input_and_send();

    HardwareSerial &m_serial;
    HardwareSerial &m_mon;
    uint32_t m_ackTimeoutMs;
    uint8_t m_maxRetry;

    bool m_waitingAck;
    bool m_ackMatched;
    char m_expectedAckId[9];

    char m_lineBuf[101];
    size_t m_lineLen;

    uint8_t m_rx[sizeof(Data_t)];
    size_t m_rxPos;
};

#endif
