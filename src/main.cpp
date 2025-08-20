#include <Arduino.h> 
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include "DHTesp.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// SSID dan Password WiFi
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// Telegram BOT
#define BOTtoken "7786397106:AAGWVzRzKrt8ZgHTvnek1ouKk6NkrywxLCw"
#define CHAT_ID "1380948390"

#define pin_merah 22
#define pin_putih 23
#define DHT_PIN 15

#define NTP_SERVER "pool.ntp.org"
#define UTC_OFFSET 7*3600
#define UTC_OFFSET_DST  0

#define WAKTU_KIRIM 30000 
#define WAKTU_BACA 1000 

#define SUHU_KRITIS 35.0
#define KELEMBABAN_KRITIS 40.0

float Suhu = 24.6;
float Kelembaban = 60.72;
bool sudahKirimAlert = false;
bool status_kipas = false;  
bool status_gudang_normal = true;  
bool kondisiKritis = false;

WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);

DHTesp dhtSensor;
AsyncWebServer server(80);

String Format(int Data);
String Waktu();
void KirimTelegram();
void KirimAlert(String pesan);
void BacaTelegram();
void aturStatusOtomatis();
void handleNewMessages(int numNewMessages);
void kipasControl(bool isOn);
void sirineControl(bool isOn);
void checkSuhuKelembaban();

// Fungsi untuk menambahkan header CORS ke semua respons
void addCORSHeaders(AsyncWebServerResponse *response) {
  response->addHeader("Access-Control-Allow-Origin", "http://localhost:5173");
  response->addHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  response->addHeader("Access-Control-Allow-Headers", "Content-Type");
}

void setup() {
  pinMode(pin_merah, OUTPUT);
  pinMode(pin_putih, OUTPUT);

  digitalWrite(pin_merah, LOW);
  digitalWrite(pin_putih, LOW);
  
  Serial.begin(115200);
  dhtSensor.setup(DHT_PIN, DHTesp::DHT22);
 
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Sedang menghubungkan ke WiFi..");
  }
  
  Serial.println(WiFi.localIP());
  Serial.println("Sinkronisasi Waktu di Internet :");
  configTime(UTC_OFFSET, UTC_OFFSET_DST, NTP_SERVER);
  
  String pesanAwal = "Sistem monitoring gudang telah dimulai! \nKetik /menu untuk melihat command\n" + Waktu();
  bot.sendMessage(CHAT_ID, pesanAwal, "");

  // ===================================
  // ENDPOINT SERVER WEB UNTUK DASHBOARD
  // ===================================

  // Penanganan untuk preflight OPTIONS request
  server.on("/", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response = request->beginResponse(204); // 204 No Content
    addCORSHeaders(response);
    request->send(response);
  });
  server.on("/api/ping", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response = request->beginResponse(204);
    addCORSHeaders(response);
    request->send(response);
  });
  server.on("/api/status", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response = request->beginResponse(204);
    addCORSHeaders(response);
    request->send(response);
  });
  server.on("/api/command", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response = request->beginResponse(204);
    addCORSHeaders(response);
    request->send(response);
  });

  // Endpoint untuk koneksi (ping)
  server.on("/api/ping", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "OK");
    addCORSHeaders(response);
    request->send(response);
  });

  // Endpoint untuk data status
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(128);
    doc["temperature"] = Suhu;
    doc["humidity"] = Kelembaban;
    doc["isWarehouseNormal"] = status_gudang_normal;
    doc["fanStatus"] = status_kipas;
    doc["sirenStatus"] = (digitalRead(pin_putih) == HIGH);

    String jsonResponse;
    serializeJson(doc, jsonResponse);
    
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", jsonResponse);
    addCORSHeaders(response);
    request->send(response);
  });
  
  // Endpoint untuk mengirim command
  server.on("/api/command", HTTP_POST, [](AsyncWebServerRequest *request){
    // Penanganan preflight request (OPTIONS) sudah ada di atas
    if (request->hasParam("command", true)) {
      String command = request->getParam("command", true)->value();
      if (command == "kipas_on") {
        kipasControl(true);
        request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Kipas dinyalakan\"}");
      }
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Command tidak ditemukan\"}");
    }
  });

  server.begin();
}

void loop() {
  static unsigned long KirimTerakhir = 0;
  static unsigned long BacaTerakhir = 0;
  static unsigned long CekTerakhir = 0;
  unsigned long t = millis();
  
  if(t - BacaTerakhir >= WAKTU_BACA) {
    BacaTerakhir = t;
    Serial.print(Waktu()); Serial.println("\nStatus: Refresh Telegram");
    BacaTelegram();
  }
  
  if(t - CekTerakhir >= 5000) {
    CekTerakhir = t;
    TempAndHumidity data = dhtSensor.getTempAndHumidity();
    Suhu = data.temperature;
    Kelembaban = data.humidity;
    checkSuhuKelembaban();
    aturStatusOtomatis();
  }
  
  if(t - KirimTerakhir >= WAKTU_KIRIM) {
    KirimTerakhir = t;
    Serial.print(Waktu()); Serial.println("Mengirim Pesan Ke Telegram");
    KirimTelegram();
    sudahKirimAlert = false;
  }
}

void aturStatusOtomatis() {
  bool kondisiBaru = (Suhu >= SUHU_KRITIS || Kelembaban <= KELEMBABAN_KRITIS);
  
  if (kondisiBaru != kondisiKritis) {
    kondisiKritis = kondisiBaru;
    
    if (kondisiKritis) {
      kipasControl(true);
      sirineControl(true);
      status_gudang_normal = false;
      Serial.println("Kondisi kritis terdeteksi! Kipas ON, Sirine ON");
    } else {
      kipasControl(false);
      sirineControl(false);
      status_gudang_normal = true;
      Serial.println("Kondisi normal! Kipas OFF, Sirine OFF");
    }
  }
}

String Format(int Data) {
  String s = "00" + String(Data);
  return s.substring(s.length() - 2);
}

String Waktu() {
  struct tm w;
  if (!getLocalTime(&w))
    return "Sinkronisasi waktu gagal, Mengsinkronisasi ulang.....";
  return "Waktu: " + Format(w.tm_hour) + ":" + Format(w.tm_min) + ":" + Format(w.tm_sec) + " " +
    "\nTanggal: " + String(w.tm_mday) + "-" + String(w.tm_mon + 1) + "-" + String(w.tm_year + 1900);
}

void checkSuhuKelembaban() {
  if (!sudahKirimAlert) {
    if (Suhu >= SUHU_KRITIS) {
      sudahKirimAlert = true;
      String alertMsg = "⚠️ PERINGATAN: Suhu gudang terlalu tinggi!⚠️\n" + Waktu() + 
                      "\nSuhu saat ini: " + String(Suhu) + "°C" +
                      "\nAmbang batas: " + String(SUHU_KRITIS) + "°C" +
                      "\nKipas dan sirine telah dinyalakan secara otomatis!";
      KirimAlert(alertMsg);
    }
    else if (Kelembaban <= KELEMBABAN_KRITIS) {
      sudahKirimAlert = true;
      String alertMsg = "⚠️ PERINGATAN: Kelembaban gudang terlalu rendah!⚠️\n" + Waktu() + 
                      "\nKelembaban saat ini: " + String(Kelembaban) + "%" +
                      "\nAmbang batas: " + String(KELEMBABAN_KRITIS) + "%" +
                      "\nKipas dan sirine telah dinyalakan secara otomatis!";
      KirimAlert(alertMsg);
    }
  }
}

void KirimAlert(String pesan) {
  Serial.println("Mengirim Alert:");
  Serial.println(pesan);
  bot.sendMessage(CHAT_ID, pesan, "");
}

void KirimTelegram() {
  String msg = "Status suhu dan kelembapan di gudang saat ini:";
  msg += "\n" + Waktu();
  msg += "\nSuhu: " + String(Suhu) + "°C";
  msg += "\nKelembaban: " + String(Kelembaban) + "%";
  msg += "\nStatus Gudang: " + String(status_gudang_normal ? "Normal" : "⚠️Kritis⚠️");
  msg += "\nStatus Kipas: " + String(status_kipas ? "ON" : "OFF");
  msg += "\nStatus Sirine: " + String(digitalRead(pin_putih) == HIGH ? "ON" : "OFF");
  Serial.println(msg);
  bot.sendMessage(CHAT_ID, msg, "");
}

void BacaTelegram() {
  int banyakPesan = bot.getUpdates(bot.last_message_received + 1);
  
  while(banyakPesan) {
    Serial.println("got response from user");
    handleNewMessages(banyakPesan);
    banyakPesan = bot.getUpdates(bot.last_message_received + 1);
  }
}

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    
    String chat_id = String(bot.messages[i].chat_id);
    if (chat_id != CHAT_ID) {
      bot.sendMessage(chat_id, "chat id anda: " + String(chat_id), "");
      continue;
    }

    String text = bot.messages[i].text;
    Serial.println(text);

    String from_name = bot.messages[i].from_name;

    if (text == "/menu") {
      String welcome = "Selamat Datang, " + from_name + ".\n";
      welcome += "Berikut list command untuk kontrol:\n";
      welcome += "/cek_ambang - untuk melihat ambang batas kritis\n";
      bot.sendMessage(chat_id, welcome, "");
    }

    if (text == "/cek_ambang") {
      String ambang = "Ambang batas kritis:\n";
      ambang += "Suhu: ≥ " + String(SUHU_KRITIS) + "°C\n";
      ambang += "Kelembaban: ≤ " + String(KELEMBABAN_KRITIS) + "%";
      bot.sendMessage(chat_id, ambang, "");
    }
  }
}

void kipasControl(bool isOn) {
  if(isOn) {
    digitalWrite(pin_merah, HIGH);
    status_kipas = true;
  }
  else {
    digitalWrite(pin_merah, LOW);
    status_kipas = false;
  }
}

void sirineControl(bool isOn) {
  if (isOn) {
    digitalWrite(pin_putih, HIGH);
  } else {
    digitalWrite(pin_putih, LOW);
  }
}