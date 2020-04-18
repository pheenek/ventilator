
#include "U8glib.h"

//#define MOTOR_CW  1 // PD1 PIN2
//#define MOTOR_CCW 0 // PD0 PIN3
//#define MOTOR_PWM 9 // PB1 PIN15
//#define MOTOR_ENA  2 // PD2 PIN4
//#define MOTOR_ENB  3 //PD3 PIN5

#define MOTOR_CW  4
#define MOTOR_CCW 5
#define MOTOR_PWM 9 // PB1 PIN15
#define MOTOR_ENA  2 // PD2 PIN4
#define MOTOR_ENB  3 //PD3 PIN5

#define POT_CW  A3
#define POT_CCW A0
#define POT_KEY A2

#define U8G_SS_PIN 12
#define U8G_MOSI  11
#define U8G_SCK   13

#define ENCODER_CW 6
#define ENCODER_CCW 10

#define MENU_ITEMS  3
#define SETTINGS_ITEMS  5
#define DISPLAY_ITEMS  5
#define DISPLAY_TIME  2000

#define START_BUTTON A4

#define PERCENT_PER_SEC  222
#define ENCODER_LIMIT  77
const float ENCODER_FACTOR = 0.77;

typedef struct {
  int TV;
  int BPM;
  int IERatio;
} Volume_t;

typedef struct {
  int peak;
  int plateau;
  int peep;
} Pressure_t;

typedef enum {
  STATUS_SCREEN,
  MENU_SCREEN,
  SETTINGS_MENU,
  DISPLAY_MENU,
  SET_SCREEN
} Screens_t;

typedef enum {
  KEY_DIAL_UP,
  KEY_DIAL_DOWN,
  KEY_DIAL_SELECT
} Key_t;

typedef enum {
  NAV_NULL,
  NAV_NEXT,
  NAV_PREVIOUS,
  NAV_FORWARD,
  NAV_BACK
} Navigation_t;

typedef enum {
  VOL_DISPLAY,
  PRESSURE_DISPLAY,
  VOL_PRESSURE_DISPLAY
} StatusScreen_t;

typedef enum {
  NO_SETTING,
  BPM,
  TV,
  IER
} Setting_t;

typedef enum {
  MOTOR_HALT,
  CLOCKWISE,
  COUNTERCLOCKWISE
} MotorDirection_t;

typedef enum {
  VENT_IDLE,
  VENT_RUNNING,
  VENT_SETTING
} VentState_t;

typedef struct {
  int maxEncoderStroke;
  int period;
  int iTime;
  int eTime;
  int forwardMotorSpeed;
  int revMotorSpeed;
} Stroke_t;

U8GLIB_ST7920_128X64_1X u8g(U8G_SCK, U8G_MOSI, U8G_SS_PIN);

Volume_t volume = {
  .TV = 50,
  .BPM = 15,
  .IERatio = 2
};

Stroke_t stroke = {
  .maxEncoderStroke = 38, // Taking encoder reading btwn 0-76
  .period = 4000,
  .iTime = 0,
  .eTime = 0,
  .forwardMotorSpeed = 100,
  .revMotorSpeed = 100
};

Pressure_t pressure = {0, 0, 0};
volatile Screens_t displayScreen = STATUS_SCREEN;
volatile StatusScreen_t statusDisplay = VOL_DISPLAY;
volatile Setting_t currentSetting;
volatile unsigned long lastDialPress = millis();
volatile unsigned long lastStartButtonPress = millis();
unsigned long displayTime = millis();
StatusScreen_t lastDisplay = VOL_DISPLAY;
volatile VentState_t ventState = VENT_IDLE;
MotorDirection_t ventStrokeDir = CLOCKWISE;
volatile bool runHomeSequence = false;
Navigation_t menuNav = NAV_NULL;
volatile int currentSettingVal = 0;

int selectedMenuItem = 0;
int currentMenuItemNo = 0;

volatile int encodedValue = 0, KEY = 0, lastEncodedValue = 0;
volatile int motorEncodedValue = 0, lastMotorEncodedValue = 0;

void setup() {
  Serial.begin(115200);
  motorInit();
  pciSetup();
  delay(1000);
  startupDisplay();
  homingSequence();
}

void startupDisplay() {
  
  u8g.firstPage();
  do {
    u8g.drawStr(0, 32, "BOOTING...");
  } while(u8g.nextPage());
}

void loop() {
  u8g.firstPage();
  do {
      updateDisplay();
  } while(u8g.nextPage());

  switch (ventState) {
    case VENT_IDLE:
      vent_stop();
      break;
    case VENT_RUNNING:
      vent_cycle();
      break;
    case VENT_SETTING:
      vent_settings();
      break;
    default:
      break;
  }
}

void initVent() {
  
}

void vent_stop() {
  updateMotor(MOTOR_HALT);
  delay(500);
  if (runHomeSequence) {
    runHomeSequence = false;
    homingSequence();
  }
}

void vent_settings() {
  vent_stop();
  switch (currentSetting) {
    case NO_SETTING:
      break;
    case BPM:
      if (menuNav == NAV_BACK) {
        menuNav = NAV_NULL;
        volume.BPM = currentSettingVal;
        Serial.print("Saving BPM value: ");
        Serial.println(volume.BPM);
        updateVentCycleParams();
      }
      break;
    case TV:
      if (menuNav == NAV_BACK) {
        Serial.print("Saving TV value: ");
        menuNav = NAV_NULL;
        volume.TV = currentSettingVal;
        Serial.println(volume.TV);
        stroke.maxEncoderStroke = volume.TV * ENCODER_FACTOR;
        Serial.print("Max Stroke: ");
        Serial.println(stroke.maxEncoderStroke);
        updateVentCycleParams();
      }
      break;
    case IER:
      if (menuNav == NAV_BACK) {
        menuNav = NAV_NULL;
        volume.IERatio = currentSettingVal;
        Serial.print("Saving IE Ratio: ");
        Serial.println(volume.IERatio);
        updateVentCycleParams();
      }
      break;
    default:
      break;
  }
}

void updateVentCycleParams() {
  // set the period of a single cycle from the BPM
  stroke.period = 60/volume.BPM;
  Serial.print("Set period: ");
  Serial.println(stroke.period);
  // set the inspiration time iTime = (I*period)/(I+E)
  stroke.iTime = (1 * stroke.period)/(1 + volume.IERatio);
  Serial.print("Set iTime: ");
  Serial.println(stroke.iTime);
  // set the expiration time eTime = (E*period)/(I+E)
  stroke.eTime = (volume.IERatio * stroke.period)/(1 + volume.IERatio);
  Serial.print("Set eTime: ");
  Serial.println(stroke.eTime);
  // calculate the motor speed
  float strokeSpeed = volume.TV / stroke.iTime;
  stroke.forwardMotorSpeed = (strokeSpeed * 255)/PERCENT_PER_SEC;
  Serial.print("Set forward motor speed: ");
  Serial.println(stroke.forwardMotorSpeed);
  strokeSpeed = volume.TV / stroke.eTime;
  stroke.revMotorSpeed = (strokeSpeed * 255)/PERCENT_PER_SEC;
  Serial.print("Set rev motor speed: ");
  Serial.println(stroke.revMotorSpeed);
}

void vent_cycle() {

  if (runHomeSequence) {
    runHomeSequence = false;
    homingSequence();
    delay(1000);
  }
  int encoderVal = readEncoder();
//  Serial.print("Encoder value");
//  Serial.println(encoderVal);
  int motorSpeed = 0;

  switch (ventStrokeDir) {
    case MOTOR_HALT:
      
      break;
    case CLOCKWISE:
    {
      if (encoderVal >= stroke.maxEncoderStroke) {
//        Serial.println("----------------------");
//        Serial.println("Max reached!");
//        Serial.println("----------------------");
        ventStrokeDir = COUNTERCLOCKWISE;
        motorSpeed = stroke.revMotorSpeed;
      } else {
        motorSpeed = stroke.forwardMotorSpeed;
      }
      break;
    }
    case COUNTERCLOCKWISE:
      if (encoderVal <= 0) {
//        Serial.println("----------------------");
//        Serial.println("Min reached!");
//        Serial.println("----------------------");
        ventStrokeDir = CLOCKWISE;
        motorSpeed = stroke.forwardMotorSpeed;
      } else {
        motorSpeed = stroke.revMotorSpeed;
      }
      break;
    default:
      break;
  }
  setMotorSpeed(motorSpeed);
  updateMotor(ventStrokeDir);
}

void homingSequence() {
//  Serial.println("HOMING SEQUENCE");
//  Serial.println();
  int homeMotorSpeed = 125;
  bool homeFound = false;
  MotorDirection_t dir = CLOCKWISE, lastDir = MOTOR_HALT;
  int encoderVal, lastEncoderVal, minEncoderVal;
  encoderVal = readEncoder();
  minEncoderVal = encoderVal;
  char homeBuf[20], lastBuf[20], currentBuf[20], encBuf[20];
  unsigned long updateMillis = millis();
  
  while (!homeFound) {
    
    lastEncoderVal = encoderVal;
    encoderVal = readEncoder();

    // THIS UPDATE IS TOO SLOW TO WORK HERE. USE SERIAL INSTEAD
//    u8g.firstPage();
//    do {
//        sprintf(homeBuf, "Enc Val: %d %d", encoderVal, lastEncoderVal);
//        sprintf(encBuf, "Dir: %d %d", dir, lastDir);
//        sprintf(currentBuf, "Min: %d", minEncoderVal);
//        sprintf(lastBuf, "Speed: %d", homeMotorSpeed);
//        u8g.setFont(u8g_font_6x13);
//        u8g.drawStr(0, 14, homeBuf);
//        u8g.drawStr(0, 28, currentBuf);
//        u8g.drawStr(0, 42, encBuf);
//        u8g.drawStr(0, 56, lastBuf);
//    } while(u8g.nextPage());

    if (lastEncoderVal == encoderVal) {
      setMotorSpeed(homeMotorSpeed);
      updateMotor(dir);
    }else if (lastEncoderVal > encoderVal) {
      // decreasing encoder value      
      switch (dir) {
        case CLOCKWISE:
        {
          switch (lastDir) {
            case MOTOR_HALT:
            {
              minEncoderVal = encoderVal;
              dir = CLOCKWISE;
              break;
            }
            case COUNTERCLOCKWISE:
            {
              if (minEncoderVal == encoderVal) {
//                Serial.println("----------------------");
//                Serial.println("Home found!");
//                Serial.println("----------------------");
                homeFound = true;
                dir = MOTOR_HALT;
              }
              break;
            }
            default:
              break;
          }
          break;
        }
        case COUNTERCLOCKWISE:
        {
          switch (lastDir) {
            case CLOCKWISE:
              minEncoderVal = encoderVal;
              dir = COUNTERCLOCKWISE;
              break;
            default:
              break;
          }
          break;
        }
        default:
          break;
      }
      
    } else {
      // increasing encoder value
      switch (dir) {
        case CLOCKWISE:
        {
          switch (lastDir) {
            case MOTOR_HALT:
              lastDir = CLOCKWISE;
              dir = COUNTERCLOCKWISE;
              break;
            default:
              break;
          }
          break;
        }
        case COUNTERCLOCKWISE:
        { 
          switch (lastDir) {
            case CLOCKWISE:
              homeMotorSpeed = 80;
              lastDir = COUNTERCLOCKWISE;
              dir = CLOCKWISE;
              break;
            default:
              break;
          }
          break;
        }
        default:
          break;
      }
      
    }
    setMotorSpeed(homeMotorSpeed);
    updateMotor(dir);
  }

//  set position as home
  motorEncodedValue = 0;
}

int readScrollEncoder() {
  return encodedValue >> 2;
}

int readEncoder() {
  return motorEncodedValue >> 2;
}

void updateDisplay() {
  switch(displayScreen) {
    case STATUS_SCREEN:
    {
      currentMenuItemNo = 0;
      switch(statusDisplay) {
        case VOL_DISPLAY:
          drawVolScreen(volume.TV, volume.BPM, volume.IERatio);
          break;
        case PRESSURE_DISPLAY:
          drawPressureScreen(10, 0, 0);
          break;
        case VOL_PRESSURE_DISPLAY:
          if (millis() - displayTime > DISPLAY_TIME) {
            displayTime = millis();
            switch(lastDisplay) {
              case VOL_DISPLAY:
                drawVolScreen(volume.TV, volume.BPM, volume.IERatio);
                lastDisplay = VOL_DISPLAY;
                break;
              case PRESSURE_DISPLAY:
                drawPressureScreen(10, 0, 0);
                lastDisplay = PRESSURE_DISPLAY;
                break;
            }
          }
          break;
        default:
          break;
      }
      break;
    }
    case MENU_SCREEN:
      currentMenuItemNo = MENU_ITEMS;
      drawMenu();
      break;
    case SETTINGS_MENU:
      currentMenuItemNo = SETTINGS_ITEMS;
      drawSettingsScreen();
      break;
    case DISPLAY_MENU:
      currentMenuItemNo = DISPLAY_ITEMS;
      drawDisplayScreen();
      break;
    case SET_SCREEN:
      currentMenuItemNo = 0;
      char *setting;
      int setValue = encodedValue >> 2;
      switch (currentSetting) {
        case BPM:
          setting = "BPM";
          break;
        case TV:
          setting = "Tidal Vol";
          break;
        case IER:
          setting = "I:E Ratio";
          break;
        default:
          break;
      }
      drawSetScreen(setting, setValue);
      break;
    default:
      break;
  }
}

void drawSetScreen(char *setting, int setValue) {
  char settingBuf[20], valueBuf[20];
  u8g.setFont(u8g_font_10x20);
  sprintf(settingBuf, "%s", setting);
  u8g.drawStr(0, 14, settingBuf);

  sprintf(valueBuf, "%d", setValue);
  u8g.drawStr(64, 32, valueBuf);
}

void drawMenu() {
  char *menuItems[] = {"Settings", "Display", "Back"};
  uint8_t i, h;
  u8g_uint_t w, d;

  u8g.setFont(u8g_font_6x13);
  u8g.setFontRefHeightText();
  u8g.setFontPosTop();

  h = u8g.getFontAscent()-u8g.getFontDescent();
  w = u8g.getWidth();

  char *menuTitle = "MENU";
  d = (w - u8g.getStrWidth(menuTitle))/2;
  u8g.drawStr(d, 0, menuTitle);

  for(i = 0; i < MENU_ITEMS; i++) {
    d = (w - u8g.getStrWidth(menuItems[i]))/2;
    u8g.setDefaultForegroundColor();
    if (i == selectedMenuItem) {
//    if (i == 2) {
      u8g.drawBox(0, (i+1)*h+1, w, h);
      u8g.setDefaultBackgroundColor();
    }
    u8g.drawStr(d, (i+1)*h, menuItems[i]);
  }
}

void drawSettingsScreen() {
  char *settingsMenuItems[] = {"BPM", "Tidal Vol", "I:E Ratio", "Back", "Exit"};
  uint8_t i, h;
  u8g_uint_t w, d;

  u8g.setFont(u8g_font_6x13);
  u8g.setFontRefHeightText();
  u8g.setFontPosTop();

  h = u8g.getFontAscent()-u8g.getFontDescent();
  w = u8g.getWidth();

  char *menuTitle = "SETTINGS";
  d = (w - u8g.getStrWidth(menuTitle))/2;
  u8g.drawStr(d, 0, menuTitle);

  for(i = 0; i < SETTINGS_ITEMS; i++) {
    d = (w - u8g.getStrWidth(settingsMenuItems[i]))/2;
    u8g.setDefaultForegroundColor();
    if (i == selectedMenuItem) {
//    if (i == 2) {
      u8g.drawBox(0, (i+1)*h+1, w, h);
      u8g.setDefaultBackgroundColor();
    }
    u8g.drawStr(d, (i+1)*h, settingsMenuItems[i]);
  }
}

void drawDisplayScreen() {
  char *displayMenuItems[] = {"Volume", "Pressure", "Volume and Pressure", "Back", "Exit"};

  uint8_t i, h;
  u8g_uint_t w, d;

  u8g.setFont(u8g_font_6x13);
  u8g.setFontRefHeightText();
  u8g.setFontPosTop();

  h = u8g.getFontAscent()-u8g.getFontDescent();
  w = u8g.getWidth();

  char *menuTitle = "DISPLAY";
  d = (w - u8g.getStrWidth(menuTitle))/2;
  u8g.drawStr(d, 0, menuTitle);

  for(i = 0; i < DISPLAY_ITEMS; i++) {
    d = (w - u8g.getStrWidth(displayMenuItems[i]))/2;
    u8g.setDefaultForegroundColor();
    if (i == selectedMenuItem) {
//    if (i == 2) {
      u8g.drawBox(0, (i+1)*h+1, w, h);
      u8g.setDefaultBackgroundColor();
    }
    u8g.drawStr(d, (i+1)*h, displayMenuItems[i]);
  }
}

void motorInit() {
  pinMode(MOTOR_CW, OUTPUT);
  pinMode(MOTOR_CCW, OUTPUT);
  pinMode(MOTOR_PWM, OUTPUT);
  pinMode(MOTOR_ENA, OUTPUT);
  pinMode(MOTOR_ENB, OUTPUT);

  digitalWrite(MOTOR_ENA, HIGH);
  digitalWrite(MOTOR_ENB, HIGH);
}

void updateMotor(MotorDirection_t motorDirection) {
  switch(motorDirection) {
    case MOTOR_HALT:
      stopMotor();
      break;
    case CLOCKWISE:
      setCWDirection();
      break;
    case COUNTERCLOCKWISE:
      setCCWDirection();
      break;
    default:
      break;
  }
}

void stopMotor() {
//  digitalWrite(MOTOR_PWM, LOW);
  digitalWrite(MOTOR_CW, LOW);
  digitalWrite(MOTOR_CCW, LOW);
}

void setCWDirection() {
  digitalWrite(MOTOR_CW, LOW);
  digitalWrite(MOTOR_CCW, HIGH);
}

void setCCWDirection() {
  digitalWrite(MOTOR_CW, HIGH);
  digitalWrite(MOTOR_CCW, LOW);
}

void setMotorSpeed(int motorSpeed) {
  analogWrite(MOTOR_PWM, motorSpeed);
}

void pciSetup()
{
  digitalWrite(POT_CW, HIGH);
  digitalWrite(POT_CCW, HIGH);
  digitalWrite(POT_KEY, HIGH);
  
  digitalWrite(ENCODER_CW, HIGH);
  digitalWrite(ENCODER_CCW, HIGH);

  digitalWrite(START_BUTTON, HIGH);
  
  PCICR |= (1 << PCIE0) | (1 << PCIE1) | (1 << PCIE2);
  PCMSK0 |=  (1 << PCINT2);
  PCMSK1 |= (1 << PCINT8) | (1 << PCINT10) | (1 << PCINT11) | (1 << PCINT12);
  PCMSK2 |= (1 << PCINT22);

  sei();
}

ISR (PCINT2_vect, ISR_ALIASOF(PCINT0_vect));

ISR (PCINT0_vect) {
  int MSB = digitalRead(ENCODER_CW);
  int LSB = digitalRead(ENCODER_CCW);

  int encoded = (MSB << 1) | LSB;
  int sum = (lastMotorEncodedValue << 2) | encoded;

  if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) motorEncodedValue++;
  if (sum == 0b1110 || sum == 0b1000 || sum == 0b0001 || sum == 0b0111) motorEncodedValue--;
  
  lastMotorEncodedValue = encoded;
}

ISR (PCINT1_vect) {
  if (displayScreen != STATUS_SCREEN) {
    int MSB = digitalRead(POT_CW);
    int LSB = digitalRead(POT_CCW);
    
    int encoded = (MSB << 1) | LSB;
    int sum = (lastEncodedValue << 2) | encoded;
    
    if (displayScreen != SET_SCREEN) {
      if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) {
        int newVal = encodedValue - 1;
        if (((newVal >> 2) < currentMenuItemNo) && ((newVal >> 2) >= 0)) encodedValue--;
      }
      if (sum == 0b1110 || sum == 0b1000 || sum == 0b0001 || sum == 0b0111) {
        int newVal = encodedValue + 1;
        if ((newVal >> 2) < currentMenuItemNo) encodedValue++;
      }
  
      int currentVal = encodedValue >> 2;
      if ((currentVal < currentMenuItemNo) && (encodedValue >= 0)) {
        selectedMenuItem = currentVal;
      }
      
    } else {
      if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) {
        int newVal = encodedValue - 1;
        if (((newVal >> 2) < 100) && ((newVal >> 2) >= 0)) encodedValue--;
      }
      if (sum == 0b1110 || sum == 0b1000 || sum == 0b0001 || sum == 0b0111) {
        int newVal = encodedValue + 1;
        if ((newVal >> 2) < 100) encodedValue++;
      }
    }
    lastEncodedValue = encoded;
  }

  if (bit_is_clear(PINC, PC4)) {
    if (millis() - lastStartButtonPress > 500) {
      lastStartButtonPress = millis();
      switch (ventState) {
        case VENT_IDLE:
          runHomeSequence = true;
          ventState = VENT_RUNNING;
          Serial.println("Vent Running!");
          break;
        case VENT_RUNNING:
          runHomeSequence = true;
          ventState = VENT_IDLE;
          Serial.println("Vent Idle!");
          break;
        default:
          break;
      }
    }
  }

  if (bit_is_clear(PINC, PC2)) {
    if (millis() - lastDialPress > 500) {
      lastDialPress = millis();
      switch(displayScreen) {
        case STATUS_SCREEN:
        {
          if (ventState == VENT_RUNNING) runHomeSequence = true;
          ventState = VENT_SETTING;
          displayScreen = MENU_SCREEN;
          break;
        }
        case MENU_SCREEN:
        {
          switch(selectedMenuItem) {
            case 0:
              displayScreen = SETTINGS_MENU;
              break;
            case 1:
              displayScreen = DISPLAY_MENU;
              break;
            case 2:
              ventState = VENT_IDLE;
              displayScreen = STATUS_SCREEN;
              break;
            default:
              break;
          }
        }
          break;
        case SETTINGS_MENU:
        {
          switch(selectedMenuItem) {
            case 0:
              currentSetting = BPM;
              displayScreen = SET_SCREEN;
              break;
            case 1:
              currentSetting = TV;
              displayScreen = SET_SCREEN;
              break;
            case 2:
              currentSetting = IER;
              displayScreen = SET_SCREEN;
              break;
            case 3:
              displayScreen = MENU_SCREEN;
              break;
            case 4:
              ventState = VENT_IDLE;
              displayScreen = STATUS_SCREEN;
              break;
            default:
              break;
          }
        }
          break;
        case DISPLAY_MENU:
        {
          switch(selectedMenuItem) {
            case 0:
              ventState = VENT_IDLE;
              statusDisplay = VOL_DISPLAY;
              displayScreen = STATUS_SCREEN;
              break;
            case 1:
              ventState = VENT_IDLE;
              statusDisplay = PRESSURE_DISPLAY;
              displayScreen = STATUS_SCREEN;
              break;
            case 2:
              ventState = VENT_IDLE;
              statusDisplay = VOL_PRESSURE_DISPLAY;
              displayScreen = STATUS_SCREEN;
              break;
            case 3:
              displayScreen = MENU_SCREEN;
              break;
            case 4:
              ventState = VENT_IDLE;
              displayScreen = STATUS_SCREEN;
              break;
            default:
              break;
          }
        }
          break;
        case SET_SCREEN:
          currentSettingVal = readScrollEncoder();
          menuNav = NAV_BACK;
          displayScreen = SETTINGS_MENU;
          break;
      }
      selectedMenuItem = 0;
      encodedValue = 0;
    }
  }
  
}

void drawVolScreen(int vol, int RR, int IERatio) {
//  Serial.println("Draw Volume Screen");
  u8g.setFont(u8g_font_10x20);
  char volBuf[20], RRBuf[20], IERatioBuf[20];

//  Serial.print("TV: ");
//  Serial.print(volume.TV);
  sprintf(volBuf, "TV=%d max", vol);
//  Serial.print("\tBPM");
  sprintf(RRBuf, "BPM=%d/min", RR);
//  Serial.println("\tIE Ratio");
  sprintf(IERatioBuf, "I:E=1:%d", IERatio);

//  Serial.println("Drawing values");
  u8g.drawStr(0, 14, "Set:");
  u8g.drawStr(10, 30, volBuf);
  u8g.drawStr(10, 46, RRBuf);
  u8g.drawStr(10, 64, IERatioBuf);
}

void drawPressureScreen(int peak, int plateau, int peep) {
  u8g.setFont(u8g_font_10x20);
  char peakBuf[20], plateauBuf[20], peepBuf[20];

  sprintf(peakBuf, "Peak=%d", peak);
  sprintf(plateauBuf, "Plateau=%d", plateau);
  sprintf(peepBuf, "PEEP=%d", peep);

  u8g.drawStr(0, 14, "P(cmH2O)");
  u8g.drawStr(10, 30, peakBuf);
  u8g.drawStr(10, 46, plateauBuf);
  u8g.drawStr(10, 64, peepBuf);
}
