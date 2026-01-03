// Connects to hardcoded wifi, I use phone tethering
// Writes a short haiku every time you press the button, prompt on line 107

#include <M5Unified.h> 
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

static const int WIDTH  = 240;
static const int HEIGHT = 135;

const char* WIFI_SSID     = "name";
const char* WIFI_PASS     = "pass";
const char* OPENAI_API_KEY = "sk-proj-xxxxxxxxx";

String haiku = "Press A\nfor a haiku";

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

String extractHaiku(const String &json) {
  int outIdx = json.indexOf("output_text");
  if (outIdx < 0) return "No output";

  int textKey = json.indexOf("\"text\"", outIdx);
  if (textKey < 0) return "No text key";

  int start = json.indexOf('"', textKey + 6);
  if (start < 0) return "No text value";
  start++;

  String result;
  bool escaped = false;

  for (unsigned int i = start; i < json.length(); i++) {
    char c = json[i];
    if (escaped) {
      if (c == 'n') result += '\n';
      else if (c == '"') result += '"';
      else if (c == '\\') result += '\\';
      else if (c == 'u') {
        i += 4;
        result += '-';
      }
      else result += c;
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else if (c == '"') {
      break;
    } else {
      result += c;
    }
  }

  result.trim();
  return result.length() > 0 ? result : "Empty";
}

String fetchHaiku() {
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  http.begin(client, "https://api.openai.com/v1/responses");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + OPENAI_API_KEY);
  http.setTimeout(90000);

  String body =
    "{"
      "\"model\":\"gpt-5-mini\","
      "\"input\":["
        "{"
          "\"role\":\"user\","
          "\"content\":["
            "{"
              "\"type\":\"input_text\","
              "\"text\":\"Write a haiku about space. Only the three lines.\""
            "}"
          "]"
        "}"
      "]"
    "}";

  int httpCode = http.POST(body);

  Serial.printf("HTTP code: %d\n", httpCode);

  if (httpCode != 200) {
    http.end();
    return "HTTP " + String(httpCode);
  }

  String response = http.getString();
  http.end();

  Serial.println("----- RESPONSE -----");
  Serial.println(response);
  Serial.println("--------------------");

  return extractHaiku(response);
}

void setup() {
  Serial.begin(115200);

  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);

  drawScreen("Connecting...");

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  drawScreen("Press A\nfor a haiku");
}

void loop() {
  M5.update();

  if (M5.BtnA.wasClicked()) {
    drawScreen("Thinking...");
    haiku = fetchHaiku();
    drawScreen(haiku);
  }

  delay(20);
}
