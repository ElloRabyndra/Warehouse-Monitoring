/*
  Program : Koneksi dengan Bot Telegram
  Oleh    : EruP
  Tanggal : 15 Juli 2023
*/

#include <Arduino.h>  // This include is necessary for PlatformIO/VSCode
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
//#include <ArduinoJson.h>
#include "DHTesp.h"

// SSID dan Password WiFi
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// Telegram BOT
#define BOTtoken "7786397106:AAGWVzRzKrt8ZgHTvnek1ouKk6NkrywxLCw"
#define CHAT_ID "1380948390"

#define pin_merah       22
#define pin_putih       23
#define DHT_PIN         15

#define NTP_SERVER      "pool.ntp.org"
#define UTC_OFFSET      7*3600    // WIB +7
#define UTC_OFFSET_DST  0

#define WAKTU_KIRIM     30000     // ms, waktu interval setiap pengiriman
#define WAKTU_BACA      1000      // ms, waktu interval setiap penerimaan

bool status_merah, status_putih;
float Suhu=24.6;
float Kelembaban=60.72;

WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);

DHTesp dhtSensor;

// Function prototypes - required for VSCode/PlatformIO
String Format(int Data);
String Waktu();
void KirimTelegram();
void BacaTelegram();
void handleNewMessages(int numNewMessages);
void led_merah(bool isOn);
void led_putih(bool isOn);

void setup() {
  // atur mode pin menjadi output
  pinMode(pin_merah, OUTPUT);
  pinMode(pin_putih, OUTPUT);

  // Serial monitor
  Serial.begin(115200);
  dhtSensor.setup(DHT_PIN, DHTesp::DHT22);
 
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
  Serial.println("Ambil Waktu Internet :");
  configTime(UTC_OFFSET, UTC_OFFSET_DST, NTP_SERVER);
}

void loop() {
  static unsigned long KirimTerakhir=0;
  static unsigned long BacaTerakhir=0;
  unsigned long t=millis();
  if(t-BacaTerakhir>=WAKTU_BACA)
  {
    BacaTerakhir=t;
    Serial.print(Waktu()); Serial.println(" Baca Telegram");
    BacaTelegram();
  }
  
  t=millis();
  if(t-KirimTerakhir>=WAKTU_KIRIM)
  {
    KirimTerakhir=t;
    Serial.print(Waktu()); Serial.println(" Kirim Telegram");
    KirimTelegram();
  }  
}

String Format(int Data)
{
  String s="00"+String(Data);
  return s.substring(s.length()-2);
}

String Waktu() {
  struct tm w;
  if (!getLocalTime(&w))
    return "Connection Err";

  return Format(w.tm_hour)+":"+Format(w.tm_min)+":"+Format(w.tm_sec)+" "+
         String(w.tm_mday)+"-"+String(w.tm_mon+1)+"-"+String(w.tm_year+1900);
}

// Akan dijalankan tiap WAKTU_KIRIM sekali
void KirimTelegram()
{
  TempAndHumidity data = dhtSensor.getTempAndHumidity();
  Suhu=data.temperature;
  Kelembaban=data.humidity;
  String msg="Waktu : "+String(Waktu())+"\nSuhu : "+String(Suhu)+"°C\nKelembaban : "+String(Kelembaban)+"%";
  Serial.println(msg);
  bot.sendMessage(CHAT_ID, msg,"");
}

// Akan dijalankan tiap WAKTU_BACA sekali
void BacaTelegram()
{
  int banyakPesan = bot.getUpdates(bot.last_message_received + 1);
  
  while(banyakPesan) {
    Serial.println("got response");
    handleNewMessages(banyakPesan);
    banyakPesan = bot.getUpdates(bot.last_message_received + 1);
  }
}

// Memproses pesan yang diterima
void handleNewMessages(int numNewMessages) {
  for (int i=0; i<numNewMessages; i++) {
    
    // Cek Chat ID
    String chat_id = String(bot.messages[i].chat_id);
    if (chat_id != CHAT_ID){
      bot.sendMessage(chat_id, "Pengguna Gelap :) "+String(chat_id), "");
      continue;
    }

    // Terima pesan dari telegram
    String text = bot.messages[i].text;
    Serial.println(text);

    String from_name = bot.messages[i].from_name;

    if (text == "/start") {
      String welcome = "Selamat Datang, " + from_name + ".\n";
      welcome += "Komen berikut untuk kontrol LED.\n\n";
      welcome += "/merah_on untuk LED Merah ON \n";
      welcome += "/merah_off untuk LED Merah OFF \n";
      welcome += "/putih_on untuk LED Putih ON \n";
      welcome += "/putih_off untuk LED Putih OFF \n";
      welcome += "/status_merah mendapatkan status LED Merah\n";
      welcome += "/status_putih mendapatkan status LED Putih\n";
      bot.sendMessage(chat_id, welcome, "");
    }

    if (text == "/merah_on") {
      bot.sendMessage(chat_id, "LED Merah ON", "");
      led_merah(true);
    }
    
    if (text == "/merah_off") {
      bot.sendMessage(chat_id, "LED Merah OFF", "");
      led_merah(false);
    }

    if (text == "/putih_on") {
      bot.sendMessage(chat_id, "LED Putih ON", "");
      led_putih(true);
    }
    
    if (text == "/putih_off") {
      bot.sendMessage(chat_id, "LED Putih OFF", "");
      led_putih(false);
    }
    
    if (text == "/status_merah") {
      if (digitalRead(pin_merah)){
        bot.sendMessage(chat_id, "LED Merah ON", "");
      }
      else{
        bot.sendMessage(chat_id, "LED Merah OFF", "");
      }
    }
    
    if (text == "/status_putih") {
      if (digitalRead(pin_putih)){
        bot.sendMessage(chat_id, "LED Putih ON", "");
      }
      else{
        bot.sendMessage(chat_id, "LED Putih OFF", "");
      }
    }
  }
}

void led_merah(bool isOn){
  if(isOn){
    digitalWrite(pin_merah, HIGH);
  }
  else{
    digitalWrite(pin_merah, LOW);
  }
}

void led_putih(bool isOn){
  if(isOn){
    digitalWrite(pin_putih, HIGH);
  }
  else{
    digitalWrite(pin_putih, LOW);
  }
}