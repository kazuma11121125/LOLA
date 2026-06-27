#include "tcp.h"

ReliableLoRaLink g_link(SerialLoRa, SerialMon);

void setup() {
    g_link.begin();
}

void loop() {
    g_link.poll();
}