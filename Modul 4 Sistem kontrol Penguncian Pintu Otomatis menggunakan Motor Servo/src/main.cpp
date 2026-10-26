#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <HTTPClient.h>
#include "env.h"

// ================= KONFIGURASI WI-FI =================
const char* ssid = ENV_WIFI_SSID;
const char* password = ENV_WIFI_PASSWORD;

// ================= KONFIGURASI THINGSPEAK =============
const char *thingspeakApiKey = ENV_THINGSPEAK_API_KEY;
const unsigned long thingspeakChannel = ENV_THINGSPEAK_CHANNEL;

// ================= KONFIGURASI SERVO ==================
Servo myservo;

// Pin Servo sesuai diagram.json (esp:D2 -> servo1:PWM)
const int servoPin = 2;
const int servoClosedPos = 0;
const int servoOpenPos = 90;

// ================= KONFIGURASI PASSWORD ===============
const char correctPassword[] = ENV_DOOR_PASSWORD;
const int passwordLength = 6;

bool isLocked = true;
bool isUnlocking = false;

unsigned long unlockStartTime = 0;
unsigned long lastThingSpeakUpdate = 0;

const unsigned long delayDuration = 5; // Durasi terbuka dalam detik
const unsigned long thingspeakInterval = 15000; // 15 detik

WebServer server(80);

// Kirim status ke ThingSpeak via HTTPClient non-blocking
void sendToThingSpeak() {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        int statusVal = isLocked ? 0 : 1;
        int stateVal  = isUnlocking ? 2 : (isLocked ? 0 : 1);
        
        String url = "http://api.thingspeak.com/update?api_key=" + String(thingspeakApiKey) +
                     "&field1=" + String(statusVal) +
                     "&field2=" + String(stateVal) +
                     "&field3=" + String(delayDuration);

        http.begin(url);
        http.setTimeout(2000);
        int httpCode = http.GET();
        if (httpCode > 0) {
            Serial.printf("[ThingSpeak] Data pintu terkirim! Respon: %d\n", httpCode);
        } else {
            Serial.printf("[ThingSpeak] Gagal mengirim: %s\n", http.errorToString(httpCode).c_str());
        }
        http.end();
    }
}

// ================= HALAMAN WEB (NEUMORPHISM) ==========
void handleRoot() {
    String html = R"rawliteral(<!DOCTYPE html>
<html lang="id">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Sistem Smart Lock Pintu IoT</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: 'Segoe UI', -apple-system, BlinkMacSystemFont, Roboto, sans-serif;
            background: #e0e5ec;
            min-height: 100vh;
            color: #4a5568;
            display: flex;
            justify-content: center;
            align-items: center;
            padding: 24px 16px;
        }
        .neu-card {
            width: 100%;
            max-width: 360px;
            background: #e0e5ec;
            border-radius: 32px;
            padding: 34px 26px;
            box-shadow: 14px 14px 28px #b8c4d6, -14px -14px 28px #ffffff;
            text-align: center;
        }
        .header h1 {
            font-size: 22px;
            font-weight: 700;
            color: #2d3748;
            margin-bottom: 4px;
        }
        .header p {
            font-size: 13px;
            color: #718096;
            margin-bottom: 22px;
        }
        .status-container {
            margin-bottom: 20px;
        }
        .status-pill {
            display: inline-flex;
            align-items: center;
            gap: 8px;
            padding: 10px 22px;
            border-radius: 50px;
            font-size: 14px;
            font-weight: 700;
            letter-spacing: 0.5px;
            background: #e0e5ec;
            box-shadow: 6px 6px 12px #b8c4d6, -6px -6px 12px #ffffff;
            transition: all 0.3s ease;
        }
        .status-pill.locked {
            color: #e53e3e;
        }
        .status-pill.unlocked {
            color: #38a169;
            box-shadow: inset 4px 4px 8px #b8c4d6, inset -4px -4px 8px #ffffff;
        }
        .status-sub {
            font-size: 12px;
            color: #718096;
            margin-top: 8px;
            min-height: 18px;
            font-weight: 600;
            transition: color 0.3s ease;
        }
        .pin-screen {
            background: #e0e5ec;
            border-radius: 20px;
            padding: 14px 20px;
            margin-bottom: 22px;
            box-shadow: inset 5px 5px 10px #b8c4d6, inset -5px -5px 10px #ffffff;
            display: flex;
            justify-content: center;
            align-items: center;
            height: 56px;
        }
        .pin-dots {
            display: flex;
            gap: 12px;
        }
        .dot {
            width: 14px;
            height: 14px;
            border-radius: 50%;
            background: #e0e5ec;
            box-shadow: inset 2px 2px 4px #b8c4d6, inset -2px -2px 4px #ffffff;
            transition: all 0.2s ease;
        }
        .dot.filled {
            background: #3182ce;
            box-shadow: 0 0 10px rgba(49, 130, 206, 0.5);
            transform: scale(1.15);
        }
        .keypad-grid {
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            gap: 14px;
            margin-bottom: 20px;
        }
        .neu-btn {
            background: #e0e5ec;
            border: none;
            outline: none;
            height: 58px;
            border-radius: 18px;
            font-size: 22px;
            font-weight: 600;
            color: #2d3748;
            cursor: pointer;
            box-shadow: 6px 6px 12px #b8c4d6, -6px -6px 12px #ffffff;
            transition: all 0.15s ease;
            user-select: none;
            display: flex;
            justify-content: center;
            align-items: center;
        }
        .neu-btn:hover {
            box-shadow: 4px 4px 8px #b8c4d6, -4px -4px 8px #ffffff;
            color: #1a202c;
        }
        .neu-btn:active {
            box-shadow: inset 4px 4px 8px #b8c4d6, inset -4px -4px 8px #ffffff;
            transform: scale(0.96);
        }
        .btn-clear {
            color: #e53e3e;
            font-size: 18px;
            font-weight: 700;
        }
        .btn-enter {
            color: #38a169;
            font-size: 18px;
            font-weight: 700;
        }
        .footer {
            font-size: 11px;
            color: #a0aec0;
        }
    </style>
</head>
<body>
    <div class="neu-card">
        <div class="header">
            <h1>Kontrol Pintu Smart</h1>
            <p>ESP32 Servo & Cloud ThingSpeak</p>
        </div>

        <div class="status-container">
            <div id="statusPill" class="status-pill locked">
                <span id="statusIcon">🔒</span>
                <span id="statusText">PINTU TERKUNCI</span>
            </div>
            <div id="statusSub" class="status-sub">Siap menerima PIN</div>
        </div>

        <div class="pin-screen">
            <div class="pin-dots">
                <span class="dot"></span>
                <span class="dot"></span>
                <span class="dot"></span>
                <span class="dot"></span>
                <span class="dot"></span>
                <span class="dot"></span>
            </div>
        </div>

        <div class="keypad-grid">
            <button type="button" class="neu-btn" onclick="appendDigit('1')">1</button>
            <button type="button" class="neu-btn" onclick="appendDigit('2')">2</button>
            <button type="button" class="neu-btn" onclick="appendDigit('3')">3</button>

            <button type="button" class="neu-btn" onclick="appendDigit('4')">4</button>
            <button type="button" class="neu-btn" onclick="appendDigit('5')">5</button>
            <button type="button" class="neu-btn" onclick="appendDigit('6')">6</button>

            <button type="button" class="neu-btn" onclick="appendDigit('7')">7</button>
            <button type="button" class="neu-btn" onclick="appendDigit('8')">8</button>
            <button type="button" class="neu-btn" onclick="appendDigit('9')">9</button>

            <button type="button" class="neu-btn btn-clear" onclick="clearPin()">C</button>
            <button type="button" class="neu-btn" onclick="appendDigit('0')">0</button>
            <button type="button" class="neu-btn btn-enter" onclick="submitPin()">E</button>
        </div>

        <div class="footer">
            Otomatis menutup dalam 5 detik &bull; PIN: 123456
        </div>
    </div>

    <script>
        let pin = "";
        const maxLen = 6;
        const dots = document.querySelectorAll(".dot");
        const statusPill = document.getElementById("statusPill");
        const statusIcon = document.getElementById("statusIcon");
        const statusText = document.getElementById("statusText");
        const statusSub = document.getElementById("statusSub");

        let countdownInterval = null;

        function updateDots() {
            dots.forEach((dot, index) => {
                if (index < pin.length) dot.classList.add("filled");
                else dot.classList.remove("filled");
            });
        }

        function appendDigit(digit) {
            if (pin.length < maxLen) {
                pin += digit;
                updateDots();
                statusSub.style.color = "#718096";
                statusSub.innerText = "Memasukkan PIN (" + pin.length + "/" + maxLen + ")";
            }
        }

        function clearPin() {
            pin = "";
            updateDots();
            statusSub.style.color = "#718096";
            statusSub.innerText = "PIN dibersihkan";
        }

        function setUIState(locked) {
            if (locked) {
                statusPill.className = "status-pill locked";
                statusIcon.innerText = "🔒";
                statusText.innerText = "PINTU TERKUNCI";
            } else {
                statusPill.className = "status-pill unlocked";
                statusIcon.innerText = "🔓";
                statusText.innerText = "PINTU TERBUKA";
            }
        }

        function submitPin() {
            if (pin.length !== maxLen) {
                statusSub.style.color = "#e53e3e";
                statusSub.innerText = "Masukkan lengkap 6 digit!";
                return;
            }

            statusSub.style.color = "#3182ce";
            statusSub.innerText = "Memverifikasi...";

            fetch('/toggle?password=' + encodeURIComponent(pin) + '&ajax=1')
                .then(res => res.json())
                .then(data => {
                    if (data.success) {
                        setUIState(false);
                        let remaining = 5;
                        statusSub.style.color = "#38a169";
                        statusSub.innerText = "Pintu Terbuka! Menutup otomatis dalam " + remaining + "s...";
                        
                        if (countdownInterval) clearInterval(countdownInterval);
                        countdownInterval = setInterval(() => {
                            remaining--;
                            if (remaining > 0) {
                                statusSub.innerText = "Pintu Terbuka! Menutup otomatis dalam " + remaining + "s...";
                            } else {
                                clearInterval(countdownInterval);
                                statusSub.innerText = "Mengunci kembali...";
                                fetchStatus();
                            }
                        }, 1000);
                    } else {
                        statusSub.style.color = "#e53e3e";
                        statusSub.innerText = "Password Salah!";
                        setTimeout(clearPin, 800);
                    }
                })
                .catch(() => {
                    statusSub.style.color = "#e53e3e";
                    statusSub.innerText = "Gagal terhubung ke ESP32!";
                });
        }

        function fetchStatus() {
            fetch('/status')
                .then(res => res.json())
                .then(data => {
                    if (data.locked) {
                        setUIState(true);
                        if (countdownInterval) clearInterval(countdownInterval);
                        if (statusSub.innerText.indexOf("Menutup") !== -1 || statusSub.innerText.indexOf("Mengunci") !== -1) {
                            statusSub.style.color = "#718096";
                            statusSub.innerText = "Siap menerima PIN";
                            clearPin();
                        }
                    } else {
                        setUIState(false);
                    }
                })
                .catch(() => {});
        }

        // Polling status secara realtime setiap 1 detik
        // Ini memastikan saat ESP32 mengunci kembali setelah 5 detik, teks otomatis kembali ke "PINTU TERKUNCI"
        setInterval(fetchStatus, 1000);
    </script>
</body>
</html>)rawliteral";

    server.send(200, "text/html", html);
}

// ================= API STATUS REALTIME ================
void handleStatus() {
    String json = "{\"locked\":" + String(isLocked ? "true" : "false") + 
                  ",\"unlocking\":" + String(isUnlocking ? "true" : "false") + 
                  ",\"status\":\"" + (isLocked ? "Terkunci" : "Terbuka") + "\"}";
    server.send(200, "application/json", json);
}

// ================= KONTROL PINTU =======================
void handleToggle() {
    String requestPassword = server.arg("password");

    if (requestPassword.length() != passwordLength) {
        if (server.hasArg("ajax")) {
            server.send(400, "application/json", "{\"success\":false,\"message\":\"Panjang password tidak valid\"}");
        } else {
            server.send(400, "text/plain", "Panjang password tidak valid");
        }
        return;
    }

    if (requestPassword != correctPassword) {
        if (server.hasArg("ajax")) {
            server.send(403, "application/json", "{\"success\":false,\"message\":\"Password Salah!\"}");
        } else {
            server.sendHeader("Location", "/?error=1");
            server.send(303, "text/plain", "");
        }
        return;
    }

    // Membuka pintu
    myservo.write(servoOpenPos);
    isLocked = false;
    isUnlocking = true;
    unlockStartTime = millis();
    Serial.println("Password Benar (123456)! Pintu dibuka.");
    sendToThingSpeak();

    if (server.hasArg("ajax")) {
        server.send(200, "application/json", "{\"success\":true,\"message\":\"Pintu Terbuka!\"}");
    } else {
        server.sendHeader("Location", "/");
        server.send(303, "text/plain", "");
    }
}

// ================= SETUP ===============================
void setup() {
    Serial.begin(115200);
    delay(500);

    // Setup ESP32Servo
    ESP32PWM::allocateTimer(0);
    myservo.setPeriodHertz(50);          // Standard 50Hz Servo
    myservo.attach(servoPin, 500, 2400); // Attach pin 2 dengan pulsa standar
    myservo.write(servoClosedPos);

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

    server.on("/", HTTP_GET, handleRoot);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/toggle", HTTP_GET, handleToggle);

    server.begin();
    Serial.println("Web Server Aktif!");
}

// ================= LOOP =================================
void loop() {
    server.handleClient();

    unsigned long currentTime = millis();

    // Mengunci pintu kembali setelah durasi pembukaan (5 detik)
    if (isUnlocking && (currentTime - unlockStartTime >= delayDuration * 1000UL)) {
        myservo.write(servoClosedPos);
        isLocked = true;
        isUnlocking = false;
        Serial.println("Pintu dikunci otomatis setelah 5 detik.");
        sendToThingSpeak(); // Kirim update status terkunci ke ThingSpeak
    }

    // Mengirim status berkala ke ThingSpeak setiap 15 detik
    if (currentTime - lastThingSpeakUpdate >= thingspeakInterval) {
        sendToThingSpeak();
        lastThingSpeakUpdate = currentTime;
    }

    delay(2);
}
