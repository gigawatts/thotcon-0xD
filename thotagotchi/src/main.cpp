/* Thotagotchi Game for 2025 ThotCon 0xD Badge
   By: Kujo
*/

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "pet_sprites.h"

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite petLayer = TFT_eSprite(&tft);

// Pin definitions
#define BUZZER_PIN 5
#define SELECT_TOUCH_PIN 27
#define Q1_TOUCH_PIN 13
#define Q2_TOUCH_PIN 12
#define Q3_TOUCH_PIN 14
const int ledPins[] = {21, 22, 19, 17, 16, 25};

// Constants
const int NUM_LEDS = 6;
const int screenW = 240;
const int screenH = 240;

const int spriteW = 240;
const int spriteH = 179;
const int spriteY = 20;  // Top of sprite rectangle
const int buttonY = spriteY + spriteH + 1;
const int centerThreshold = 40;
const int buttonAreaHeight = screenH - buttonY;

const int petWidth = petBitmapWidth * petScale;
const int petHeight = petBitmapHeight * petScale;

const int maxHunger = 100;
const int maxHappiness = 100;
const int hungerDecay = 5;
const int happinessDecay = 5;
const int deathThreshold = 5;

// Game State
int hunger = 20;
int happiness = 100;
bool dead = false;
int badTicks = 0;
bool menuChanged = true;

// Movement
enum MoveMode { WANDER, DVD_BOUNCE };
MoveMode moveMode = WANDER;
unsigned long lastUpdate = 0;
unsigned long gameTickInterval = 5000;
unsigned long lastFrameTime = 0;
const unsigned long frameInterval = 100;

int petX = 60;
int petY = 60;

// === speed settings ===
int petDX               = 5;   // DVD_BOUNCE movement speed
int petDY               = 5;
const int wanderStep    = 5;   // pixels per wander “step”
const int chaseStepFood = 8;   // pixels/frame while heading to food
const int chaseStepBall = 10;  // pixels/frame while chasing ball

int wanderDX = 2;
int wanderDY = 1;
unsigned long lastMoveStep = 0;
const unsigned long wanderInterval = 500; // ms per move

unsigned long lastIdleChirp = 0;
const unsigned long idleChirpCheckInterval = 3000; // Check every x seconds

// Poop handling
struct Poop {
  int x;
  int y;
  bool active;
};
const int MAX_POOPS = 25;
Poop poops[MAX_POOPS];
unsigned long lastPoopCheck = 0;
unsigned long poopCheckInterval = 5000;

// Eating mode
bool isEating = false;
unsigned long eatingStartTime = 0;
bool foodActive = false;
bool hasEatenCurrentFood = false;
int foodX, foodY = 0;
int bounceCount = 0;
unsigned long lastBounceTime = 0;
const int bounceInterval = 300;  // ms between up/down jumps after eating
bool bounceUp = true;

// Play mode
bool isPlaying = false;
unsigned long playingStartTime = 0;
const unsigned long playTimeout = 6000;   // seconds of play time
const int ballRadius = 12;                // Radius of the beach ball
const int ballDiameter = ballRadius * 2;  // Convenience
int ballX, ballY;                         // Ball position
float ballVX, ballVY = 0;
float ballColorOffset = 0;                // degrees, for beach ball spin
const float ballFriction = 0.98;          // Slow down gradually
const float ballSpeed = 3;                // How hard the pet hits the ball
unsigned long lastBallHit = 0;
const unsigned long ballHitCooldown = 300;  // ms cooldown between hits

// Touch
bool touchDetected = false;
int currentMenuIndex = 0;
int previousMenuIndex = -1;  // -1 so first draw always happens
bool wasCenterPressed = false;

const int TOUCH_ON_DELTA = 15;
const int TOUCH_OFF_DELTA = 5;
int baseline0, baseline1, baseline2, baselineSelect;

void calibrateTouch() {
  long sum0 = 0, sum1 = 0, sum2 = 0, sumSelect = 0;
  for (int i = 0; i < 100; i++) {
    sum0 += touchRead(Q2_TOUCH_PIN);
    sum1 += touchRead(Q1_TOUCH_PIN);
    sum2 += touchRead(Q3_TOUCH_PIN);
    sumSelect += touchRead(SELECT_TOUCH_PIN);
    delay(10);
  }
  baseline0 = sum0 / 100;
  baseline1 = sum1 / 100;
  baseline2 = sum2 / 100;
  baselineSelect = sumSelect / 100;
}


bool readTouchWheelAngle(float &angle_out) {
  int v0 = touchRead(Q2_TOUCH_PIN);
  int v1 = touchRead(Q1_TOUCH_PIN);
  int v2 = touchRead(Q3_TOUCH_PIN);

  bool touchNow = false;
  if (touchDetected) {
    if (v0 > (baseline0 - (TOUCH_ON_DELTA + TOUCH_OFF_DELTA)) && 
        v1 > (baseline1 - (TOUCH_ON_DELTA + TOUCH_OFF_DELTA)) && 
        v2 > (baseline2 - (TOUCH_ON_DELTA + TOUCH_OFF_DELTA))) {
      touchNow = false;
    } else {
      touchNow = true;
    }
  } else {
    if (v0 < (baseline0 - TOUCH_ON_DELTA) || 
        v1 < (baseline1 - TOUCH_ON_DELTA) || 
        v2 < (baseline2 - TOUCH_ON_DELTA)) {
      touchNow = true;
    }
  }

  if (!touchNow) {
    touchDetected = false;
    return false;
  }

  float t0 = 1000 - v0;
  float t1 = 1000 - v1;
  float t2 = 1000 - v2;
  float total = t0 + t1 + t2;
  if (total < 1) total = 1;
  t0 /= total;
  t1 /= total;
  t2 /= total;

  float x = t2 * cos(0) + t1 * cos(2 * PI / 3) + t0 * cos(4 * PI / 3);
  float y = t2 * sin(0) + t1 * sin(2 * PI / 3) + t0 * sin(4 * PI / 3);

  float angle_rad = atan2(-y, x);
  float angle_deg = angle_rad * 180.0 / PI;
  if (angle_deg < 0) angle_deg += 360.0;

  angle_out = angle_deg;
  touchDetected = true;
  return true;
}


bool isCenterPressed() {
  return (touchRead(SELECT_TOUCH_PIN) < (baselineSelect - centerThreshold));
}


void playTone(int freq, int duration) {
  tone(BUZZER_PIN, freq, duration);
  delay(duration); // blocking, but fine for short effects
  noTone(BUZZER_PIN);
}


// --------------------  non-blocking tone helper --------------------
unsigned long toneStopAt = 0;               // when to silence the buzzer
void playToneNB(uint16_t freq, uint16_t ms) // NB = non-blocking
{
  tone(BUZZER_PIN, freq);
  toneStopAt = millis() + ms;
}
void serviceTone()                           // call once each loop()
{
  if (toneStopAt && millis() >= toneStopAt) {
    noTone(BUZZER_PIN);
    toneStopAt = 0;
  }
}

// --- quick random chirp/grumble when the pet is ignored --------------
void petChirp() {
  // {frequency (Hz), duration (ms)}
  const uint16_t chirps[][2] = {
    {1500,  90},
    {1850,  80},
    {1200, 110},
    {2000,  70}
  };
  uint8_t idx = random(0, sizeof(chirps) / sizeof(chirps[0]));
  playToneNB(chirps[idx][0], chirps[idx][1]);   // non-blocking tone
}


void setLEDs(uint8_t count) {
  for (uint8_t i = 0; i < NUM_LEDS; i++)
    digitalWrite(ledPins[i], i < count ? HIGH : LOW);
}


void updateLEDs()
{
  if      (hunger < 16)  setLEDs(6);   // 0–15  → 6 LEDs
  else if (hunger < 32)  setLEDs(5);   // 16–31 → 5 LEDs
  else if (hunger < 48)  setLEDs(4);   // 32–47 → 4 LEDs
  else if (hunger < 64)  setLEDs(3);   // 48–63 → 3 LEDs
  else if (hunger < 80)  setLEDs(2);   // 64–79 → 2 LEDs
  else if (hunger < 100) setLEDs(1);   // 80–99 → 1 LED
  else                   setLEDs(0);   // 100   → all off
}


void spawnPoop() {
  for (int i = 0; i < MAX_POOPS; i++) {
    if (!poops[i].active) {
      poops[i].x = petX;
      poops[i].y = petY;
      poops[i].active = true;
      break;
    }
  }
}


void handleEatingBounce() {
  unsigned long now = millis();
  const unsigned long eatingDuration = 1500; // milliseconds
  const unsigned long bounceInterval = 200;  // Bounce up/down every X ms

  static bool bounceUp = true;
  static unsigned long lastBounceTime = 0;

  if (now - eatingStartTime >= eatingDuration) {
    isEating = false;  // Done eating
    foodActive = false;
    hasEatenCurrentFood = false;
    // Decrease hunger
    hunger = max(0, hunger - 16);
    return;
  }

  if (now - lastBounceTime >= bounceInterval) {
    lastBounceTime = now;
    bounceUp = !bounceUp;

    if (bounceUp) {
      petY -= 5;
      playToneNB(1200, 60);
    } else {
      petY += 5;
      playToneNB(700,  60);
    }

    // Make sure he doesn't wander off sprite field while bouncing
    petY = constrain(petY, 0, spriteH - petHeight);
  }
}


void movePet() {
  static unsigned long lastMoveTime = 0;
  const unsigned long moveInterval = 200;
  static unsigned long lastPetBallHitTime = 0;
  const unsigned long petChaseCooldown = 600; // ms cooldown after hit

  unsigned long now = millis();
  if (now - lastMoveTime < moveInterval) return;
  lastMoveTime = now;

  if (isEating) {
    handleEatingBounce();
    return;
  }

  if (isPlaying) {
    // Ball movement
    ballX += ballVX;
    ballY += ballVY;
    ballVX *= ballFriction;
    ballVY *= ballFriction;

    if (abs(ballVX) < 0.05) ballVX = 0;
    if (abs(ballVY) < 0.05) ballVY = 0;

    // Bounce ball off walls
    if (ballX <= 0 || ballX >= spriteW - ballDiameter) {
      ballVX = -ballVX;
      ballX = constrain(ballX, 0, spriteW - ballDiameter);
    }
    if (ballY <= 0 || ballY >= spriteH - ballDiameter) {
      ballVY = -ballVY;
      ballY = constrain(ballY, 0, spriteH - ballDiameter);
    }

    // Pet chases ball
    int petCenterX = petX + petWidth / 2;
    int petCenterY = petY + petHeight / 2;
    int ballCenterX = ballX + ballRadius;
    int ballCenterY = ballY + ballRadius;

    int distX = ballCenterX - petCenterX;
    int distY = ballCenterY - petCenterY;
    int distanceSquared = distX * distX + distY * distY;
    int collisionDistance = (petWidth / 2) + ballRadius;

    // Handle collision
    if (distanceSquared < collisionDistance * collisionDistance) {
      if (now - lastBallHit >= ballHitCooldown) {
        float angle = atan2(distY, distX);
        float hitStrength = 5.0 + random(-10, 10) * 0.1;
        ballVX = cos(angle) * hitStrength;
        ballVY = sin(angle) * hitStrength;
        lastBallHit = now;
      }
    }

    // Only chase ball if enough time passed after last hit
    if (now - lastBallHit > ballHitCooldown) {
      if (abs(distX) > 4) petX += (distX > 0) ? chaseStepBall : -chaseStepBall;
      if (abs(distY) > 4) petY += (distY > 0) ? chaseStepBall : -chaseStepBall;
      petX = constrain(petX, 0, spriteW - petWidth);
      petY = constrain(petY, 0, spriteH - petHeight);
    }

    // Spin ball ONLY if moving
    if (ballVX != 0 || ballVY != 0) {
      ballColorOffset += 30;
      if (ballColorOffset >= 360) ballColorOffset -= 360;
    }

    // Check if play time expired
    if (millis() - playingStartTime >= playTimeout) {
      isPlaying = false;    // End playing
      happiness = min(100, happiness + 20);  // Reward happiness
    }

    return; // exit early
  }

  if (foodActive && !hasEatenCurrentFood) {
    int dx = (foodX + foodW / 2) - (petX + petWidth / 2);
    int dy = (foodY + foodH / 2) - (petY + petHeight / 2);

    // if (abs(dx) > 2) petX += (dx > 0) ? petDX : -petDX;
    // if (abs(dy) > 2) petY += (dy > 0) ? petDY : -petDY;
    if (abs(dx) > 2) petX += (dx > 0) ? chaseStepFood : -chaseStepFood;
    if (abs(dy) > 2) petY += (dy > 0) ? chaseStepFood : -chaseStepFood;

    petX = constrain(petX, 0, spriteW - petWidth);
    petY = constrain(petY, 0, spriteH - petHeight);

    if (abs((foodX + foodW / 2) - (petX + petWidth / 2)) < 8 &&
        abs((foodY + foodH / 2) - (petY + petHeight / 2)) < 8) {
      isEating = true;
      eatingStartTime = millis();
      hasEatenCurrentFood = true;
    }
    return;
  }

  // Normal random wander
  if (moveMode == DVD_BOUNCE) {
    petX += petDX;
    petY += petDY;

    if (petX <= 0 || petX >= spriteW - petWidth) petDX = -petDX;
    if (petY <= 0 || petY >= spriteH - petHeight) petDY = -petDY;
  } else {
    int dx = random(-1, 2);
    int dy = random(-1, 2);

    if (petX <= 10) dx = 1;
    else if (petX >= spriteW - petWidth - 10) dx = -1;

    if (petY <= 10) dy = 1;
    else if (petY >= spriteH - petHeight - 10) dy = -1;

    petX += dx * wanderStep;
    petY += dy * wanderStep;

    petX = constrain(petX, 0, spriteW - petWidth);
    petY = constrain(petY, 0, spriteH - petHeight);
  }
}


void applyAction() {
  if (currentMenuIndex == 0 && !foodActive && !isEating && !isPlaying && !dead) {
    // Feed
    foodX = random(20, spriteW - foodW - 20);
    foodY = random(20, spriteH - foodH - 20);
    foodActive = true;
    playTone(2000, 100);

  } else if (currentMenuIndex == 1 && !foodActive && !isPlaying && !dead) {
    // Play
    isPlaying = true;
    playingStartTime = millis();
    ballX = random(20, spriteW - ballDiameter - 20);
    ballY = random(20, spriteH - ballDiameter - 20);
    ballVX = 0;  // Reset velocity
    ballVY = 0;
    playTone(2000, 100);

  } else if (currentMenuIndex == 2 && !dead) {
    // Clean
    for (int i = 0; i < MAX_POOPS; i++) {
      poops[i].active = false;
    }
    playTone(2000, 100);
  }
}


void drawHUD() {
  tft.setTextSize(2);
  tft.setCursor(10, 2);

  int fullHearts = (100 - (hunger / 2) - (50 - happiness / 2)) / 20;
  for (int i = 0; i < 5; i++) {
    tft.setTextColor((i < fullHearts) ? TFT_RED : TFT_DARKGREY, TFT_BLACK);
    tft.print("\x03 ");
  }

  // tft.setCursor(160, 3);
  // tft.setTextColor(TFT_WHITE, TFT_BLACK);
  // tft.print(hunger);

  tft.setCursor(200, 3);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  if (happiness > 66)
    tft.print(":)");
  else if (happiness > 33)
    tft.print(":|");
  else
    tft.print(":(");
}


void drawButtons() {
  static int previousMenuIndex = -1;
  
  if (currentMenuIndex == previousMenuIndex) {
    // No change, skip re-drawing buttons
    return;
  }
  
  const char* labels[] = {"Feed", "Play", "Clean"};
  int buttonWidth = 240 / 3;
  tft.setTextSize(2);

  for (int i = 0; i < 3; i++) {
    int x = i * buttonWidth;
    if (i == currentMenuIndex) {
      tft.fillRect(x, buttonY, buttonWidth, buttonAreaHeight, TFT_YELLOW);
      tft.setTextColor(TFT_BLACK, TFT_YELLOW);
    } else {
      tft.fillRect(x, buttonY, buttonWidth, buttonAreaHeight, TFT_WHITE);
      tft.setTextColor(TFT_BLACK, TFT_WHITE);
    }
    int textX = x + (buttonWidth / 2) - (strlen(labels[i]) * 6);
    tft.setCursor(textX, buttonY + 10);
    tft.print(labels[i]);
  }

  previousMenuIndex = currentMenuIndex;  // Update after draw
}


//------------------------------------------------------------------
// Universal 1-bpp scaler: works with TFT_eSPI *and* TFT_eSprite
//------------------------------------------------------------------
template <class GFX>     // GFX = TFT_eSPI or TFT_eSprite
void drawScaledBitmap1bpp(
        GFX           &dst,            // where to draw (screen or sprite)
        const uint8_t *bitmap,         // PROGMEM 1-bpp image
        int            X,  int Y,      // top-left unless centered*
        int            Width, 
        int            Height,
        int            Scale,          // integer ≥1
        uint16_t       fgColor,
        uint16_t       bgColor,
        bool           transparentBg = true,
        bool           centeredX      = false,
        bool           centeredY      = false
  ) {
    if (centeredX) X = (dst.width()  - Width  * Scale) >> 1;
    if (centeredY) Y = (dst.height() - Height * Scale) >> 1;

    const int rowBytes = (Width + 7) >> 3;

    for (int row = 0; row < Height; ++row) {
        for (int col = 0; col < Width; ++col) {
            int byteIdx = row * rowBytes + (col >> 3);
            uint8_t  mask = 0x80 >> (col & 7);
            bool bitOn = pgm_read_byte(&bitmap[byteIdx]) & mask;

            if (bitOn || !transparentBg) {
                uint16_t c = bitOn ? fgColor : bgColor;
                int x0 = X + col * Scale;
                int y0 = Y + row * Scale;
                if (Scale == 1) dst.drawPixel(x0, y0, c);
                else            dst.fillRect(x0, y0, Scale, Scale, c);
            }
        }
    }
}


void drawPoops() {
  for (int i = 0; i < 25; i++) {
    if (poops[i].active) {
      drawScaledBitmap1bpp(
        petLayer,             // draw into the sprite
        poop_bitmap,
        poops[i].x, poops[i].y,  
        poopW, poopH,         // original size
        poopScale,            // scale
        TFT_BROWN,            // fg color
        TFT_BLACK,            // bg color (ignored when transparent=true)
        true                  // transparent background
      );
    }
  }
}


void drawBeachBall(int x, int y) {
  int r = ballRadius;
  int centerX = x + r;
  int centerY = y + r;

  // Draw base white circle
  petLayer.fillCircle(centerX, centerY, r, TFT_WHITE);

  // Now draw rotated slices
  int numSlices = 4;  // red, yellow, blue, green
  int sliceAngle = 360 / numSlices;

  uint16_t sliceColors[] = {TFT_RED, TFT_YELLOW, TFT_BLUE, TFT_GREEN};

  for (int i = 0; i < numSlices; i++) {
    float angleDeg = ballColorOffset + i * sliceAngle;
    float angleRad = angleDeg * PI / 180.0;

    int sliceX = centerX + (r / 2) * cos(angleRad);
    int sliceY = centerY + (r / 2) * sin(angleRad);

    petLayer.fillCircle(sliceX, sliceY, r / 2, sliceColors[i]);
  }

  // Center dot
  petLayer.fillCircle(centerX, centerY, 3, TFT_WHITE);

  // outer border
  petLayer.drawCircle(centerX, centerY, r, TFT_BLACK);
}


void drawPetFace(const uint8_t *bmp, int x, int y) {
    drawScaledBitmap1bpp(
        petLayer,             // draw into the sprite
        bmp,
        x, y,
        // 16, 16,               
        petBitmapWidth, petBitmapHeight, // original size
        petScale,             // scale
        TFT_WHITE,            // fg color
        TFT_BLACK,            // bg color (ignored when transparent=true)
        true                  // transparent background
    );
}


// void drawDeadtext() {
//   int textSize = 6;
//   int charWidth = 6;
//   int charHeight = 8;
//   int textLength = 4; // "DEAD"

//   int textPixelWidth = charWidth * textLength * textSize;
//   int textPixelHeight = charHeight * textSize;

//   int x = (spriteW - textPixelWidth) / 2;
//   int y = (spriteH - textPixelHeight) / 2;

//   petLayer.setTextColor(TFT_RED, TFT_BLACK);
//   petLayer.setTextSize(textSize);
//   petLayer.setCursor(x, y);
//   petLayer.println("DEAD");
// }


void handleDeath() {
  // petLayer.fillSprite(TFT_BLACK); // Clear background

  drawPetFace(pet_dead, petX, petY);

  // Draw the grave scaled
  drawScaledBitmap1bpp(petLayer,
                     grave_back_bitmap,
                     0, 0, graveW, graveH, graveScale,
                     TFT_DARKGREY, TFT_BLACK, true,
                     true, true      // centred X & Y
  );
  drawScaledBitmap1bpp(petLayer,
                      grave_rip_bitmap,
                      0, 0, graveW, graveH, graveScale,
                      TFT_WHITE, TFT_BLACK, true, 
                      true, true      // centred X & Y
  );

  petLayer.pushSprite(0, spriteY);

  // Death Sounds
  playTone(400, 300);
  delay(100);
  playTone(300, 300);
  delay(100);
  playTone(200, 600);

  while (true) {
    // Freeze the game
  }
}


void drawUI() {
  petLayer.fillSprite(TFT_BLACK);

  drawHUD();
  drawPoops();

  if (dead) {
    handleDeath();
  } else if (happiness < 30) {
    drawPetFace(pet_sad, petX, petY);
  } else {
    drawPetFace(pet_happy, petX, petY);
  }

  if (foodActive && !hasEatenCurrentFood) {
    drawScaledBitmap1bpp(
        petLayer,        // draw into the sprite
        food_bitmap,
        foodX, foodY,
        foodW, foodH,    // original size
        foodScale,       // scale
        TFT_ORANGE,      // fg color
        TFT_BLACK,       // bg color (ignored when transparent=true)
        true             // transparent background
      );
  }

  if (isPlaying) {
    drawBeachBall(ballX, ballY);  // Always draw every frame
  }

  drawButtons();
  // petLayer.drawRect(0, 0, spriteW, spriteH, TFT_GREEN); // Debugging Green frame
  petLayer.pushSprite(0, spriteY);
}


void showSplashScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(3);

  const char* title = "THoTaGoTcHi";
  // Rainbow palette (extended ROYGBIV: 11 evenly-spaced hues)
  uint16_t titleColors[11] = {
    TFT_RED,         // Red
    TFT_ORANGE,      // Red-Orange
    TFT_YELLOW,      // Yellow
    TFT_GREENYELLOW, // Yellow-Green
    TFT_GREEN,       // Green
    TFT_CYAN,        // Green-Blue
    TFT_BLUE,        // Blue
    TFT_NAVY,        // Indigo
    TFT_PURPLE,      // Violet
    TFT_MAGENTA,     // Violet-Magenta
    TFT_PINK         // Pink (tail of spectrum)
  };

  for (int i = 0; i < strlen(title); i++) {
    tft.setTextColor(titleColors[i % (sizeof(titleColors) / sizeof(titleColors[0]))], TFT_BLACK);
    tft.setCursor(25 + i * 18, 30);
    tft.print(title[i]);
  }

  // Draw egg
  drawScaledBitmap1bpp(
    tft,
    egg_bitmap,
    0, 80,           // X, Y on screen
    eggW, eggH,      // Width, Height
    eggScale,        // Scale
    TFT_GOLD,        // foreground (‘1’ bits)
    TFT_BLACK,       // background (optional)
    true,            // transparent background
    true,            // Centered X
    false            // Centered Y
  );

  tft.setTextSize(2);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setCursor(50, 170);
  tft.println("Made by Kujo");
  tft.setCursor(105, 190);
  tft.println("for");
  tft.setCursor(55, 210);
  tft.println("THOTCON 0xD");

  // play startup tune
  playTone(880, 100);   // A5
  playTone(988, 100);   // B5
  playTone(1047, 150);  // C6

  delay(3000);
  
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(50, 5);
  tft.println("Instructions");
  tft.println(" ");
  tft.println("Touch Wheel:");
  tft.println("Left to Feed");
  tft.println("Down to Play");
  tft.println("Right to Clean");
  tft.println("Center to Select");
  tft.println(" ");
  tft.println(" ");
  tft.println(" ");
  tft.println("Reset Btn to Restart");
  delay(5000);

  tft.fillScreen(TFT_BLACK);
}

//-----------------------------------------------------------

void setup() {
  // Serial.begin(115200);
  // delay(1000);
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  petLayer.createSprite(spriteW, spriteH);
  petLayer.setColorDepth(8);

  pinMode(BUZZER_PIN, OUTPUT);
  for (int i = 0; i < NUM_LEDS; i++) pinMode(ledPins[i], OUTPUT);

  calibrateTouch();
  showSplashScreen();
  drawButtons();

  lastFrameTime = millis(); 
  lastUpdate = millis();
  lastPoopCheck = millis();
}


void loop() {
  serviceTone();      // <-- keep buzzer non-blocking

  unsigned long now = millis();

  /* -----------------------------------------
   Pause hunger / happiness decay while
   the pet is busy eating or playing
   ----------------------------------------- */
  bool decayPaused = (isEating || isPlaying);

  // Touch processing
  float wheelAngle;
  if (!dead && readTouchWheelAngle(wheelAngle)) {
    int newMenuIndex;
    if (wheelAngle > 45 && wheelAngle <= 135) newMenuIndex = 2; // right
    else if (wheelAngle > 135 && wheelAngle <= 225) newMenuIndex = 1; // down
    else if (wheelAngle > 225 && wheelAngle <= 315) newMenuIndex = 0; // left
    else newMenuIndex = 3; // up

    if (newMenuIndex != currentMenuIndex) {
      currentMenuIndex = newMenuIndex;
      drawButtons();
    }
  
  if (currentMenuIndex == 3 && isCenterPressed()) {
      moveMode = (moveMode == WANDER) ? DVD_BOUNCE : WANDER;
      playTone(2000, 100);
    }
  }
  
  bool centerPressed = isCenterPressed();
  if (!dead && centerPressed && !wasCenterPressed && currentMenuIndex != 3) {
    applyAction();
  }
  wasCenterPressed = centerPressed;
  
  
  if (!dead) {
    if (now - lastUpdate >= gameTickInterval) {
      lastUpdate += gameTickInterval;
      
      /* Freeze decay while eating or playing */
      if (!decayPaused) {
        hunger = min(maxHunger, hunger + hungerDecay);
        happiness = max(0, happiness - happinessDecay);
      }

      // Serial.print("Hunger: ");
      // Serial.print(hunger);
      // Serial.print("  Happiness: ");
      // Serial.println(happiness);
      
      // death-watch counter
      if (hunger >= 100 && happiness <= 0) badTicks++;
      else badTicks = 0;
      
      if (badTicks >= deathThreshold) {
        dead = true;
        for (int i = 0; i < NUM_LEDS; i++) digitalWrite(ledPins[i], LOW);
      }
    }
    
    if (now - lastPoopCheck > poopCheckInterval) {
      if (hunger > 80 || happiness < 20 || hunger < 15) {
        spawnPoop();
      }
      lastPoopCheck = now;
    }

    if (!isEating && !isPlaying && !foodActive) {
      if (now - lastIdleChirp > idleChirpCheckInterval) {
        lastIdleChirp = now;
        if (random(0, 100) < 10) {  // 10% chance every interval
          petChirp();
        }
      }
    }


    if (now - lastFrameTime >= frameInterval) {
      lastFrameTime += frameInterval;
      movePet();
      updateLEDs();
    }
  }

  drawUI();
}
