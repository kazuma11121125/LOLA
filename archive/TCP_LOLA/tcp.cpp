#include "tcp.h"

#include <Arduino.h>
#include <time.h>

ReliableLoRaLink::ReliableLoRaLink(HardwareSerial &serial, HardwareSerial &mon)
	: m_serial(serial),
	  m_mon(mon),
	  m_ackTimeoutMs(1500),
	  m_maxRetry(5),
	  m_waitingAck(false),
	  m_ackMatched(false),
	  m_expectedAckId{0},
	  m_lineBuf{0},
	  m_lineLen(0),
	  m_rx{0},
	  m_rxPos(0) {}

void ReliableLoRaLink::begin() {
	m_mon.begin(9600);
	delay(500);

	// LoRaをノーマルモード(M0=0, M1=0)
	pinMode(LoRa_ModeSettingPin_M0, OUTPUT);
	pinMode(LoRa_ModeSettingPin_M1, OUTPUT);
	digitalWrite(LoRa_ModeSettingPin_M0, LOW);
	digitalWrite(LoRa_ModeSettingPin_M1, LOW);

	// UARTを初期化
	m_serial.begin(9600, SERIAL_8N1, LoRa_RxPin, LoRa_TxPin);

	m_mon.println("準備完了: 文字列を入力して Enter を押してください");
}

void ReliableLoRaLink::poll() {
	poll_serial_input_and_send();
	pump_receive();
}

void ReliableLoRaLink::make_id_string(char *id_str, size_t size) {
	uint8_t id[4];
	// 現在時刻をIDとして使用
	uint32_t now = (uint32_t)time(nullptr);
	id[0] = (now >> 24) & 0xFF;
	id[1] = (now >> 16) & 0xFF;
	id[2] = (now >> 8) & 0xFF;
	id[3] = now & 0xFF;
	snprintf(id_str, size, "%02X%02X%02X%02X", id[0], id[1], id[2], id[3]);
}

void ReliableLoRaLink::fill_data(Data_t &d, const char *text, uint16_t senderFlag, const char *idOverride) {
	memset(&d, 0, sizeof(Data_t));
    // IDを生成してセットします。idOverrideが指定されている場合はそれを使用し、そうでない場合はmake_id_stringで生成したIDを使用します。
	if (idOverride == nullptr) {
		make_id_string(d.id, sizeof(d.id));
	} else {
		strncpy(d.id, idOverride, sizeof(d.id) - 1);
		d.id[sizeof(d.id) - 1] = '\0';
	}
    // 送信者フラグをセット
	d.is_Sender = senderFlag;

	size_t n = strlen(text);
	if (n > sizeof(d.payload)) {
		n = sizeof(d.payload);
	}
	d.length = static_cast<uint16_t>(n);
	memcpy(d.payload, text, n);
}

bool ReliableLoRaLink::send_data_struct(const Data_t &data) {
    // データ構造をUARTで送信します。送信が成功したかどうかを返します。
	size_t sent = m_serial.write(reinterpret_cast<const uint8_t *>(&data), sizeof(Data_t));
	m_serial.flush();
	return sent == sizeof(Data_t);
}

bool ReliableLoRaLink::parse_one_data(Data_t &out) {
	while (m_serial.available() > 0) {
		m_rx[m_rxPos++] = static_cast<uint8_t>(m_serial.read());
		if (m_rxPos == sizeof(Data_t)) {
			memcpy(&out, m_rx, sizeof(Data_t));
			m_rxPos = 0;
			return true;
		}
	}

	return false;
}

void ReliableLoRaLink::send_ack(const char *id) {
	Data_t ackData;
	fill_data(ackData, "", 0, id);
	send_data_struct(ackData);
    // ACKはペイロードが空で、IDが受信したデータと同じものを送ります
}

bool ReliableLoRaLink::is_valid_data(const Data_t &d) const {
	return d.length <= sizeof(d.payload);
    // データ長がペイロードのサイズを超えていないかをチェックします
}

bool ReliableLoRaLink::is_ack_of_waiting_packet(const Data_t &d) const {
	return m_waitingAck && d.length == 0 && strncmp(d.id, m_expectedAckId, sizeof(d.id)) == 0;
    // ACKはペイロードが空で、IDが期待しているものと一致する必要があります
}

void ReliableLoRaLink::handle_data(const Data_t &data) {
	if (!is_valid_data(data)) {
		m_mon.println("破棄: データ長が不正です");
		return;
	}

	if (is_ack_of_waiting_packet(data)) {
		m_ackMatched = true;
		return;
	}

	m_mon.print("受信 id=");
	m_mon.print(data.id);
	m_mon.print(" 長さ=");
	m_mon.println(data.length);

	m_mon.print("ペイロード: ");
	m_mon.write(reinterpret_cast<const uint8_t *>(data.payload), data.length);
	m_mon.println();

	send_ack(data.id);
}

void ReliableLoRaLink::pump_receive() {
	Data_t data;
	while (parse_one_data(data)) {
		handle_data(data);
	}
}

bool ReliableLoRaLink::sendText(const char *text) {
	Data_t payload;
	fill_data(payload, text, 1);

	strncpy(m_expectedAckId, payload.id, sizeof(m_expectedAckId) - 1);
	m_expectedAckId[sizeof(m_expectedAckId) - 1] = '\0';
	m_waitingAck = true;

	for (uint8_t attempt = 1; attempt <= m_maxRetry; ++attempt) {
		m_ackMatched = false;

		if (!send_data_struct(payload)) {
			m_mon.println("送信失敗: UART書き込みエラー");
			continue;
		}

		m_mon.print("送信試行 ");
		m_mon.print(attempt);
		m_mon.print("/");
		m_mon.print(m_maxRetry);
		m_mon.print(" id=");
		m_mon.print(payload.id);
		m_mon.println();

		uint32_t start = millis();
		while (millis() - start < m_ackTimeoutMs) {
			pump_receive();
			if (m_ackMatched) {
				m_waitingAck = false;
				m_mon.println("ACK受信: 送信成功");
				return true;
			}
			delay(2);
		}
		m_mon.println("ACKタイムアウト -> 再送");
	}

	m_waitingAck = false;
	m_mon.println("送信失敗: 最大再送回数に到達");
	return false;
}

void ReliableLoRaLink::poll_serial_input_and_send() {
	while (m_mon.available() > 0) {
		// 1文字ずつ読み取る
		char c = static_cast<char>(m_mon.read());
		if (c == '\r') {
			continue;
		}
		// 改行が来たら、これまでの入力を送信する
		if (c == '\n') {
			if (m_lineLen > 0) {
				m_lineBuf[m_lineLen] = '\0';
				sendText(m_lineBuf);
				m_lineLen = 0;
			}
			continue;
		}
		// それ以外の文字はバッファに追加する
		if (m_lineLen < sizeof(m_lineBuf) - 1) {
			m_lineBuf[m_lineLen++] = c;
		}
	}
}
