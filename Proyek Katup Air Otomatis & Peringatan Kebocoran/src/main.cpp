/*
 ============================================================================
  PROYEK: KATUP AIR OTOMATIS & PERINGATAN KEBOCORAN IOT (UBIDOTS MQTT)
  Board: ESP32 DevKit V1
  Cloud IoT: Ubidots MQTT Broker (industrial.api.ubidots.com)
 ============================================================================
  WIRING PIN HARDWARE & SIMULASI WOKWI:
  - Kabel ORANYE / KUNING (PWM Servo)        -> Pin D14 (GPIO 14)
  - Kabel MERAH (VCC / V+ Servo)             -> Pin VIN (5V) ESP32
  - Kabel COKELAT / HITAM (GND Servo)        -> Pin GND ESP32
  - Kaki Positif (+) Buzzer                  -> Pin D13 (GPIO 13)
  - Kaki Negatif (-) Buzzer                  -> Pin GND ESP32
  - Kaki Anoda (+) LED Merah (Flushing)      -> Pin D12 (GPIO 12) melalui Resistor 220Ω-330Ω
  - Kaki Katoda (-) LED Merah                -> Pin GND ESP32
  - Kaki Anoda (+) LED Hijau (Power System)  -> Pin D15 (GPIO 15) melalui Resistor 220Ω-330Ω
  - Kaki Katoda (-) LED Hijau                -> Pin GND ESP32
 ============================================================================
  DAFTAR 9 VARIABEL SISTEM (UBIDOTS MQTT):
  
  [VARIABEL KONTROL - SUBSCRIBE / INPUT]:
  1. power-sistem               : Saklar daya utama (1 = ON, 0 = OFF)
  2. status-katup               : Mode kontrol katup (0 = Darurat, 1 = Standby, 2 = Flushing)

  [VARIABEL TELEMETRI & ANALITIK - PUBLISH / OUTPUT]:
  3. status-power-terkonfirmasi : Feedback status daya terkonfirmasi (1 = ON, 0 = OFF)
  4. status-katup-terkonfirmasi : Feedback status mode katup aktif (0, 1, 2)
  5. sudut-katup-aktual         : Posisi sudut aktual motor servo (0°, 90°, 180°)
  6. estimasi-debit-aliran      : Debit aliran air saat ini (0, 15, 30 L/menit)
  7. total-volume-air          : Akumulasi total volume air yang mengalir (Liter)
  8. jumlah-insiden-kebocoran  : Counter total insiden kebocoran (Mode 0)
  9. efisiensi-penggunaan-air  : Rasio efisiensi pemakaian air (0 - 100%)
 ============================================================================
*/

#include <Arduino.h>
#include <WiFi.h>
#include <UbidotsEsp32Mqtt.h>
#include <ESP32Servo.h>
#include "env.h"

// ================= KREDENSIAL DARI .ENV ===================
const char* WIFI_SSID       = ENV_WIFI_SSID;
const char* WIFI_PASS       = ENV_WIFI_PASSWORD;
const char* UBIDOTS_TOKEN   = ENV_UBIDOTS_TOKEN;
const char* DEVICE_LABEL    = ENV_DEVICE_LABEL;
const char* VARIABLE_STATUS = ENV_VARIABLE_STATUS; // "status-katup"
const char* VARIABLE_POWER  = ENV_VARIABLE_POWER;  // "power-sistem"

// ================= KONFIGURASI PIN HARDWARE ===============
const int PIN_SERVO  = 14; // GPIO 14 (D14) - Motor Servo Katup Air
const int PIN_BUZZER = 13; // GPIO 13 (D13) - Buzzer Alarm Kebocoran
const int PIN_LED    = 12; // GPIO 12 (D12) - Indikator Flushing (LED Merah)
const int PIN_POWER  = 15; // GPIO 15 (D15) - Indikator Daya Sistem (LED Hijau)

Servo valveServo;
Ubidots ubidots(UBIDOTS_TOKEN);

// ================= STATUS POKOK SISTEM ====================
int currentValveAngle = 0;       // Posisi sudut servo saat ini (derajat)
int lastStateValue = 1;          // Target mode katup: 0=Darurat, 1=Standby, 2=Flushing
bool isSystemOn = false;         // Status saklar daya (Default: OFF sampai dinyalakan)

// ================= ALARM BUZZER NON-BLOCKING ==============
unsigned long lastBuzzerToggle = 0;
bool buzzerIsOn = false;

// ================= VARIABEL ANALITIK & METRIK =============
unsigned long lastTimeTracking = 0;
unsigned long lastTelemetryPublish = 0;
const unsigned long telemetryInterval = 15000; // Kirim telemetri berkala setiap 15 detik

float durasiStandbySec   = 0.0;  // Akumulasi durasi Mode Standby (detik)
float durasiFlushingSec  = 0.0;  // Akumulasi durasi Mode Flushing (detik)
float totalVolumeLiter   = 0.0;  // Akumulasi total volume air yang mengalir (Liter)

int incidentCount = 0;           // Counter total insiden kebocoran (Mode 0)
int previousStateForIncident = 1;// Pelacak transisi mode ke darurat

// ================= FUNGSI INSTRUMEN ANALITIK ==============
// Menghitung estimasi debit air real-time berdasarkan mode aktif
float getEstimasiDebit() {
    if (!isSystemOn) return 0.0;           // Sistem OFF -> Aliran 0 L/menit
    if (lastStateValue == 0) return 0.0;   // Mode 0 (Darurat) -> Katup tertutup = 0 L/menit
    if (lastStateValue == 1) return 15.0;  // Mode 1 (Standby) -> Debit normal = 15 L/menit
    if (lastStateValue == 2) return 30.0;  // Mode 2 (Flushing) -> Debit penuh = 30 L/menit
    return 0.0;
}

// Menghitung skor efisiensi pemakaian air (0 - 100%)
// Mempertimbangkan rasio penggunaan air hemat (Standby vs Total Aliran)
// serta penalti jika terdeteksi insiden kebocoran pipa (Mode 0)
float getEfisiensiAir() {
    float totalAliranSec = durasiStandbySec + durasiFlushingSec;
    
    // Jika belum ada aliran aktif sejak sistem dinyalakan
    if (totalAliranSec <= 0.1) {
        float skorAwal = 100.0 - (incidentCount * 10.0);
        return constrain(skorAwal, 0.0, 100.0);
    }

    // 1. Rasio durasi pemakaian normal (Standby) terhadap total durasi aliran aktif
    float rasioStandby = (durasiStandbySec / totalAliranSec) * 100.0;

    // 2. Faktor penalti insiden kebocoran (setiap insiden mengurangi skor integritas 5%)
    float penaltiInsiden = incidentCount * 5.0;

    float efisiensi = rasioStandby - penaltiInsiden;

    // Batasi nilai selalu berada di rentang 0.0 s.d. 100.0%
    return constrain(efisiensi, 0.0, 100.0);
}

// ================= LOGIKA AKTUASI SISTEM ==================
void applySystemState() {
    if (!isSystemOn) {
        // KONDISI SISTEM MATI (OFF):
        // Katup terkunci rapat di 0°, LED Hijau & Merah padam, Buzzer mati
        currentValveAngle = 0;
        valveServo.write(currentValveAngle);

        if (buzzerIsOn) {
            noTone(PIN_BUZZER);
            digitalWrite(PIN_BUZZER, LOW);
            buzzerIsOn = false;
        }
        digitalWrite(PIN_LED, LOW);   // LED Merah MATI
        digitalWrite(PIN_POWER, LOW); // LED Hijau Power MATI

        Serial.println("===========================================");
        Serial.println(">>> [POWER OFF] SISTEM DIMATIKAN! <<<");
        Serial.println("• Katup Dikunci di 0° (Tertutup Total)");
        Serial.println("• Indikator LED Hijau & Merah PADAM");
        Serial.println("• Alarm Buzzer NONAKTIF");
        Serial.println("===========================================");
        return;
    }

    // KONDISI SISTEM HIDUP (ON):
    digitalWrite(PIN_POWER, HIGH); // LED Hijau Power MENYALA

    Serial.println("===========================================");
    Serial.println(">>> [POWER ON] SISTEM AKTIF & BERJALAN! <<<");

    if (lastStateValue == 0) {
        // Mode 0: Bahaya / Kebocoran Air Terdeteksi
        currentValveAngle = 0;
        valveServo.write(currentValveAngle);
        tone(PIN_BUZZER, 2000);
        buzzerIsOn = true;
        digitalWrite(PIN_LED, LOW);
        Serial.println("• Mode: 0 (BAHAYA KEBOCORAN)");
        Serial.println("• Katup DITUTUP (0°) & Alarm Buzzer AKTIF (2000 Hz)");

    } else if (lastStateValue == 1) {
        // Mode 1: Operasi Normal Standby
        currentValveAngle = 90;
        valveServo.write(currentValveAngle);
        if (buzzerIsOn) {
            noTone(PIN_BUZZER);
            digitalWrite(PIN_BUZZER, LOW);
            buzzerIsOn = false;
        }
        digitalWrite(PIN_LED, LOW);
        Serial.println("• Mode: 1 (NORMAL STANDBY)");
        Serial.println("• Katup Terbuka 50% (90°), Siaga Tanpa Alarm");

    } else if (lastStateValue == 2) {
        // Mode 2: Aliran Maksimal / Flushing Pipa
        currentValveAngle = 180;
        valveServo.write(currentValveAngle);
        if (buzzerIsOn) {
            noTone(PIN_BUZZER);
            digitalWrite(PIN_BUZZER, LOW);
            buzzerIsOn = false;
        }
        digitalWrite(PIN_LED, HIGH); // LED Merah Indikator Flushing MENYALA
        Serial.println("• Mode: 2 (MAX FLOW / FLUSHING)");
        Serial.println("• Katup DIBUKA PENUH (180°) & LED Merah MENYALA");
    }
    Serial.println("===========================================");
}

// ================= CALLBACK SAAT NILAI BERUBAH ========
void callback(char *topic, byte *payload, unsigned int length) {
    String message = "";
    for (int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    float value = message.toFloat();
    String topicStr = String(topic);

    Serial.println("\n-------------------------------------------");
    Serial.printf("[Ubidots MQTT] Pesan Diterima: %s -> %s\n", topicStr.c_str(), message.c_str());

    // 1. KONTROL SAKLAR DAYA (power-sistem)
    if (topicStr.indexOf(VARIABLE_POWER) != -1) {
        bool newPower = (value >= 1.0);
        if (newPower != isSystemOn) {
            isSystemOn = newPower;
            Serial.printf(">>> [SAKLAR POWER] Diubah menjadi: %s\n", isSystemOn ? "ON" : "OFF");
            applySystemState();
        }
    } 
    // 2. KONTROL MODE KATUP (status-katup)
    else if (topicStr.indexOf(VARIABLE_STATUS) != -1) {
        int newState = (int)value;
        if (isSystemOn) {
            // Insiden dihitung jika berpindah ke Mode 0 saat sistem aktif
            if (newState == 0 && previousStateForIncident != 0) {
                incidentCount++;
                Serial.printf(">>> [INSIDEN] Insiden Kebocoran #%d Terdeteksi!\n", incidentCount);
            }
            previousStateForIncident = newState;
            lastStateValue = newState;
            Serial.printf(">>> [PERINTAH MODE] Mode diubah ke: %d\n", lastStateValue);
            applySystemState();
        } else {
            // Jika Power OFF, simpan target mode tanpa menggerakkan katup
            lastStateValue = newState;
            Serial.printf(">>> [INFO] Sistem sedang OFF. Mode %d disimpan (katup tetap 0°).\n", newState);
        }
    }
    Serial.println("-------------------------------------------\n");

    // PUBLISH FEEDBACK INSTAN TERKONFIRMASI:
    // Mengirim status terkonfirmasi dan debit aliran aktual secara instan saat kontrol diubah
    ubidots.add("status-power-terkonfirmasi", isSystemOn ? 1 : 0);
    ubidots.add("status-katup-terkonfirmasi", lastStateValue);
    ubidots.add("sudut-katup-aktual", isSystemOn ? currentValveAngle : 0);
    ubidots.add("estimasi-debit-aliran", getEstimasiDebit());
    ubidots.publish(DEVICE_LABEL);
}

// ================= SETUP UTAMA ============================
void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n\n===========================================");
    Serial.println("  PROYEK: KATUP AIR OTOMATIS & ALARM IOT   ");
    Serial.println("  HARDWARE ESP32 + CLOUD UBIDOTS MQTT      ");
    Serial.println("===========================================");

    // Inisialisasi Output Buzzer, LED Indikator, & LED Power
    pinMode(PIN_BUZZER, OUTPUT);
    pinMode(PIN_LED, OUTPUT);
    pinMode(PIN_POWER, OUTPUT);
    
    digitalWrite(PIN_BUZZER, LOW);
    digitalWrite(PIN_LED, LOW);
    digitalWrite(PIN_POWER, LOW); // Default: Sistem awal OFF, LED Hijau PADAM

    // Alokasikan semua timer PWM (0..3) ESP32 agar Servo (50Hz) & Tone (2000Hz) stabil
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);

    // Inisialisasi Motor Servo Katup
    valveServo.setPeriodHertz(50);
    valveServo.attach(PIN_SERVO, 500, 2400);
    valveServo.write(0); // Posisi awal tertutup (0°)
    currentValveAngle = 0;
    Serial.printf("[Hardware] Servo Pin D%d (0°), Buzzer Pin D%d, LED Flushing Pin D%d, LED Power Pin D%d Siap.\n", 
                  PIN_SERVO, PIN_BUZZER, PIN_LED, PIN_POWER);

    // Inisialisasi Timer Tracking Waktu
    lastTimeTracking = millis();

    // Koneksi Wi-Fi
    Serial.printf("[Wi-Fi] Menghubungkan ke Wi-Fi '%s' ...\n", WIFI_SSID);
    ubidots.connectToWifi(WIFI_SSID, WIFI_PASS);

    // Inisialisasi & Konfigurasi MQTT Ubidots
    ubidots.setCallback(callback);
    ubidots.setup();
    ubidots.reconnect();

    // Subscribe ke variabel kontrol (power-sistem & status-katup)
    ubidots.subscribeLastValue(DEVICE_LABEL, VARIABLE_STATUS);
    ubidots.subscribeLastValue(DEVICE_LABEL, VARIABLE_POWER);
    Serial.printf("[Ubidots] Berhasil Subscribe ke Device: '%s'\n", DEVICE_LABEL);
    Serial.printf("[Ubidots] Menunggu perintah Power ('%s') & Mode ('%s')...\n\n", VARIABLE_POWER, VARIABLE_STATUS);

    // Inisialisasi Awal: Publikasikan semua 7 variabel telemetri agar langsung teregister di Ubidots
    // Dibagi menjadi 2 Batch dengan jeda 1.5 detik agar memenuhi rate limit broker Ubidots (max 1 req/s)
    Serial.println("[Ubidots] Mengirim data inisialisasi awal ke cloud...");
    ubidots.add("status-power-terkonfirmasi", 0);
    ubidots.add("status-katup-terkonfirmasi", lastStateValue);
    ubidots.add("sudut-katup-aktual", 0);
    ubidots.add("estimasi-debit-aliran", 0.0);
    bool init1 = ubidots.publish(DEVICE_LABEL);
    Serial.printf("[Ubidots] Inisialisasi Batch 1: %s\n", init1 ? "BERHASIL" : "GAGAL");

    delay(1500); // Jeda 1.5 detik mematuhi rate limit broker Ubidots
    ubidots.loop();

    ubidots.add("total-volume-air", 0.0);
    ubidots.add("jumlah-insiden-kebocoran", 0);
    ubidots.add("efisiensi-penggunaan-air", 100.0);
    bool init2 = ubidots.publish(DEVICE_LABEL);
    Serial.printf("[Ubidots] Inisialisasi Batch 2: %s\n", init2 ? "BERHASIL" : "GAGAL");
    Serial.println("[Ubidots] Inisialisasi awal selesai.\n");
}

// ================= LOOP UTAMA =============================
void loop() {
    // Menjaga koneksi MQTT tetap terhubung
    if (!ubidots.connected()) {
        Serial.println("[Ubidots] Koneksi terputus. Menghubungkan kembali...");
        ubidots.reconnect();
        ubidots.subscribeLastValue(DEVICE_LABEL, VARIABLE_STATUS);
        ubidots.subscribeLastValue(DEVICE_LABEL, VARIABLE_POWER);
    }

    ubidots.loop();

    unsigned long currentMillis = millis();

    // 1. Integrasi Waktu Operasi & Akumulasi Volume Air
    float dtSec = (currentMillis - lastTimeTracking) / 1000.0;
    lastTimeTracking = currentMillis;

    // Batasi dtSec jika terjadi jeda panjang (misal reconnect Wi-Fi) agar tidak terjadi lonjakan fiktif
    if (dtSec > 5.0) dtSec = 0.1;

    float debitSekarang = getEstimasiDebit();

    if (isSystemOn) {
        if (lastStateValue == 1) {
            durasiStandbySec += dtSec;
        } else if (lastStateValue == 2) {
            durasiFlushingSec += dtSec;
        }

        // Kalkulasi akumulasi total volume air (Liter) berdasarkan integrasi waktu (dtSec)
        // Formula: totalVolumeLiter += (debitSekarangLperMin / 60.0) * dtSec
        if (debitSekarang > 0.0) {
            totalVolumeLiter += (debitSekarang / 60.0) * dtSec;
        }
    }

    // 2. Sirine Bip-Bip Intermiten Non-Blocking saat Bahaya / Mode 0 & Power ON
    if (isSystemOn && lastStateValue == 0) {
        if (currentMillis - lastBuzzerToggle >= 300) {
            lastBuzzerToggle = currentMillis;
            if (buzzerIsOn) {
                noTone(PIN_BUZZER);
                buzzerIsOn = false;
            } else {
                tone(PIN_BUZZER, 2000); // Frekuensi alarm 2000 Hz
                buzzerIsOn = true;
            }
        }
    } else {
        // Matikan buzzer jika sistem OFF atau keluar dari mode darurat
        if (buzzerIsOn) {
            noTone(PIN_BUZZER);
            digitalWrite(PIN_BUZZER, LOW);
            buzzerIsOn = false;
        }
    }

    // 3. Pengiriman Telemetri & Analitik ke Ubidots Tiap 15 Detik
    if (currentMillis - lastTelemetryPublish >= telemetryInterval) {
        lastTelemetryPublish = currentMillis;

        float estimasiDebit = debitSekarang;
        float skorEfisiensi = getEfisiensiAir();

        // PENTING: Ubidots STEM memberlakukan rate limit 1 request/detik per token,
        // serta ukuran buffer MQTT 256 bytes.
        // Maka data dibagi menjadi 2 batch ramah-buffer dengan jeda 1.5 detik.

        // Batch 1: Status Pokok & Debit Aliran (4 Variabel ~200 bytes)
        ubidots.add("status-power-terkonfirmasi", isSystemOn ? 1 : 0);
        ubidots.add("status-katup-terkonfirmasi", lastStateValue);
        ubidots.add("sudut-katup-aktual", isSystemOn ? currentValveAngle : 0);
        ubidots.add("estimasi-debit-aliran", estimasiDebit);
        bool pub1 = ubidots.publish(DEVICE_LABEL);

        // Jeda 1.5 detik agar broker Ubidots tidak mendrop Batch 2 karena rate limit
        delay(1500);
        ubidots.loop();

        // Batch 2: Akumulasi Volume, Insiden, & Efisiensi (3 Variabel ~160 bytes)
        ubidots.add("total-volume-air", totalVolumeLiter);
        ubidots.add("jumlah-insiden-kebocoran", incidentCount);
        ubidots.add("efisiensi-penggunaan-air", skorEfisiensi);
        bool pub2 = ubidots.publish(DEVICE_LABEL);

        // Cetak Ringkasan Telemetri ke Serial Monitor
        Serial.println("==================== TELEMETRI UBIDOTS ====================");
        Serial.printf("• Status Power : %s (Indikator: %s)\n", 
                      isSystemOn ? "ON (Sistem Berjalan)" : "OFF (Sistem Terkunci)", 
                      isSystemOn ? "LED HIJAU MENYALA" : "LED PADAM");
        Serial.printf("• Mode Katup   : %d | Sudut Katup: %d°\n", lastStateValue, isSystemOn ? currentValveAngle : 0);
        Serial.printf("• Debit Air    : %.1f L/menit | Total Volume: %.2f Liter\n", estimasiDebit, totalVolumeLiter);
        Serial.printf("• Insiden Bocor: %d kali | Efisiensi Air: %.1f %%\n", incidentCount, skorEfisiensi);
        Serial.printf("• Status Kirim : Batch 1 [%s] | Batch 2 [%s]\n",
                      pub1 ? "OK" : "GAGAL", pub2 ? "OK" : "GAGAL");
        Serial.println("===========================================================\n");
    }

    delay(10);
}
