#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#define PIN        52       // 資料腳位，視硬體接線調整
#define WIDTH      5
#define HEIGHT     5
#define NUMPIXELS  (WIDTH * HEIGHT)

Adafruit_NeoPixel strip(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);
uint8_t defaultBrightness = 50;

void setup() {
  Serial.begin(115200);
  Serial.println(F("WS2812 5x5 test"));
  strip.begin();
  strip.show(); // 初始化全部關閉
  strip.setBrightness(defaultBrightness); // 可依需求調整
}
// 2D(x,y) -> 1D index 映射（簡單 row-major，y*WIDTH + x）
uint16_t XY(uint8_t x, uint8_t y) {
  if (x >= WIDTH || y >= HEIGHT) return 0;
  return y * WIDTH + x;
}

// 產生彩虹色（0-255）
uint32_t Wheel(uint8_t pos) {
  pos = 255 - pos;
  if (pos < 85) {
    return strip.Color(255 - pos * 3, 0, pos * 3);
  }
  if (pos < 170) {
    pos -= 85;
    return strip.Color(0, pos * 3, 255 - pos * 3);
  }
  pos -= 170;
  return strip.Color(pos * 3, 255 - pos * 3, 0);
}

// scale a 24-bit color by 0-255 brightness
uint32_t scaleColor(uint32_t color, uint8_t scale) {
  uint8_t r = (color >> 16) & 0xFF;
  uint8_t g = (color >> 8) & 0xFF;
  uint8_t b = color & 0xFF;
  r = (uint8_t)((r * scale) >> 8);
  g = (uint8_t)((g * scale) >> 8);
  b = (uint8_t)((b * scale) >> 8);
  return strip.Color(r, g, b);
}

// 32-step sine table (0..255)
const uint8_t SINE32[32] = {
  128,152,176,198,217,233,244,251,254,251,244,233,217,198,176,152,
  128,104,80,58,39,23,12,5,2,5,12,23,39,58,80,104
};

// 非對稱呼吸查表：漸暗較慢，變亮較快（slow fall, quick rise）
// 用於待機模式，產生 "斜直角三角鋸齒波" 感覺
const uint8_t ASYM_BREATH[32] = {
  255,245,235,225,215,205,195,185,175,165,155,145,135,125,115,105,
   95, 85, 75, 65, 55, 45, 35, 28,  28, 68,108,148,188,208,232,255
};

// ---------- 說明與重要注意事項 ----------
// - `XY(x,y)`：使用 row-major 映射 (index = y * WIDTH + x)。
//   例如 (0,0) 為第一顆，(1,0) 為第二顆，(0,1) 為第六顆 (對於 5x5)。
// - 亮度有兩層控制：
//   1) 全域亮度 `strip.setBrightness(value)`：在呼叫 `strip.show()` 前設定，
//      會影響送出的實際 PWM 等級（節省 flash 設定、硬體上限）。
//   2) 顏色縮放 `scaleColor(color, scale)`：在軟體上縮放 RGB 值，然後再
//      經由 `setBrightness` 做最終縮放。兩者疊加會影響看起來的明亮範圍。
// - 若你在硬體上只看到中央 3x3 發亮，通常原因為：
//   * 程式還沒上傳最新版本（請確認已上傳），或
//   * 全域亮度太低使得邊緣像素看起來關閉（建議先把 `defaultBrightness` 調高測試），或
//   * LED 物理接線 / 棧板走線導致某些像素無電或資料線中斷。
// 本檔已將待機模式設為「全版 5x5 呼吸」，下方的 `mode_idle()` 會把
// 所有像素設成同一色並顯示；若你仍看到 3x3，請先上傳本程式再回報。


// (已移除舊的單一 loop，使用檔案底部的 mode-based loop 以避免重複定義)

// （已移除 5x5 跑馬文字與彩燈相關函式，僅保留三種狀態顯示）

// 三種模式：0=閃爍加工中(顯示 BUSY 英文)、1=波浪舞、2=欄位彩虹滑動
void mode_blink_busy() {
  // 非阻塞的黃燈呼吸三角形，使用 SINE32 查表但以線性插值提升時間分辨率
  // 透過 millis() 計算相位，使得函式呼叫快速返回，不會阻塞其他工作
  const uint32_t orange = strip.Color(255, 128, 0);
  const uint32_t period_ms = 2400; // 呼吸週期 (ms)，可調整
  const uint8_t SINE_SIZE = 32;

  static unsigned long start = 0;
  unsigned long now = millis();
  if (start == 0) start = now;
  uint32_t elapsed = (now - start) % period_ms; // 0..period_ms-1

  // 計算在 0..SINE_SIZE 範圍內的分數位置 (包含小數)，用於插值
  uint32_t scaled = (uint32_t)elapsed * (uint32_t)SINE_SIZE;
  uint32_t idx = scaled / period_ms; // 0..SINE_SIZE-1
  uint32_t rem = scaled % period_ms; // 用來做線性插值的權重 (0..period_ms-1)
  uint8_t a = SINE32[idx];
  uint8_t b = SINE32[(idx + 1) % SINE_SIZE];
  uint32_t interp = ((uint32_t)a * (period_ms - rem) + (uint32_t)b * rem) / period_ms;
  uint8_t bright = (uint8_t)interp; // 0..255
  uint32_t color = scaleColor(orange, bright);

  // 清除整面板
  for (uint16_t i = 0; i < NUMPIXELS; i++) strip.setPixelColor(i, 0);

  // 計算三角形（頂點在 y=0，底邊放在倒數第二列）
  int center = (WIDTH - 1) / 2;
  int maxY = HEIGHT - 2;
  if (maxY < 1) maxY = 1;
  for (int y = 0; y <= maxY; y++) {
    int halfMax = (WIDTH - 1) / 2;
    int half = (y * halfMax + maxY / 2) / maxY;
    int x0 = center - half;
    int x1 = center + half;
    if (x0 < 0) x0 = 0;
    if (x1 >= WIDTH) x1 = WIDTH - 1;
    for (int x = x0; x <= x1; x++) {
      strip.setPixelColor(XY(x, y), color);
    }
  }

  strip.show();
}
// 新增兩個模式：待機（綠色圓形緩和呼吸）與緊急（高亮紅色全板閃爍）

void mode_idle() {
  // 非阻塞的待機呼吸（使用 ASYM_BREATH），將整個面板以綠色做 1 秒週期呼吸
  const uint32_t period_ms = 1000; // 1 秒完整週期
  const uint8_t ASYM_SIZE = 32;
  static unsigned long start = 0;
  unsigned long now = millis();
  if (start == 0) start = now;
  uint32_t elapsed = (now - start) % period_ms; // 0..period_ms-1

  // 計算索引與插值權重以取得更平滑的過度
  uint32_t scaled = (uint32_t)elapsed * (uint32_t)ASYM_SIZE;
  uint32_t idx = scaled / period_ms; // 0..ASYM_SIZE-1
  uint32_t rem = scaled % period_ms;
  uint8_t a = ASYM_BREATH[idx];
  uint8_t b = ASYM_BREATH[(idx + 1) % ASYM_SIZE];
  uint32_t interp = ((uint32_t)a * (period_ms - rem) + (uint32_t)b * rem) / period_ms;
  uint8_t s = (uint8_t)interp;

  // map s (0..255) to desired scale range (80..200)
  uint8_t scale = 80 + ((uint16_t)s * 120) / 255; // 80..200
  uint32_t green = strip.Color(0, 255, 0);
  uint32_t scaledColor = scaleColor(green, scale);

  strip.setBrightness(defaultBrightness);
  for (uint16_t i = 0; i < NUMPIXELS; i++) {
    strip.setPixelColor(i, scaledColor);
  }
  strip.show();
}

void mode_emergency() {
  // 非阻塞的緊急閃爍：每 120ms 反轉一次全紅 / 全關狀態
  static bool on = false;
  const uint32_t interval_ms = 120;
  static unsigned long last = 0;
  unsigned long now = millis();
  if (now - last >= interval_ms) {
    last = now;
    on = !on;
  }
  strip.setBrightness(255);
  if (on) {
    uint32_t red = strip.Color(255, 0, 0);
    for (uint16_t i = 0; i < NUMPIXELS; i++) strip.setPixelColor(i, red);
  } else {
    for (uint16_t i = 0; i < NUMPIXELS; i++) strip.setPixelColor(i, 0);
  }
  strip.show();
}

void loop_modes() {
  static uint8_t mode = 0;
  static unsigned long lastSwitch = 0;
  unsigned long now = millis();
  // 每 12 秒自動切換模式
  if (now - lastSwitch > 12000) {
    mode = (mode + 1) % 3;
    lastSwitch = now;
  }

  // ensure normal brightness by default
  strip.setBrightness(defaultBrightness);

  if (mode == 0) mode_blink_busy();           // 處理中: 黃色三角形呼吸
  else if (mode == 1) mode_idle();            // 待機: 綠色圓形緩和呼吸
  else mode_emergency();                       // 緊急: 高亮紅色閃爍全板
}

// 取代原有 loop
void loop() {
  loop_modes();
}

