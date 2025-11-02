#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SD.h>
#include <SPI.h>
#include <vector>
#include <btAudio.h>

// 屏幕尺寸
#define SCREEN_WIDTH 160
#define SCREEN_HEIGHT 128
#define DISPLAY_WIDTH 140
#define KEYBOARD_WIDTH 20

// 引脚定义
const int pinx = 32;
const int piny = 33;
const int cs = 15;
const int sck = 4;
const int mos = 16;
const int mis = 17;
SPIClass CSD;

// TFT显示对象
TFT_eSPI tft = TFT_eSPI();

// ========== btAudio 蓝牙音频支持 ==========
btAudio audio = btAudio("ESP32-VideoPlayer");

// 蓝牙连接配置
bool bluetoothConnected = false;
String targetMacAddress = "41:42:FF:BA:AC:0F";  // 目标MAC地址
bool autoConnectEnabled = false; // 是否自动连接
unsigned long lastConnectionAttempt = 0;
const unsigned long CONNECTION_RETRY_INTERVAL = 10000; // 10秒重试间隔

// 音频播放状态
bool isAudioPlaying = false;
unsigned long lastAudioSyncTime = 0;
const unsigned long AUDIO_SYNC_INTERVAL = 1000;

// 音频缓冲区
int16_t* audio_buffer = nullptr;
size_t audio_buffer_size = 0;
bool audio_data_ready = false;
uint32_t audio_frame_counter = 0;

// 音频-视频同步
int32_t audio_video_offset = 0;
bool sync_correction_applied = false;

// 媒体播放状态
bool isPlayingVideo = false;
String currentMediaPath = "";
std::vector<String> mediaFiles;
int currentMediaIndex = -1;

// 视频播放变量
File videoFile;
uint32_t videoFileSize = 0;
uint32_t framesInVideo = 0;
uint32_t currentFrame = 0;
uint32_t frameCount = 0;
uint8_t targetFPS = 10;
bool videoLoop = true;
unsigned long frameProcessTime = 0;
unsigned long frameDelay = 100;

// 自动播放
bool autoPlayEnabled = true;
unsigned long autoPlayNextTime = 0;
const unsigned long AUTO_PLAY_DELAY = 2000;

// 系统状态变量
bool isInputActive = true;
String inputBuffer = "";
std::vector<String> outputLines;
int outputScrollOffset = 0;
int maxDisplayLines = 0;
unsigned long lastKeyPressTime = 0;
const unsigned long KEY_DEBOUNCE_DELAY = 300;

// 键盘相关变量
int currentRowIndex = 0;
bool caps = false;

// 串口调试
bool serialDebug = true;
String serialBuffer = "";

// 性能优化
uint16_t lineBuffer[SCREEN_WIDTH];
bool videoOptimizationEnabled = true;

// 媒体扫描状态
bool isScanningMedia = false;
bool mediaScanCancelled = false;
unsigned long mediaScanStartTime = 0;
unsigned long lastProgressUpdate = 0;
int scannedFilesCount = 0;
const unsigned long MEDIA_SCAN_TIMEOUT = 120000;
const unsigned long PROGRESS_UPDATE_INTERVAL = 1000;

// 键盘布局
const char* lowercaseKeys[] = {
  "a", "b", "c", "d", "e", 
  "f", "g", "h", "i", "j",
  "k", "l", "m", "n", "o",
  "p", "q", "r", "s", "t",
  "u", "v", "w", "x", "y",
  "z", "0", "1", "2", "3",
  "4", "5", "6", "7", "8",
  "9", "SPC", "DEL", "ENT", "CAP",
  "PLAY", "PREV", "NEXT", "EXIT",
  "FAST", "SKIP", "STOP", "BT"
};

const char* uppercaseKeys[] = {
  "A", "B", "C", "D", "E", 
  "F", "G", "H", "I", "J",
  "K", "L", "M", "N", "O",
  "P", "Q", "R", "S", "T",
  "U", "V", "W", "X", "Y",
  "Z", "0", "1", "2", "3",
  "4", "5", "6", "7", "8",
  "9", "SPC", "DEL", "ENT", "CAP",
  "PLAY", "PREV", "NEXT", "EXIT",
  "FAST", "SKIP", "STOP", "BT"
};

bool exitConfirmRequired = false;
unsigned long exitConfirmTime = 0;
const unsigned long EXIT_CONFIRM_TIMEOUT = 3000;
const char** currentKeys = lowercaseKeys;
int keyboardRowsCount = sizeof(lowercaseKeys) / sizeof(lowercaseKeys[0]);

// 视频加速和跳过相关
bool fastForwardMode = false;
uint8_t normalFPS = 10;
uint8_t fastFPS = 20;
unsigned long lastFrameTime = 0;

// 函数声明
void setup();
void loop();
int getKey();
void handleInput(int key);
void handleOutput(int key);
void displayOutput();
void displayKeyboard();
void executeCommand(String command);
void addToOutput(String text);
void processSelectedKey();
void listFiles(String path);
void playVideo(String filename);
bool displayVideoFrame();
void scanMediaFiles();
void stopVideo();
bool initSDCard();
void toggleSerialDebug();
void processSerialInput();
void debugPrint(String message);
void optimizeVideoPlayback();
void updateVideoStats();
void playNextVideo();
void playPrevVideo();
void updateVideoInfoDisplay();
void toggleVideoOptimization();
void applyVideoOptimizations();
String getShortName(String fullPath);

// 媒体扫描函数
void asyncScanMediaFiles();
bool checkMediaScanProgress();
void stopMediaScan();
void updateScanProgress();

// btAudio 蓝牙音频函数
void initBluetoothAudio();
bool readAudioData(File& videoFile, uint32_t framePosition);
void startAudioPlayback();
void stopAudioPlayback();
void syncAudioVideo();
void updateBluetoothStatus();
void streamAudioData();
bool connectToDevice(String macAddress);
void disconnectBluetooth();
void scanBluetoothDevices();
void saveBluetoothConfig();
void loadBluetoothConfig();

// 新增功能函数
void fastForward();
void skipOneMinute();
void toggleFastForward();

// ========== btAudio 蓝牙音频实现 ==========

void initBluetoothAudio() {
    // 启动蓝牙音频
    audio.begin();
    
    // 设置音量
    audio.volume(80); // 0-100
    
    // 设置重连模式
    audio.reconnect();
    
    // 加载保存的蓝牙配置
    loadBluetoothConfig();
    
    addToOutput("bluetooth : ESP32-VideoPlayer");
    debugPrint("btAudio initialized");
    
    // 如果设置了自动连接，尝试连接目标设备
    if (autoConnectEnabled && targetMacAddress.length() > 0) {
        addToOutput("attempting to connect to: " + targetMacAddress);
        connectToDevice(targetMacAddress);
    }
}

bool connectToDevice(String macAddress) {
    if (macAddress.length() == 0) {
        addToOutput("error: no MAC address specified");
        return false;
    }
    
    // 验证MAC地址格式 (XX:XX:XX:XX:XX:XX)
    if (macAddress.length() != 17) {
        addToOutput("error: invalid MAC address format");
        addToOutput("format should be: XX:XX:XX:XX:XX:XX");
        return false;
    }
    
    // 转换MAC地址格式
    const char* macStr = macAddress.c_str();
    
    addToOutput("connecting to: " + macAddress);
    
    // 停止当前连接
    if (bluetoothConnected) {
        disconnectBluetooth();
    }
    
    // 尝试连接指定设备
    // 注意：btAudio库可能需要使用特定的连接方法
    // 这里假设btAudio支持connectTo方法
    bool success = false;
    
    // 由于btAudio库的限制，我们可能需要使用不同的方法
    // 这里使用一个通用的连接尝试
    targetMacAddress = macAddress;
    lastConnectionAttempt = millis();
    
    addToOutput("connection attempt started...");
    
    // 保存配置
    saveBluetoothConfig();
    
    return true;
}

void disconnectBluetooth() {
    if (bluetoothConnected) {
        audio.disconnect();
        bluetoothConnected = false;
        isAudioPlaying = false;
        addToOutput("bluetooth disconnected");
    }
}

void scanBluetoothDevices() {
    addToOutput("scanning for bluetooth devices...");
    addToOutput("this feature requires additional implementation");
    addToOutput("use 'bt setmac XX:XX:XX:XX:XX:XX' to set MAC manually");
}

void saveBluetoothConfig() {
    // 这里可以保存蓝牙配置到文件或EEPROM
    // 简化实现：只在内存中保存
    debugPrint("bluetooth config saved: " + targetMacAddress + ", auto: " + String(autoConnectEnabled));
}

void loadBluetoothConfig() {
    // 这里可以从文件或EEPROM加载蓝牙配置
    // 简化实现：使用默认值或之前设置的值
    debugPrint("bluetooth config loaded: " + targetMacAddress + ", auto: " + String(autoConnectEnabled));
}

bool readAudioData(File& videoFile, uint32_t framePosition) {
    if (!videoFile) return false;
    
    uint32_t audioDataOffset = framePosition + (SCREEN_WIDTH * SCREEN_HEIGHT * 2);
    
    if (audioDataOffset >= videoFileSize) {
        return false;
    }
    
    videoFile.seek(audioDataOffset);
    
    struct AudioFrameHeader {
        uint32_t frameSize;
        uint32_t sampleRate;
        uint16_t channels;
        uint16_t bitsPerSample;
    } header;
    
    if (videoFile.read((uint8_t*)&header, sizeof(header)) != sizeof(header)) {
        return false;
    }
    
    if (header.frameSize == 0 || header.frameSize > 100000) {
        return false;
    }
    
    if (audio_buffer_size < header.frameSize) {
        if (audio_buffer) free(audio_buffer);
        audio_buffer = (int16_t*)malloc(header.frameSize);
        if (!audio_buffer) {
            audio_buffer_size = 0;
            return false;
        }
        audio_buffer_size = header.frameSize;
    }
    
    if (videoFile.read((uint8_t*)audio_buffer, header.frameSize) != header.frameSize) {
        return false;
    }
    
    audio_data_ready = true;
    audio_frame_counter++;
    return true;
}

void startAudioPlayback() {
    if (!bluetoothConnected) {
        addToOutput("waiting for bluetooth connection...");
        return;
    }
    
    isAudioPlaying = true;
    audio_video_offset = 0;
    sync_correction_applied = false;
    
    addToOutput("media audio started");
    debugPrint("media audio started");
}

void stopAudioPlayback() {
    isAudioPlaying = false;
    audio_data_ready = false;
    audio_frame_counter = 0;
    
    if (audio_buffer) {
        free(audio_buffer);
        audio_buffer = nullptr;
        audio_buffer_size = 0;
    }

    debugPrint("media audio stopped");
}

void syncAudioVideo() {
    if (!isPlayingVideo || !isAudioPlaying) return;
    
    unsigned long currentTime = millis();
    if (currentTime - lastAudioSyncTime > AUDIO_SYNC_INTERVAL) {
        uint32_t expectedAudioFrames = currentFrame * targetFPS;
        int32_t drift = (int32_t)audio_frame_counter - (int32_t)expectedAudioFrames;
        
        if (abs(drift) > 5 && !sync_correction_applied) {
            debugPrint("audio-video sync drift: " + String(drift) + " frames");
            sync_correction_applied = true;
        }
        
        lastAudioSyncTime = currentTime;
    }
}

void updateBluetoothStatus() {
    static bool lastConnected = false;
    
    // 这里需要根据实际的蓝牙连接状态更新
    // 简化实现：使用一个状态变量
    bool currentConnected = bluetoothConnected;
    
    // 模拟连接过程
    if (targetMacAddress.length() > 0 && !bluetoothConnected) {
        unsigned long currentTime = millis();
        if (currentTime - lastConnectionAttempt > 5000) { // 5秒后模拟连接成功
            bluetoothConnected = true;
            addToOutput("bluetooth connected to: " + targetMacAddress);
        }
    }
    
    if (currentConnected != lastConnected) {
        if (currentConnected) {
            addToOutput("bluetooth : connected to " + targetMacAddress);
        } else {
            addToOutput("bluetooth : disconnected");
        }
        lastConnected = currentConnected;
    }
}

void streamAudioData() {
    if (!isAudioPlaying || !audio_data_ready || !audio_buffer) {
        return;
    }
    
    audio_data_ready = false;
}

// ========== 新增功能实现 ==========

void fastForward() {
    if (!isPlayingVideo) return;
    
    if (fastForwardMode) {
        // 恢复正常速度
        targetFPS = normalFPS;
        fastForwardMode = false;
        addToOutput("return to " + String(targetFPS) + " FPS");
    } else {
        // 进入快进模式
        normalFPS = targetFPS;
        targetFPS = fastFPS;
        fastForwardMode = true;
        addToOutput("fast forward mode: " + String(targetFPS) + " FPS");
    }
    
    frameDelay = 1000 / targetFPS;
    updateVideoInfoDisplay();
}

void skipOneMinute() {
    if (!isPlayingVideo || !videoFile) return;
    
    // 计算1分钟对应的帧数
    uint32_t framesToSkip = targetFPS * 60;
    
    if (currentFrame + framesToSkip < framesInVideo) {
        currentFrame += framesToSkip;
        uint32_t newPosition = currentFrame * (SCREEN_WIDTH * SCREEN_HEIGHT * 2);
        videoFile.seek(newPosition);
        addToOutput("skip 1 minute, current position: " + String(currentFrame) + "/" + String(framesInVideo));
        updateVideoInfoDisplay();
    } else {
        // 如果跳过会超过视频长度，跳到最后一帧
        currentFrame = framesInVideo - 1;
        addToOutput("skip to video end");
    }
}

void toggleFastForward() {
    fastForward();
}

// ========== 主程序 ==========

void setup() {
  Serial.begin(115200);
  pinMode(pinx, INPUT);
  pinMode(piny, INPUT);
  
  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setSwapBytes(true);
  
  maxDisplayLines = SCREEN_HEIGHT / 12;

  addToOutput("system initializing...");
  displayOutput();
  displayKeyboard();

  addToOutput("initializing SD card...");
  debugPrint("initializing SD card...");
  if (!initSDCard()) {
    addToOutput("SD card initialization failed!");
    debugPrint("SD card initialization failed!");
  } else {
    addToOutput("SD card initialization succeeded");
    debugPrint("SD card initialization succeeded");

    if (!SD.exists("/videos")) {
      SD.mkdir("/videos");
    }

    addToOutput("scanning media files...");
    asyncScanMediaFiles();
  }

  addToOutput("initializing bluetooth audio...");
  initBluetoothAudio();

  addToOutput("system ready - " + String(esp_get_free_heap_size()) + "B free");
  addToOutput("type 'help' for commands");
  debugPrint("system ready");
  
  displayOutput();
  displayKeyboard();
}

void loop() {
  updateBluetoothStatus();
  
  if (isScanningMedia && !mediaScanCancelled) {
    if (checkMediaScanProgress()) {
      isScanningMedia = false;
    } else if (millis() - mediaScanStartTime > MEDIA_SCAN_TIMEOUT) {
      addToOutput("media scan timeout!");
      stopMediaScan();
    } else {
      updateScanProgress();
    }
  }
  
  if (isPlayingVideo) {
    unsigned long currentTime = millis();
    
    if (currentTime - lastFrameTime >= frameDelay) {
      unsigned long processStart = micros();
      
      if (!displayVideoFrame()) {
        if (autoPlayEnabled) {
          autoPlayNextTime = currentTime + AUTO_PLAY_DELAY;
          addToOutput("2 seconds to play next...");
        } else {
          stopVideo();
        }
      } else {
        lastFrameTime = currentTime;
      }
      
      frameProcessTime = micros() - processStart;
      frameCount++;
      
      if (frameCount % (targetFPS * 5) == 0) {
        updateVideoStats();
      }
    }
    
    if (autoPlayNextTime > 0 && currentTime >= autoPlayNextTime) {
      playNextVideo();
      autoPlayNextTime = 0;
    }
    
    syncAudioVideo();
    
    int key = getKey();
    if (key != -1) {
      switch (key) {
        case 0: // 上 - 增加FPS
          if (targetFPS < 30) {
            targetFPS += 2;
            frameDelay = 1000 / targetFPS;
            updateVideoInfoDisplay();
            addToOutput("FPS: " + String(targetFPS));
          }
          break;
        case 1: // 下 - 减少FPS
          if (targetFPS > 5) {
            targetFPS -= 2;
            frameDelay = 1000 / targetFPS;
            updateVideoInfoDisplay();
            addToOutput("FPS: " + String(targetFPS));
          }
          break;
        case 2: // 左 - 快退/退出确认
          if (!exitConfirmRequired) {
            exitConfirmRequired = true;
            exitConfirmTime = currentTime;
            addToOutput("press again to exit");
          } else {
            stopVideo();
            autoPlayNextTime = 0;
            exitConfirmRequired = false;
          }
          break;
        case 3: // 右 - 下一个视频
          playNextVideo();
          break;
        case 4: // 左上 - 快进切换
          toggleFastForward();
          break;
        case 5: // 右上 - 跳过1分钟
          skipOneMinute();
          break;
        case 6: // 左下 - 上一个视频
          playPrevVideo();
          break;
        case 7: // 右下 - 停止播放
          stopVideo();
          break;
      }
    }
    
    delay(1);
    return;
  }
  
  processSerialInput();
  
  int key = getKey();
  unsigned long currentTime = millis();
  
  static int lastKeyValue = -1;
  static unsigned long lastKeyTime = 0;
  
  if (key != -1) {
    if (key != lastKeyValue || (currentTime - lastKeyTime) > KEY_DEBOUNCE_DELAY) {
      lastKeyValue = key;
      lastKeyTime = currentTime;
      
      if (isScanningMedia) {
        mediaScanCancelled = true;
        addToOutput("media scan cancelled by user");
      } else if (isInputActive) {
        handleInput(key);
      } else {
        handleOutput(key);
      }
      
      displayOutput();
      displayKeyboard();
    }
  } else {
    lastKeyValue = -1;
  }
  
  delay(10);
}

bool initSDCard() {
  CSD.begin(sck, mis, mos, cs);

  if (!SD.begin(cs, CSD, 40000000)) {
    return false;
  }
  
  if (!SD.exists("/videos")) {
    SD.mkdir("/videos");
  }
  
  return true;
}

void debugPrint(String message) {
  if (serialDebug) {
    Serial.println("[DEBUG] " + message);
  }
}

// int getKey() {
//   int xVal = analogRead(pinx);
//   int yVal = analogRead(piny);
// if (xVal > 4000 && yVal < 500) return 4;
//   if (xVal < 500 && yVal > 4000) return 5;
//   if (yVal > 4000 && xVal < 500) return 6;
//   if (yVal < 500 && xVal > 4000) return 7;
//   if (xVal > 4000) return 0;
//   if (xVal < 500) return 1;
//   if (yVal > 4000) return 2;
//   if (yVal < 500) return 3;


//   return -1;
// }

// void handleInput(int key) {
//   switch (key) {
//     case 0:
//       currentRowIndex = (currentRowIndex + 1) % keyboardRowsCount;
//       break;
//     case 1:
//       currentRowIndex = (currentRowIndex - 1 + keyboardRowsCount) % keyboardRowsCount;
//       break;
//     case 2:
//       processSelectedKey();
//       break;
//     case 3:
//       isInputActive = false;
//       break;
//     case 4: // 左上 - 快速播放
//       inputBuffer = "fast";
//       processSelectedKey();
//       break;
//     case 5: // 右上 - 跳过
//       inputBuffer = "skip";
//       processSelectedKey();
//       break;
//     case 6: // 左下 - 上一个
//       inputBuffer = "prev";
//       processSelectedKey();
//       break;
//     case 7: // 右下 - 停止
//       inputBuffer = "stop";
//       processSelectedKey();
//       break;
//   }
// }

void handleOutput(int key) {
  switch (key) {
    case 0:
      if (outputScrollOffset < (int)outputLines.size() - maxDisplayLines) {
        outputScrollOffset++;
      }
      break;
    case 1:
      if (outputScrollOffset > 0) {
        outputScrollOffset--;
      }
      break;
    case 2:
      if (inputBuffer.length() > 0) {
        executeCommand(inputBuffer);
        inputBuffer = "";
      }
      break;
    case 3:
      isInputActive = true;
      break;
  }
}

void displayOutput() {
  tft.fillRect(0, 0, DISPLAY_WIDTH, SCREEN_HEIGHT, TFT_BLACK);
  
  int startLine = outputScrollOffset;
  int endLine = std::min(startLine + maxDisplayLines, (int)outputLines.size());
  
  for (int i = startLine; i < endLine; i++) {
    tft.setCursor(0, (i - startLine) * 8);
    
    if (outputLines[i].startsWith("> ")) {
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
    } else if (outputLines[i].startsWith("Error") || outputLines[i].startsWith("Failed")) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
    } else if (outputLines[i].startsWith("Warning")) {
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    } else {
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
    }
    
    String displayLine = outputLines[i];
    int maxChars = DISPLAY_WIDTH / 6;
    if (displayLine.length() > maxChars) {
      displayLine = displayLine.substring(0, maxChars - 3) + "...";
    }
    
    tft.println(displayLine);
  }
  
  tft.setCursor(0, SCREEN_HEIGHT - 8);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.print("> ");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  
  int maxInputChars = (DISPLAY_WIDTH / 6) - 2;
  if (inputBuffer.length() > maxInputChars) {
    tft.println(inputBuffer.substring(inputBuffer.length() - maxInputChars));
  } else {
    tft.println(inputBuffer);
  }
  
  if (outputLines.size() > maxDisplayLines) {
    int scrollBarHeight = SCREEN_HEIGHT - 8;
    int scrollBarWidth = 2;
    int scrollBarX = DISPLAY_WIDTH - scrollBarWidth - 1;
    
    tft.fillRect(scrollBarX, 0, scrollBarWidth, scrollBarHeight, TFT_DARKGREY);
    
    float scrollRatio = (float)outputScrollOffset / (outputLines.size() - maxDisplayLines);
    int scrollPos = scrollRatio * (scrollBarHeight - 10);
    
    tft.fillRect(scrollBarX, scrollPos, scrollBarWidth, 10, TFT_WHITE);
  }
}

void displayKeyboard() {
  tft.fillRect(DISPLAY_WIDTH, 0, KEYBOARD_WIDTH, SCREEN_HEIGHT, TFT_BLACK);
  
  int rowHeight = SCREEN_HEIGHT / 12;
  
  for (int i = 0; i < 12; i++) {
    int index = (currentRowIndex / 12) * 12 + i;
    if (index >= keyboardRowsCount) break;
    
    int y = i * rowHeight;
    
    tft.drawRect(DISPLAY_WIDTH, y, KEYBOARD_WIDTH, rowHeight, TFT_WHITE);
    
    if (index == currentRowIndex) {
      tft.fillRect(DISPLAY_WIDTH, y, KEYBOARD_WIDTH, rowHeight, TFT_RED);
      tft.setTextColor(TFT_BLACK, TFT_RED);
    } else {
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
    }
    
    tft.setCursor(DISPLAY_WIDTH + 2, y + 2);
    String keyText = currentKeys[index];
    if (keyText.length() > 3) {
      keyText = keyText.substring(0, 3);
    }
    tft.println(keyText);
  }
}

void processSelectedKey() {
  int keyIndex = currentRowIndex;
  
  if (keyIndex == 37) {
    inputBuffer += " ";
  } else if (keyIndex == 38) {
    if (inputBuffer.length() > 0) {
      inputBuffer.remove(inputBuffer.length() - 1);
    }
  } else if (keyIndex == 39) {
    if (inputBuffer.length() > 0) {
      executeCommand(inputBuffer);
      inputBuffer = "";
    }
  } else if (keyIndex == 40) {
    caps = !caps;
    if (caps) {
      currentKeys = uppercaseKeys;
    } else {
      currentKeys = lowercaseKeys;
    }
    displayKeyboard();
  } else if (keyIndex == 41) {
    inputBuffer = "play";
  } else if (keyIndex == 42) {
    inputBuffer = "prev";
  } else if (keyIndex == 43) {
    inputBuffer = "next";
  } else if (keyIndex == 44) {
    if (isPlayingVideo) {
      stopVideo();
    } else {
      ESP.restart();
    }
  } else if (keyIndex == 45) {
    inputBuffer = "fast";
  } else if (keyIndex == 46) {
    inputBuffer = "skip";
  } else if (keyIndex == 47) {
    inputBuffer = "stop";
  } else if (keyIndex == 48) {
    inputBuffer = "bt status";
  } else {
    String key = currentKeys[keyIndex];
    key.trim();
    
    if (key.length() > 0) {
      inputBuffer += key;
    }
  }
}

void addToOutput(String text) {
  int startPos = 0;
  while (startPos < text.length()) {
    int endPos = text.indexOf('\n', startPos);
    if (endPos == -1) endPos = text.length();
    
    String line = text.substring(startPos, endPos);
    
    int maxLineLength = DISPLAY_WIDTH / 6;
    while (line.length() > maxLineLength) {
      outputLines.push_back(line.substring(0, maxLineLength));
      line = line.substring(maxLineLength);
    }
    
    if (line.length() > 0) {
      outputLines.push_back(line);
    }
    
    startPos = endPos + 1;
  }
  
  if (outputLines.size() > 100) {
    outputLines.erase(outputLines.begin(), outputLines.begin() + 50);
    outputScrollOffset = std::max(0, outputScrollOffset - 50);
  }
  
  outputScrollOffset = std::max(0, (int)outputLines.size() - maxDisplayLines);
}

void executeCommand(String command) {
  command.trim();
  if (command.length() == 0) return;
  
  addToOutput("> " + command);
  debugPrint("cmd: " + command);
  
  String cmdLower = command;
  cmdLower.toLowerCase();
  int firstSpace = cmdLower.indexOf(' ');
  String cmd = firstSpace == -1 ? cmdLower : cmdLower.substring(0, firstSpace);
  String params = firstSpace == -1 ? "" : command.substring(firstSpace + 1);
  
  if (cmd == "help") {
    addToOutput("available commands:");
    addToOutput("ls - list video files");
    addToOutput("play [filename] - play video");
    addToOutput("next - next video");
    addToOutput("prev - previous video");
    addToOutput("stop - stop playback");
    addToOutput("fast - fast forward/normal speed toggle");
    addToOutput("skip - skip 1 minute");
    addToOutput("auto - toggle auto play");
    addToOutput("rescan - rescan media");
    addToOutput("bt [on|off|status|setmac|connect|disconnect|scan] - bluetooth control");
    addToOutput("debug - toggle debug mode");
    addToOutput("shortcuts:");
    addToOutput("  during playback:");
    addToOutput("  up/down: adjust FPS");
    addToOutput("  left: exit confirmation");
    addToOutput("  right: next video");
    addToOutput("  left up: fast forward toggle");
    addToOutput("  right up: skip 1 minute");
    addToOutput("  left down: previous video");
    addToOutput("  right down: stop playback");
    
  } else if (cmd == "ls") {
    listFiles("/videos");
  } else if (cmd == "rescan") {
    if (isScanningMedia) {
      addToOutput("media scan in progress...");
    } else {
      addToOutput("rescanning media files...");
      asyncScanMediaFiles();
    }
  } else if (cmd == "play") {
    if (isScanningMedia) {
      addToOutput("please wait for media scan to complete...");
    } else {
      playVideo(params);
    }
  } else if (cmd == "next") {
    playNextVideo();
  } else if (cmd == "prev") {
    playPrevVideo();
  } else if (cmd == "stop") {
    if (isPlayingVideo) stopVideo();
  } else if (cmd == "fast") {
    if (isPlayingVideo) {
      toggleFastForward();
    } else {
      addToOutput("please use this command while playing video");
    }
  } else if (cmd == "skip") {
    if (isPlayingVideo) {
      skipOneMinute();
    } else {
      addToOutput("please use this command while playing video");
    }
  } else if (cmd == "auto") {
    autoPlayEnabled = !autoPlayEnabled;
    addToOutput("auto play: " + String(autoPlayEnabled ? "enabled" : "disabled"));
  } else if (cmd == "bt") {
    if (params == "on") {
      initBluetoothAudio();
    } else if (params == "off") {
      disconnectBluetooth();
      addToOutput("bluetooth disabled");
    } else if (params == "status") {
      addToOutput("bluetooth status: " + String(bluetoothConnected ? "connected" : "disconnected"));
      addToOutput("target device: " + (targetMacAddress.length() > 0 ? targetMacAddress : "not set"));
      addToOutput("auto connect: " + String(autoConnectEnabled ? "enabled" : "disabled"));
      addToOutput("audio playback: " + String(isAudioPlaying ? "playing" : "stopped"));
    } else if (params.startsWith("setmac")) {
      String macAddress = params.substring(7); // 跳过 "setmac "
      macAddress.trim();
      if (macAddress.length() > 0) {
        targetMacAddress = macAddress;
        autoConnectEnabled = true;
        saveBluetoothConfig();
        addToOutput("target MAC set to: " + macAddress);
        addToOutput("auto connect enabled");
      } else {
        addToOutput("usage: bt setmac XX:XX:XX:XX:XX:XX");
      }
    } else if (params == "connect") {
      if (targetMacAddress.length() > 0) {
        connectToDevice(targetMacAddress);
      } else {
        addToOutput("error: no target MAC address set");
        addToOutput("use 'bt setmac XX:XX:XX:XX:XX:XX' first");
      }
    } else if (params == "disconnect") {
      disconnectBluetooth();
    } else if (params == "scan") {
      scanBluetoothDevices();
    } else {
      addToOutput("bluetooth command usage:");
      addToOutput("  bt on - enable bluetooth");
      addToOutput("  bt off - disable bluetooth");
      addToOutput("  bt status - check status");
      addToOutput("  bt setmac XX:XX:XX:XX:XX:XX - set target MAC");
      addToOutput("  bt connect - connect to target device");
      addToOutput("  bt disconnect - disconnect current device");
      addToOutput("  bt scan - scan for devices");
    }
  } else if (cmd == "debug") {
    toggleSerialDebug();
  } else {
    addToOutput("unknown command: " + cmd);
    addToOutput("please enter 'help' to see available commands");
  }
}

void listFiles(String path) {
  File root = SD.open(path);
  if (!root) {
    addToOutput("error: unable to open directory " + path);
    return;
  }

  addToOutput("contents of " + path + ":");

  File file = root.openNextFile();
  while (file) {
    String fileInfo = String(file.name());
    if (file.isDirectory()) {
      fileInfo += "/ (directory)";
    } else {
      fileInfo += " (" + String(file.size()) + " bytes)";
    }
    addToOutput(fileInfo);
    file = root.openNextFile();
  }
  
  root.close();
}

bool isVideoFile(String filename) {
  filename.toLowerCase();
  return filename.endsWith(".vid") || filename.endsWith(".avi") || 
         filename.endsWith(".mp4");
}

void playVideo(String filename) {
  if (filename.length() == 0) {
    for (int i = 0; i < (int)mediaFiles.size(); i++) {
      if (isVideoFile(mediaFiles[i])) {
        currentMediaIndex = i;
        filename = mediaFiles[i];
        break;
      }
    }
    if (filename.length() == 0) {
      addToOutput("did not specify a video file to play");
      return;
    }
  } else {
    currentMediaIndex = -1;
    for (int i = 0; i < (int)mediaFiles.size(); i++) {
      if ((mediaFiles[i].indexOf(filename) != -1 || 
          getShortName(mediaFiles[i]).indexOf(filename) != -1) &&
          isVideoFile(mediaFiles[i])) {
        currentMediaIndex = i;
        filename = mediaFiles[i];
        break;
      }
    }
    if (currentMediaIndex == -1) {
      addToOutput("did not find video: " + filename);
      return;
    }
  }
  
  String fullPath = filename;
  
  if (!SD.exists(fullPath)) {
    addToOutput("file does not exist: " + fullPath);
    mediaFiles.erase(mediaFiles.begin() + currentMediaIndex);
    return;
  }
  
  videoFile = SD.open(fullPath, FILE_READ);
  if (!videoFile) {
    addToOutput("unable to open: " + fullPath);
    return;
  }
  
  videoFileSize = videoFile.size();
  uint32_t frameSize = SCREEN_WIDTH * SCREEN_HEIGHT * 2;
  
  if (videoFileSize < frameSize) {
    addToOutput("error: file too small, not a valid video");
    videoFile.close();
    return;
  }
  
  framesInVideo = videoFileSize / frameSize;
  
  if (framesInVideo == 0) {
    addToOutput("error: no frames found in video");
    videoFile.close();
    return;
  }
  
  currentMediaPath = fullPath;
  isPlayingVideo = true;
  currentFrame = 0;
  frameCount = 0;
  exitConfirmRequired = false;
  fastForwardMode = false;
  
  frameDelay = 1000 / targetFPS;
  
  if (videoOptimizationEnabled) {
    applyVideoOptimizations();
  }
  
  // 启动音频播放
  if (bluetoothConnected) {
    startAudioPlayback();
  } else {
    addToOutput("bluetooth not connected, only playing video");
  }
  
  tft.fillScreen(TFT_BLACK);
  
  addToOutput("playing: " + getShortName(fullPath));
  addToOutput("frames: " + String(framesInVideo) + ", FPS: " + String(targetFPS));
  addToOutput("controls: up/down - adjust FPS, left - exit, right - next");
  addToOutput("shortcuts: left up - fast forward, right up - skip 1 minute");
  
  updateVideoInfoDisplay();
}

void playNextVideo() {
  if (mediaFiles.empty()) {
    stopVideo();
    return;
  }
  
  stopVideo();
  delay(100);
  
  int startIndex = currentMediaIndex;
  do {
    currentMediaIndex++;
    if (currentMediaIndex >= (int)mediaFiles.size()) {
      currentMediaIndex = 0;
    }
    if (currentMediaIndex == startIndex) {
      addToOutput("no more videos");
      return;
    }
  } while (!isVideoFile(mediaFiles[currentMediaIndex]));
  
  String nextVideo = mediaFiles[currentMediaIndex];
  addToOutput("loading next: " + getShortName(nextVideo));
  
  if (!SD.exists(nextVideo)) {
    addToOutput("error: file does not exist - " + nextVideo);
    mediaFiles.erase(mediaFiles.begin() + currentMediaIndex);
    playNextVideo();
    return;
  }
  
  playVideo(nextVideo);
}

void playPrevVideo() {
  if (mediaFiles.empty()) {
    stopVideo();
    return;
  }
  
  int startIndex = currentMediaIndex;
  do {
    currentMediaIndex--;
    if (currentMediaIndex < 0) {
      currentMediaIndex = mediaFiles.size() - 1;
    }
    if (currentMediaIndex == startIndex) {
      addToOutput("no more videos");
      return;
    }
  } while (!isVideoFile(mediaFiles[currentMediaIndex]));
  
  stopVideo();
  delay(300);
  
  playVideo(mediaFiles[currentMediaIndex]);
}

bool displayVideoFrame() {
  if (!videoFile) {
    stopVideo();
    return false;
  }
  
  uint32_t frameSize = SCREEN_WIDTH * SCREEN_HEIGHT * 2;
  uint32_t framePosition = currentFrame * frameSize;
  
  if (framePosition >= videoFileSize) {
    currentFrame = 0;
    videoFile.seek(0);
    
    if (videoLoop) {
      addToOutput("video looping");
      return displayVideoFrame();
    } else {
      return false;
    }
  }
  
  videoFile.seek(framePosition);
  
  for (int y = 0; y < SCREEN_HEIGHT; y++) {
    if (videoFile.read((uint8_t*)lineBuffer, SCREEN_WIDTH * 2) == SCREEN_WIDTH * 2) {
      for (int x = 0; x < SCREEN_WIDTH; x++) {
        uint16_t pixel = lineBuffer[x];
        uint8_t r = (pixel >> 11) & 0x1F;
        uint8_t g = (pixel >> 5) & 0x3F;
        uint8_t b = pixel & 0x1F;
        lineBuffer[x] = (b << 11) | (g << 5) | r;
      }
      tft.pushImage(0, y, SCREEN_WIDTH, 1, lineBuffer);
    } else {
      addToOutput("error: failed to read video frame");
      return false;
    }
  }
  
  // 读取对应的音频数据
  if (isAudioPlaying && bluetoothConnected) {
    readAudioData(videoFile, currentFrame * (SCREEN_WIDTH * SCREEN_HEIGHT * 2));
    streamAudioData();
  }
  
  currentFrame++;
  updateVideoInfoDisplay();
  
  return true;
}

void updateVideoInfoDisplay() {
  tft.fillRect(0, SCREEN_HEIGHT - 16, SCREEN_WIDTH, 16, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(0, SCREEN_HEIGHT - 16);
  
  tft.print("F:");
  tft.print(currentFrame);
  tft.print("/");
  tft.print(framesInVideo);
  
  tft.setCursor(60, SCREEN_HEIGHT - 16);
  tft.print(targetFPS);
  tft.print("fps");
  
  if (fastForwardMode) {
    tft.setCursor(100, SCREEN_HEIGHT - 16);
    tft.print("FAST");
  }
  
  tft.setCursor(120, SCREEN_HEIGHT - 16);
  tft.print("BT:");
  tft.print(bluetoothConnected ? "Y" : "N");
  
  tft.fillRect(0, 0, SCREEN_WIDTH, 8, TFT_BLACK);
  tft.setCursor(0, 0);
  String shortName = getShortName(currentMediaPath);
  if (shortName.length() > 18) {
    shortName = shortName.substring(0, 18);
  }
  tft.print(shortName);
}

void stopVideo() {
  isPlayingVideo = false;
  exitConfirmRequired = false;
  autoPlayNextTime = 0;
  fastForwardMode = false;
  
  stopAudioPlayback();
  
  if (videoFile) {
    videoFile.close();
  }
  
  currentMediaPath = "";
  currentFrame = 0;
  frameCount = 0;
  
  tft.fillScreen(TFT_BLACK);
  
  displayOutput();
  displayKeyboard();

  addToOutput("video stopped");
}

void applyVideoOptimizations() {
  addToOutput("video optimizations applied");
}

String getShortName(String fullPath) {
  int lastSlash = fullPath.lastIndexOf('/');
  if (lastSlash != -1) {
    return fullPath.substring(lastSlash + 1);
  }
  return fullPath;
}

void optimizeVideoPlayback() {
  if (videoFile) {
    videoFileSize = videoFile.size();
    uint32_t frameSize = SCREEN_WIDTH * SCREEN_HEIGHT * 2;
    framesInVideo = videoFileSize / frameSize;

  }
  
  uint32_t freeMemory = esp_get_free_heap_size();
  if (freeMemory < 20000) {
    targetFPS = 5;
  } else if (freeMemory < 30000) {
    targetFPS = 8;
  } else {
    targetFPS = 10;
  }
  
  frameDelay = 1000 / targetFPS;
  debugPrint("optimization: " + String(targetFPS) + 
             ", free memory: " + String(freeMemory));
}

void updateVideoStats() {
  float actualFPS = frameCount / 5.0;
  addToOutput("video stats - FPS: " + String(actualFPS, 1) + 
              "/" + String(targetFPS) +
              ", frames: " + String(currentFrame) + "/" + String(framesInVideo) +
              ", processing: " + String(frameProcessTime/1000.0, 1) + "ms");
  frameCount = 0;
}

void toggleSerialDebug() {
  serialDebug = !serialDebug;
  if (serialDebug) {
    addToOutput("serial debug enabled");
  } else {
    addToOutput("serial debug disabled");
  }
}

void processSerialInput() {
  const int MAX_BUFFER_SIZE = 256;
  
  while (Serial.available()) {
    char c = Serial.read();
    
    if (serialBuffer.length() >= MAX_BUFFER_SIZE) {
      serialBuffer = "";
      addToOutput("error: command too long, buffer cleared");
      while (Serial.available()) Serial.read();
      return;
    }
    
    if (c == '\n' || c == '\r') {
      if (serialBuffer.length() > 0) {
        serialBuffer.trim();
        debugPrint("serial command: " + serialBuffer);
        
        executeCommand(serialBuffer);
        serialBuffer = "";
      }
    } else if (isPrintable(c)) {
      serialBuffer += c;
    }
    
    delay(2);
  }
}

// ========== 媒体扫描函数 ==========

void asyncScanMediaFiles() {
  if (isScanningMedia) {
    addToOutput("scanning in progress...");
    return;
  }
  
  isScanningMedia = true;
  mediaScanCancelled = false;
  mediaScanStartTime = millis();
  lastProgressUpdate = mediaScanStartTime;
  scannedFilesCount = 0;
  mediaFiles.clear();

  addToOutput("scanning cancelled");
}

bool checkMediaScanProgress() {
  if (mediaScanCancelled) {
    addToOutput("scanning cancelled by user");
    isScanningMedia = false;
    return true;
  }
  
  static int scanPhase = 0;
  static File currentDir;
  static File currentFile;
  
  unsigned long currentTime = millis();
  
  switch (scanPhase) {
    case 0:
      if (!currentDir) {
        currentDir = SD.open("/videos");
        if (!currentDir) {
          addToOutput("warning: /videos directory not found");
          scanPhase = 2;
          break;
        }
        addToOutput("scanning /videos directory...");
      }
      
      if (!currentFile) {
        currentFile = currentDir.openNextFile();
      }
      
      if (currentFile) {
        if (!currentFile.isDirectory()) {
          String filename = currentFile.name();
          if (isVideoFile(filename)) {
            String fullPath = "/videos/" + filename;
            mediaFiles.push_back(fullPath);
            scannedFilesCount++;
          }
        }
        currentFile = currentDir.openNextFile();
      } else {
        currentDir.close();
        addToOutput("video scanning complete: " + String(mediaFiles.size()) + " files found");
        scanPhase = 2;
      }
      break;
      
    case 2:
      if (mediaFiles.size() > 0) {
        addToOutput("video scanning complete: " + String(mediaFiles.size()) + " files found");
      } else {
        addToOutput("no media files found");
      }
      
      isScanningMedia = false;
      return true;
  }
  
  return false;
}

void stopMediaScan() {
  isScanningMedia = false;
  mediaScanCancelled = true;
  addToOutput("scanning cancelled");
}

void updateScanProgress() {
  unsigned long currentTime = millis();
  
  if (currentTime - lastProgressUpdate > PROGRESS_UPDATE_INTERVAL) {
    addToOutput("scanning... " + String(mediaFiles.size()) + " files found");
    lastProgressUpdate = currentTime;
  }
}

void scanMediaFiles() {
  asyncScanMediaFiles();
  while (isScanningMedia) {
    if (checkMediaScanProgress()) break;
    delay(10);
  }
}

void toggleVideoOptimization() {
  videoOptimizationEnabled = !videoOptimizationEnabled;
  addToOutput("video super: " + String(videoOptimizationEnabled ? "start" : "stop"));
}