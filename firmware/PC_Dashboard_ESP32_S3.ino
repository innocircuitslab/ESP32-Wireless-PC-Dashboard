#include <WiFi.h>
#include <WiFiUdp.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// =====================================================
// WIFI
// =====================================================
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const uint16_t UDP_PORT = 4210;

// =====================================================
// TFT ILI9341
// =====================================================
#define TFT_CS    8
#define TFT_RST   9
#define TFT_DC    10
#define TFT_MOSI  11
#define TFT_SCLK  12
#define TFT_BL    13

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

// =====================================================
// RAW ROTARY ENCODER
// Final project wiring: A=GPIO4, B=GPIO5, SW=GPIO6.
// =====================================================
#define ENABLE_ENCODER 1

#define ENC_A   4
#define ENC_B   5
#define ENC_SW  6

const bool ENCODER_REVERSED = false;

// =====================================================
// NETWORK
// =====================================================
WiFiUDP udp;
const unsigned long OFFLINE_TIMEOUT_MS = 6000;
unsigned long lastPacketMs = 0;

IPAddress pcIP;
uint16_t pcPort = 0;
bool havePcEndpoint = false;

// =====================================================
// PC DATA
// =====================================================
float cpuPercent = 0;
float ramPercent = 0;
float downloadMB = 0;
float uploadMB   = 0;
unsigned long uptimeSeconds = 0;

float cpuFreqMHz  = 0;
float diskReadMB  = 0;
float diskWriteMB = 0;
int processCount  = 0;

float totalRxGB = 0;
float totalTxGB = 0;

float latencyMs   = -1;
float diskFreePct = 0;

float topCpuPct = 0;
char topCpuName[40] = "N/A";

float topRamMB = 0;
char topRamName[40] = "N/A";

// =====================================================
// CPU HISTORY
// =====================================================
#define HISTORY_SIZE 52
float cpuHistory[HISTORY_SIZE];
int historyCount = 0;

// =====================================================
// DASHBOARD PAGES
// =====================================================
int currentPage = 0;
const int PAGE_COUNT = 4;

bool redrawStatic = true;
bool offlineShown = false;

// =====================================================
// CONTROL MODE
// =====================================================
bool controlMode = false;
int pageBeforeControl = 0;

const char* CONTROL_LABELS[] = {
  "VOLUME +",
  "VOLUME -",
  "PLAY / PAUSE",
  "MUTE",
  "LOCK PC",
  "SCREENSHOT",
  "BACK"
};

const char* CONTROL_COMMANDS[] = {
  "VOL_UP",
  "VOL_DOWN",
  "PLAY_PAUSE",
  "MUTE",
  "LOCK",
  "SCREENSHOT",
  "BACK"
};

const int CONTROL_COUNT = 7;
int controlSelected = 0;

String controlMessage = "";
uint16_t controlMessageColor = ILI9341_WHITE;
unsigned long controlMessageUntil = 0;

// =====================================================
// ENCODER
// =====================================================
uint8_t encoderState = 0;
int encoderAccumulator = 0;

// Button state machine
bool buttonLastReading = HIGH;
bool buttonStableState = HIGH;
unsigned long buttonLastChangeMs = 0;
unsigned long buttonPressStartMs = 0;
bool buttonLongHandled = false;

const unsigned long BUTTON_DEBOUNCE_MS = 25;
const unsigned long LONG_PRESS_MS = 1200;

// =====================================================
// COLORS
// =====================================================
#define COLOR_BG       ILI9341_BLACK
#define COLOR_TEXT     ILI9341_WHITE
#define COLOR_ACCENT   ILI9341_CYAN
#define COLOR_GREEN    ILI9341_GREEN
#define COLOR_YELLOW   ILI9341_YELLOW
#define COLOR_RED      ILI9341_RED
#define COLOR_GREY     0x8410
#define COLOR_DARK     0x2104
#define COLOR_HEADER   0x0010

// =====================================================
// HEALTH
// =====================================================
enum HealthLevel {
  HEALTH_OK = 0,
  HEALTH_WARN = 1,
  HEALTH_ALERT = 2
};

HealthLevel getHealthLevel() {
  if (latencyMs < 0 ||
      cpuPercent >= 95 ||
      ramPercent >= 95 ||
      diskFreePct < 5) {
    return HEALTH_ALERT;
  }

  if (cpuPercent >= 85 ||
      ramPercent >= 85 ||
      diskFreePct < 15 ||
      latencyMs > 150) {
    return HEALTH_WARN;
  }

  return HEALTH_OK;
}

uint16_t healthColor(HealthLevel level) {
  if (level == HEALTH_ALERT) return COLOR_RED;
  if (level == HEALTH_WARN)  return COLOR_YELLOW;
  return COLOR_GREEN;
}

const char* healthText(HealthLevel level) {
  if (level == HEALTH_ALERT) return "ALERT";
  if (level == HEALTH_WARN)  return "WARN";
  return "OK";
}

// =====================================================
// HELPERS
// =====================================================
void printCentered(const String &text, int y, int size, uint16_t color) {
  int16_t x1, y1;
  uint16_t w, h;

  tft.setTextSize(size);
  tft.setTextColor(color, COLOR_BG);
  tft.getTextBounds(text, 0, y, &x1, &y1, &w, &h);

  int x = (320 - w) / 2;
  tft.setCursor(x, y);
  tft.print(text);
}

String formatUptime(unsigned long sec) {
  unsigned long days = sec / 86400UL;
  sec %= 86400UL;

  unsigned long hours = sec / 3600UL;
  sec %= 3600UL;

  unsigned long minutes = sec / 60UL;

  char buffer[32];

  if (days > 0) {
    sprintf(buffer, "%lud %02lu:%02lu", days, hours, minutes);
  } else {
    sprintf(buffer, "%02lu:%02lu", hours, minutes);
  }

  return String(buffer);
}

String shortName(const char* src, int maxChars = 22) {
  String s = String(src);

  if ((int)s.length() <= maxChars)
    return s;

  return s.substring(0, maxChars - 2) + "..";
}

void drawHeader(const char* title) {
  tft.fillRect(0, 0, 320, 32, COLOR_HEADER);

  tft.setTextSize(2);
  tft.setTextColor(COLOR_TEXT, COLOR_HEADER);
  tft.setCursor(10, 8);
  tft.print(title);

  HealthLevel level = getHealthLevel();
  uint16_t c = healthColor(level);

  tft.fillCircle(270, 16, 5, c);

  tft.setTextSize(1);
  tft.setTextColor(c, COLOR_HEADER);
  tft.setCursor(281, 13);
  tft.print(healthText(level));
}

void refreshHeaderStatus() {
  tft.fillRect(258, 4, 62, 24, COLOR_HEADER);

  HealthLevel level = getHealthLevel();
  uint16_t c = healthColor(level);

  tft.fillCircle(270, 16, 5, c);

  tft.setTextSize(1);
  tft.setTextColor(c, COLOR_HEADER);
  tft.setCursor(281, 13);
  tft.print(healthText(level));
}

void drawBar(int x, int y, int w, int h, float value, uint16_t color) {
  value = constrain(value, 0, 100);

  tft.drawRect(x, y, w, h, COLOR_GREY);

  int fillWidth = (int)((w - 4) * value / 100.0);

  tft.fillRect(x + 2, y + 2, w - 4, h - 4, COLOR_BG);

  if (fillWidth > 0) {
    tft.fillRect(x + 2, y + 2, fillWidth, h - 4, color);
  }
}

void drawStatusRow(
  int y,
  const char* label,
  const String& value,
  uint16_t valueColor
) {
  tft.fillRect(12, y, 296, 24, COLOR_BG);

  tft.setTextSize(1);
  tft.setTextColor(COLOR_GREY, COLOR_BG);
  tft.setCursor(15, y + 7);
  tft.print(label);

  tft.setTextSize(2);
  tft.setTextColor(valueColor, COLOR_BG);
  tft.setCursor(175, y + 3);
  tft.print(value);
}

// =====================================================
// PAGE 1 - OVERVIEW
// =====================================================
void drawMainStatic() {
  tft.fillScreen(COLOR_BG);
  drawHeader("PC DASHBOARD");

  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextSize(2);

  tft.setCursor(15, 46);
  tft.print("CPU");

  tft.setCursor(15, 88);
  tft.print("RAM");

  tft.setTextSize(1);
  tft.setTextColor(COLOR_GREY, COLOR_BG);

  tft.setCursor(15, 139);
  tft.print("DOWNLOAD");

  tft.setCursor(173, 139);
  tft.print("UPLOAD");

  tft.setCursor(15, 194);
  tft.print("UPTIME");

  tft.setCursor(292, 224);
  tft.print("1/4");
}

void updateMainPage() {
  refreshHeaderStatus();

  uint16_t cpuColor =
    cpuPercent >= 95 ? COLOR_RED :
    cpuPercent >= 85 ? COLOR_YELLOW :
    COLOR_ACCENT;

  uint16_t ramColor =
    ramPercent >= 95 ? COLOR_RED :
    ramPercent >= 85 ? COLOR_YELLOW :
    COLOR_ACCENT;

  tft.fillRect(230, 42, 80, 25, COLOR_BG);
  tft.setTextSize(2);
  tft.setTextColor(cpuColor, COLOR_BG);
  tft.setCursor(245, 46);
  tft.printf("%.0f%%", cpuPercent);

  drawBar(15, 69, 290, 12, cpuPercent, cpuColor);

  tft.fillRect(230, 84, 80, 25, COLOR_BG);
  tft.setTextColor(ramColor, COLOR_BG);
  tft.setCursor(245, 88);
  tft.printf("%.0f%%", ramPercent);

  drawBar(15, 111, 290, 12, ramPercent, ramColor);

  tft.fillRect(15, 152, 135, 30, COLOR_BG);
  tft.setTextSize(2);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setCursor(15, 155);
  tft.printf("%.2f", downloadMB);
  tft.setTextSize(1);
  tft.print(" MB/s");

  tft.fillRect(170, 152, 135, 30, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(173, 155);
  tft.printf("%.2f", uploadMB);
  tft.setTextSize(1);
  tft.print(" MB/s");

  tft.fillRect(15, 207, 220, 27, COLOR_BG);
  tft.setTextSize(2);
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.setCursor(15, 207);
  tft.print(formatUptime(uptimeSeconds));
}

// =====================================================
// PAGE 2 - PERFORMANCE
// =====================================================
void drawPerformanceStatic() {
  tft.fillScreen(COLOR_BG);
  drawHeader("PERFORMANCE");

  tft.setTextSize(1);
  tft.setTextColor(COLOR_GREY, COLOR_BG);

  tft.setCursor(15, 45);
  tft.print("CPU FREQUENCY");

  tft.setCursor(168, 45);
  tft.print("PROCESSES");

  tft.setCursor(15, 92);
  tft.print("DISK READ");

  tft.setCursor(168, 92);
  tft.print("DISK WRITE");

  tft.setCursor(15, 138);
  tft.print("CPU HISTORY");

  tft.drawRect(15, 153, 290, 65, COLOR_GREY);
  tft.drawFastHLine(16, 185, 288, COLOR_DARK);

  tft.setCursor(292, 224);
  tft.print("2/4");
}

void drawCPUHistory() {
  int x = 16;
  int y = 154;
  int w = 288;
  int h = 63;

  tft.fillRect(x + 1, y + 1, w - 2, h - 2, COLOR_BG);
  tft.drawFastHLine(x + 1, y + h / 2, w - 2, COLOR_DARK);

  if (historyCount < 2)
    return;

  float step = (float)(w - 4) / (HISTORY_SIZE - 1);

  for (int i = 1; i < historyCount; i++) {
    int x1 = x + 2 + (int)((i - 1) * step);
    int x2 = x + 2 + (int)(i * step);

    int y1 = y + h - 3 -
      (int)((cpuHistory[i - 1] / 100.0) * (h - 6));

    int y2 = y + h - 3 -
      (int)((cpuHistory[i] / 100.0) * (h - 6));

    tft.drawLine(x1, y1, x2, y2, COLOR_GREEN);
  }
}

void updatePerformancePage() {
  refreshHeaderStatus();

  tft.fillRect(15, 59, 135, 25, COLOR_BG);
  tft.setTextSize(2);
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.setCursor(15, 61);

  if (cpuFreqMHz > 0) {
    tft.printf("%.2f GHz", cpuFreqMHz / 1000.0);
  } else {
    tft.print("N/A");
  }

  tft.fillRect(168, 59, 135, 25, COLOR_BG);
  tft.setCursor(168, 61);
  tft.printf("%d", processCount);

  tft.fillRect(15, 105, 135, 25, COLOR_BG);
  tft.setCursor(15, 107);
  tft.printf("%.2f", diskReadMB);
  tft.setTextSize(1);
  tft.print(" MB/s");

  tft.fillRect(168, 105, 135, 25, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(168, 107);
  tft.printf("%.2f", diskWriteMB);
  tft.setTextSize(1);
  tft.print(" MB/s");

  drawCPUHistory();
}

// =====================================================
// PAGE 3 - NETWORK
// =====================================================
void drawNetworkStatic() {
  tft.fillScreen(COLOR_BG);
  drawHeader("NETWORK");

  tft.setTextSize(1);
  tft.setTextColor(COLOR_GREY, COLOR_BG);

  tft.setCursor(15, 47);
  tft.print("DOWNLOAD");

  tft.setCursor(168, 47);
  tft.print("UPLOAD");

  tft.setCursor(15, 105);
  tft.print("LATENCY");

  tft.setCursor(168, 105);
  tft.print("WI-FI SIGNAL");

  tft.setCursor(15, 163);
  tft.print("TOTAL RX");

  tft.setCursor(168, 163);
  tft.print("TOTAL TX");

  tft.setCursor(292, 224);
  tft.print("3/4");
}

void updateNetworkPage() {
  refreshHeaderStatus();

  tft.fillRect(15, 60, 135, 34, COLOR_BG);
  tft.setTextSize(2);
  tft.setTextColor(COLOR_GREEN, COLOR_BG);
  tft.setCursor(15, 63);
  tft.printf("%.2f", downloadMB);
  tft.setTextSize(1);
  tft.print(" MB/s");

  tft.fillRect(168, 60, 135, 34, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(168, 63);
  tft.printf("%.2f", uploadMB);
  tft.setTextSize(1);
  tft.print(" MB/s");

  tft.fillRect(15, 118, 135, 34, COLOR_BG);
  tft.setTextSize(2);

  if (latencyMs < 0) {
    tft.setTextColor(COLOR_RED, COLOR_BG);
    tft.setCursor(15, 121);
    tft.print("OFFLINE");
  } else {
    uint16_t latencyColor =
      latencyMs > 150 ? COLOR_RED :
      latencyMs > 80  ? COLOR_YELLOW :
      COLOR_GREEN;

    tft.setTextColor(latencyColor, COLOR_BG);
    tft.setCursor(15, 121);
    tft.printf("%.0f ms", latencyMs);
  }

  tft.fillRect(168, 118, 135, 34, COLOR_BG);
  tft.setTextSize(2);

  int rssi = WiFi.RSSI();

  if (rssi > -60)
    tft.setTextColor(COLOR_GREEN, COLOR_BG);
  else if (rssi > -75)
    tft.setTextColor(COLOR_YELLOW, COLOR_BG);
  else
    tft.setTextColor(COLOR_RED, COLOR_BG);

  tft.setCursor(168, 121);
  tft.printf("%d dBm", rssi);

  tft.fillRect(15, 176, 135, 34, COLOR_BG);
  tft.setTextSize(2);
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.setCursor(15, 179);
  tft.printf("%.2f", totalRxGB);
  tft.setTextSize(1);
  tft.print(" GB");

  tft.fillRect(168, 176, 135, 34, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(168, 179);
  tft.printf("%.2f", totalTxGB);
  tft.setTextSize(1);
  tft.print(" GB");
}

// =====================================================
// PAGE 4 - SYSTEM HEALTH
// =====================================================
void drawHealthStatic() {
  tft.fillScreen(COLOR_BG);
  drawHeader("SYSTEM HEALTH");

  tft.setTextSize(1);
  tft.setTextColor(COLOR_GREY, COLOR_BG);

  tft.setCursor(15, 45);
  tft.print("TOP CPU PROCESS");

  tft.setCursor(15, 97);
  tft.print("TOP RAM PROCESS");

  tft.drawFastHLine(15, 145, 290, COLOR_DARK);

  tft.setCursor(292, 224);
  tft.print("4/4");
}

void updateHealthPage() {
  refreshHeaderStatus();

  tft.fillRect(15, 58, 290, 32, COLOR_BG);
  tft.setTextSize(2);
  tft.setTextColor(
    topCpuPct >= 80 ? COLOR_RED :
    topCpuPct >= 50 ? COLOR_YELLOW :
    COLOR_TEXT,
    COLOR_BG
  );

  tft.setCursor(15, 61);
  tft.print(shortName(topCpuName, 20));

  tft.setCursor(245, 61);
  tft.printf("%.0f%%", topCpuPct);

  tft.fillRect(15, 110, 290, 32, COLOR_BG);
  tft.setTextSize(2);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setCursor(15, 113);
  tft.print(shortName(topRamName, 20));

  tft.setCursor(225, 113);

  if (topRamMB >= 1024.0) {
    tft.printf("%.1fG", topRamMB / 1024.0);
  } else {
    tft.printf("%.0fM", topRamMB);
  }

  bool cpuOk = cpuPercent < 85;
  bool cpuWarn = cpuPercent < 95;

  drawStatusRow(
    151,
    "CPU",
    cpuOk ? "OK" : (cpuWarn ? "WARN" : "ALERT"),
    cpuOk ? COLOR_GREEN : (cpuWarn ? COLOR_YELLOW : COLOR_RED)
  );

  bool ramOk = ramPercent < 85;
  bool ramWarn = ramPercent < 95;

  drawStatusRow(
    174,
    "MEMORY",
    ramOk ? "OK" : (ramWarn ? "WARN" : "ALERT"),
    ramOk ? COLOR_GREEN : (ramWarn ? COLOR_YELLOW : COLOR_RED)
  );

  bool diskOk = diskFreePct >= 15;
  bool diskWarn = diskFreePct >= 5;

  String diskText = String(diskFreePct, 0) + "% FREE";

  drawStatusRow(
    197,
    "DISK",
    diskText,
    diskOk ? COLOR_GREEN : (diskWarn ? COLOR_YELLOW : COLOR_RED)
  );

  tft.fillRect(190, 221, 90, 18, COLOR_BG);
  tft.setTextSize(1);

  if (latencyMs < 0) {
    tft.setTextColor(COLOR_RED, COLOR_BG);
    tft.setCursor(190, 226);
    tft.print("NET: OFFLINE");
  } else {
    uint16_t c =
      latencyMs > 150 ? COLOR_RED :
      latencyMs > 80  ? COLOR_YELLOW :
      COLOR_GREEN;

    tft.setTextColor(c, COLOR_BG);
    tft.setCursor(190, 226);
    tft.printf("NET: %.0fms", latencyMs);
  }
}

// =====================================================
// DASHBOARD RENDER
// =====================================================
void drawStaticPage() {
  if (currentPage == 0)
    drawMainStatic();
  else if (currentPage == 1)
    drawPerformanceStatic();
  else if (currentPage == 2)
    drawNetworkStatic();
  else
    drawHealthStatic();

  redrawStatic = false;
}

void updateCurrentPage() {
  if (controlMode)
    return;

  if (redrawStatic)
    drawStaticPage();

  if (currentPage == 0)
    updateMainPage();
  else if (currentPage == 1)
    updatePerformancePage();
  else if (currentPage == 2)
    updateNetworkPage();
  else
    updateHealthPage();
}

// =====================================================
// CONTROL MODE UI
// =====================================================
void drawControlMessage() {
  tft.fillRect(0, 215, 320, 25, COLOR_BG);

  if (controlMessage.length() == 0)
    return;

  tft.setTextSize(1);
  tft.setTextColor(controlMessageColor, COLOR_BG);
  tft.setCursor(10, 225);
  tft.print(controlMessage);
}

void setControlMessage(
  const String& msg,
  uint16_t color,
  unsigned long durationMs = 1800
) {
  controlMessage = msg;
  controlMessageColor = color;
  controlMessageUntil = millis() + durationMs;

  if (controlMode)
    drawControlMessage();
}

void drawControlScreen() {
  tft.fillScreen(COLOR_BG);

  tft.fillRect(0, 0, 320, 32, COLOR_HEADER);

  tft.setTextSize(2);
  tft.setTextColor(COLOR_TEXT, COLOR_HEADER);
  tft.setCursor(10, 8);
  tft.print("PC CONTROL");

  tft.setTextSize(1);
  tft.setTextColor(COLOR_GREY, COLOR_HEADER);
  tft.setCursor(245, 13);
  tft.print("HOLD=EXIT");

  const int visibleItems = 5;
  int first = controlSelected - 2;

  if (first < 0)
    first = 0;

  if (first > CONTROL_COUNT - visibleItems)
    first = CONTROL_COUNT - visibleItems;

  if (first < 0)
    first = 0;

  for (int row = 0; row < visibleItems; row++) {
    int index = first + row;

    if (index >= CONTROL_COUNT)
      break;

    int y = 43 + row * 33;
    bool selected = (index == controlSelected);

    if (selected) {
      tft.fillRoundRect(10, y, 300, 28, 4, COLOR_ACCENT);
      tft.setTextColor(ILI9341_BLACK, COLOR_ACCENT);
    } else {
      tft.drawRoundRect(10, y, 300, 28, 4, COLOR_DARK);
      tft.setTextColor(COLOR_TEXT, COLOR_BG);
    }

    tft.setTextSize(2);
    tft.setCursor(22, y + 7);

    if (selected)
      tft.print("> ");
    else
      tft.print("  ");

    tft.print(CONTROL_LABELS[index]);
  }

  drawControlMessage();
}

void enterControlMode() {
  pageBeforeControl = currentPage;
  controlMode = true;
  controlSelected = 0;
  controlMessage = "Rotate = select   Press = run";
  controlMessageColor = COLOR_GREY;
  controlMessageUntil = millis() + 2500;

  drawControlScreen();
}

void exitControlMode() {
  controlMode = false;
  currentPage = pageBeforeControl;
  redrawStatic = true;
  controlMessage = "";
  updateCurrentPage();
}

void sendControlCommand(const char* cmd) {
  if (!havePcEndpoint) {
    setControlMessage("NO PC LINK", COLOR_RED);
    return;
  }

  udp.beginPacket(pcIP, pcPort);
  udp.print("CMD,");
  udp.print(cmd);

  bool ok = udp.endPacket();

  if (ok) {
    setControlMessage(
      String("SENT: ") + cmd,
      COLOR_YELLOW,
      1200
    );
  } else {
    setControlMessage("SEND FAILED", COLOR_RED);
  }
}

void executeSelectedControl() {
  if (controlSelected == CONTROL_COUNT - 1) {
    exitControlMode();
    return;
  }

  sendControlCommand(
    CONTROL_COMMANDS[controlSelected]
  );
}

// =====================================================
// OFFLINE
// =====================================================
void drawOffline() {
  tft.fillScreen(COLOR_BG);

  tft.fillRect(0, 0, 320, 32, COLOR_HEADER);
  tft.setTextSize(2);
  tft.setTextColor(COLOR_TEXT, COLOR_HEADER);
  tft.setCursor(10, 8);
  tft.print("PC DASHBOARD");

  printCentered("OFFLINE", 85, 4, COLOR_RED);
  printCentered("Waiting for PC...", 145, 2, COLOR_GREY);

  offlineShown = true;
}

// =====================================================
// HISTORY
// =====================================================
void addCPUHistory(float cpu) {
  if (historyCount < HISTORY_SIZE) {
    cpuHistory[historyCount] = cpu;
    historyCount++;
  } else {
    for (int i = 0; i < HISTORY_SIZE - 1; i++) {
      cpuHistory[i] = cpuHistory[i + 1];
    }

    cpuHistory[HISTORY_SIZE - 1] = cpu;
  }
}

// =====================================================
// ACK FROM PC
// Example:
// ACK,VOL_UP,OK
// ACK,LOCK,ERR
// =====================================================
void processAck(char* packet) {
  char cmd[32] = "";
  char result[16] = "";

  int fields = sscanf(
    packet,
    "ACK,%31[^,],%15[^\n]",
    cmd,
    result
  );

  if (fields >= 2 && controlMode) {
    if (strcmp(result, "OK") == 0) {
      setControlMessage(
        String("DONE: ") + cmd,
        COLOR_GREEN,
        1800
      );
    } else {
      setControlMessage(
        String("FAILED: ") + cmd,
        COLOR_RED,
        2200
      );
    }
  }
}

// =====================================================
// TELEMETRY PARSE
// CPU,RAM,DOWN,UP,UPTIME,FREQ,DREAD,DWRITE,PROC,RX,TX,
// LATENCY,DISKFREE,TOPCPUPCT,TOPCPUNAME,TOPRAMMB,TOPRAMNAME
// =====================================================
void processTelemetry(char* packet) {
  float c, r;
  float down, up;
  unsigned long upTime;

  float freq;
  float dRead;
  float dWrite;
  int processes;

  float rxGB;
  float txGB;

  float latency;
  float diskFree;
  float topCpu;

  char cpuName[40] = "N/A";

  float ramMB;
  char ramName[40] = "N/A";

  int fields = sscanf(
    packet,
    "%f,%f,%f,%f,%lu,%f,%f,%f,%d,%f,%f,%f,%f,%f,%39[^,],%f,%39[^\n]",
    &c,
    &r,
    &down,
    &up,
    &upTime,
    &freq,
    &dRead,
    &dWrite,
    &processes,
    &rxGB,
    &txGB,
    &latency,
    &diskFree,
    &topCpu,
    cpuName,
    &ramMB,
    ramName
  );

  if (fields < 17)
    return;

  cpuPercent = c;
  ramPercent = r;

  downloadMB = down;
  uploadMB = up;

  uptimeSeconds = upTime;
  cpuFreqMHz = freq;

  diskReadMB = dRead;
  diskWriteMB = dWrite;
  processCount = processes;

  totalRxGB = rxGB;
  totalTxGB = txGB;

  latencyMs = latency;
  diskFreePct = diskFree;

  topCpuPct = topCpu;

  strncpy(
    topCpuName,
    cpuName,
    sizeof(topCpuName) - 1
  );

  topCpuName[sizeof(topCpuName) - 1] = '\0';

  topRamMB = ramMB;

  strncpy(
    topRamName,
    ramName,
    sizeof(topRamName) - 1
  );

  topRamName[sizeof(topRamName) - 1] = '\0';

  addCPUHistory(cpuPercent);

  lastPacketMs = millis();

  if (offlineShown) {
    offlineShown = false;

    if (controlMode) {
      drawControlScreen();
    } else {
      redrawStatic = true;
    }
  }

  if (!controlMode)
    updateCurrentPage();
}

// =====================================================
// ROTARY ENCODER
// =====================================================
void handleEncoder() {
  static const int8_t table[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
  };

  uint8_t newState =
    (digitalRead(ENC_A) << 1) |
     digitalRead(ENC_B);

  if (newState == encoderState)
    return;

  uint8_t transition =
    (encoderState << 2) | newState;

  encoderAccumulator += table[transition];
  encoderState = newState;

  int direction = 0;

  if (encoderAccumulator >= 4) {
    encoderAccumulator = 0;
    direction = 1;
  } else if (encoderAccumulator <= -4) {
    encoderAccumulator = 0;
    direction = -1;
  }

  if (ENCODER_REVERSED)
    direction *= -1;

  if (direction == 0)
    return;

  if (controlMode) {
    controlSelected += direction;

    if (controlSelected >= CONTROL_COUNT)
      controlSelected = 0;

    if (controlSelected < 0)
      controlSelected = CONTROL_COUNT - 1;

    drawControlScreen();
    return;
  }

  currentPage += direction;

  if (currentPage >= PAGE_COUNT)
    currentPage = 0;

  if (currentPage < 0)
    currentPage = PAGE_COUNT - 1;

  redrawStatic = true;

  if (!offlineShown)
    updateCurrentPage();
}

// =====================================================
// BUTTON ACTIONS
// =====================================================
void handleShortPress() {
  if (controlMode) {
    executeSelectedControl();
    return;
  }

  // Preserve old behavior:
  // short press from dashboard returns to page 1.
  currentPage = 0;
  redrawStatic = true;

  if (!offlineShown)
    updateCurrentPage();
}

void handleLongPress() {
  if (controlMode) {
    exitControlMode();
  } else {
    enterControlMode();
  }
}

void handleButton() {
  bool reading = digitalRead(ENC_SW);

  if (reading != buttonLastReading) {
    buttonLastReading = reading;
    buttonLastChangeMs = millis();
  }

  if (
    millis() - buttonLastChangeMs >= BUTTON_DEBOUNCE_MS &&
    reading != buttonStableState
  ) {
    buttonStableState = reading;

    if (buttonStableState == LOW) {
      buttonPressStartMs = millis();
      buttonLongHandled = false;
    } else {
      // Released.
      if (!buttonLongHandled) {
        handleShortPress();
      }
    }
  }

  if (
    buttonStableState == LOW &&
    !buttonLongHandled &&
    millis() - buttonPressStartMs >= LONG_PRESS_MS
  ) {
    buttonLongHandled = true;
    handleLongPress();
  }
}

// =====================================================
// WIFI
// =====================================================
void connectWiFi() {
  tft.fillScreen(COLOR_BG);
  printCentered("CONNECTING...", 90, 2, COLOR_ACCENT);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
  }

  udp.begin(UDP_PORT);

  tft.fillScreen(COLOR_BG);

  printCentered("CONNECTED", 65, 2, COLOR_GREEN);
  printCentered(WiFi.localIP().toString(), 110, 2, COLOR_TEXT);

  delay(1800);
  redrawStatic = true;
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);

#if ENABLE_ENCODER
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);

  encoderState =
    (digitalRead(ENC_A) << 1) |
     digitalRead(ENC_B);
#endif

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  SPI.begin(
    TFT_SCLK,
    -1,
    TFT_MOSI,
    TFT_CS
  );

  tft.begin();
  tft.setRotation(1);

  for (int i = 0; i < HISTORY_SIZE; i++) {
    cpuHistory[i] = 0;
  }

  connectWiFi();

  lastPacketMs = millis();

  drawStaticPage();
  printCentered("Waiting for PC...", 205, 1, COLOR_GREY);
}

// =====================================================
// LOOP
// =====================================================
void loop() {
#if ENABLE_ENCODER
  handleEncoder();
  handleButton();
#endif

  // Clear expired control feedback.
  if (
    controlMode &&
    controlMessage.length() > 0 &&
    controlMessageUntil > 0 &&
    (long)(millis() - controlMessageUntil) >= 0
  ) {
    controlMessage = "";
    controlMessageUntil = 0;
    drawControlMessage();
  }

  // Wi-Fi reconnect
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    delay(500);
  }

  int packetSize = udp.parsePacket();

  if (packetSize > 0) {
    // This is the PC-side UDP socket that sent telemetry/ACK.
    pcIP = udp.remoteIP();
    pcPort = udp.remotePort();
    havePcEndpoint = true;

    char packet[384];

    int len = udp.read(
      packet,
      sizeof(packet) - 1
    );

    if (len > 0) {
      packet[len] = '\0';

      if (strncmp(packet, "ACK,", 4) == 0) {
        processAck(packet);
      } else {
        processTelemetry(packet);
      }
    }
  }

  if (
    millis() - lastPacketMs > OFFLINE_TIMEOUT_MS &&
    !offlineShown
  ) {
    drawOffline();
  }

  delay(1);
}