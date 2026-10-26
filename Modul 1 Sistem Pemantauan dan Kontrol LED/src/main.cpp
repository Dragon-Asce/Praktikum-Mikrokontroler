#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include "env.h"

// Konfigurasi Wi-Fi dari .env
const char *ssid = ENV_WIFI_SSID;
const char *password = ENV_WIFI_PASSWORD;

// Konfigurasi ThingSpeak API dari .env
const char *thingspeakApiKey = ENV_THINGSPEAK_API_KEY;
const unsigned long thingspeakChannel = ENV_THINGSPEAK_CHANNEL;

// Pin LED (Sesuai diagram.json DevKit V1)
const int ledPin1 = 21; // esp:D21 -> led1 (Merah)
const int ledPin2 = 18; // esp:D18 -> led2 (Biru)
const int ledPin3 = 5;  // esp:D5  -> led3 (Hijau)
const int ledPin4 = 19; // esp:D19 -> led4 (Kuning)

WebServer server(80);

// Status LED (0 = Mati, 1 = Nyala)
int stateLED1 = 0;
int stateLED2 = 0;
int stateLED3 = 0;
int stateLED4 = 0;

// Variabel Waktu ThingSpeak (Rate Limiting)
unsigned long lastThingSpeakUpdate = 0;
const unsigned long thingspeakInterval = 15000; // Minimal 15 detik sekali
bool needsThingSpeakUpdate = false;

// Fungsi pembuat tampilan HTML dengan CSS modern dan responsif
String getHtml() {
    String html = R"rawliteral(<!DOCTYPE html>
<html lang="id">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Kontrol & Pemantauan LED IoT</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: 'Segoe UI', -apple-system, BlinkMacSystemFont, Roboto, sans-serif;
            background: linear-gradient(135deg, #0f172a 0%, #1e293b 100%);
            min-height: 100vh;
            color: #f8fafc;
            display: flex;
            justify-content: center;
            align-items: center;
            padding: 24px 16px;
        }
        .container {
            width: 100%;
            max-width: 640px;
            background: rgba(30, 41, 59, 0.75);
            backdrop-filter: blur(16px);
            -webkit-backdrop-filter: blur(16px);
            border: 1px solid rgba(255, 255, 255, 0.1);
            border-radius: 20px;
            padding: 30px;
            box-shadow: 0 20px 40px rgba(0, 0, 0, 0.45);
        }
        .header {
            text-align: center;
            margin-bottom: 26px;
        }
        .header h1 {
            font-size: 26px;
            font-weight: 700;
            margin-bottom: 8px;
            background: linear-gradient(90deg, #38bdf8, #818cf8);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }
        .header p {
            font-size: 14px;
            color: #94a3b8;
        }
        .grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
            gap: 16px;
        }
        .card {
            background: rgba(15, 23, 42, 0.65);
            border: 1px solid rgba(255, 255, 255, 0.08);
            border-radius: 14px;
            padding: 18px;
            transition: transform 0.2s ease, box-shadow 0.2s ease;
        }
        .card:hover {
            transform: translateY(-2px);
            box-shadow: 0 8px 24px rgba(0, 0, 0, 0.35);
        }
        .card-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 14px;
        }
        .card-title {
            display: flex;
            align-items: center;
            gap: 10px;
            font-size: 15px;
            font-weight: 600;
        }
        .dot {
            width: 12px;
            height: 12px;
            border-radius: 50%;
            display: inline-block;
            transition: all 0.3s ease;
        }
        .dot-red.on { background: #ef4444; box-shadow: 0 0 12px #ef4444; }
        .dot-blue.on { background: #3b82f6; box-shadow: 0 0 12px #3b82f6; }
        .dot-green.on { background: #10b981; box-shadow: 0 0 12px #10b981; }
        .dot-yellow.on { background: #f59e0b; box-shadow: 0 0 12px #f59e0b; }
        .dot.off { background: #475569; box-shadow: none; }

        .badge {
            font-size: 11px;
            font-weight: 700;
            padding: 3px 8px;
            border-radius: 6px;
            letter-spacing: 0.5px;
        }
        .badge-on { background: rgba(34, 197, 94, 0.15); color: #4ade80; border: 1px solid rgba(74, 222, 128, 0.3); }
        .badge-off { background: rgba(148, 163, 184, 0.1); color: #94a3b8; border: 1px solid rgba(148, 163, 184, 0.2); }

        .btn-group {
            display: flex;
            gap: 8px;
        }
        .btn {
            flex: 1;
            padding: 10px 12px;
            font-size: 13px;
            font-weight: 600;
            border: none;
            border-radius: 8px;
            cursor: pointer;
            transition: all 0.2s ease;
        }
        .btn:active {
            transform: scale(0.96);
        }
        .btn-red { background: linear-gradient(135deg, #ef4444, #dc2626); color: #fff; box-shadow: 0 4px 12px rgba(239, 68, 68, 0.35); }
        .btn-red:hover { background: linear-gradient(135deg, #f87171, #ef4444); }

        .btn-blue { background: linear-gradient(135deg, #3b82f6, #2563eb); color: #fff; box-shadow: 0 4px 12px rgba(59, 130, 246, 0.35); }
        .btn-blue:hover { background: linear-gradient(135deg, #60a5fa, #3b82f6); }

        .btn-green { background: linear-gradient(135deg, #10b981, #059669); color: #fff; box-shadow: 0 4px 12px rgba(16, 185, 129, 0.35); }
        .btn-green:hover { background: linear-gradient(135deg, #34d399, #10b981); }

        .btn-yellow { background: linear-gradient(135deg, #f59e0b, #d97706); color: #fff; box-shadow: 0 4px 12px rgba(245, 158, 11, 0.35); }
        .btn-yellow:hover { background: linear-gradient(135deg, #fbbf24, #f59e0b); }

        .btn-off { background: #334155; color: #cbd5e1; }
        .btn-off:hover { background: #475569; color: #fff; }

        .footer {
            margin-top: 24px;
            text-align: center;
            font-size: 12px;
            color: #64748b;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>Kontrol & Pemantauan LED</h1>
            <p>ESP32 Web Server terintegrasi Cloud ThingSpeak</p>
        </div>
        <form action="/led" method="POST">
            <div class="grid">
)rawliteral";

    // Card 1: Lampu 1 (Merah)
    html += "<div class='card'>";
    html += "<div class='card-header'><div class='card-title'>";
    html += "<span class='dot dot-red " + String(stateLED1 ? "on" : "off") + "'></span>Lampu 1 (Merah)";
    html += "</div><span class='badge " + String(stateLED1 ? "badge-on'>NYALA" : "badge-off'>MATI") + "</span></div>";
    html += "<div class='btn-group'>";
    html += "<button class='btn btn-red' type='submit' name='nyala_1'>Nyalakan</button>";
    html += "<button class='btn btn-off' type='submit' name='mati_1'>Matikan</button>";
    html += "</div></div>";

    // Card 2: Lampu 2 (Biru)
    html += "<div class='card'>";
    html += "<div class='card-header'><div class='card-title'>";
    html += "<span class='dot dot-blue " + String(stateLED2 ? "on" : "off") + "'></span>Lampu 2 (Biru)";
    html += "</div><span class='badge " + String(stateLED2 ? "badge-on'>NYALA" : "badge-off'>MATI") + "</span></div>";
    html += "<div class='btn-group'>";
    html += "<button class='btn btn-blue' type='submit' name='nyala_2'>Nyalakan</button>";
    html += "<button class='btn btn-off' type='submit' name='mati_2'>Matikan</button>";
    html += "</div></div>";

    // Card 3: Lampu 3 (Hijau)
    html += "<div class='card'>";
    html += "<div class='card-header'><div class='card-title'>";
    html += "<span class='dot dot-green " + String(stateLED3 ? "on" : "off") + "'></span>Lampu 3 (Hijau)";
    html += "</div><span class='badge " + String(stateLED3 ? "badge-on'>NYALA" : "badge-off'>MATI") + "</span></div>";
    html += "<div class='btn-group'>";
    html += "<button class='btn btn-green' type='submit' name='nyala_3'>Nyalakan</button>";
    html += "<button class='btn btn-off' type='submit' name='mati_3'>Matikan</button>";
    html += "</div></div>";

    // Card 4: Lampu 4 (Kuning)
    html += "<div class='card'>";
    html += "<div class='card-header'><div class='card-title'>";
    html += "<span class='dot dot-yellow " + String(stateLED4 ? "on" : "off") + "'></span>Lampu 4 (Kuning)";
    html += "</div><span class='badge " + String(stateLED4 ? "badge-on'>NYALA" : "badge-off'>MATI") + "</span></div>";
    html += "<div class='btn-group'>";
    html += "<button class='btn btn-yellow' type='submit' name='nyala_4'>Nyalakan</button>";
    html += "<button class='btn btn-off' type='submit' name='mati_4'>Matikan</button>";
    html += "</div></div>";

    html += R"rawliteral(
            </div>
        </form>
        <div class="footer">
            Status LED diperbarui realtime ke Cloud ThingSpeak (Rate Limit: 15s)
        </div>
    </div>
</body>
</html>)rawliteral";

    return html;
}

// Fungsi Pengiriman HTTP Asinkron ke ThingSpeak
void sendToThingSpeak() {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        
        // Buat URL Update Multi-field sekaligus
        String url = "http://api.thingspeak.com/update?api_key=" + String(thingspeakApiKey) +
                     "&field1=" + String(stateLED1) +
                     "&field2=" + String(stateLED2) +
                     "&field3=" + String(stateLED3) +
                     "&field4=" + String(stateLED4);

        http.begin(url);
        http.setTimeout(3000); // Set timeout maksimal 3 detik agar tidak membuat ESP32 menggantung
        
        int httpCode = http.GET();
        if (httpCode > 0) {
            String payload = http.getString();
            payload.trim();
            if (payload == "0") {
                Serial.println("[ThingSpeak] Gagal: Kena Rate Limit (tunggu 15 detik) atau Write API Key salah!");
            } else {
                Serial.printf("[ThingSpeak] Berhasil disimpan! Entry ID: %s (Status: L1=%d, L2=%d, L3=%d, L4=%d)\n",
                              payload.c_str(), stateLED1, stateLED2, stateLED3, stateLED4);
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

    pinMode(ledPin1, OUTPUT);
    pinMode(ledPin2, OUTPUT);
    pinMode(ledPin3, OUTPUT);
    pinMode(ledPin4, OUTPUT);

    digitalWrite(ledPin1, LOW);
    digitalWrite(ledPin2, LOW);
    digitalWrite(ledPin3, LOW);
    digitalWrite(ledPin4, LOW);

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

    // Routing Web Server
    server.on("/", HTTP_GET, []() {
        server.send(200, "text/html", getHtml());
    });

    server.on("/led", HTTP_POST, []() {
        if (server.hasArg("nyala_1")) { stateLED1 = 1; digitalWrite(ledPin1, HIGH); }
        else if (server.hasArg("mati_1")) { stateLED1 = 0; digitalWrite(ledPin1, LOW); }
        else if (server.hasArg("nyala_2")) { stateLED2 = 1; digitalWrite(ledPin2, HIGH); }
        else if (server.hasArg("mati_2")) { stateLED2 = 0; digitalWrite(ledPin2, LOW); }
        else if (server.hasArg("nyala_3")) { stateLED3 = 1; digitalWrite(ledPin3, HIGH); }
        else if (server.hasArg("mati_3")) { stateLED3 = 0; digitalWrite(ledPin3, LOW); }
        else if (server.hasArg("nyala_4")) { stateLED4 = 1; digitalWrite(ledPin4, HIGH); }
        else if (server.hasArg("mati_4")) { stateLED4 = 0; digitalWrite(ledPin4, LOW); }

        server.send(200, "text/html", getHtml());
        needsThingSpeakUpdate = true; // Tandai bahwa ada perubahan status yang perlu dikirim
    });

    server.begin();
    Serial.println("Web server aktif!");
}

void loop() {
    server.handleClient();

    // Kirim data ke ThingSpeak tanpa mengganggu Web Server (Menggunakan millis non-blocking)
    // Pengiriman pertama kali langsung dieksekusi (lastThingSpeakUpdate == 0)
    if (needsThingSpeakUpdate && (lastThingSpeakUpdate == 0 || millis() - lastThingSpeakUpdate >= thingspeakInterval)) {
        sendToThingSpeak();
        lastThingSpeakUpdate = millis();
        needsThingSpeakUpdate = false;
    }

    delay(2); // Menjaga stabilitas CPU
}
