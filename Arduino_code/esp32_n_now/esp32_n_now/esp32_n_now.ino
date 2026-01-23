#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

// 这里填你刚才从串口读到的 S3 真实 MAC
uint8_t S3_MAC[] = {0x20, 0x6E, 0xF1, 0xD6, 0x15, 0x7C}; 

void OnSent(const uint8_t *mac, esp_now_send_status_t st) {
  Serial.printf(" >> 送达状态: %s\n", st == ESP_NOW_SEND_SUCCESS ? "对方收到ACK" : "对方未响应");
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  // 强制锁定信道 1 (最稳)
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) return;
  esp_now_register_send_cb((esp_now_send_cb_t)OnSent);

  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, S3_MAC, 6);
  p.channel = 1; 
  p.encrypt = false;
  esp_now_add_peer(&p);
}

void loop() {
  char msg[] = "test123";
  esp_now_send(S3_MAC, (uint8_t*)msg, sizeof(msg));
  delay(1000);
}