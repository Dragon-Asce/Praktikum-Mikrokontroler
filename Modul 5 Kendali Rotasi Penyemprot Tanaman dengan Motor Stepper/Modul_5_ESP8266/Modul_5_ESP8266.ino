/*
 ============================================================================
  PRAKTIKUM 5: KENDALI ROTASI PENYEMPROT TANAMAN IOT (ESP8266 + DRIVER ULN2003A)
  Modul: Kendali Rotasi Penyemprot Tanaman dengan Motor Stepper & Cloud ThingSpeak
  Driver: ULN2003A (Motor Stepper 28BYJ-48 5V)
  Desain: Glassmorphism UI Responsif & Kendali Non-Blocking
  Board: NodeMCU 1.0 (ESP-12E Module) / Trainer Kit Lab Polman
 ============================================================================
  WIRING PIN ULN2003A KE NODEMCU ESP8266:
  - IN1 Driver ULN2003A -> Pin D1 (GPIO 5)
  - IN2 Driver ULN2003A -> Pin D2 (GPIO 4)
  - IN3 Driver ULN2003A -> Pin D5 (GPIO 14)
  - IN4 Driver ULN2003A -> Pin D6 (GPIO 12)
  - VCC Driver ULN2003A -> Pin VIN (5V) NodeMCU / Adaptor 5V Eksternal
  - GND Driver ULN2003A -> Pin GND NodeMCU
 ============================================================================
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

// ================= PENGATURAN WI-FI FISIK =================
const char *ssid = "e";
const char *password = "2,71828e";

// ================= PENGATURAN CLOUD THINGSPEAK =============
const char *thingspeakApiKey = "0FW5RJUPWPIZ086Q";
const unsigned long thingspeakChannel = 3550264;

// ================= KONFIGURASI PIN ULN2003A ===============
const int in1Pin = D1; // GPIO 5
const int in2Pin = D2; // GPIO 4
const int in3Pin = D5; // GPIO 14
const int in4Pin = D6; // GPIO 12

// Status Motor: 0 = STOP, 1 = CW (Searah Jarum Jam), -1 = CCW (Berlawanan Jarum Jam)
int dirStatus = 0;

// ================= KONFIGURASI STEPPER 28BYJ-48 ===========
// Pola 4-Langkah (Full-Step High Torque) untuk Driver ULN2003A
const int stepSequence[4][4] = {
    {1, 1, 0, 0},
    {0, 1, 1, 0},
    {0, 0, 1, 1},
    {1, 0, 0, 1}
};

int stepIndex = 0;
unsigned long lastStepTime = 0;
const unsigned long stepIntervalMicros = 2200; // Kecepatan motor (2.2ms per step)

ESP8266WebServer server(80);
WiFiClient client;

// Prototipe fungsi
void handleMotorControl();
void sendDataToThingSpeak(int status, int speed);

// ================= KENDALI MOTOR STEPPER ULN2003A =========
void setOutputStep(int a, int b, int c, int d) {
    digitalWrite(in1Pin, a);
    digitalWrite(in2Pin, b);
    digitalWrite(in3Pin, c);
    digitalWrite(in4Pin, d);
}

void matikanKoilULN2003() {
    // Matikan semua koil saat motor berhenti agar tidak panas & hemat daya
    setOutputStep(0, 0, 0, 0);
}

// ================= HALAMAN WEB GLASSMORPHISM ===============
String getHtml() {
    String statusTitle = "Penyemprot Berhenti";
    String statusSubtitle = "Motor Stepper dalam posisi siaga (OFF)";
    String statusClass = "status-stop";
    String badgeText = "STANDBY • MOTOR OFF";
    String cwActive = (dirStatus == 1) ? "active" : "";
    String ccwActive = (dirStatus == -1) ? "active" : "";
    String stopActive = (dirStatus == 0) ? "active" : "";

    if (dirStatus == 1) {
        statusTitle = "Menyemprot Searah Jarum Jam";
        statusSubtitle = "Motor berputar CW (Kecepatan ~450 Step/s)";
        statusClass = "status-cw";
        badgeText = "BERPUTAR • CW ↻";
    } else if (dirStatus == -1) {
        statusTitle = "Menyemprot Berlawanan Jarum Jam";
        statusSubtitle = "Motor berputar CCW (Kecepatan ~450 Step/s)";
        statusClass = "status-ccw";
        badgeText = "BERPUTAR • CCW ↺";
    }

    String html = R"rawliteral(<!DOCTYPE html>
<html lang="id">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Kendali Penyemprot Tanaman IoT - ESP8266</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: 'Segoe UI', -apple-system, BlinkMacSystemFont, Roboto, sans-serif;
            background: radial-gradient(circle at 15% 15%, rgba(16, 185, 129, 0.22) 0%, transparent 45%),
                        radial-gradient(circle at 85% 85%, rgba(6, 182, 212, 0.22) 0%, transparent 45%),
                        radial-gradient(circle at 50% 50%, rgba(99, 102, 241, 0.12) 0%, transparent 60%),
                        #0b1320;
            min-height: 100vh;
            color: #f8fafc;
            display: flex;
            justify-content: center;
            align-items: center;
            padding: 24px 16px;
        }
        .glass-card {
            width: 100%;
            max-width: 500px;
            background: rgba(18, 28, 45, 0.65);
            backdrop-filter: blur(20px);
            -webkit-backdrop-filter: blur(20px);
            border: 1px solid rgba(255, 255, 255, 0.12);
            border-radius: 26px;
            padding: 32px 28px;
            box-shadow: 0 25px 50px -12px rgba(0, 0, 0, 0.6), inset 0 1px 1px rgba(255, 255, 255, 0.15);
            text-align: center;
        }
        .header {
            margin-bottom: 24px;
        }
        .header .app-icon {
            font-size: 38px;
            margin-bottom: 8px;
            display: inline-block;
            filter: drop-shadow(0 0 14px rgba(52, 211, 153, 0.45));
        }
        .header h1 {
            font-size: 23px;
            font-weight: 700;
            letter-spacing: -0.3px;
            margin-bottom: 6px;
            background: linear-gradient(90deg, #34d399, #38bdf8);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }
        .header p {
            font-size: 13px;
            color: #94a3b8;
        }
        
        .status-panel {
            background: rgba(11, 19, 32, 0.65);
            border: 1px solid rgba(255, 255, 255, 0.08);
            border-radius: 18px;
            padding: 20px;
            margin-bottom: 24px;
            box-shadow: inset 0 2px 4px rgba(0, 0, 0, 0.3);
            transition: all 0.3s ease;
        }
        .status-badge {
            display: inline-flex;
            align-items: center;
            gap: 6px;
            font-size: 11px;
            font-weight: 700;
            letter-spacing: 0.8px;
            padding: 4px 12px;
            border-radius: 20px;
            margin-bottom: 12px;
        }
        .status-cw .status-badge {
            background: rgba(16, 185, 129, 0.2);
            color: #34d399;
            border: 1px solid rgba(52, 211, 153, 0.4);
            box-shadow: 0 0 14px rgba(52, 211, 153, 0.35);
        }
        .status-ccw .status-badge {
            background: rgba(6, 182, 212, 0.2);
            color: #38bdf8;
            border: 1px solid rgba(56, 189, 248, 0.4);
            box-shadow: 0 0 14px rgba(56, 189, 248, 0.35);
        }
        .status-stop .status-badge {
            background: rgba(148, 163, 184, 0.15);
            color: #94a3b8;
            border: 1px solid rgba(148, 163, 184, 0.25);
        }
        .status-title {
            font-size: 17px;
            font-weight: 600;
            color: #f1f5f9;
            margin-bottom: 4px;
        }
        .status-sub {
            font-size: 12px;
            color: #64748b;
        }

        .telemetry-row {
            display: flex;
            justify-content: center;
            gap: 12px;
            margin-top: 14px;
            padding-top: 12px;
            border-top: 1px solid rgba(255, 255, 255, 0.05);
            flex-wrap: wrap;
        }
        .telemetry-item {
            font-size: 11px;
            color: #94a3b8;
            background: rgba(255, 255, 255, 0.04);
            padding: 4px 10px;
            border-radius: 8px;
        }
        .telemetry-item strong {
            color: #e2e8f0;
        }

        .controls-grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 14px;
            margin-bottom: 14px;
        }
        .btn-glass {
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            gap: 6px;
            padding: 18px 14px;
            text-decoration: none;
            border-radius: 16px;
            font-weight: 600;
            font-size: 15px;
            transition: all 0.25s ease;
            cursor: pointer;
            border: 1px solid rgba(255, 255, 255, 0.12);
            user-select: none;
        }
        .btn-glass:active {
            transform: scale(0.97);
        }
        .btn-glass .icon {
            font-size: 22px;
        }
        .btn-glass .subtext {
            font-size: 11px;
            font-weight: 400;
            opacity: 0.8;
        }

        .btn-cw {
            background: linear-gradient(135deg, rgba(16, 185, 129, 0.2), rgba(5, 150, 105, 0.35));
            color: #a7f3d0;
            border-color: rgba(52, 211, 153, 0.35);
            box-shadow: 0 8px 20px rgba(16, 185, 129, 0.15);
        }
        .btn-cw:hover, .btn-cw.active {
            background: linear-gradient(135deg, rgba(16, 185, 129, 0.38), rgba(5, 150, 105, 0.55));
            box-shadow: 0 0 24px rgba(52, 211, 153, 0.4);
            transform: translateY(-2px);
            color: #fff;
        }

        .btn-ccw {
            background: linear-gradient(135deg, rgba(6, 182, 212, 0.2), rgba(8, 145, 178, 0.35));
            color: #bae6fd;
            border-color: rgba(56, 189, 248, 0.35);
            box-shadow: 0 8px 20px rgba(6, 182, 212, 0.15);
        }
        .btn-ccw:hover, .btn-ccw.active {
            background: linear-gradient(135deg, rgba(6, 182, 212, 0.38), rgba(8, 145, 178, 0.55));
            box-shadow: 0 0 24px rgba(56, 189, 248, 0.4);
            transform: translateY(-2px);
            color: #fff;
        }

        .btn-stop {
            width: 100%;
            background: linear-gradient(135deg, rgba(239, 68, 68, 0.2), rgba(220, 38, 38, 0.35));
            color: #fecdd3;
            border: 1px solid rgba(248, 113, 113, 0.35);
            box-shadow: 0 8px 20px rgba(239, 68, 68, 0.15);
            flex-direction: row;
            padding: 14px 20px;
            font-size: 15px;
        }
        .btn-stop:hover, .btn-stop.active {
            background: linear-gradient(135deg, rgba(239, 68, 68, 0.38), rgba(220, 38, 38, 0.55));
            box-shadow: 0 0 24px rgba(248, 113, 113, 0.4);
            transform: translateY(-2px);
            color: #fff;
        }

        .footer {
            margin-top: 24px;
            font-size: 11px;
            color: #64748b;
        }
    </style>
</head>
<body>
    <div class="glass-card">
        <div class="header">
            <div class="app-icon">🌱</div>
            <h1>Penyemprot Tanaman IoT</h1>
            <p>NodeMCU Stepper ULN2003A & Cloud ThingSpeak</p>
        </div>

        <div class="status-panel )rawliteral" + statusClass + R"rawliteral(">
            <div class="status-badge">)rawliteral" + badgeText + R"rawliteral(</div>
            <div class="status-title">)rawliteral" + statusTitle + R"rawliteral(</div>
            <div class="status-sub">)rawliteral" + statusSubtitle + R"rawliteral(</div>
            <div class="telemetry-row">
                <span class="telemetry-item">Driver: <strong>ULN2003A</strong></span>
                <span class="telemetry-item">Motor: <strong>28BYJ-48 5V</strong></span>
                <span class="telemetry-item">Pin: <strong>D1, D2, D5, D6</strong></span>
            </div>
        </div>

        <div class="controls-grid">
            <a class="btn-glass btn-cw )rawliteral" + cwActive + R"rawliteral(" href="/motor?cw=on">
                <span class="icon">↻</span>
                <span>Putar CW</span>
                <span class="subtext">Searah Jarum Jam</span>
            </a>
            <a class="btn-glass btn-ccw )rawliteral" + ccwActive + R"rawliteral(" href="/motor?ccw=on">
                <span class="icon">↺</span>
                <span>Putar CCW</span>
                <span class="subtext">Berlawanan Jarum Jam</span>
            </a>
        </div>

        <a class="btn-glass btn-stop )rawliteral" + stopActive + R"rawliteral(" href="/motor?stop=on">
            <span class="icon">⏹</span>
            <span>Hentikan Motor (STOP)</span>
        </a>

        <div class="footer">
            Data status & kecepatan disinkronkan otomatis ke Channel ThingSpeak
        </div>
    </div>
</body>
</html>)rawliteral";

    return html;
}

// ================= SETUP UTAMA ============================
void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n\n===========================================");
    Serial.println(" ESP8266 MODUL 5: PENYEMPROT TANAMAN ULN2003 ");
    Serial.println("===========================================");

    pinMode(in1Pin, OUTPUT);
    pinMode(in2Pin, OUTPUT);
    pinMode(in3Pin, OUTPUT);
    pinMode(in4Pin, OUTPUT);
    matikanKoilULN2003(); // Pastikan semua koil dalam keadaan mati saat awal
    Serial.println("[Stepper] Driver ULN2003A siap pada Pin D1, D2, D5, D6.");

    // Konek ke Wi-Fi Fisik
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.printf("Menghubungkan ke Wi-Fi: %s ", ssid);

    int timeoutCount = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        timeoutCount++;
        if (timeoutCount > 60) {
            Serial.println("\n[PERINGATAN] Gagal terhubung ke Wi-Fi! Periksa SSID & Password.");
            break;
        }
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[SUKSES] Wi-Fi Terhubung!");
        Serial.print(">>> BUKA BROWSER DI HP / LAPTOP: http://");
        Serial.print(WiFi.localIP());
        Serial.println(" <<<");
    }

    // Rute Server HTTP
    server.on("/", HTTP_GET, []() {
        server.send(200, "text/html", getHtml());
    });

    server.on("/motor", HTTP_GET, handleMotorControl);

    server.begin();
    Serial.println("Web Server Glassmorphism Aktif & Siap Mengendalikan Motor!");
}

// ================= LOOP UTAMA =============================
void loop() {
    server.handleClient();

    // Jalankan stepper secara halus dan non-blocking jika dirStatus != 0
    if (dirStatus != 0) {
        unsigned long currentMicros = micros();
        if (currentMicros - lastStepTime >= stepIntervalMicros) {
            lastStepTime = currentMicros;

            if (dirStatus == 1) {
                stepIndex = (stepIndex + 3) % 4; // Searah Jarum Jam (CW pada output shaft gearbox 28BYJ-48)
            } else if (dirStatus == -1) {
                stepIndex = (stepIndex + 1) % 4; // Berlawanan Jarum Jam (CCW pada output shaft gearbox 28BYJ-48)
            }

            setOutputStep(
                stepSequence[stepIndex][0],
                stepSequence[stepIndex][1],
                stepSequence[stepIndex][2],
                stepSequence[stepIndex][3]
            );
        }
    }

    yield(); // Mencegah reset watchdog ESP8266
}

// ================= KONTROL MOTOR ==========================
void handleMotorControl() {
    if (server.hasArg("cw") && server.arg("cw") == "on") {
        dirStatus = 1;
        Serial.println("\n[Motor] Status: Running CW (Searah Jarum Jam)");
        sendDataToThingSpeak(dirStatus, 450);

    } else if (server.hasArg("ccw") && server.arg("ccw") == "on") {
        dirStatus = -1;
        Serial.println("\n[Motor] Status: Running CCW (Berlawanan Jarum Jam)");
        sendDataToThingSpeak(dirStatus, 450);

    } else if (server.hasArg("stop") && server.arg("stop") == "on") {
        dirStatus = 0;
        matikanKoilULN2003(); // Matikan koil saat berhenti agar motor dingin
        Serial.println("\n[Motor] Status: STOP (Koil Dinonaktifkan)");
        sendDataToThingSpeak(dirStatus, 0);
    }

    server.sendHeader("Location", "/", true);
    server.send(303, "text/plain", "");
}

// ================= PENGIRIMAN DATA KE THINGSPEAK ==========
void sendDataToThingSpeak(int status, int speed) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String url = "http://api.thingspeak.com/update?api_key=" + String(thingspeakApiKey) +
                     "&field1=" + String(status) +
                     "&field2=" + String(speed);

        http.begin(client, url);
        http.setTimeout(2500);
        int httpCode = http.GET();
        if (httpCode > 0) {
            String payload = http.getString();
            payload.trim();
            Serial.printf("[ThingSpeak] Terkirim! Status: %d, Speed: %d (Respon: %s)\n", status, speed, payload.c_str());
        } else {
            Serial.printf("[ThingSpeak] Gagal mengirim: %s\n", http.errorToString(httpCode).c_str());
        }
        http.end();
    }
}

