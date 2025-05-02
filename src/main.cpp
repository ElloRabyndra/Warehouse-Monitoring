/*
  Program : SISTEM PEMANTAUAN SUHU DAN KELEMBABAN BERBASIS INTERNET OF THINGS UNTUK OPTIMALISASI PENYIMPANAN GUDANG
  Dibuat Oleh : Kelompok 7
  Tanggal : 30 April 2025
  Modifikasi : Penggunaan relay untuk kontrol kipas dan indikator status gudang
*/

#include <Arduino.h>  
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include "DHTesp.h"

// Definisi pin yang digunakan
#define DHT_PIN 15
const int relay = 26;

// SSID dan Password WiFi
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// Telegram BOT
#define BOTtoken "7786397106:AAGWVzRzKrt8ZgHTvnek1ouKk6NkrywxLCw"
#define CHAT_ID "1380948390"

#define NTP_SERVER "pool.ntp.org"
#define UTC_OFFSET 7*3600 // untuk waktu WIB UTC+7
#define UTC_OFFSET_DST 0

#define WAKTU_KIRIM 30000 // ms, 30 detik waktu interval setiap pengiriman
#define WAKTU_BACA 1000 // ms, 1 detik waktu interval setiap refresh

// inisialisasi Ambang batas suhu dan kelembaban
#define SUHU_KRITIS 35.0 // °C
#define KELEMBABAN_KRITIS 40.0 // %

bool status_kipas = false;  // Status kipas (LED merah melalui relay)
bool status_gudang_normal = true;  // Status gudang (LED putih melalui relay)
float Suhu = 24.6;
float Kelembaban = 60.72;
bool sudahKirimAlert = false;  // Flag untuk menghindari pengiriman notifikasi berulang
bool kondisiKritis = false;    // Flag untuk status kondisi kritis

WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);

DHTesp dhtSensor;

String Format(int Data);
String Waktu();
void KirimTelegram();
void KirimAlert(String pesan);
void BacaTelegram();
void handleNewMessages(int numNewMessages);
void kipasControl(bool isOn);
void gudangStatusControl(bool isNormal);
void checkSuhuKelembaban();
void aturStatusOtomatis();

void setup() {
  // atur mode pin relay sebagai output
  pinMode(relay, OUTPUT);
  
  // Serial monitor
  Serial.begin(115200);
  dhtSensor.setup(DHT_PIN, DHTesp::DHT22);
 
  // Set status awal: gudang normal, kipas mati
  gudangStatusControl(true);
  kipasControl(false);

  // Hubungkan ke Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Sedang menghubungkan ke WiFi..");
  }
  
  // IP Address
  Serial.println(WiFi.localIP());
  Serial.println("Sedang menyinkronisasi Waktu di Internet....");
  configTime(UTC_OFFSET, UTC_OFFSET_DST, NTP_SERVER);
  
  // Kirim pesan awal ke Telegram bahwa sistem telah dimulai
  String pesanAwal = "Sistem monitoring gudang telah dimulai! \nKetik /start untuk melihat command\n" + Waktu();
  bot.sendMessage(CHAT_ID, pesanAwal, "");
}

void loop() {
  static unsigned long KirimTerakhir = 0;
  static unsigned long BacaTerakhir = 0;
  static unsigned long CekTerakhir = 0;
  unsigned long t = millis();
  
  // Refresh pesan Telegram
  if(t - BacaTerakhir >= WAKTU_BACA) {
    BacaTerakhir = t;
    Serial.print(Waktu()); Serial.println("\n Status: Refresh Telegram");
    BacaTelegram();
  }
  
  // Cek suhu dan kelembaban
  if(t - CekTerakhir >= 5000) {  // Cek setiap 5 detik
    CekTerakhir = t;
    TempAndHumidity data = dhtSensor.getTempAndHumidity();
    Suhu = data.temperature;
    Kelembaban = data.humidity;
    checkSuhuKelembaban();
    aturStatusOtomatis();  // Atur status secara otomatis berdasarkan kondisi
  }
  
  // Kirim update reguler ke Telegram
  if(t - KirimTerakhir >= WAKTU_KIRIM) {
    KirimTerakhir = t;
    Serial.print(Waktu()); Serial.println("\n Status: Mengirim ke Telegram");
    KirimTelegram();
    // Reset flag setelah waktu tertentu agar alert dapat dikirim kembali
    sudahKirimAlert = false;
  }  
}

// Fungsi untuk mengatur status berdasarkan kondisi suhu dan kelembaban
void aturStatusOtomatis() {
  bool kondisiBaru = (Suhu >= SUHU_KRITIS || Kelembaban <= KELEMBABAN_KRITIS);
  
  if (kondisiBaru != kondisiKritis) {
    kondisiKritis = kondisiBaru;
    
    if (kondisiKritis) {
      // Kondisi kritis: Kipas ON (LED merah via relay), status gudang tidak normal (LED putih mati)
      kipasControl(true); // Nyalakan kipas
      gudangStatusControl(false); // Tandai gudang kritis
      Serial.println("Kondisi kritis terdeteksi! Kipas ON");
    } else {
      // Kondisi normal: Kipas OFF, status gudang normal (LED putih nyala)
      kipasControl(false); // Matikan kipas
      gudangStatusControl(true); // Tandai gudang normal
      Serial.println("Kondisi normal! Kipas OFF");
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
    return "Connection Err";

  return "Waktu: " + Format(w.tm_hour) + ":" + Format(w.tm_min) + ":" + Format(w.tm_sec) + " " +
         "\nTanggal: " + String(w.tm_mday) + "-" + String(w.tm_mon + 1) + "-" + String(w.tm_year + 1900);
}

// Fungsi untuk memeriksa suhu dan kelembaban
void checkSuhuKelembaban() {
  if (!sudahKirimAlert) {
    if (Suhu >= SUHU_KRITIS) {
      String alertMsg = "⚠️ PERINGATAN: Suhu gudang terlalu tinggi!⚠️\n" + Waktu() + 
                      "\nSuhu saat ini: " + String(Suhu) + "°C" +
                      "\nAmbang batas: " + String(SUHU_KRITIS) + "°C" +
                      "\nKipas telah dinyalakan secara otomatis!";
      KirimAlert(alertMsg);
      kipasControl(true);
      gudangStatusControl(false);
      sudahKirimAlert = true;
    }
    else if (Kelembaban <= KELEMBABAN_KRITIS) {
      String alertMsg = "⚠️ PERINGATAN: Kelembaban gudang terlalu rendah!⚠️\n" + Waktu() + 
                      "\nKelembaban saat ini: " + String(Kelembaban) + "%" +
                      "\nAmbang batas: " + String(KELEMBABAN_KRITIS) + "%" +
                      "\nKipas telah dinyalakan secara otomatis!";
      KirimAlert(alertMsg);
      kipasControl(true);
      gudangStatusControl(false);
      sudahKirimAlert = true;
    }
  }
}

// Khusus untuk notifikasi/alert
void KirimAlert(String pesan) {
  Serial.println("Mengirim Alert:");
  Serial.println(pesan);
  bot.sendMessage(CHAT_ID, pesan, "");
}

// Akan dijalankan tiap WAKTU_KIRIM sekali
void KirimTelegram() {
  String msg = "Status suhu dan kelembapan di gudang saat ini:";
  msg += "\n" + Waktu();
  msg += "\nSuhu: " + String(Suhu) + "°C";
  msg += "\nKelembaban: " + String(Kelembaban) + "%";
  msg += "\nStatus Gudang: " + String(status_gudang_normal ? "Normal" : "⚠️Kritis⚠️");
  msg += "\nStatus Kipas: " + String(status_kipas ? "ON" : "OFF");
  Serial.println(msg);
  bot.sendMessage(CHAT_ID, msg, "");
}

// Akan dijalankan tiap WAKTU_BACA sekali
void BacaTelegram() {
  int banyakPesan = bot.getUpdates(bot.last_message_received + 1);
  
  while(banyakPesan) {
    Serial.println("got response from user");
    handleNewMessages(banyakPesan);
    banyakPesan = bot.getUpdates(bot.last_message_received + 1);
  }
}

// Memproses pesan yang diterima
void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    
    // Cek Chat ID
    String chat_id = String(bot.messages[i].chat_id);
    if (chat_id != CHAT_ID) {
      bot.sendMessage(chat_id, "Anda salah memasukkan chat_id, chat id anda: " + String(chat_id), "");
      continue;
    }

    // Terima pesan dari telegram
    String text = bot.messages[i].text;
    Serial.println(text);

    String from_name = bot.messages[i].from_name;

    if (text == "/start") {
      String welcome = "Selamat Datang, " + from_name + ".\n";
      welcome += "Anda bisa cek command di menu atau berikut list command untuk kontrol suhu dan kelembapan gudang:\n\n";
      welcome += "/cek_suhu untuk cek suhu pada gudang \n";
      welcome += "/cek_kelembapan untuk cek kelembapan pada gudang \n";
      welcome += "/kipas_on untuk Menghidupkan Kipas \n";
      welcome += "/kipas_off untuk Mematikan kipas \n";
      welcome += "/status_kipas mendapatkan status kipas\n";
      welcome += "/status_gudang mendapatkan status gudang\n";
      welcome += "/cek_ambang untuk melihat ambang batas kritis yang ditetapkan\n";
      bot.sendMessage(chat_id, welcome, "");
    }

    if (text == "/cek_suhu") {
      bot.sendMessage(chat_id, "Waktu : " + String(Waktu()) + "\nSuhu gudang : " + String(Suhu) + "°C");
    }

    if (text == "/cek_kelembapan") {
      bot.sendMessage(chat_id, "Waktu : " + String(Waktu()) + "\nKelembaban gudang : " + String(Kelembaban) + "%");
    }

    if (text == "/cek_ambang") {
      String ambang = "Ambang batas kritis:\n";
      ambang += "Suhu: ≥ " + String(SUHU_KRITIS) + "°C\n";
      ambang += "Kelembaban: ≤ " + String(KELEMBABAN_KRITIS) + "%";
      bot.sendMessage(chat_id, ambang, "");
    }

    // Perintah manual tetap bisa dijalankan tapi akan dioverride oleh mode otomatis
    // pada pengecekan suhu dan kelembaban berikutnya
    if (text == "/kipas_on") {
      digitalWrite(relay, HIGH);  // Mengaktifkan relay untuk menyalakan kipas (LED merah)
      status_kipas = true;
      bot.sendMessage(chat_id, "Kipas sukses diganti ke status ON", "");
      kipasControl(true);
    }
    
    if (text == "/kipas_off") {
      digitalWrite(relay, LOW);   // Mematikan relay untuk mematikan kipas (LED merah)
      status_kipas = false;
      bot.sendMessage(chat_id, "Kipas sukses diganti ke status OFF", "");
      kipasControl(false);
    }
    
    if (text == "/status_kipas") {
      bot.sendMessage(chat_id, status_kipas ? "Kipas ON" : "Kipas OFF", "");
    }
    
    if (text == "/status_gudang") {
      bot.sendMessage(chat_id, status_gudang_normal ? "Status Gudang Normal" : "⚠️Status Gudang Kritis⚠️", "");
    }
  }
}

// Fungsi untuk mengontrol kipas (LED merah) melalui relay
void kipasControl(bool isOn) {
  if(isOn) {
    digitalWrite(relay, HIGH);  // Mengaktifkan relay untuk menyalakan kipas (LED merah)
    status_kipas = true;
  }
  else {
    digitalWrite(relay, LOW);   // Mematikan relay untuk mematikan kipas (LED merah)
    status_kipas = false;
  }
}

// Fungsi untuk mengontrol status gudang (LED putih)
void gudangStatusControl(bool isNormal) {
  status_gudang_normal = isNormal;
  // Status gudang dikontrol melalui relay yang sama
  // Status ditandai melalui variabel saja tanpa mengubah GPIO
}