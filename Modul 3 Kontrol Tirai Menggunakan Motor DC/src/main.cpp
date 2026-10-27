#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include "env.h"

// Konfigurasi Wi-Fi dari .env
const char *ssid = ENV_WIFI_SSID;
const char *password = ENV_WIFI_PASSWORD;

// Pin Driver A4988 sesuai diagram.json (STEP -> GPIO 12, DIR -> GPIO 13)
const int stepPin = 12; // esp:D12 -> driver1:STEP
const int dirPin  = 13; // esp:D13 -> driver1:DIR

// Konfigurasi Stepper Motor
const int stepsPerRev = 200; // Standard NEMA 17 (1.8 derajat / step)
const int targetSteps = 800; // Jumlah step untuk buka/tutup tirai (4 putaran)

// Status Tirai & Pergerakan
bool tiraiTerbuka = false;
bool isMoving = false;
String moveAction = ""; // "opening" atau "closing"

// Konfigurasi ThingSpeak dari .env
const char *thingspeakApiKey = ENV_THINGSPEAK_API_KEY;
const unsigned long thingspeakChannel = ENV_THINGSPEAK_CHANNEL;

WebServer server(80);

// ================= FUNGSI PUTAR STEPPER (A4988) =======
void rotateStepper(bool directionHigh, int steps) {
    digitalWrite(dirPin, directionHigh ? HIGH : LOW);
    
    for (int i = 0; i < steps; i++) {
        digitalWrite(stepPin, HIGH);
        delayMicroseconds(1000); // 1ms HIGH
        digitalWrite(stepPin, LOW);
        delayMicroseconds(1000); // 1ms LOW
        
        // Cek berkala agar watchdog tidak reset di perulangan panjang
        if (i % 50 == 0) {
            delay(1);
        }
    }
}

// ================= KIRIM DATA THINGSPEAK ==============
void sendToThingSpeak(int status) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String url = "http://api.thingspeak.com/update?api_key=" + String(thingspeakApiKey) +
                     "&field1=" + String(status);
        http.begin(url);
        http.setTimeout(2500);
        int httpCode = http.GET();
        if (httpCode > 0) {
            Serial.printf("[ThingSpeak] Status tirai (%d) terkirim! Respon: %d\n", status, httpCode);
        } else {
            Serial.printf("[ThingSpeak] Gagal mengirim: %s\n", http.errorToString(httpCode).c_str());
        }
        http.end();
    }
}

// ================= GENERATOR HTML AURORAMORPHISM ======
String getHtml() {
    String html = R"rawliteral(<!DOCTYPE html>
<html lang="id">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Kontrol Tirai Pintar IoT - Aurora</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: 'Segoe UI', -apple-system, BlinkMacSystemFont, Roboto, sans-serif;
            background: radial-gradient(circle at 10% 20%, rgba(0, 242, 254, 0.22) 0%, transparent 45%),
                        radial-gradient(circle at 90% 15%, rgba(127, 0, 255, 0.25) 0%, transparent 45%),
                        radial-gradient(circle at 50% 85%, rgba(0, 230, 118, 0.2) 0%, transparent 50%),
                        radial-gradient(circle at 80% 80%, rgba(225, 0, 255, 0.16) 0%, transparent 40%),
                        #070a16;
            min-height: 100vh;
            color: #f8fafc;
            display: flex;
            justify-content: center;
            align-items: center;
            padding: 24px 16px;
            overflow-x: hidden;
        }

        .aurora-card {
            width: 100%;
            max-width: 440px;
            background: rgba(15, 23, 42, 0.65);
            backdrop-filter: blur(24px);
            -webkit-backdrop-filter: blur(24px);
            border: 1px solid rgba(255, 255, 255, 0.15);
            border-radius: 28px;
            padding: 34px 28px;
            box-shadow: 0 25px 50px -12px rgba(0, 0, 0, 0.7),
                        0 0 35px rgba(0, 242, 254, 0.14),
                        inset 0 1px 1px rgba(255, 255, 255, 0.2);
            text-align: center;
            position: relative;
        }

        .header {
            margin-bottom: 22px;
        }
        .header h1 {
            font-size: 24px;
            font-weight: 800;
            letter-spacing: -0.3px;
            margin-bottom: 6px;
            background: linear-gradient(135deg, #00f2fe 0%, #4facfe 35%, #00e676 70%, #e100ff 100%);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }
        .header p {
            font-size: 13px;
            color: #94a3b8;
        }

        /* Simulasi Jendela & Tirai Visual */
        .window-frame {
            width: 220px;
            height: 140px;
            margin: 0 auto 22px auto;
            background: linear-gradient(180deg, #1e293b 0%, #0f172a 100%);
            border: 4px solid rgba(255, 255, 255, 0.15);
            border-radius: 16px;
            position: relative;
            overflow: hidden;
            box-shadow: inset 0 0 20px rgba(0,0,0,0.6), 0 0 20px rgba(0, 242, 254, 0.15);
            display: flex;
            justify-content: space-between;
        }
        .window-scenery {
            position: absolute;
            inset: 0;
            display: flex;
            justify-content: center;
            align-items: center;
            font-size: 38px;
            background: radial-gradient(circle at center, rgba(0, 242, 254, 0.2), transparent 70%);
            transition: all 0.5s ease;
        }
        .curtain-panel {
            width: 50%;
            height: 100%;
            background: linear-gradient(135deg, rgba(127, 0, 255, 0.75), rgba(225, 0, 255, 0.75));
            backdrop-filter: blur(4px);
            border-right: 1px solid rgba(255, 255, 255, 0.2);
            border-left: 1px solid rgba(255, 255, 255, 0.2);
            transition: transform 0.8s cubic-bezier(0.4, 0, 0.2, 1);
            position: relative;
            z-index: 2;
        }
        .curtain-left {
            transform-origin: left;
        }
        .curtain-right {
            transform-origin: right;
        }
        /* State Tirai Terbuka */
        .open .curtain-left {
            transform: scaleX(0.18);
        }
        .open .curtain-right {
            transform: scaleX(0.18);
        }

        /* Panel Status Realtime */
        .status-box {
            background: rgba(10, 16, 30, 0.6);
            border: 1px solid rgba(255, 255, 255, 0.08);
            border-radius: 18px;
            padding: 16px 20px;
            margin-bottom: 24px;
            box-shadow: inset 0 2px 4px rgba(0,0,0,0.3);
            transition: all 0.3s ease;
        }
        .status-pill {
            display: inline-flex;
            align-items: center;
            gap: 8px;
            font-size: 11px;
            font-weight: 700;
            letter-spacing: 0.8px;
            padding: 4px 14px;
            border-radius: 20px;
            margin-bottom: 8px;
        }
        .pill-open {
            background: rgba(0, 242, 254, 0.18);
            color: #00f2fe;
            border: 1px solid rgba(0, 242, 254, 0.45);
            box-shadow: 0 0 16px rgba(0, 242, 254, 0.35);
        }
        .pill-closed {
            background: rgba(168, 85, 247, 0.18);
            color: #c084fc;
            border: 1px solid rgba(168, 85, 247, 0.45);
            box-shadow: 0 0 16px rgba(168, 85, 247, 0.3);
        }
        .pill-moving {
            background: rgba(250, 204, 21, 0.18);
            color: #fde047;
            border: 1px solid rgba(250, 204, 21, 0.45);
            box-shadow: 0 0 16px rgba(250, 204, 21, 0.35);
            animation: pulse 1s infinite alternate;
        }
        @keyframes pulse {
            0% { opacity: 0.7; transform: scale(0.97); }
            100% { opacity: 1; transform: scale(1.03); }
        }

        .status-main {
            font-size: 18px;
            font-weight: 700;
            color: #f8fafc;
            margin-bottom: 4px;
        }
        .status-desc {
            font-size: 12px;
            color: #94a3b8;
        }

        /* Tombol Kontrol Aurora */
        .btn-group {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 14px;
            margin-bottom: 18px;
        }
        .btn-aurora {
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            gap: 6px;
            padding: 16px 14px;
            border-radius: 18px;
            font-size: 14px;
            font-weight: 700;
            cursor: pointer;
            border: 1px solid transparent;
            text-decoration: none;
            transition: all 0.25s ease;
            user-select: none;
        }
        .btn-aurora:active:not(:disabled) {
            transform: scale(0.96);
        }
        .btn-aurora .icon {
            font-size: 22px;
        }
        .btn-aurora .subtext {
            font-size: 10px;
            font-weight: 400;
            opacity: 0.8;
        }

        .btn-open {
            background: linear-gradient(135deg, rgba(0, 242, 254, 0.25), rgba(0, 230, 118, 0.25));
            border-color: rgba(0, 242, 254, 0.4);
            color: #67e8f9;
            box-shadow: 0 0 20px rgba(0, 242, 254, 0.18);
        }
        .btn-open:hover:not(:disabled) {
            background: linear-gradient(135deg, rgba(0, 242, 254, 0.4), rgba(0, 230, 118, 0.4));
            box-shadow: 0 0 28px rgba(0, 242, 254, 0.45);
            transform: translateY(-2px);
            color: #fff;
        }

        .btn-close {
            background: linear-gradient(135deg, rgba(127, 0, 255, 0.25), rgba(225, 0, 255, 0.25));
            border-color: rgba(225, 0, 255, 0.4);
            color: #f0abfc;
            box-shadow: 0 0 20px rgba(225, 0, 255, 0.18);
        }
        .btn-close:hover:not(:disabled) {
            background: linear-gradient(135deg, rgba(127, 0, 255, 0.4), rgba(225, 0, 255, 0.4));
            box-shadow: 0 0 28px rgba(225, 0, 255, 0.45);
            transform: translateY(-2px);
            color: #fff;
        }

        .btn-aurora:disabled {
            opacity: 0.45;
            cursor: not-allowed;
            transform: none !important;
            box-shadow: none !important;
        }

        .telemetry {
            display: flex;
            justify-content: center;
            gap: 10px;
            font-size: 11px;
            color: #64748b;
            padding-top: 14px;
            border-top: 1px solid rgba(255, 255, 255, 0.06);
        }
        .telemetry span {
            background: rgba(255, 255, 255, 0.04);
            padding: 3px 8px;
            border-radius: 6px;
        }
        .footer {
            margin-top: 16px;
            font-size: 11px;
            color: #475569;
        }
    </style>
</head>
<body>
    <div class="aurora-card">
        <div class="header">
            <h1>Kendalikan Tirai Pintar</h1>
            <p>Smart Curtain Stepper Motor A4988 & Cloud IoT</p>
        </div>

        <!-- Ilustrasi Jendela Tirai Visual -->
        <div id="windowFrame" class="window-frame">
            <div id="sceneryIcon" class="window-scenery">☀️</div>
            <div class="curtain-panel curtain-left"></div>
            <div class="curtain-panel curtain-right"></div>
        </div>

        <!-- Panel Status Realtime -->
        <div class="status-box">
            <div id="statusPill" class="status-pill pill-closed">
                <span id="pillDot">●</span>
                <span id="pillText">TERTUTUP RAPAT</span>
            </div>
            <div id="statusMain" class="status-main">Tirai Saat Ini Tertutup</div>
            <div id="statusDesc" class="status-desc">Motor Stepper siap menerima perintah buka</div>
        </div>

        <!-- Tombol Aksi Buka/Tutup -->
        <div class="btn-group">
            <button id="btnOpen" class="btn-aurora btn-open" onclick="triggerCurtain('buka')">
                <span class="icon">🪟</span>
                <span>Buka Tirai</span>
                <span class="subtext">Putar CW 800 Step</span>
            </button>
            <button id="btnClose" class="btn-aurora btn-close" onclick="triggerCurtain('tutup')">
                <span class="icon">🚪</span>
                <span>Tutup Tirai</span>
                <span class="subtext">Putar CCW 800 Step</span>
            </button>
        </div>

        <div class="telemetry">
            <span>NEMA 17</span>
            <span>Driver A4988</span>
            <span>Pin D12 / D13</span>
        </div>

        <div class="footer">
            Integrasi Realtime ThingSpeak (Field 1: Status Buka/Tutup)
        </div>
    </div>

    <script>
        let isOpen = false;
        let isMoving = false;

        const windowFrame = document.getElementById('windowFrame');
        const sceneryIcon = document.getElementById('sceneryIcon');
        const statusPill = document.getElementById('statusPill');
        const pillText = document.getElementById('pillText');
        const statusMain = document.getElementById('statusMain');
        const statusDesc = document.getElementById('statusDesc');
        const btnOpen = document.getElementById('btnOpen');
        const btnClose = document.getElementById('btnClose');

        function updateUI(open, moving, action) {
            isOpen = open;
            isMoving = moving;

            if (moving) {
                statusPill.className = 'status-pill pill-moving';
                btnOpen.disabled = true;
                btnClose.disabled = true;

                if (action === 'opening') {
                    pillText.innerText = 'SEDANG MEMBUKA...';
                    statusMain.innerText = 'Tirai Sedang Dibuka ⏳';
                    statusDesc.innerText = 'Motor stepper sedang berputar 4 putaran (800 step)...';
                    windowFrame.classList.add('open');
                    sceneryIcon.innerText = '☀️';
                } else {
                    pillText.innerText = 'SEDANG MENUTUP...';
                    statusMain.innerText = 'Tirai Sedang Ditutup ⏳';
                    statusDesc.innerText = 'Motor stepper sedang menutup tirai kembali...';
                    windowFrame.classList.remove('open');
                    sceneryIcon.innerText = '🌙';
                }
            } else {
                if (isOpen) {
                    windowFrame.classList.add('open');
                    statusPill.className = 'status-pill pill-open';
                    pillText.innerText = 'TERBUKA LEBAR';
                    statusMain.innerText = 'Tirai Sedang Terbuka ✨';
                    statusDesc.innerText = 'Cahaya leluasa masuk, motor dalam posisi standby';
                    sceneryIcon.innerText = '☀️';
                    btnOpen.disabled = true;
                    btnClose.disabled = false;
                } else {
                    windowFrame.classList.remove('open');
                    statusPill.className = 'status-pill pill-closed';
                    pillText.innerText = 'TERTUTUP RAPAT';
                    statusMain.innerText = 'Tirai Sedang Tertutup 🔒';
                    statusDesc.innerText = 'Ruangan terlindungi, motor dalam posisi standby';
                    sceneryIcon.innerText = '🌙';
                    btnOpen.disabled = false;
                    btnClose.disabled = true;
                }
            }
        }

        function triggerCurtain(action) {
            if (isMoving) return;

            // Beri feedback langsung di antarmuka
            updateUI(action === 'buka', true, action === 'buka' ? 'opening' : 'closing');

            let formData = new URLSearchParams();
            formData.append(action, 'on');
            formData.append('ajax', '1');

            fetch('/tirai', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: formData
            })
            .then(res => res.json())
            .then(data => {
                // Perbarui status final setelah stepper selesai
                updateUI(data.open, false, '');
            })
            .catch(() => {
                fetchStatus();
            });
        }

        function fetchStatus() {
            fetch('/status')
                .then(res => res.json())
                .then(data => {
                    updateUI(data.open, data.moving, data.action);
                })
                .catch(() => {});
        }

        // Ambil status pertama kali saat halaman dibuka
        fetchStatus();
        // Polling status secara berkala setiap 2 detik
        setInterval(fetchStatus, 2000);
    </script>
</body>
</html>)rawliteral";

    return html;
}

// ================= SETUP ===============================
void setup() {
    Serial.begin(115200);
    delay(500);

    pinMode(stepPin, OUTPUT);
    pinMode(dirPin, OUTPUT);
    digitalWrite(stepPin, LOW);
    digitalWrite(dirPin, LOW);

    // WiFi STA Wokwi
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWiFi Connected!");
    Serial.print("Alamat IP Web Server: ");
    Serial.println(WiFi.localIP());

    server.on("/", HTTP_GET, []() {
        server.send(200, "text/html", getHtml());
    });

    server.on("/status", HTTP_GET, []() {
        String json = "{\"open\":" + String(tiraiTerbuka ? "true" : "false") + 
                      ",\"moving\":" + String(isMoving ? "true" : "false") + 
                      ",\"action\":\"" + moveAction + "\"}";
        server.send(200, "application/json", json);
    });

    server.on("/tirai", HTTP_POST, []() {
        bool isAjax = server.hasArg("ajax");

        if (server.hasArg("buka")) {
            if (!tiraiTerbuka && !isMoving) {
                isMoving = true;
                moveAction = "opening";
                Serial.println("[Tirai] Perintah Buka diterima. Memutar motor...");
                
                rotateStepper(LOW, targetSteps); // Putar Searah Jarum Jam (800 step)
                
                tiraiTerbuka = true;
                isMoving = false;
                moveAction = "";
                Serial.println("[Tirai] Tirai Berhasil Terbuka!");
                sendToThingSpeak(1);
            }
        } else if (server.hasArg("tutup")) {
            if (tiraiTerbuka && !isMoving) {
                isMoving = true;
                moveAction = "closing";
                Serial.println("[Tirai] Perintah Tutup diterima. Memutar motor...");
                
                rotateStepper(HIGH, targetSteps); // Putar Berlawanan Jarum Jam (800 step)
                
                tiraiTerbuka = false;
                isMoving = false;
                moveAction = "";
                Serial.println("[Tirai] Tirai Berhasil Tertutup!");
                sendToThingSpeak(0);
            }
        }

        if (isAjax) {
            String json = "{\"open\":" + String(tiraiTerbuka ? "true" : "false") + 
                          ",\"moving\":false,\"action\":\"\"}";
            server.send(200, "application/json", json);
        } else {
            server.send(200, "text/html", getHtml());
        }
    });

    server.begin();
    Serial.println("Web Server Aktif!");
}

// ================= LOOP =================================
void loop() {
    server.handleClient();
    delay(2);
}
