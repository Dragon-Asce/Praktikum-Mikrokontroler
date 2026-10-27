#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include "env.h"

// Konfigurasi Wi-Fi dari .env
const char *ssid = ENV_WIFI_SSID;
const char *password = ENV_WIFI_PASSWORD;

// Konfigurasi ThingSpeak dari .env
const char *thingspeakApiKey = ENV_THINGSPEAK_API_KEY;
const unsigned long thingspeakChannel = ENV_THINGSPEAK_CHANNEL;

// Pin Buzzer & Channel PWM (ESP32 LEDC)
const int buzzerPin = 2; // esp:D2 -> bz1:2
const int buzzerChannel = 0;

WebServer server(80);

// Frekuensi nada (Hz)
const int C  = 261;
const int D  = 294;
const int E  = 329;
const int F  = 349;
const int G  = 392;
const int A  = 440;
const int B  = 493;
const int C2 = 523;

// Passcode default dari .env
const String targetPasscode = ENV_TARGET_PASSCODE;

// Halaman web Keypad Modern
const char *html = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>IoT Musik Bank - Keypad Keamanan</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: 'Segoe UI', -apple-system, BlinkMacSystemFont, Roboto, sans-serif;
            background: linear-gradient(135deg, #0f172a 0%, #1e1b4b 100%);
            min-height: 100vh;
            color: #f8fafc;
            display: flex;
            justify-content: center;
            align-items: center;
            padding: 20px;
        }
        .vault-card {
            width: 100%;
            max-width: 380px;
            background: rgba(30, 41, 59, 0.85);
            backdrop-filter: blur(16px);
            -webkit-backdrop-filter: blur(16px);
            border: 1px solid rgba(255, 255, 255, 0.12);
            border-radius: 24px;
            padding: 28px 24px;
            box-shadow: 0 25px 50px -12px rgba(0, 0, 0, 0.6);
            text-align: center;
        }
        .vault-header h1 {
            font-size: 22px;
            font-weight: 700;
            margin-bottom: 6px;
            background: linear-gradient(90deg, #38bdf8, #a855f7);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }
        .vault-header p {
            font-size: 13px;
            color: #94a3b8;
            margin-bottom: 20px;
        }
        .pin-display-wrapper {
            background: #090d16;
            border: 2px solid #334155;
            border-radius: 14px;
            padding: 14px;
            margin-bottom: 14px;
            display: flex;
            justify-content: center;
            align-items: center;
            letter-spacing: 12px;
            font-size: 26px;
            font-family: monospace;
            height: 56px;
            color: #38bdf8;
            transition: all 0.2s ease;
        }
        .status-msg {
            font-size: 13px;
            font-weight: 600;
            min-height: 22px;
            margin-bottom: 18px;
            transition: all 0.3s ease;
        }
        .status-idle { color: #94a3b8; }
        .status-success { color: #4ade80; }
        .status-error { color: #f87171; }
        .status-loading { color: #38bdf8; }

        .keypad-grid {
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            gap: 12px;
            margin-bottom: 8px;
        }
        .btn-key {
            background: rgba(51, 65, 85, 0.7);
            border: 1px solid rgba(255, 255, 255, 0.08);
            color: #f1f5f9;
            font-size: 22px;
            font-weight: 600;
            height: 56px;
            border-radius: 14px;
            cursor: pointer;
            transition: all 0.15s ease;
            user-select: none;
            display: flex;
            justify-content: center;
            align-items: center;
        }
        .btn-key:hover {
            background: rgba(71, 85, 105, 0.9);
            transform: translateY(-2px);
        }
        .btn-key:active {
            transform: scale(0.95);
        }
        .btn-del {
            background: rgba(239, 68, 68, 0.2);
            color: #fca5a5;
            border-color: rgba(239, 68, 68, 0.3);
            font-size: 15px;
        }
        .btn-del:hover {
            background: rgba(239, 68, 68, 0.35);
        }
        .btn-ok {
            background: linear-gradient(135deg, #10b981, #059669);
            color: #fff;
            border: none;
            font-size: 16px;
            box-shadow: 0 4px 14px rgba(16, 185, 129, 0.4);
        }
        .btn-ok:hover {
            background: linear-gradient(135deg, #34d399, #10b981);
        }
        .footer {
            margin-top: 20px;
            font-size: 11px;
            color: #64748b;
        }
    </style>
</head>
<body>
    <div class="vault-card">
        <div class="vault-header">
            <h1>IoT Musik Bank</h1>
            <p>Masukkan 4 Digit Passcode Musik</p>
        </div>

        <div class="pin-display-wrapper">
            <span id="pinDisplay">_ _ _ _</span>
        </div>

        <div class="status-msg status-idle" id="statusMsg">Siap menerima kode</div>

        <div class="keypad-grid">
            <button type="button" class="btn-key" onclick="pressKey('1')">1</button>
            <button type="button" class="btn-key" onclick="pressKey('2')">2</button>
            <button type="button" class="btn-key" onclick="pressKey('3')">3</button>

            <button type="button" class="btn-key" onclick="pressKey('4')">4</button>
            <button type="button" class="btn-key" onclick="pressKey('5')">5</button>
            <button type="button" class="btn-key" onclick="pressKey('6')">6</button>

            <button type="button" class="btn-key" onclick="pressKey('7')">7</button>
            <button type="button" class="btn-key" onclick="pressKey('8')">8</button>
            <button type="button" class="btn-key" onclick="pressKey('9')">9</button>

            <button type="button" class="btn-key btn-del" onclick="deleteKey()">DEL</button>
            <button type="button" class="btn-key" onclick="pressKey('0')">0</button>
            <button type="button" class="btn-key btn-ok" onclick="submitPasscode()">OK</button>
        </div>

        <div class="footer">
            Passcode Default: 1234 &bull; ESP32 Buzzer & ThingSpeak
        </div>
    </div>

    <script>
        let code = "";
        const display = document.getElementById("pinDisplay");
        const status = document.getElementById("statusMsg");

        function updateDisplay() {
            if (code.length === 0) {
                display.innerText = "_ _ _ _";
            } else {
                let mask = "";
                for (let i = 0; i < code.length; i++) mask += "● ";
                for (let i = code.length; i < 4; i++) mask += "_ ";
                display.innerText = mask.trim();
            }
        }

        function pressKey(val) {
            if (code.length < 4) {
                code += val;
                updateDisplay();
                status.className = "status-msg status-idle";
                status.innerText = "Memasukkan kode...";
            }
        }

        function deleteKey() {
            if (code.length > 0) {
                code = code.slice(0, -1);
                updateDisplay();
            }
            if (code.length === 0) {
                status.className = "status-msg status-idle";
                status.innerText = "Siap menerima kode";
            }
        }

        function submitPasscode() {
            if (code.length !== 4) {
                status.className = "status-msg status-error";
                status.innerText = "Masukkan lengkap 4 digit!";
                return;
            }

            status.className = "status-msg status-loading";
            status.innerText = "Memverifikasi...";

            let formData = new URLSearchParams();
            formData.append('passcode', code);

            fetch('/play', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: formData
            })
            .then(res => {
                if (res.ok) {
                    status.className = "status-msg status-success";
                    status.innerText = "Kode Benar! Memainkan Musik 🎵";
                } else {
                    status.className = "status-msg status-error";
                    status.innerText = "Passcode Salah! Coba lagi ❌";
                }
                setTimeout(() => {
                    code = "";
                    updateDisplay();
                }, 1800);
            })
            .catch(err => {
                status.className = "status-msg status-error";
                status.innerText = "Gagal menghubungi ESP32!";
            });
        }
    </script>
</body>
</html>
)rawliteral";

// Inisialisasi PWM Buzzer menggunakan modul Hardware LEDC ESP32
void setupBuzzer() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(buzzerPin, 2000, 8);
#else
    ledcSetup(buzzerChannel, 2000, 8);
    ledcAttachPin(buzzerPin, buzzerChannel);
#endif
}

// Memainkan satu nada dengan PWM LEDC
void playTone(int freq, int duration) {
    if (freq > 0) {
        Serial.printf("[Buzzer] Mainkan nada: %d Hz (%d ms)\n", freq, duration);
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
        ledcWriteTone(buzzerPin, freq);
#else
        ledcWriteTone(buzzerChannel, freq);
#endif
    }
    delay(duration);

    // Matikan nada
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWriteTone(buzzerPin, 0);
#else
    ledcWriteTone(buzzerChannel, 0);
#endif
    delay(60);
}

void playMelody() {
    Serial.println("[Buzzer] === Memulai Melodi Musik Bank ===");
    int melody[] = {C, E, C, E, F, G, G, B, C2, B, C2, B, G};
    int totalNotes = sizeof(melody) / sizeof(melody[0]);

    for (int i = 0; i < 2; i++) { // Ulangi 2 kali sesuai modul
        for (int j = 0; j < totalNotes; j++) {
            playTone(melody[j], 300);
        }
        delay(200);
    }
    Serial.println("[Buzzer] === Selesai Memainkan Melodi ===");
}

void sendToThingSpeak(int field, int status) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String url = "http://api.thingspeak.com/update?api_key=" + String(thingspeakApiKey) +
                     "&field" + String(field) + "=" + String(status);
        http.begin(url);
        http.setTimeout(3000);
        int httpCode = http.GET();
        if (httpCode > 0) {
            String payload = http.getString();
            payload.trim();
            if (payload == "0") {
                Serial.println("[ThingSpeak] Gagal: Rate limit atau Write API Key salah!");
            } else {
                Serial.printf("[ThingSpeak] Terkirim! Entry ID: %s (Field%d = %d)\n", payload.c_str(), field, status);
            }
        } else {
            Serial.printf("[ThingSpeak] Gagal koneksi, Error: %s\n", http.errorToString(httpCode).c_str());
        }
        http.end();
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);

    // Setup Hardware PWM Buzzer
    setupBuzzer();

    // Bunyi bip singkat tanda buzzer aktif & siap
    playTone(1000, 150);

    // Konek ke Wi-Fi Wokwi
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
        server.send(200, "text/html", html);
    });

    server.on("/play", HTTP_POST, []() {
        if (server.hasArg("passcode")) {
            String entered = server.arg("passcode");
            if (entered == targetPasscode) {
                server.send(200, "text/plain", "OK");
                Serial.println("\n[Web] Passcode Benar (1234)! Memainkan musik...");
                sendToThingSpeak(1, 1);
                playMelody();
            } else {
                server.send(403, "text/plain", "Passcode salah!");
                Serial.printf("\n[Web] Passcode Salah: %s\n", entered.c_str());
                // Bunyi peringatan nada rendah tanda salah
                playTone(150, 400);
            }
        } else {
            server.send(400, "text/plain", "Passcode tidak ditemukan!");
        }
    });

    server.begin();
    Serial.println("Web Server Aktif!");
}

void loop() {
    server.handleClient();
    delay(2);
}

