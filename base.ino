#include "esp32_e220900t22s_jp_lib_v2.h"
#include <Arduino.h>

// - PCターミナル(Serial)から入力した文字列をLoRaへ送信
// - LoRaから受信したバイト列をPCターミナルへ表示
//
//  LoRaモジュール側の無線設定(チャネル/SF/BW等)は事前に2台で一致させる

void setup() {
	SerialMon.begin(9600);
	delay(500);

	// LoRaをノーマルモード(M0=0, M1=0)
	pinMode(LoRa_ModeSettingPin_M0, OUTPUT);
	pinMode(LoRa_ModeSettingPin_M1, OUTPUT);
	digitalWrite(LoRa_ModeSettingPin_M0, LOW);
	digitalWrite(LoRa_ModeSettingPin_M1, LOW);

	// UART初期化
	SerialLoRa.begin(9600, SERIAL_8N1, LoRa_RxPin, LoRa_TxPin);

	SerialMon.println("ready: type text and press Enter");
}

void loop() {
	static char line[200] = {0};
	static int len = 0;

	// ---- PC -> LoRa ----
	while (SerialMon.available() > 0) {
		char c = (char)SerialMon.read();

		if (c == '\r') continue;

		if (c == '\n') {
			if (len > 0) {
				SerialLoRa.write((uint8_t *)line, len);
				SerialLoRa.flush();
				SerialMon.println("send succeeded.");
			}
			len = 0;
			memset(line, 0, sizeof(line));
			continue;
		}

		if (len < (int)sizeof(line) - 1) {
			line[len++] = c;
		}
	}

	// ---- LoRa -> PC ----
	if (SerialLoRa.available() > 0) {
		SerialMon.print("recv: ");
		while (SerialLoRa.available() > 0) {
			uint8_t b = (uint8_t)SerialLoRa.read();
			if (b >= 0x20 && b <= 0x7E) {
				SerialMon.print((char)b);
			} else {
				SerialMon.printf("\\x%02X", b);
			}
			delay(1);
		}
		SerialMon.println();
	}

	delay(2);
}

