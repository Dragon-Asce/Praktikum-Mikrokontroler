#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include "env.h"

// ================= KONFIGURASI WI-FI =================
const char *ssid = ENV_WIFI_SSID;
const char *password = ENV_WIFI_PASSWORD;

// ================= KONFIGURASI THINGSPEAK ============
const char *thingspeakApiKey = ENV_THINGSPEAK_API_KEY;
const unsigned long thingspeakChannel = ENV_THINGSPEAK_CHANNEL;

// ================= KONFIGURASI PIN (A4988) ============
// Pin sesuai diagram.json (esp:D12 -> STEP, esp:D13 -> DIR)
const int stepPin = 12;
const int dirPin  = 13;

// 0 = Motor OFF, 1 = CW, -1 = CCW
int dirStatus = 0;

// Kecepatan & timing step (non-blocking)
unsigned long lastStepTime = 0;
const unsigned long stepIntervalMicros = 2000; // Jeda pulsa step (2ms)

WebServer server(80);

// Prototipe fungsi
void handleMotorControl();
void sendDataToThingSpeak(int status, int speed);

// ================= FUNGSI TAMPILAN GLASSMORPHISM =====
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
        statusSubtitle = "Motor berputar CW (Kecepatan 250 Step/s)";
        statusClass = "status-cw";
        badgeText = "BERPUTAR • CW ↻";
    } else if (dirStatus == -1) {
        statusTitle = "Menyemprot Berlawanan Jarum Jam";
        statusSubtitle = "Motor berputar CCW (Kecepatan 250 Step/s)";
        statusClass = "status-ccw";
        badgeText = "BERPUTAR • CCW ↺";
    }

    String html = R"rawliteral(<!DOCTYPE html>
<html lang="id">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Kendali Penyemprot Tanaman IoT</title>
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
            <p>Kendali Motor Stepper A4988 & Cloud ThingSpeak</p>
        </div>

        <div class="status-panel )rawliteral" + statusClass + R"rawliteral(">
            <div class="status-badge">)rawliteral" + badgeText + R"rawliteral(</div>
            <div class="status-title">)rawliteral" + statusTitle + R"rawliteral(</div>
            <div class="status-sub">)rawliteral" + statusSubtitle + R"rawliteral(</div>
            <div class="telemetry-row">
                <span class="telemetry-item">Driver: <strong>A4988</strong></span>
                <span class="telemetry-item">Kecepatan: <strong>250 Step/s</strong></span>
                <span class="telemetry-item">Pin: <strong>D12 / D13</strong></span>
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

// ================= SETUP ==============================
void setup() {
    Serial.begin(115200);
    delay(500);

    pinMode(stepPin, OUTPUT);
    pinMode(dirPin, OUTPUT);
    digitalWrite(stepPin, LOW);
    digitalWrite(dirPin, LOW);

    // Konek WiFi Wokwi
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    Serial.print("Connecting to WiFi...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nConnected to WiFi");
    Serial.print("Alamat IP lokal: ");
    Serial.println(WiFi.localIP());

    // Halaman utama
    server.on("/", HTTP_GET, []() {
        server.send(200, "text/html", getHtml());
    });

    server.on("/motor", HTTP_GET, handleMotorControl);

    server.begin();
    Serial.println("Web server aktif!");
}

// ================= LOOP ===============================
void loop() {
    server.handleClient();

    // Jalankan motor secara non-blocking jika dirStatus != 0
    if (dirStatus != 0) {
        unsigned long currentMicros = micros();
        if (currentMicros - lastStepTime >= stepIntervalMicros) {
            lastStepTime = currentMicros;
            
            // Generate 1 pulsa step ke A4988
            digitalWrite(stepPin, HIGH);
            delayMicroseconds(10);
            digitalWrite(stepPin, LOW);
        }
    }

    delay(2); // Menjaga stabilitas CPU
}

// ================= KONTROL MOTOR ======================
void handleMotorControl() {
    if (server.hasArg("cw") && server.arg("cw") == "on") {
        dirStatus = 1;
        digitalWrite(dirPin, LOW); // Set arah CW (LOW agar putaran searah jarum jam)
        Serial.println("[Motor] Status: Running CW");
        sendDataToThingSpeak(dirStatus, 250);

    } else if (server.hasArg("ccw") && server.arg("ccw") == "on") {
        dirStatus = -1;
        digitalWrite(dirPin, HIGH); // Set arah CCW (HIGH agar putaran berlawanan jarum jam)
        Serial.println("[Motor] Status: Running CCW");
        sendDataToThingSpeak(dirStatus, 250);

    } else if (server.hasArg("stop") && server.arg("stop") == "on") {
        dirStatus = 0;
        Serial.println("[Motor] Status: STOP");
        sendDataToThingSpeak(dirStatus, 0);
    }

    server.sendHeader("Location", "/", true);
    server.send(303, "text/plain", "");
}

// ================= KIRIM DATA THINGSPEAK ==============
void sendDataToThingSpeak(int status, int speed) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String url = "http://api.thingspeak.com/update?api_key=" + String(thingspeakApiKey) +
                     "&field1=" + String(status) +
                     "&field2=" + String(speed);

        http.begin(url);
        http.setTimeout(2500);
        int httpCode = http.GET();
        if (httpCode > 0) {
            String payload = http.getString();
            payload.trim();
            Serial.printf("[ThingSpeak] Terkirim! Status: %d, Speed: %d (Entry: %s)\n", status, speed, payload.c_str());
        } else {
            Serial.printf("[ThingSpeak] Gagal mengirim: %s\n", http.errorToString(httpCode).c_str());
        }
        http.end();
    }
}
