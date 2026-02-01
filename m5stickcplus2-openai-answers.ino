#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>


static const int WIDTH  = 240;
static const int HEIGHT = 135;

static const char* TEXT_IDLE = "Hold M5 button\nto ask your question";
static const char* TEXT_CONNECTING = "Connecting...";
static const char* TEXT_TRANSCRIBING = "Transcribing...";
static const char* TEXT_THINKING = "Thinking...";
static const char* TEXT_MIC_ERROR = "Mic error";
static const char* TEXT_COULDNT_HEAR = "Couldn't hear.\nTry again.";
static const char* TEXT_RECORDING_PREFIX = "Recording... ";

const char* WIFI_SSID     = "name";
const char* WIFI_PASS     = "password";
const char* OPENAI_API_KEY = "sk-proj-xxxx";

// Audio settings
static const int SAMPLE_RATE = 16000;
static const int MAX_RECORD_SECONDS = 10;
static const int MAX_RECORD_SAMPLES = SAMPLE_RATE * MAX_RECORD_SECONDS;
static int16_t* audioBuffer = nullptr;
static int recordedSamples = 0;

String response = TEXT_IDLE;
static unsigned long responseShownAtMs = 0;
static bool idleScreenShown = true;
static unsigned long lastRedrawMs = 0;

// Forward declarations
void drawBatteryOverlay(M5Canvas &canvas);
void drawWifiStatus(M5Canvas &canvas);

static String normalizeDisplayText(const String &text) {
  String out = text;
  // Handle escaped unicode sequences (with or without backslash)
  out.replace("\\u00C7", "C");
  out.replace("u00C7", "C");
  out.replace("\\u00E7", "c");
  out.replace("u00E7", "c");
  out.replace("\\u011E", "G");
  out.replace("u011E", "G");
  out.replace("\\u011F", "g");
  out.replace("u011F", "g");
  out.replace("\\u0130", "I");
  out.replace("u0130", "I");
  out.replace("\\u0131", "i");
  out.replace("u0131", "i");
  out.replace("\\u00D6", "O");
  out.replace("u00D6", "O");
  out.replace("\\u00F6", "o");
  out.replace("u00F6", "o");
  out.replace("\\u015E", "S");
  out.replace("u015E", "S");
  out.replace("\\u015F", "s");
  out.replace("u015F", "s");
  out.replace("\\u00DC", "U");
  out.replace("u00DC", "U");
  out.replace("\\u00FC", "u");
  out.replace("u00FC", "u");

  out.replace("\u00C7", "C");
  out.replace("\u00E7", "c");
  out.replace("\u011E", "G");
  out.replace("\u011F", "g");
  out.replace("\u0130", "I");
  out.replace("\u0131", "i");
  out.replace("\u00D6", "O");
  out.replace("\u00F6", "o");
  out.replace("\u015E", "S");
  out.replace("\u015F", "s");
  out.replace("\u00DC", "U");
  out.replace("\u00FC", "u");
  return out;
}

static String extractTranscriptionText(const String &json) {
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, json);
  if (err) return "";
  if (!doc.containsKey("text")) return "";
  return doc["text"].as<String>();
}

static String extractResponseText(const String &json) {
  DynamicJsonDocument doc(16384);
  DeserializationError err = deserializeJson(doc, json);
  if (err) return "";

  if (doc.containsKey("output_text")) {
    return doc["output_text"].as<String>();
  }

  if (doc.containsKey("output") && doc["output"].is<JsonArray>()) {
    for (JsonObject item : doc["output"].as<JsonArray>()) {
      if (item["type"] == "message" && item["content"].is<JsonArray>()) {
        for (JsonObject content : item["content"].as<JsonArray>()) {
          const char* ctype = content["type"];
          if (ctype && (!strcmp(ctype, "output_text") || !strcmp(ctype, "text"))) {
            return content["text"].as<String>();
          }
        }
      }
    }
  }

  return "";
}

void drawScreen(const String &text) {
  String displayText = normalizeDisplayText(text);
  M5Canvas canvas(&M5.Display);
  canvas.createSprite(WIDTH, HEIGHT);
  canvas.fillSprite(TFT_BLACK);
  canvas.setTextColor(TFT_WHITE);
  canvas.setTextDatum(MC_DATUM);
  canvas.setTextFont(2);

  int lineCount = 1;
  for (unsigned int i = 0; i < displayText.length(); i++) {
    if (displayText[i] == '\n') lineCount++;
  }

  int lineHeight = canvas.fontHeight() + 4;
  int startY = (HEIGHT - lineCount * lineHeight) / 2 + lineHeight / 2;

  int lineNum = 0;
  int lineStart = 0;
  for (unsigned int i = 0; i <= displayText.length(); i++) {
    if (i == displayText.length() || displayText[i] == '\n') {
      String line = displayText.substring(lineStart, i);
      canvas.drawString(line, WIDTH / 2, startY + lineNum * lineHeight);
      lineNum++;
      lineStart = i + 1;
    }
  }
  drawWifiStatus(canvas);
  // draw battery overlay (percent + icon)
  drawBatteryOverlay(canvas);

  canvas.pushSprite(0, 0);
  canvas.deleteSprite();
}

void drawWifiStatus(M5Canvas &canvas) {
  const int x = 6;
  const int y = 5;
  const int bh = 14; // match battery icon height
  const int r3 = bh / 2;     // outer arc radius
  const int r2 = r3 - 2;
  const int r1 = r3 - 4;
  const int cx = x + r3;
  const int cy = y + r3;

  wl_status_t st = WiFi.status();
  uint16_t color = (st == WL_CONNECTED) ? TFT_WHITE : TFT_DARKGREY;

  int bars = 0;
  if (st == WL_CONNECTED) {
    int rssi = WiFi.RSSI();
    if (rssi >= -60) bars = 3;
    else if (rssi >= -75) bars = 2;
    else bars = 1;
  }

  // draw iOS-style arcs (top half of circles) + dot
  if (bars >= 1) canvas.drawCircle(cx, cy, r1, color);
  if (bars >= 2) canvas.drawCircle(cx, cy, r2, color);
  if (bars >= 3) canvas.drawCircle(cx, cy, r3, color);

  // mask lower half to keep only top arcs
  canvas.fillRect(x, cy, r3 * 2 + 2, r3 + 2, TFT_BLACK);

  // center dot
  if (bars >= 1) canvas.fillCircle(cx, cy, 1, color);
}

void drawProgress(int seconds) {
  String msg = String(TEXT_RECORDING_PREFIX) + String(seconds);
  drawScreen(msg);
}

bool recordAudio() {
  Serial.println("\n========== RECORDING ==========");
  Serial.println("Hold button A to record (max 10 seconds)...");
  
  if (!audioBuffer) {
    Serial.printf("Allocating buffer: %d samples, %d bytes\n", MAX_RECORD_SAMPLES, MAX_RECORD_SAMPLES * sizeof(int16_t));
    audioBuffer = (int16_t*)malloc(MAX_RECORD_SAMPLES * sizeof(int16_t));
    if (!audioBuffer) {
      Serial.println("ERROR: Failed to allocate audio buffer!");
      return false;
    }
    Serial.println("Buffer allocated OK");
  } else {
    Serial.println("Using existing buffer");
  }

  Serial.println("Starting mic...");
  M5.Mic.begin();
  
  recordedSamples = 0;
  unsigned long startTime = millis();
  int samplesPerSecond = SAMPLE_RATE;
  bool buttonReleased = false;

  for (int sec = 0; sec < MAX_RECORD_SECONDS; sec++) {
    // Update button state
    M5.update();
    
    // Check if button A is still pressed
    if (!M5.BtnA.isPressed()) {
      buttonReleased = true;
      Serial.printf("Button released at %d seconds\n", sec);
      break;
    }
    
    Serial.printf("Recording second %d/%d...\n", sec + 1, MAX_RECORD_SECONDS);
    drawProgress(MAX_RECORD_SECONDS - sec);
    
    M5.Mic.record(&audioBuffer[sec * samplesPerSecond], samplesPerSecond, SAMPLE_RATE);
    while (M5.Mic.isRecording()) {
      M5.update();
      
      // Check button again during recording
      if (!M5.BtnA.isPressed()) {
        buttonReleased = true;
        Serial.println("Button released during recording");
        break;
      }
      delay(10);
    }
    
    recordedSamples += samplesPerSecond;
    
    if (buttonReleased) {
      break;
    }
  }
  
  M5.Mic.end();
  Serial.printf("Recording complete: %d samples (%d seconds)\n", recordedSamples, recordedSamples / SAMPLE_RATE);

  // Audio stats
  int16_t minVal = 32767, maxVal = -32768;
  int64_t sum = 0;
  for (int i = 0; i < recordedSamples; i++) {
    if (audioBuffer[i] < minVal) minVal = audioBuffer[i];
    if (audioBuffer[i] > maxVal) maxVal = audioBuffer[i];
    sum += abs(audioBuffer[i]);
  }
  Serial.printf("Audio stats: min=%d, max=%d, avg=%lld\n", minVal, maxVal, sum / recordedSamples);
  Serial.println("================================\n");
  
  return recordedSamples > SAMPLE_RATE; // At least 1 second of audio
}

void createWavHeader(uint8_t* header, int dataSize) {
  int fileSize = dataSize + 36;
  int byteRate = SAMPLE_RATE * 1 * 16 / 8;
  int blockAlign = 1 * 16 / 8;

  memcpy(header, "RIFF", 4);
  header[4] = fileSize & 0xFF;
  header[5] = (fileSize >> 8) & 0xFF;
  header[6] = (fileSize >> 16) & 0xFF;
  header[7] = (fileSize >> 24) & 0xFF;
  memcpy(header + 8, "WAVE", 4);
  memcpy(header + 12, "fmt ", 4);
  header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0;
  header[20] = 1; header[21] = 0;
  header[22] = 1; header[23] = 0;
  header[24] = SAMPLE_RATE & 0xFF;
  header[25] = (SAMPLE_RATE >> 8) & 0xFF;
  header[26] = (SAMPLE_RATE >> 16) & 0xFF;
  header[27] = (SAMPLE_RATE >> 24) & 0xFF;
  header[28] = byteRate & 0xFF;
  header[29] = (byteRate >> 8) & 0xFF;
  header[30] = (byteRate >> 16) & 0xFF;
  header[31] = (byteRate >> 24) & 0xFF;
  header[32] = blockAlign; header[33] = 0;
  header[34] = 16; header[35] = 0;
  memcpy(header + 36, "data", 4);
  header[40] = dataSize & 0xFF;
  header[41] = (dataSize >> 8) & 0xFF;
  header[42] = (dataSize >> 16) & 0xFF;
  header[43] = (dataSize >> 24) & 0xFF;
}

String transcribeAudio() {
  Serial.println("\n========== TRANSCRIBING ==========");
  
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(60);

  Serial.println("Connecting to api.openai.com...");
  if (!client.connect("api.openai.com", 443)) {
    Serial.println("ERROR: Connection failed!");
    return "Connection failed";
  }
  Serial.println("Connected");

  int audioDataSize = recordedSamples * sizeof(int16_t);
  uint8_t wavHeader[44];
  createWavHeader(wavHeader, audioDataSize);

  String boundary = "----ESP32Boundary";
  
  String bodyStart = "--" + boundary + "\r\n";
  bodyStart += "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n";
  bodyStart += "Content-Type: audio/wav\r\n\r\n";
  
  String bodyEnd = "\r\n--" + boundary + "\r\n";
  bodyEnd += "Content-Disposition: form-data; name=\"model\"\r\n\r\n";
  bodyEnd += "whisper-1\r\n";
  bodyEnd += "--" + boundary + "--\r\n";

  int contentLength = bodyStart.length() + 44 + audioDataSize + bodyEnd.length();
  Serial.printf("Content length: %d bytes (audio: %d bytes)\n", contentLength, audioDataSize);

  Serial.println("Sending request headers...");
  client.print("POST /v1/audio/transcriptions HTTP/1.1\r\n");
  client.print("Host: api.openai.com\r\n");
  client.print("Authorization: Bearer " + String(OPENAI_API_KEY) + "\r\n");
  client.print("Content-Type: multipart/form-data; boundary=" + boundary + "\r\n");
  client.print("Content-Length: " + String(contentLength) + "\r\n");
  client.print("Connection: close\r\n\r\n");

  Serial.println("Sending audio data...");
  client.print(bodyStart);
  client.write(wavHeader, 44);
  
  // Send in chunks to avoid watchdog
  int chunkSize = 4096;
  uint8_t* ptr = (uint8_t*)audioBuffer;
  int remaining = audioDataSize;
  int sent = 0;
  while (remaining > 0) {
    int toSend = min(chunkSize, remaining);
    client.write(ptr, toSend);
    ptr += toSend;
    remaining -= toSend;
    sent += toSend;
    if (sent % 16384 == 0) {
      Serial.printf("  Sent %d / %d bytes\n", sent, audioDataSize);
    }
    delay(1);
  }
  
  client.print(bodyEnd);
  Serial.println("Request sent, waiting for response...");

  unsigned long timeout = millis();
  while (!client.available()) {
    if (millis() - timeout > 60000) {
      Serial.println("ERROR: Timeout waiting for response!");
      client.stop();
      return "Timeout";
    }
    delay(100);
  }

  Serial.println("Response received, reading headers...");
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    Serial.println("  " + line);
    if (line == "\r") break;
  }

  String response = client.readString();
  client.stop();

  Serial.println("Whisper response body:");
  Serial.println(response);

  String result = extractTranscriptionText(response);
  if (result.length() == 0) {
    Serial.println("ERROR: No 'text' field in response!");
    return "No transcription";
  }
  
  Serial.println("Transcription: " + result);
  Serial.println("==================================\n");
  
  return result;
}

String askGPT(const String &question) {
  Serial.println("\n========== ASKING GPT ==========");
  Serial.println("Question: " + question);
  
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  Serial.println("Connecting to OpenAI...");
  http.begin(client, "https://api.openai.com/v1/responses");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + OPENAI_API_KEY);
  http.setTimeout(90000);

  String escaped = question;
  escaped.replace("\\", "\\\\");
  escaped.replace("\"", "\\\"");
  escaped.replace("\n", " ");

  String body =
    "{"
      "\"model\":\"gpt-5-mini\","
      "\"input\":["
        "{"
          "\"role\":\"user\","
          "\"content\":["
            "{"
              "\"type\":\"input_text\","
              "\"text\":\"" + escaped + " Answer in 20 words or less.\""
            "}"
          "]"
        "}"
      "]"
    "}";

  Serial.println("Sending request...");
  Serial.println("Body: " + body);
  
  int httpCode = http.POST(body);
  Serial.printf("HTTP response code: %d\n", httpCode);

  if (httpCode != 200) {
    String error = http.getString();
    Serial.println("Error response: " + error);
    http.end();
    return "HTTP " + String(httpCode);
  }

  String resp = http.getString();
  http.end();

  Serial.println("GPT response:");
  Serial.println(resp);

  String result = extractResponseText(resp);
  if (result.length() == 0) {
    Serial.println("ERROR: No output text in response!");
    return "No output";
  }

  Serial.println("Extracted answer: " + result);
  Serial.println("=================================\n");

  return result;
}

String wordWrap(const String &text, int maxChars) {
  String result;
  String word;
  int lineLen = 0;
  
  for (unsigned int i = 0; i <= text.length(); i++) {
    char c = (i < text.length()) ? text[i] : ' ';
    
    if (c == ' ' || c == '\n') {
      if (lineLen + word.length() > maxChars) {
        result += '\n';
        lineLen = 0;
      }
      result += word;
      lineLen += word.length();
      word = "";
      
      if (c == ' ' && lineLen > 0) {
        result += ' ';
        lineLen++;
      }
      if (c == '\n') {
        result += '\n';
        lineLen = 0;
      }
    } else {
      word += c;
    }
  }
  
  return result;
}

// Read battery and convert to percent (best-effort for M5 devices)
int getBatteryPercent() {
  // M5 API kullan
  int level = M5.Power.getBatteryLevel();
  if (level >= 0 && level <= 100) return level;

  // Fallback to voltage-based estimate
  float v = M5.Power.getBatteryVoltage();
  if (v > 20.0) v /= 1000.0; // some builds return mV

  // Map 3.0V -> 0% and 4.2V -> 100%
  if (v <= 0.0) return -1;
  int pct = (int)((v - 3.0) / (4.2 - 3.0) * 100.0);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

// Draw small battery icon + percentage on the given canvas (top-right)
void drawBatteryOverlay(M5Canvas &canvas) {
  int pct = getBatteryPercent();

  int bw = 34; // battery box width
  int bh = 14; // battery box height
  int bx = WIDTH - bw - 12; // leave some margin
  int by = 5;

  // Outline and terminal
  canvas.drawRect(bx, by, bw, bh, TFT_WHITE);
  canvas.fillRect(bx + bw, by + bh/3, 4, bh/3, TFT_WHITE);

  if (pct >= 0) {
    int fillW = map(pct, 0, 100, 2, bw - 4);
    uint16_t color = (pct > 60) ? TFT_GREEN : (pct > 30) ? TFT_YELLOW : TFT_RED;
    canvas.fillRect(bx + 2, by + 2, fillW, bh - 4, color);

    canvas.setTextFont(2);
    canvas.setTextDatum(MR_DATUM);
    canvas.setTextColor(TFT_WHITE);
    canvas.drawString(String(pct) + "%", bx - 6, by + bh/2);
  } else {
    canvas.setTextFont(2);
    canvas.setTextDatum(MR_DATUM);
    canvas.setTextColor(TFT_WHITE);
    canvas.drawString("??%", bx - 6, by + bh/2);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n========================================");
  Serial.println("       M5StickC Plus Voice Assistant");
  Serial.println("========================================\n");

  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);

  drawScreen(TEXT_CONNECTING);

  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());

  drawScreen(TEXT_IDLE);
  idleScreenShown = true;
  Serial.println("\nReady! Press button A to ask a question.\n");
}

void loop() {
  M5.update();

  if (M5.BtnA.wasPressed()) {
    Serial.println("\n*** BUTTON A PRESSED ***");
    Serial.println("Holding button to record... Release to stop.\n");
    Serial.printf("Free heap before recording: %d bytes\n", ESP.getFreeHeap());
    
    if (!recordAudio()) {
      drawScreen(TEXT_MIC_ERROR);
      delay(2000);
      drawScreen(TEXT_IDLE);
      return;
    }

    Serial.printf("Free heap after recording: %d bytes\n", ESP.getFreeHeap());

    drawScreen(TEXT_TRANSCRIBING);
    String question = transcribeAudio();

    if (question.length() < 2 || question.startsWith("No ") || question.startsWith("Parse") || question.startsWith("Connection") || question.startsWith("Timeout")) {
      Serial.println("Transcription failed or empty");
      drawScreen(TEXT_COULDNT_HEAR);
      delay(2000);
      drawScreen(TEXT_IDLE);
      return;
    }

    drawScreen(TEXT_THINKING);
    String answer = askGPT(question);
    
    response = wordWrap(answer, 25);
    Serial.println("Final display text:");
    Serial.println(response);
    drawScreen(response);
    responseShownAtMs = millis();
    idleScreenShown = false;
    
    Serial.printf("Free heap at end: %d bytes\n", ESP.getFreeHeap());
    Serial.println("\n*** INTERACTION COMPLETE ***\n");
  }

  if (!idleScreenShown && responseShownAtMs > 0 && (millis() - responseShownAtMs) >= 20000) {
    drawScreen(TEXT_IDLE);
    responseShownAtMs = 0;
    idleScreenShown = true;
  }

  // Periodic refresh so battery/charging indicator updates
  if (millis() - lastRedrawMs >= 300) {
    if (idleScreenShown) {
      drawScreen(TEXT_IDLE);
    } else if (responseShownAtMs > 0) {
      drawScreen(response);
    }
    lastRedrawMs = millis();
  }

  delay(20);
}
