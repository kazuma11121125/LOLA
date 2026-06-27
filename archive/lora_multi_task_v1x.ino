#include "esp32_e220900t22s_jp_lib_v2.h"
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>


//const int ledPin = 2;

int ReceiveComplete;

CLoRa lora;
WebServer server(80);

typedef struct {
  char text[200];
} SendMessage_t;

QueueHandle_t gSendQueue = NULL;
SemaphoreHandle_t gStateMutex = NULL;

String gLastRecvDecoded = "";
String gLastRecvHex = "";
int gLastRssi = 0;
uint32_t gRecvCount = 0;
String gLastSendResult = "none";

const char *WIFI_AP_SSID = "LoRa-Bridge_2";
const char *WIFI_AP_PASS = "password1234";
//struct LoRaConfigItem_t config;
//struct RecvFrameE220900T22SJP_t data;

/** prototype declaration **/
void LoRaRecvTask(void *pvParameters);
void LoRaSendTask(void *pvParameters);
void ReadDataFromConsole(char *msg, int max_msg_len);
void PollConsoleInputAndQueue(void);
void SetupWiFiAndWeb(void);
void HandleRoot(void);
void HandleSend(void);
void HandleStatus(void);
String EscapeJson(const String &s);

static void CreateLoRaTask(TaskFunction_t fn, const char *name, uint32_t stack, UBaseType_t prio) {
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_FREERTOS_UNICORE)
  xTaskCreate(fn, name, stack, NULL, prio, NULL);
#else
  xTaskCreateUniversal(fn, name, stack, NULL, prio, NULL, APP_CPU_NUM);
#endif
}

void setup() {
  // put your setup code here, to run once:
  SerialMon.begin(9600);
  delay(1000); // SerialMon init wait
  SerialMon.println();

  //  pinMode(ledPin, OUTPUT);



  // LoRa設定値の読み込み
  if (lora.LoadConfigSetting(CONFIG_FILENAME, lora.config)) {
    SerialMon.printf("Loading Configfile failed. The default value is set.\n");
  } else {
    SerialMon.printf("Loading Configfile succeeded.\n");
  }

  // E220-900T22S(JP)へのLoRa初期設定
  if (lora.InitLoRaModule(lora.config)) {
    SerialMon.printf("init error\n");
    for (;;) {
      //
      //      digitalWrite(ledPin,HIGH);
      //      delay(1000);
      //      digitalWrite(ledPin,LOW);
    }
    return;
  }

  // ノーマルモード(M0=0,M1=0)へ移行する
  lora.SwitchToNormalMode();

  ReceiveComplete = 0;
  gSendQueue = xQueueCreate(8, sizeof(SendMessage_t));
  gStateMutex = xSemaphoreCreateMutex();
  SetupWiFiAndWeb();

  // マルチタスク
  CreateLoRaTask(LoRaRecvTask, "LoRaRecvTask", 8192, 1);
  CreateLoRaTask(LoRaSendTask, "LoRaSendTask", 8192, 1);
}

void loop() {
  server.handleClient();
  delay(2);
}

void LoRaRecvTask(void *pvParameters) {

  while (1) {
    if (ReceiveComplete) {
      delay(10);                   // be suitable
      continue;
    }

    if (lora.ReceiveFrame(&lora.data) == 0) {
      String decoded = "";
      String hex = "";

      for (int i = 0; i < lora.data.recv_data_len; i++) {
        uint8_t b = lora.data.recv_data[i];
        if (b >= 0x20 && b <= 0x7E) {
          decoded += (char)b;
        } else {
          char tmpEsc[5];
          snprintf(tmpEsc, sizeof(tmpEsc), "\\x%02X", b);
          decoded += tmpEsc;
        }

        char tmpHex[4];
        snprintf(tmpHex, sizeof(tmpHex), "%02x ", b);
        hex += tmpHex;
      }

      SerialMon.printf("\nrecv data:\n");
      SerialMon.printf("decoded: ");
      SerialMon.printf("%s", decoded.c_str());
      SerialMon.printf("\n");
      SerialMon.printf("hex dump:\n");
      SerialMon.printf("%s", hex.c_str());
      SerialMon.printf("\n");
      SerialMon.printf("RSSI: %d dBm\n", lora.data.rssi);
      SerialMon.printf("\n");

      if (gStateMutex && xSemaphoreTake(gStateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        gLastRecvDecoded = decoded;
        gLastRecvHex = hex;
        gLastRssi = lora.data.rssi;
        gRecvCount++;
        xSemaphoreGive(gStateMutex);
      }

      SerialMon.flush();
    }

    ReceiveComplete = 0;

    delay(1);
  }
}

void LoRaSendTask(void *pvParameters) {
  SendMessage_t msg;

  while (1) {
    PollConsoleInputAndQueue();

    if (xQueueReceive(gSendQueue, &msg, pdMS_TO_TICKS(20)) != pdTRUE) {
      delay(1);
      continue;
    }

    if (lora.SendFrame(lora.config, (uint8_t *)msg.text, strlen(msg.text)) == 0) {
      SerialMon.printf("send succeeded.\n");
      SerialMon.printf("\n");
      if (gStateMutex && xSemaphoreTake(gStateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        gLastSendResult = "ok: ";
        gLastSendResult += msg.text;
        xSemaphoreGive(gStateMutex);
      }
    } else {
      SerialMon.printf("send failed.\n");
      SerialMon.printf("\n");
      if (gStateMutex && xSemaphoreTake(gStateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        gLastSendResult = "failed";
        xSemaphoreGive(gStateMutex);
      }
    }

    SerialMon.flush();

    delay(1);
  }
}

void PollConsoleInputAndQueue(void) {
  static char line[200] = {0};
  static int len = 0;

  while (SerialMon.available() > 0) {
    char incoming = SerialMon.read();
    if (incoming == 0x00 || incoming > 0x7F) {
      continue;
    }

    if (incoming == '\r') {
      continue;
    }

    if (incoming == '\n') {
      if (len > 0) {
        line[len] = '\0';
        SendMessage_t out = {0};
        strncpy(out.text, line, sizeof(out.text) - 1);
        xQueueSend(gSendQueue, &out, 0);
      }
      len = 0;
      memset(line, 0, sizeof(line));
      continue;
    }

    if (len < (int)sizeof(line) - 1) {
      line[len++] = incoming;
    }
  }
}

void SetupWiFiAndWeb(void) {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  SerialMon.printf("WiFi AP started: SSID=%s, IP=%s\n", WIFI_AP_SSID, ip.toString().c_str());

  server.on("/", HTTP_GET, HandleRoot);
  server.on("/api/send", HTTP_POST, HandleSend);
  server.on("/api/status", HTTP_GET, HandleStatus);
  server.begin();
  SerialMon.printf("Web server started on port 80\n");
}

void HandleRoot(void) {
  const char html[] = R"HTML(
<!doctype html>
<html lang="ja">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>LoRa Bridge Monitor</title>
  <style>
    body { font-family: sans-serif; margin: 24px; background: #0f172a; color: #e2e8f0; }
    .card { background: #1e293b; padding: 16px; border-radius: 12px; margin-bottom: 12px; }
    input { width: 70%; padding: 8px; }
    button { padding: 8px 12px; margin-left: 8px; }
    pre { white-space: pre-wrap; word-break: break-word; }
  </style>
</head>
<body>
  <h2>LoRa Bridge (ESP32-C3)</h2>
  <div class="card">
    <input id="msg" placeholder="send message" />
    <button onclick="sendMsg()">送信</button>
    <div id="sendResult"></div>
  </div>
  <div class="card">
    <div>受信回数: <span id="count">0</span></div>
    <div>RSSI: <span id="rssi">-</span> dBm</div>
    <div>Last Send: <span id="lastSend">-</span></div>
    <h4>Decoded</h4>
    <pre id="decoded">-</pre>
    <h4>Hex</h4>
    <pre id="hex">-</pre>
  </div>
  <script>
    async function sendMsg() {
      const msg = document.getElementById('msg').value;
      const res = await fetch('/api/send', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: 'msg=' + encodeURIComponent(msg)
      });
      const data = await res.json();
      document.getElementById('sendResult').textContent = data.message || '';
    }

    async function refresh() {
      const res = await fetch('/api/status');
      const s = await res.json();
      document.getElementById('count').textContent = s.recvCount;
      document.getElementById('rssi').textContent = s.rssi;
      document.getElementById('decoded').textContent = s.decoded;
      document.getElementById('hex').textContent = s.hex;
      document.getElementById('lastSend').textContent = s.lastSend;
    }
    setInterval(refresh, 1000);
    refresh();
  </script>
</body>
</html>
  )HTML";

  server.send(200, "text/html", html);
}

void HandleSend(void) {
  String msg = "";
  if (server.hasArg("msg")) {
    msg = server.arg("msg");
  } else if (server.hasArg("plain")) {
    msg = server.arg("plain");
  }

  msg.trim();
  if (msg.length() == 0) {
    server.send(400, "application/json", "{\"ok\":false,\"message\":\"empty message\"}");
    return;
  }
  if (msg.length() > 180) {
    msg = msg.substring(0, 180);
  }

  SendMessage_t out = {0};
  strncpy(out.text, msg.c_str(), sizeof(out.text) - 1);
  if (xQueueSend(gSendQueue, &out, 0) != pdTRUE) {
    server.send(503, "application/json", "{\"ok\":false,\"message\":\"send queue full\"}");
    return;
  }

  server.send(200, "application/json", "{\"ok\":true,\"message\":\"queued\"}");
}

String EscapeJson(const String &s) {
  String out = "";
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\\') out += "\\\\";
    else if (c == '"') out += "\\\"";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else out += c;
  }
  return out;
}

void HandleStatus(void) {
  String decoded;
  String hex;
  int rssi;
  uint32_t recvCount;
  String lastSend;

  if (gStateMutex && xSemaphoreTake(gStateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    decoded = gLastRecvDecoded;
    hex = gLastRecvHex;
    rssi = gLastRssi;
    recvCount = gRecvCount;
    lastSend = gLastSendResult;
    xSemaphoreGive(gStateMutex);
  } else {
    decoded = "";
    hex = "";
    rssi = 0;
    recvCount = 0;
    lastSend = "mutex-timeout";
  }

  String json = "{";
  json += "\"recvCount\":" + String(recvCount) + ",";
  json += "\"rssi\":" + String(rssi) + ",";
  json += "\"decoded\":\"" + EscapeJson(decoded) + "\",";
  json += "\"hex\":\"" + EscapeJson(hex) + "\",";
  json += "\"lastSend\":\"" + EscapeJson(lastSend) + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

void ReadDataFromConsole(char *msg, int max_msg_len) {
  int len = 0;
  char *start_p = msg;

  while (len < max_msg_len) {
    if (SerialMon.available() > 0) {
      char incoming_byte = SerialMon.read();
      if (incoming_byte == 0x00 || incoming_byte > 0x7F)
        continue;
      *(start_p + len) = incoming_byte;
      // 最短で3文字(1文字 + CR LF)
      if (incoming_byte == 0x0a && len >= 2 && (*(start_p + len - 1)) == 0x0d) {
        break;
      }
      len++;
    }
    delay(1);
  }

  // msgからCR LFを削除
  len = strlen(msg);
  for (int i = 0; i < len; i++) {
    if (msg[i] == 0x0D || msg[i] == 0x0A) {
      msg[i] = '\0';
    }
  }
}
