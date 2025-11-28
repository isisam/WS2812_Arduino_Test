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

// (已移除舊的單一 loop，使用檔案底部的 mode-based loop 以避免重複定義)

// （已移除 5x5 跑馬文字與彩燈相關函式，僅保留三種狀態顯示）

// 三種模式：0=閃爍加工中(顯示 BUSY 英文)、1=波浪舞、2=欄位彩虹滑動
void mode_blink_busy() {
  // 顯示一個佔滿面板的「近似等邊三角形」並以橘色呼吸燈表示加工中。
  // 說明：5x5 點陣無法精確表現數學上的等邊三角形，下面使用一個線性內插
  // 方法將三角形自上向下展開，底邊會觸及畫面下緣以達到「全版面」效果。
  static uint8_t phase = 0;
  const uint32_t orange = strip.Color(255, 128, 0);

  // 呼吸強度
  uint8_t bright = SINE32[phase & 31]; // 0..255
  uint32_t scaled = scaleColor(orange, bright);

  // 清除整面板
  for (uint16_t i = 0; i < NUMPIXELS; i++) strip.setPixelColor(i, 0);

  // 計算三角形：以畫面中間為中心，從 y=0 (頂點) 到 y=HEIGHT-2 (底邊)，最底下一列保持關閉
  int center = (WIDTH - 1) / 2; // 中心 x
  int maxY = HEIGHT - 2; // 底邊放在倒數第二列
  if (maxY < 1) maxY = 1;
  for (int y = 0; y <= maxY; y++) {
    // halfWidth 以線性插值展開至最大 (WIDTH-1)/2
    int halfMax = (WIDTH - 1) / 2; // e.g., 2 for WIDTH=5
    int half = (y * halfMax + maxY / 2) / maxY; // integer interp
    int x0 = center - half;
    int x1 = center + half;
    if (x0 < 0) x0 = 0;
    if (x1 >= WIDTH) x1 = WIDTH - 1;
    for (int x = x0; x <= x1; x++) {
      strip.setPixelColor(XY(x, y), scaled);
    }
  }

  strip.show();
  phase++;
  delay(85);
}
// 新增兩個模式：待機（綠色圓形緩和呼吸）與緊急（高亮紅色全板閃爍）

void mode_idle() {
  static uint8_t phase = 0;
  // gentle breathing: map SINE32 (0..255) -> scale 80..200
  uint8_t s = SINE32[phase & 31];
  uint8_t scale = 80 + ((uint16_t)s * 120) / 255; // 80..200
  uint32_t green = strip.Color(0, 255, 0);
  uint32_t scaled = scaleColor(green, scale);
  // 全版 5x5 綠色呼吸（整個面板皆呼吸）
  for (uint16_t i = 0; i < NUMPIXELS; i++) {
    strip.setPixelColor(i, scaled);
  }
  strip.show();
  phase++;
  delay(140);
}

void mode_emergency() {
  static bool on = false;
  // set high brightness for emergency
  strip.setBrightness(255);
  if (on) {
    uint32_t red = strip.Color(255, 0, 0);
    for (uint16_t i = 0; i < NUMPIXELS; i++) strip.setPixelColor(i, red);
  } else {
    for (uint16_t i = 0; i < NUMPIXELS; i++) strip.setPixelColor(i, 0);
  }
  strip.show();
  on = !on;
  delay(120);
  // restore default brightness for other modes (will be set at loop entry)
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

