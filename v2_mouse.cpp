#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SD.h>
#include <SPI.h>
#include <vector>
#include <btAudio.h>

// 屏幕尺寸
#define SCREEN_WIDTH 160
#define SCREEN_HEIGHT 128

// 引脚定义
const int pinx = 32;    // 摇杆X轴
const int piny = 33;    // 摇杆Y轴
const int confirmPin = 35; // GPIO确认按键
const int cs = 15;
const int sck = 4;
const int mos = 16;
const int mis = 17;
SPIClass CSD;

// TFT显示对象
TFT_eSPI tft = TFT_eSPI();

// ========== btAudio 蓝牙音频支持 ==========
btAudio audio = btAudio("ESP32-VideoPlayer");

// 系统状态变量
bool isPlayingVideo = false;
bool bluetoothConnected = false;
bool videoPaused = false;
bool fullScreenMode = false;
String inputBuffer = "";

// 内存优化：将媒体文件列表存储在SD卡中而不是内存中
int mediaFileCount = 0;
int currentMediaIndex = -1;
String currentMediaPath = "";

// 视频播放变量
File videoFile;
uint32_t videoFileSize = 0;
uint32_t framesInVideo = 0;
uint32_t currentFrame = 0;
uint8_t targetFPS = 20;
bool videoLoop = true;
unsigned long lastFrameTime = 0;
unsigned long frameDelay = 50;

// 性能优化变量
bool optimizeForSpeed = true;
uint8_t frameSkip = 0;
uint32_t lastFrameDisplayed = 0;

// 界面状态
enum AppState {
  STATE_MAIN_MENU,
  STATE_VIDEO_PLAYER,
  STATE_FILE_BROWSER,
  STATE_SETTINGS
};
AppState currentState = STATE_MAIN_MENU;

// 摇杆鼠标控制 - 重新设计光标系统
int cursorX = SCREEN_WIDTH / 2;
int cursorY = SCREEN_HEIGHT / 2;
int oldCursorX = SCREEN_WIDTH / 2;
int oldCursorY = SCREEN_HEIGHT / 2;
bool cursorVisible = true;
unsigned long lastCursorMove = 0;
const unsigned long CURSOR_UPDATE_INTERVAL = 50;
bool cursorMoved = false; // 新增：标记光标是否需要重绘

// 确认按键状态
bool confirmPressed = false;
bool lastConfirmState = false;
unsigned long lastConfirmTime = 0;
const unsigned long DEBOUNCE_DELAY = 50;

// 菜单系统
int selectedMenuItem = 0;
int menuScrollOffset = 0;
const int MAX_MENU_ITEMS = 8;

// 文件浏览器
int selectedFileIndex = 0;
int fileScrollOffset = 0;
const int MAX_FILES_PER_PAGE = 7;

// 性能监控
unsigned long frameCount = 0;
unsigned long lastStatsTime = 0;
const unsigned long STATS_INTERVAL = 5000;
float actualFPS = 0;

// 串口调试
bool serialDebug = true;
String serialBuffer = "";
const unsigned long SERIAL_DEBUG_INTERVAL = 2000;
unsigned long lastSerialDebug = 0;

// 进度条变量
int lastProgressWidth = 0;

// 颜色定义
#define BACKGROUND_COLOR TFT_BLACK
#define TEXT_COLOR TFT_WHITE
#define HIGHLIGHT_COLOR TFT_RED
#define CURSOR_COLOR TFT_YELLOW
#define STATUS_BAR_COLOR TFT_DARKGREY
#define PROGRESS_BAR_COLOR TFT_GREEN

// ========== 函数声明 ==========
void setup();
void loop();
void drawMainMenu();
void drawVideoPlayer();
void drawFileBrowser();
void drawSettings();
void updateCursor();
void handleJoystick();
void handleConfirmButton();
void processUIInput();
void addToOutput(String text);
void executeCommand(String command);
void playVideo(String filename);
bool displayVideoFrame();
bool displayVideoFrameOptimized();
void stopVideo();
void playNextVideo();
void playPrevVideo();
void updateVideoInfo();
bool initSDCard();
int scanMediaFiles();
void initBluetoothAudio();
void drawCursor();
void eraseCursor();
void drawButton(int x, int y, int w, int h, String text, bool selected);
void drawStatusBar(String text);
void showMessage(String message, uint16_t color = TFT_WHITE);
void drawSmallButton(int x, int y, int w, int h, String text, bool selected);
void togglePlayPause();
void toggleFullScreen();
void drawVideoControls();
void hideVideoControls();
void showVideoControls();
void adjustPerformanceSettings();
String getMediaFileName(int index);

// 串口调试函数
void debugPrint(String message);
void processSerialInput();
void printSystemInfo();
void printDebugHelp();
void printJoystickStatus();
void printMediaInfo();
String findFileByIndex(File dir, String path, int targetIndex, int &currentIndex);
// 文件扫描函数
int scanDirectory(File dir, String path);

// ========== 串口调试函数 ==========
void debugPrint(String message) {
  if (serialDebug) {
    Serial.println("[DEBUG] " + message);
  }
}

void processSerialInput() {
  while (Serial.available()) {
    char c = Serial.read();
    
    if (c == '\n' || c == '\r') {
      if (serialBuffer.length() > 0) {
        serialBuffer.trim();
        debugPrint("Serial command: " + serialBuffer);
        
        // 处理调试命令
        if (serialBuffer == "help") {
          printDebugHelp();
        } else if (serialBuffer == "info") {
          printSystemInfo();
        } else if (serialBuffer == "status") {
          printSystemInfo();
        } else if (serialBuffer == "joystick") {
          printJoystickStatus();
        } else if (serialBuffer == "media") {
          printMediaInfo();
        } else if (serialBuffer == "debug on") {
          serialDebug = true;
          Serial.println("Serial debug enabled");
        } else if (serialBuffer == "debug off") {
          serialDebug = false;
          Serial.println("Serial debug disabled");
        } else if (serialBuffer == "reset") {
          Serial.println("Resetting system...");
          ESP.restart();
        } else if (serialBuffer == "list") {
          Serial.println("Media files:");
          for (int i = 0; i < mediaFileCount; i++) {
            Serial.println(String(i) + ": " + getMediaFileName(i));
          }
        } else if (serialBuffer == "fullscreen") {
          toggleFullScreen();
        } else if (serialBuffer == "performance") {
          adjustPerformanceSettings();
        } else {
          executeCommand(serialBuffer);
        }
        
        serialBuffer = "";
      }
    } else if (isPrintable(c)) {
      serialBuffer += c;
    }
    
    delay(2);
  }
}

void printSystemInfo() {
  Serial.println("=== System Information ===");
  Serial.println("App State: " + String(currentState));
  Serial.println("Video Playing: " + String(isPlayingVideo));
  Serial.println("Video Paused: " + String(videoPaused));
  Serial.println("Full Screen: " + String(fullScreenMode));
  Serial.println("Current Media: " + currentMediaPath);
  Serial.println("Current Frame: " + String(currentFrame) + "/" + String(framesInVideo));
  Serial.println("Target FPS: " + String(targetFPS));
  Serial.println("Actual FPS: " + String(actualFPS, 1));
  Serial.println("Frame Skip: " + String(frameSkip));
  Serial.println("Optimize Speed: " + String(optimizeForSpeed));
  Serial.println("Bluetooth: " + String(bluetoothConnected ? "Connected" : "Disconnected"));
  Serial.println("Free Heap: " + String(esp_get_free_heap_size()) + " bytes");
  Serial.println("Media Files: " + String(mediaFileCount));
  Serial.println("========================");
}

void printDebugHelp() {
  Serial.println("=== Serial Debug Commands ===");
  Serial.println("help - Show this help");
  Serial.println("info - System information");
  Serial.println("joystick - Joystick status");
  Serial.println("media - Media information");
  Serial.println("list - List media files");
  Serial.println("debug on/off - Toggle debug output");
  Serial.println("reset - Reset system");
  Serial.println("play [file] - Play video");
  Serial.println("stop - Stop video");
  Serial.println("pause - Pause video");
  Serial.println("resume - Resume video");
  Serial.println("next - Next video");
  Serial.println("prev - Previous video");
  Serial.println("scan - Rescan media");
  Serial.println("fullscreen - Toggle fullscreen mode");
  Serial.println("performance - Adjust performance settings");
  Serial.println("========================");
}

void printJoystickStatus() {
  int xVal = analogRead(pinx);
  int yVal = analogRead(piny);
  
  Serial.println("=== Joystick Status ===");
  Serial.println("X Value: " + String(xVal));
  Serial.println("Y Value: " + String(yVal));
  Serial.println("Cursor Position: (" + String(cursorX) + ", " + String(cursorY) + ")");
  
  String xDir = "Center";
  String yDir = "Center";
  
  if (xVal > 3500) xDir = "Left";
  else if (xVal < 1000) xDir = "Right";
  
  if (yVal > 3500) yDir = "Up";
  else if (yVal < 1000) yDir = "Down";
  
  Serial.println("Direction: " + xDir + ", " + yDir);
  Serial.println("====================");
}

void printMediaInfo() {
  Serial.println("=== Media Information ===");
  Serial.println("Total Files: " + String(mediaFileCount));
  Serial.println("Current Index: " + String(currentMediaIndex));
  
  if (isPlayingVideo) {
    Serial.println("Now Playing: " + currentMediaPath);
    Serial.println("Progress: " + String(currentFrame) + "/" + String(framesInVideo) + 
                  " (" + String((float)currentFrame/framesInVideo*100, 1) + "%)");
    Serial.println("File Size: " + String(videoFileSize) + " bytes");
    Serial.println("Status: " + String(videoPaused ? "PAUSED" : "PLAYING"));
    Serial.println("Full Screen: " + String(fullScreenMode ? "Yes" : "No"));
    Serial.println("Actual FPS: " + String(actualFPS, 1));
  } else {
    Serial.println("No video currently playing");
  }
  
  Serial.println("====================");
}

// ========== 文件扫描函数 ==========
int scanDirectory(File dir, String path) {
  int fileCount = 0;
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;
    
    String fullPath = path + "/" + String(entry.name());
    
    if (entry.isDirectory()) {
      if (String(entry.name()) != "System Volume Information" && 
          !String(entry.name()).startsWith(".")) {
        fileCount += scanDirectory(entry, fullPath);
      }
    } else {
      String filename = String(entry.name());
      filename.toLowerCase();
      if (filename.endsWith(".vid") || filename.endsWith(".avi") || 
          filename.endsWith(".mp4")) {
        fileCount++;
        if (serialDebug && fileCount <= 10) {
          debugPrint("Found media file: " + fullPath);
        }
      }
    }
    entry.close();
  }
  return fileCount;
}

// ========== 从SD卡获取媒体文件名 ==========
String getMediaFileName(int index) {
  if (index < 0 || index >= mediaFileCount) return "";
  
  int currentIndex = 0;
  File root = SD.open("/");
  if (!root) return "";
  
  String result = findFileByIndex(root, "", index, currentIndex);
  root.close();
  return result;
}

String findFileByIndex(File dir, String path, int targetIndex, int &currentIndex) {
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;
    
    String fullPath = path + "/" + String(entry.name());
    
    if (entry.isDirectory()) {
      if (String(entry.name()) != "System Volume Information" && 
          !String(entry.name()).startsWith(".")) {
        String result = findFileByIndex(entry, fullPath, targetIndex, currentIndex);
        if (result != "") {
          entry.close();
          return result;
        }
      }
    } else {
      String filename = String(entry.name());
      filename.toLowerCase();
      if (filename.endsWith(".vid") || filename.endsWith(".avi") || 
          filename.endsWith(".mp4")) {
        if (currentIndex == targetIndex) {
          entry.close();
          return fullPath;
        }
        currentIndex++;
      }
    }
    entry.close();
  }
  return "";
}

// ========== 光标处理函数 - 重新设计版本 ==========

void eraseCursor() {
  // 直接重绘光标所在的小区域，而不是尝试保存/恢复像素
  // 这种方法更可靠，不会留下残影
  
  if (currentState == STATE_MAIN_MENU) {
    // 在主菜单中，光标可能在按钮上，需要重绘按钮
    // 我们重新绘制整个菜单项区域
    int row = oldCursorY / 18;
    int col = (oldCursorX - 5) / 78;
    if (row >= 0 && row < 4 && col >= 0 && col < 2) {
      int index = row * 2 + col;
      if (index < 8) {
        const char* menuItems[] = {"Play", "Files", "Settings", "Performance", "Bluetooth", "Info", "Next", "Prev"};
        int x = 5 + col * 78;
        int y = 15 + row * 18;
        bool selected = (index == selectedMenuItem);
        drawSmallButton(x, y, 73, 16, menuItems[index], selected);
      }
    }
  } else if (currentState == STATE_FILE_BROWSER) {
    // 在文件浏览器中，如果光标在文件列表区域，需要重绘该文件项
    if (oldCursorY >= 12 && oldCursorY < SCREEN_HEIGHT - 25) {
      int fileIndex = fileScrollOffset + (oldCursorY - 12) / 16;
      if (fileIndex >= fileScrollOffset && fileIndex < min(fileScrollOffset + MAX_FILES_PER_PAGE, mediaFileCount)) {
        int yPos = 12 + (fileIndex - fileScrollOffset) * 16;
        bool selected = (fileIndex == selectedFileIndex);
        
        String fileName = getMediaFileName(fileIndex);
        int lastSlash = fileName.lastIndexOf('/');
        if (lastSlash != -1) {
          fileName = fileName.substring(lastSlash + 1);
        }
        if (fileName.length() > 20) {
          fileName = fileName.substring(0, 20) + "...";
        }
        
        if (selected) {
          tft.fillRect(2, yPos, SCREEN_WIDTH - 8, 14, HIGHLIGHT_COLOR);
          tft.setTextColor(BACKGROUND_COLOR, HIGHLIGHT_COLOR);
        } else {
          tft.fillRect(2, yPos, SCREEN_WIDTH - 8, 14, BACKGROUND_COLOR);
          tft.setTextColor(TEXT_COLOR, BACKGROUND_COLOR);
        }
        
        tft.setCursor(4, yPos + 3);
        tft.println(fileName);
      }
    }
  } else if (currentState == STATE_VIDEO_PLAYER && !fullScreenMode) {
    // 在视频播放器中，如果光标在控制按钮区域，需要重绘按钮
    if (oldCursorY >= SCREEN_HEIGHT - 35 && oldCursorY <= SCREEN_HEIGHT - 21) {
      int buttonStartY = SCREEN_HEIGHT - 35;
      
      // 重绘所有控制按钮
      drawSmallButton(5, buttonStartY, 30, 14, "<", false);
      drawSmallButton(40, buttonStartY, 30, 14, videoPaused ? "P" : "S", false);
      drawSmallButton(75, buttonStartY, 30, 14, ">", false);
      drawSmallButton(110, buttonStartY, 30, 14, "F", false);
    }
  } else if (currentState == STATE_SETTINGS) {
    // 在设置界面中，如果光标在返回按钮区域，需要重绘按钮
    if (oldCursorX >= 5 && oldCursorX <= 45 && oldCursorY >= SCREEN_HEIGHT - 25 && oldCursorY <= SCREEN_HEIGHT - 11) {
      drawSmallButton(5, SCREEN_HEIGHT - 25, 40, 14, "Back", false);
    }
  }
}

void drawCursor() {
  // 只在需要显示光标的状态下绘制
  if (currentState != STATE_VIDEO_PLAYER || !isPlayingVideo || videoPaused || !fullScreenMode) {
    // 绘制简单的点状光标，减少残影
    tft.drawPixel(cursorX, cursorY, CURSOR_COLOR);
    
    // 更新旧光标位置
    oldCursorX = cursorX;
    oldCursorY = cursorY;
  }
}

// ========== 界面绘制函数 ==========
void drawMainMenu() {
  tft.fillScreen(BACKGROUND_COLOR);
  
  // 标题
  tft.setTextColor(TFT_CYAN, BACKGROUND_COLOR);
  tft.setTextSize(1);
  tft.setCursor(25, 2);
  tft.println("Video Player");
  
  // 菜单项
  const char* menuItems[] = {
    "Play",
    "Files", 
    "Settings",
    "Performance",
    "Bluetooth",
    "Info",
    "Next",
    "Prev"
  };
  
  // 使用紧凑的2x4网格布局
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 2; col++) {
      int index = row * 2 + col;
      if (index >= 8) break;
      
      int x = 5 + col * 78;
      int y = 15 + row * 18;
      bool selected = (index == selectedMenuItem);
      
      drawSmallButton(x, y, 73, 16, menuItems[index], selected);
    }
  }
  
  drawStatusBar("Joystick+Btn to navigate");
  
  // 重置光标位置，避免残影
  oldCursorX = cursorX;
  oldCursorY = cursorY;
}

// ========== 绘制视频控制按钮 ==========
void drawVideoControls() {
  if (fullScreenMode) return;
  
  // 控制按钮
  int buttonWidth = 30;
  int buttonHeight = 14;
  int startY = SCREEN_HEIGHT - 35;
  
  drawSmallButton(5, startY, buttonWidth, buttonHeight, "<", false);
  drawSmallButton(40, startY, buttonWidth, buttonHeight, videoPaused ? "P" : "S", false);
  drawSmallButton(75, startY, buttonWidth, buttonHeight, ">", false);
  drawSmallButton(110, startY, buttonWidth, buttonHeight, "F", false);
  
  // 性能信息显示
  tft.setTextColor(TFT_YELLOW, BACKGROUND_COLOR);
  tft.setCursor(SCREEN_WIDTH - 40, 2);
  tft.print(String(actualFPS, 1) + "fps");
  
  // 进度条
  tft.drawRect(2, SCREEN_HEIGHT - 18, SCREEN_WIDTH - 4, 8, TFT_WHITE);
  
  // 视频信息
  tft.setTextColor(TEXT_COLOR, BACKGROUND_COLOR);
  tft.setTextSize(1);
  
  String shortName = currentMediaPath;
  int lastSlash = shortName.lastIndexOf('/');
  if (lastSlash != -1) {
    shortName = shortName.substring(lastSlash + 1);
  }
  if (shortName.length() > 15) {
    shortName = shortName.substring(0, 15) + "...";
  }
  
  tft.setCursor(2, 2);
  tft.println(shortName);
}

// ========== 隐藏视频控制按钮 ==========
void hideVideoControls() {
  if (!fullScreenMode) return;
  tft.fillRect(0, 0, SCREEN_WIDTH, 12, BACKGROUND_COLOR);
  tft.fillRect(0, SCREEN_HEIGHT - 40, SCREEN_WIDTH, 40, BACKGROUND_COLOR);
}

void drawVideoPlayer() {
  tft.fillScreen(BACKGROUND_COLOR);
  
  if (!fullScreenMode) {
    drawVideoControls();
  } else {
    hideVideoControls();
  }
  
  lastProgressWidth = 0;
  updateVideoInfo();
  
  // 重置光标位置，避免残影
  oldCursorX = cursorX;
  oldCursorY = cursorY;
}

void drawFileBrowser() {
  tft.fillScreen(BACKGROUND_COLOR);
  
  tft.setTextColor(TFT_CYAN, BACKGROUND_COLOR);
  tft.setTextSize(1);
  tft.setCursor(5, 2);
  tft.println("Files (" + String(mediaFileCount) + ")");
  
  // 文件列表 - 只显示当前页面的文件
  int startIndex = fileScrollOffset;
  int endIndex = min(startIndex + MAX_FILES_PER_PAGE, mediaFileCount);
  
  for (int i = startIndex; i < endIndex; i++) {
    int yPos = 12 + (i - startIndex) * 16;
    bool selected = (i == selectedFileIndex);
    
    // 从SD卡获取文件名
    String fileName = getMediaFileName(i);
    int lastSlash = fileName.lastIndexOf('/');
    if (lastSlash != -1) {
      fileName = fileName.substring(lastSlash + 1);
    }
    
    if (fileName.length() > 20) {
      fileName = fileName.substring(0, 20) + "...";
    }
    
    if (selected) {
      tft.fillRect(2, yPos, SCREEN_WIDTH - 8, 14, HIGHLIGHT_COLOR);
      tft.setTextColor(BACKGROUND_COLOR, HIGHLIGHT_COLOR);
    } else {
      tft.setTextColor(TEXT_COLOR, BACKGROUND_COLOR);
    }
    
    tft.setCursor(4, yPos + 3);
    tft.println(fileName);
  }
  
  // 滚动指示器
  if (mediaFileCount > MAX_FILES_PER_PAGE) {
    tft.fillRect(SCREEN_WIDTH - 4, 12, 2, 112, TFT_DARKGREY);
    float scrollRatio = (float)fileScrollOffset / (mediaFileCount - MAX_FILES_PER_PAGE);
    int scrollPos = 12 + (int)(scrollRatio * 112);
    tft.fillRect(SCREEN_WIDTH - 4, scrollPos, 2, 12, TFT_WHITE);
  }
  
  // 添加返回按钮
  drawSmallButton(SCREEN_WIDTH - 45, SCREEN_HEIGHT - 15, 40, 12, "Back", false);
  
  drawStatusBar("Select:Play  Back:Menu");
  
  // 重置光标位置，避免残影
  oldCursorX = cursorX;
  oldCursorY = cursorY;
}

void drawSettings() {
  tft.fillScreen(BACKGROUND_COLOR);
  
  tft.setTextColor(TFT_CYAN, BACKGROUND_COLOR);
  tft.setTextSize(1);
  tft.setCursor(5, 2);
  tft.println("Settings");
  
  tft.setTextColor(TEXT_COLOR, BACKGROUND_COLOR);
  
  tft.setCursor(5, 15);
  tft.println("FPS:" + String(targetFPS));
  
  tft.setCursor(5, 30);
  tft.println("Loop:" + String(videoLoop ? "Y" : "N"));
  
  tft.setCursor(5, 45);
  tft.println("BT:" + String(bluetoothConnected ? "On" : "Off"));
  
  tft.setCursor(5, 60);
  tft.println("RAM:" + String(esp_get_free_heap_size() / 1024) + "KB");
  
  tft.setCursor(5, 75);
  tft.println("Files:" + String(mediaFileCount));
  
  tft.setCursor(5, 90);
  tft.println("SpeedOpt:" + String(optimizeForSpeed ? "On" : "Off"));
  
  tft.setCursor(5, 105);
  tft.println("FrameSkip:" + String(frameSkip));
  
  drawSmallButton(5, SCREEN_HEIGHT - 25, 40, 14, "Back", false);
  
  // 重置光标位置，避免残影
  oldCursorX = cursorX;
  oldCursorY = cursorY;
}

void drawButton(int x, int y, int w, int h, String text, bool selected) {
  if (selected) {
    tft.fillRoundRect(x, y, w, h, 2, HIGHLIGHT_COLOR);
    tft.setTextColor(BACKGROUND_COLOR, HIGHLIGHT_COLOR);
  } else {
    tft.drawRoundRect(x, y, w, h, 2, TEXT_COLOR);
    tft.setTextColor(TEXT_COLOR, BACKGROUND_COLOR);
  }
  
  int textX = x + (w - text.length() * 6) / 2;
  int textY = y + (h - 8) / 2;
  
  tft.setTextSize(1);
  tft.setCursor(textX, textY);
  tft.println(text);
}

void drawSmallButton(int x, int y, int w, int h, String text, bool selected) {
  if (selected) {
    tft.fillRect(x, y, w, h, HIGHLIGHT_COLOR);
    tft.setTextColor(BACKGROUND_COLOR, HIGHLIGHT_COLOR);
  } else {
    tft.drawRect(x, y, w, h, TEXT_COLOR);
    tft.setTextColor(TEXT_COLOR, BACKGROUND_COLOR);
  }
  
  int textX = x + (w - text.length() * 6) / 2;
  int textY = y + (h - 8) / 2;
  
  tft.setTextSize(1);
  tft.setCursor(textX, textY);
  tft.println(text);
}

void drawStatusBar(String text) {
  tft.fillRect(0, SCREEN_HEIGHT - 10, SCREEN_WIDTH, 10, STATUS_BAR_COLOR);
  tft.setTextColor(TEXT_COLOR, STATUS_BAR_COLOR);
  tft.setTextSize(1);
  tft.setCursor(2, SCREEN_HEIGHT - 9);
  
  if (text.length() > 26) {
    text = text.substring(0, 26);
  }
  tft.println(text);
}

void showMessage(String message, uint16_t color) {
  tft.fillRect(5, SCREEN_HEIGHT / 2 - 8, SCREEN_WIDTH - 10, 16, BACKGROUND_COLOR);
  tft.drawRect(5, SCREEN_HEIGHT / 2 - 8, SCREEN_WIDTH - 10, 16, color);
  tft.setTextColor(color, BACKGROUND_COLOR);
  tft.setTextSize(1);
  tft.setCursor(10, SCREEN_HEIGHT / 2 - 4);
  
  if (message.length() > 22) {
    message = message.substring(0, 22) + "...";
  }
  tft.println(message);
}

// ========== 输入处理函数 ==========
void handleJoystick() {
  int xVal = analogRead(pinx);
  int yVal = analogRead(piny);
  
  unsigned long currentTime = millis();
  if (currentTime - lastCursorMove > CURSOR_UPDATE_INTERVAL) {
    int sensitivity = 2;
    
    int xOffset = 0;
    int yOffset = 0;
    
    if (xVal > 3500) xOffset = sensitivity;
    else if (xVal < 1000) xOffset = -sensitivity;
    
    if (yVal > 3500) yOffset = -sensitivity;
    else if (yVal < 1000) yOffset = sensitivity;
    
    if (xOffset != 0 || yOffset != 0) {
      int newCursorX = cursorX + xOffset;
      int newCursorY = cursorY + yOffset;
      
      newCursorX = constrain(newCursorX, 0, SCREEN_WIDTH - 1);
      newCursorY = constrain(newCursorY, 0, SCREEN_HEIGHT - 1);
      
      if (newCursorX != cursorX || newCursorY != cursorY) {
        // 标记光标需要重绘
        cursorMoved = true;
        
        cursorX = newCursorX;
        cursorY = newCursorY;
        lastCursorMove = currentTime;
      }
    }
  }
}

void handleConfirmButton() {
  bool currentStateBtn = digitalRead(confirmPin);
  unsigned long currentTime = millis();
  
  if (currentStateBtn != lastConfirmState) {
    lastConfirmTime = currentTime;
  }
  
  if ((currentTime - lastConfirmTime) > DEBOUNCE_DELAY) {
    if (currentStateBtn != confirmPressed) {
      confirmPressed = currentStateBtn;
    }
  }
  
  lastConfirmState = currentStateBtn;
}

void processUIInput() {
  static bool lastConfirm = false;
  
  if (confirmPressed && !lastConfirm) {
    debugPrint("UI Input - State: " + String(currentState) + " Cursor: (" + String(cursorX) + "," + String(cursorY) + ")");
    
    switch (currentState) {
      case STATE_MAIN_MENU:
        switch (selectedMenuItem) {
          case 0: // Play
            if (mediaFileCount > 0) {
              currentMediaIndex = 0;
              playVideo(getMediaFileName(0));
              currentState = STATE_VIDEO_PLAYER;
            } else {
              showMessage("No media files!", TFT_RED);
            }
            break;
          case 1: // Files
            currentState = STATE_FILE_BROWSER;
            selectedFileIndex = 0;
            fileScrollOffset = 0;
            drawFileBrowser();
            break;
          case 2: // Settings
            currentState = STATE_SETTINGS;
            drawSettings();
            break;
          case 3: // Performance
            adjustPerformanceSettings();
            break;
          case 4: // Bluetooth
            bluetoothConnected = !bluetoothConnected;
            showMessage("BT: " + String(bluetoothConnected ? "On" : "Off"), 
                       bluetoothConnected ? TFT_GREEN : TFT_RED);
            break;
          case 5: // Info
            showMessage("FPS: " + String(actualFPS, 1) + " RAM: " + String(esp_get_free_heap_size() / 1024) + "KB", TFT_CYAN);
            break;
          case 6: // Next
            playNextVideo();
            break;
          case 7: // Prev
            playPrevVideo();
            break;
        }
        break;
        
      case STATE_FILE_BROWSER:
        if (cursorX >= SCREEN_WIDTH - 45 && cursorX <= SCREEN_WIDTH - 5 && 
            cursorY >= SCREEN_HEIGHT - 15 && cursorY <= SCREEN_HEIGHT - 3) {
          currentState = STATE_MAIN_MENU;
          drawMainMenu();
        } else if (selectedFileIndex >= 0 && selectedFileIndex < mediaFileCount) {
          currentMediaIndex = selectedFileIndex;
          playVideo(getMediaFileName(selectedFileIndex));
          currentState = STATE_VIDEO_PLAYER;
        }
        break;
        
      case STATE_VIDEO_PLAYER:
        if (fullScreenMode) {
          toggleFullScreen();
        } else {
          int buttonStartY = SCREEN_HEIGHT - 35;
          int buttonHeight = 14;
          
          if (cursorY >= buttonStartY && cursorY <= buttonStartY + buttonHeight) {
            if (cursorX >= 5 && cursorX <= 35) {
              playPrevVideo();
            } else if (cursorX >= 40 && cursorX <= 70) {
              togglePlayPause();
            } else if (cursorX >= 75 && cursorX <= 105) {
              playNextVideo();
            } else if (cursorX >= 110 && cursorX <= 140) {
              toggleFullScreen();
            }
          }
        }
        break;
        
      case STATE_SETTINGS:
        if (cursorX >= 5 && cursorX <= 45 && cursorY >= SCREEN_HEIGHT - 25 && cursorY <= SCREEN_HEIGHT - 11) {
          currentState = STATE_MAIN_MENU;
          drawMainMenu();
        }
        break;
    }
  }
  
  lastConfirm = confirmPressed;
}

// ========== 性能调节函数 ==========
void adjustPerformanceSettings() {
  tft.fillScreen(BACKGROUND_COLOR);
  tft.setTextColor(TFT_CYAN, BACKGROUND_COLOR);
  tft.setTextSize(1);
  tft.setCursor(5, 2);
  tft.println("Performance Settings");
  
  int selectedSetting = 0;
  bool settingChanged = true;
  
  while (true) {
    if (settingChanged) {
      tft.fillRect(0, 15, SCREEN_WIDTH, SCREEN_HEIGHT - 25, BACKGROUND_COLOR);
      
      tft.setTextColor(selectedSetting == 0 ? HIGHLIGHT_COLOR : TEXT_COLOR, BACKGROUND_COLOR);
      tft.setCursor(5, 15);
      tft.println("FPS: " + String(targetFPS));
      
      tft.setTextColor(selectedSetting == 1 ? HIGHLIGHT_COLOR : TEXT_COLOR, BACKGROUND_COLOR);
      tft.setCursor(5, 30);
      tft.println("Speed Opt: " + String(optimizeForSpeed ? "ON" : "OFF"));
      
      tft.setTextColor(selectedSetting == 2 ? HIGHLIGHT_COLOR : TEXT_COLOR, BACKGROUND_COLOR);
      tft.setCursor(5, 45);
      tft.println("Frame Skip: " + String(frameSkip));
      
      tft.setTextColor(TEXT_COLOR, BACKGROUND_COLOR);
      tft.setCursor(5, 65);
      tft.println("Actual FPS: " + String(actualFPS, 1));
      tft.setCursor(5, 75);
      tft.println("Free RAM: " + String(esp_get_free_heap_size() / 1024) + "KB");
      
      drawSmallButton(5, SCREEN_HEIGHT - 25, 40, 14, "Back", false);
      
      settingChanged = false;
    }
    
    handleJoystick();
    handleConfirmButton();
    
    int yVal = analogRead(piny);
    int xVal = analogRead(pinx);
    
    static unsigned long lastNavTime = 0;
    unsigned long currentTime = millis();
    
    if (currentTime - lastNavTime > 300) {
      if (yVal > 3500 && selectedSetting > 0) {
        selectedSetting--;
        settingChanged = true;
        lastNavTime = currentTime;
      } else if (yVal < 1000 && selectedSetting < 2) {
        selectedSetting++;
        settingChanged = true;
        lastNavTime = currentTime;
      } else if (xVal > 3500) {
        switch (selectedSetting) {
          case 0:
            if (targetFPS > 5) {
              targetFPS--;
              frameDelay = 1000 / targetFPS;
            }
            break;
          case 1:
            optimizeForSpeed = !optimizeForSpeed;
            break;
          case 2:
            if (frameSkip > 0) frameSkip--;
            break;
        }
        settingChanged = true;
        lastNavTime = currentTime;
      } else if (xVal < 1000) {
        switch (selectedSetting) {
          case 0:
            if (targetFPS < 15) {
              targetFPS++;
              frameDelay = 1000 / targetFPS;
            }
            break;
          case 1:
            optimizeForSpeed = !optimizeForSpeed;
            break;
          case 2:
            if (frameSkip < 3) frameSkip++;
            break;
        }
        settingChanged = true;
        lastNavTime = currentTime;
      }
    }
    
    if (confirmPressed) {
      if (cursorX >= 5 && cursorX <= 45 && cursorY >= SCREEN_HEIGHT - 25 && cursorY <= SCREEN_HEIGHT - 11) {
        break;
      }
    }
    
    delay(50);
  }
  
  currentState = STATE_MAIN_MENU;
  drawMainMenu();
}

// ========== 切换全屏模式 ==========
void toggleFullScreen() {
  if (!isPlayingVideo) return;
  
  fullScreenMode = !fullScreenMode;
  
  if (fullScreenMode) {
    hideVideoControls();
    showMessage("Full Screen Mode", TFT_GREEN);
    debugPrint("Entered full screen mode");
  } else {
    drawVideoPlayer();
    showMessage("Normal Mode", TFT_CYAN);
    debugPrint("Exited full screen mode");
  }
}

// ========== 播放/暂停切换函数 ==========
void togglePlayPause() {
  if (isPlayingVideo) {
    videoPaused = !videoPaused;
    if (videoPaused) {
      showMessage("Paused", TFT_YELLOW);
      debugPrint("Video paused");
    } else {
      showMessage("Playing", TFT_GREEN);
      debugPrint("Video resumed");
    }
  }
}

// ========== 系统功能函数 ==========
void addToOutput(String text) {
  Serial.println("OUTPUT: " + text);
}

void executeCommand(String command) {
  command.trim();
  if (command.length() == 0) return;
  
  debugPrint("Executing command: " + command);
  
  String cmdLower = command;
  cmdLower.toLowerCase();
  
  if (cmdLower == "play" && mediaFileCount > 0) {
    currentMediaIndex = 0;
    playVideo(getMediaFileName(0));
    currentState = STATE_VIDEO_PLAYER;
  } else if (cmdLower == "stop" && isPlayingVideo) {
    stopVideo();
    currentState = STATE_MAIN_MENU;
  } else if (cmdLower == "pause" && isPlayingVideo) {
    videoPaused = true;
    showMessage("Paused", TFT_YELLOW);
  } else if (cmdLower == "resume" && isPlayingVideo) {
    videoPaused = false;
    showMessage("Playing", TFT_GREEN);
  } else if (cmdLower == "next") {
    playNextVideo();
  } else if (cmdLower == "prev") {
    playPrevVideo();
  } else if (cmdLower == "scan") {
    scanMediaFiles();
  } else if (cmdLower == "fullscreen") {
    toggleFullScreen();
  } else if (cmdLower == "performance") {
    adjustPerformanceSettings();
  } else if (cmdLower.startsWith("play ")) {
    String filename = command.substring(5);
    for (int i = 0; i < mediaFileCount; i++) {
      if (getMediaFileName(i).indexOf(filename) != -1) {
        currentMediaIndex = i;
        playVideo(getMediaFileName(i));
        currentState = STATE_VIDEO_PLAYER;
        break;
      }
    }
  } else {
    addToOutput("Unknown command: " + command);
  }
}

void playVideo(String filename) {
  if (!SD.exists(filename)) {
    showMessage("File not found!", TFT_RED);
    debugPrint("Play video failed: File not found - " + filename);
    return;
  }
  
  videoFile = SD.open(filename, FILE_READ);
  if (!videoFile) {
    showMessage("Open failed!", TFT_RED);
    debugPrint("Play video failed: Open failed - " + filename);
    return;
  }
  
  videoFileSize = videoFile.size();
  uint32_t frameSize = SCREEN_WIDTH * SCREEN_HEIGHT * 2;
  framesInVideo = videoFileSize / frameSize;
  
  if (framesInVideo == 0) {
    showMessage("Invalid file!", TFT_RED);
    videoFile.close();
    debugPrint("Play video failed: Invalid file - " + filename);
    return;
  }
  
  currentMediaPath = filename;
  isPlayingVideo = true;
  videoPaused = false;
  fullScreenMode = false;
  currentFrame = 0;
  lastFrameDisplayed = 0;
  frameCount = 0;
  lastStatsTime = millis();
  lastProgressWidth = 0;
  
  drawVideoPlayer();
  showMessage("Playing...", TFT_GREEN);
  
  debugPrint("Video started: " + filename + 
             ", Frames: " + String(framesInVideo) + 
             ", Size: " + String(videoFileSize) + " bytes");
}

// ========== 优化的视频帧显示函数 ==========
bool displayVideoFrameOptimized() {
  if (!videoFile || !isPlayingVideo || videoPaused) return false;
  
  // 应用跳帧逻辑
  if (frameSkip > 0 && (currentFrame - lastFrameDisplayed) <= frameSkip) {
    currentFrame++;
    return true;
  }
  
  uint32_t frameSize = SCREEN_WIDTH * SCREEN_HEIGHT * 2;
  uint32_t framePosition = currentFrame * frameSize;
  
  if (framePosition >= videoFileSize) {
    if (videoLoop) {
      currentFrame = 0;
      videoFile.seek(0);
      debugPrint("Video looped back to start");
    } else {
      stopVideo();
      return false;
    }
  }
  
  videoFile.seek(framePosition);
  
  // 优化的像素缓冲区
  uint16_t pixelBuffer[SCREEN_WIDTH];
  
  for (int y = 0; y < SCREEN_HEIGHT; y++) {
    if (videoFile.read((uint8_t*)pixelBuffer, SCREEN_WIDTH * 2) == SCREEN_WIDTH * 2) {
      if (optimizeForSpeed) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
          uint16_t pixel = pixelBuffer[x];
          uint8_t b = (pixel >> 11) & 0x1F;
          uint8_t g = (pixel >> 5) & 0x3F;
          uint8_t r = pixel & 0x1F;
          pixelBuffer[x] = (r << 11) | (g << 5) | b;
        }
        tft.pushImage(0, y, SCREEN_WIDTH, 1, pixelBuffer);
      }
    } else {
      debugPrint("Frame read error at frame " + String(currentFrame));
      return false;
    }
  }
  
  lastFrameDisplayed = currentFrame;
  currentFrame++;
  frameCount++;
  
  // 更新FPS计算
  unsigned long currentTime = millis();
  if (currentTime - lastStatsTime >= 1000) {
    actualFPS = (float)frameCount / ((currentTime - lastStatsTime) / 1000.0);
    frameCount = 0;
    lastStatsTime = currentTime;
  }
  
  // 非全屏模式下更新进度信息
  if (!fullScreenMode) {
    updateVideoInfo();
  }
  
  return true;
}

bool displayVideoFrame() {
  return displayVideoFrameOptimized();
}

void stopVideo() {
  isPlayingVideo = false;
  videoPaused = false;
  fullScreenMode = false;
  if (videoFile) {
    videoFile.close();
  }
  
  unsigned long currentTime = millis();
  if (lastStatsTime > 0) {
    float elapsed = (currentTime - lastStatsTime) / 1000.0;
    float fps = frameCount / elapsed;
    showMessage("FPS: " + String(fps, 1), TFT_CYAN);
    debugPrint("Video stopped - Avg FPS: " + String(fps, 1) + 
               ", Frames: " + String(frameCount));
  }
  
  debugPrint("Video stopped: " + currentMediaPath);
}

void playNextVideo() {
  if (mediaFileCount == 0) {
    showMessage("No videos!", TFT_RED);
    debugPrint("Play next failed: No media files");
    return;
  }
  
  if (isPlayingVideo) {
    stopVideo();
    delay(100);
  }
  
  currentMediaIndex = (currentMediaIndex + 1) % mediaFileCount;
  playVideo(getMediaFileName(currentMediaIndex));
  
  debugPrint("Playing next video: " + getMediaFileName(currentMediaIndex));
}

void playPrevVideo() {
  if (mediaFileCount == 0) {
    showMessage("No videos!", TFT_RED);
    debugPrint("Play prev failed: No media files");
    return;
  }
  
  if (isPlayingVideo) {
    stopVideo();
    delay(100);
  }
  
  currentMediaIndex = (currentMediaIndex - 1 + mediaFileCount) % mediaFileCount;
  playVideo(getMediaFileName(currentMediaIndex));
  
  debugPrint("Playing previous video: " + getMediaFileName(currentMediaIndex));
}

void updateVideoInfo() {
  if (!isPlayingVideo || fullScreenMode) return;
  
  float progress = (float)currentFrame / framesInVideo;
  int barWidth = (SCREEN_WIDTH - 4) * progress;
  
  if (barWidth != lastProgressWidth) {
    if (barWidth < lastProgressWidth) {
      tft.fillRect(2 + barWidth, SCREEN_HEIGHT - 18, lastProgressWidth - barWidth, 8, BACKGROUND_COLOR);
    }
    tft.fillRect(2, SCREEN_HEIGHT - 18, barWidth, 8, PROGRESS_BAR_COLOR);
    lastProgressWidth = barWidth;
  }
  
  static unsigned long lastInfoUpdate = 0;
  unsigned long currentTime = millis();
  if (currentTime - lastInfoUpdate > 500) {
    tft.fillRect(2, 2, SCREEN_WIDTH - 45, 10, BACKGROUND_COLOR);
    tft.setTextColor(TEXT_COLOR, BACKGROUND_COLOR);
    tft.setCursor(2, 2);
    tft.print(currentFrame);
    tft.print("/");
    tft.print(framesInVideo);
    tft.print("(");
    tft.print((int)(progress * 100));
    tft.print("%)");
    lastInfoUpdate = currentTime;
  }
}

// ========== SD卡和媒体扫描函数 ==========
bool initSDCard() {
  CSD.begin(sck, mis, mos, cs);
  bool success = SD.begin(cs, CSD, 80000000);
  if (!success) {
    success = SD.begin(cs, CSD, 40000000);
  }
  debugPrint("SD card initialization: " + String(success ? "SUCCESS" : "FAILED"));
  return success;
}

int scanMediaFiles() {
  int count = 0;
  
  File root = SD.open("/");
  if (!root) {
    Serial.println("Failed to open root directory");
    debugPrint("Failed to open root directory for scanning");
    return 0;
  }
  
  if (root.isDirectory()) {
    count = scanDirectory(root, "");
  }
  root.close();
  
  debugPrint("Media scan complete, found " + String(count) + " files");
  Serial.println("Found " + String(count) + " media files");
  return count;
}

// ========== 初始化函数 ==========
void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== ESP32 Video Player ===");
  Serial.println("Initializing system...");
  
  // 初始化引脚
  pinMode(pinx, INPUT);
  pinMode(piny, INPUT);
  pinMode(confirmPin, INPUT_PULLUP);
  
  // 初始化TFT
  tft.init();
  tft.setRotation(3);
  tft.fillScreen(BACKGROUND_COLOR);
  tft.setTextColor(TEXT_COLOR, BACKGROUND_COLOR);
  tft.setTextSize(1);
  tft.setSwapBytes(true);
  
  // 显示启动画面
  tft.setTextColor(TFT_CYAN, BACKGROUND_COLOR);
  tft.setTextSize(1);
  tft.setCursor(40, SCREEN_HEIGHT / 2 - 6);
  tft.println("Video Player");
  tft.setTextColor(TEXT_COLOR, BACKGROUND_COLOR);
  tft.setCursor(45, SCREEN_HEIGHT / 2 + 5);
  tft.println("Optimizing...");
  
  debugPrint("TFT initialized");
  
  // 初始化SD卡
  if (initSDCard()) {
    delay(500);
    mediaFileCount = scanMediaFiles();
  } else {
    showMessage("SD Card Failed!", TFT_RED);
    debugPrint("SD card initialization failed");
    delay(1000);
  }
  
  // 初始化蓝牙音频
  initBluetoothAudio();
  
  // 绘制主菜单
  drawMainMenu();
  
  Serial.println("System ready");
  Serial.println("Type 'help' for debug commands");
  debugPrint("System initialization complete");
  debugPrint("Free memory: " + String(esp_get_free_heap_size()) + " bytes");
}

void loop() {
  // 处理串口输入
  processSerialInput();
  
  // 处理输入
  handleJoystick();
  handleConfirmButton();
  
  processUIInput();
  
  // 视频播放逻辑
  if (isPlayingVideo && currentState == STATE_VIDEO_PLAYER && !videoPaused) {
    unsigned long currentTime = millis();
    if (currentTime - lastFrameTime >= frameDelay) {
      if (!displayVideoFrame()) {
        if (videoLoop) {
          currentFrame = 0;
          lastFrameDisplayed = 0;
          videoFile.seek(0);
        } else {
          stopVideo();
          currentState = STATE_MAIN_MENU;
          drawMainMenu();
        }
      }
      lastFrameTime = currentTime;
    }
  }
  
  // 处理光标绘制 - 新的方法
  if (cursorMoved) {
    // 先擦除旧光标（通过重绘所在区域）
    eraseCursor();
    
    // 然后绘制新光标
    drawCursor();
    
    cursorMoved = false;
  } else {
    // 如果没有移动，也确保光标可见（防止被其他绘制操作覆盖）
    static unsigned long lastCursorRefresh = 0;
    unsigned long currentTime = millis();
    if (currentTime - lastCursorRefresh > 500) { // 每500ms刷新一次光标
      if (currentState != STATE_VIDEO_PLAYER || !isPlayingVideo || videoPaused || !fullScreenMode) {
        // 先擦除再绘制，确保光标可见
        eraseCursor();
        drawCursor();
      }
      lastCursorRefresh = currentTime;
    }
  }
  
  delay(5);
}

void initBluetoothAudio() {
  audio.begin();
  audio.volume(80);
  bluetoothConnected = true;
  debugPrint("Bluetooth audio initialized");
  Serial.println("Bluetooth audio ready");
}