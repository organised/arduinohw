#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

static const int WIDTH  = 240;
static const int HEIGHT = 135;

const char* WIFI_SSID     = "name";
const char* WIFI_PASS     = "password";
const char* OPENAI_API_KEY = "sk-proj-xxxx";

// Audio settings
static const int SAMPLE_RATE = 16000;
static const int RECORD_SECONDS = 5;
static const int RECORD_SAMPLES = SAMPLE_RATE * RECORD_SECONDS;
static int16_t* audioBuffer = nullptr;

String response = "Press A\nto ask a question";

void drawScreen(const String &text) {
  M5Canvas canvas(&M5.Display);
  canvas.createSprite(WIDTH, HEIGHT);
  canvas.fillSprite(TFT_BLACK);
  canvas.setTextColor(TFT_WHITE);
  canvas.setTextDatum(MC_DATUM);
  canvas.setTextFont(2);

  int lineCount = 1;
  for (unsigned int i = 0; i < text.length(); i++) {
    if (text[i] == '\n') lineCount++;
  }

  int lineHeight = canvas.fontHeight() + 4;
  int startY = (HEIGHT - lineCount * lineHeight) / 2 + lineHeight / 2;

  int lineNum = 0;
  int lineStart = 0;
  for (unsigned int i = 0; i <= text.length(); i++) {
    if (i == text.length() || text[i] == '\n') {
      String line = text.substring(lineStart, i);
      canvas.drawString(line, WIDTH / 2, startY + lineNum * lineHeight);
      lineNum++;
      lineStart = i + 1;
    }
  }

  canvas.pushSprite(0, 0);
  canvas.deleteSprite();
}

void drawProgress(int seconds) {
  String msg = "Recording... " + String(seconds);
  drawScreen(msg);
}

bool recordAudio() {
  Serial.println("\n========== RECORDING ==========");
  
  if (!audioBuffer) {
    Serial.printf("Allocating buffer: %d samples, %d bytes\n", RECORD_SAMPLES, RECORD_SAMPLES * sizeof(int16_t));
    audioBuffer = (int16_t*)malloc(RECORD_SAMPLES * sizeof(int16_t));
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
  
  int samplesPerSecond = SAMPLE_RATE;
  for (int sec = 0; sec < RECORD_SECONDS; sec++) {
    Serial.printf("Recording second %d/%d...\n", sec + 1, RECORD_SECONDS);
    drawProgress(RECORD_SECONDS - sec);
    M5.Mic.record(&audioBuffer[sec * samplesPerSecond], samplesPerSecond, SAMPLE_RATE);
    while (M5.Mic.isRecording()) {
      delay(10);
    }
  }
  
  M5.Mic.end();
  Serial.println("Recording complete");

  // Audio stats
  int16_t minVal = 32767, maxVal = -32768;
  int64_t sum = 0;
  for (int i = 0; i < RECORD_SAMPLES; i++) {
    if (audioBuffer[i] < minVal) minVal = audioBuffer[i];
    if (audioBuffer[i] > maxVal) maxVal = audioBuffer[i];
    sum += abs(audioBuffer[i]);
  }
  Serial.printf("Audio stats: min=%d, max=%d, avg=%lld\n", minVal, maxVal, sum / RECORD_SAMPLES);
  Serial.println("================================\n");
  
  return true;
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

  int audioDataSize = RECORD_SAMPLES * sizeof(int16_t);
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

  int textIdx = response.indexOf("\"text\"");
  if (textIdx < 0) {
    Serial.println("ERROR: No 'text' field in response!");
    return "No transcription";
  }
  
  int start = response.indexOf('"', textIdx + 6);
  if (start < 0) {
    Serial.println("ERROR: Parse error!");
    return "Parse error";
  }
  start++;
  
  String result;
  bool escaped = false;
  for (unsigned int i = start; i < response.length(); i++) {
    char c = response[i];
    if (escaped) {
      result += c;
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else if (c == '"') {
      break;
    } else {
      result += c;
    }
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

  int outIdx = resp.indexOf("output_text");
  if (outIdx < 0) {
    Serial.println("ERROR: No 'output_text' in response!");
    return "No output";
  }

  int textKey = resp.indexOf("\"text\"", outIdx);
  if (textKey < 0) {
    Serial.println("ERROR: No 'text' field!");
    return "No text";
  }

  int start = resp.indexOf('"', textKey + 6);
  if (start < 0) {
    Serial.println("ERROR: Parse error!");
    return "Parse error";
  }
  start++;

  String result;
  bool esc = false;
  for (unsigned int i = start; i < resp.length(); i++) {
    char c = resp[i];
    if (esc) {
      if (c == 'n') result += '\n';
      else if (c == 'u') { i += 4; result += '-'; }
      else result += c;
      esc = false;
    } else if (c == '\\') {
      esc = true;
    } else if (c == '"') {
      break;
    } else {
      result += c;
    }
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

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n========================================");
  Serial.println("       M5StickC Plus Voice Assistant");
  Serial.println("========================================\n");

  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);

  drawScreen("Connecting...");

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

  drawScreen("Press A\nto ask a question");
  Serial.println("\nReady! Press button A to ask a question.\n");
}

void loop() {
  M5.update();

  if (M5.BtnA.wasClicked()) {
    Serial.println("\n*** BUTTON A PRESSED ***\n");
    Serial.printf("Free heap before recording: %d bytes\n", ESP.getFreeHeap());
    
    if (!recordAudio()) {
      drawScreen("Mic error");
      delay(2000);
      drawScreen("Press A\nto ask a question");
      return;
    }

    Serial.printf("Free heap after recording: %d bytes\n", ESP.getFreeHeap());

    drawScreen("Transcribing...");
    String question = transcribeAudio();

    if (question.length() < 2 || question.startsWith("No ") || question.startsWith("Parse") || question.startsWith("Connection") || question.startsWith("Timeout")) {
      Serial.println("Transcription failed or empty");
      drawScreen("Couldn't hear.\nTry again.");
      delay(2000);
      drawScreen("Press A\nto ask a question");
      return;
    }

    drawScreen("Thinking...");
    String answer = askGPT(question);
    
    response = wordWrap(answer, 25);
    Serial.println("Final display text:");
    Serial.println(response);
    drawScreen(response);
    
    Serial.printf("Free heap at end: %d bytes\n", ESP.getFreeHeap());
    Serial.println("\n*** INTERACTION COMPLETE ***\n");
  }

  delay(20);
}
