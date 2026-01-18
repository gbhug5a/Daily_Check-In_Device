/*
  This is code for the 5V 16MHz Arduino Pro Mini used in the Check-In Project.
  The project also uses a DS3231 RTC, a 16X2 LCD-I2C display, a rotary encoder,
  and a Lolin D1 Mini with an ESP8266 MCU. You can set up one, two or three
  check-in hours during the day.  You can check in ahead of the next check-in
  hour just by pressing a button.  If you don't check in, there's buzzer
  alarm, and if still no check-in, it triggers a Pushover push notice to the
  phone previously set up to receive it.  See the included PDF file for a
  detailed explanation.
*/

// Globals and defines for core and LCD display
#include <EEPROM.h>          // calibrated 1.1V band-gap stored in EEPROM
#include <avr/sleep.h>       // most of the time spent sleeping
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);
long oldmillis, debmillis, pulsemillis;
bool spaceFlag;
char dateTime[] = "<YY/MM/DD hh:mm";    // date and time fields for display
char *segment[] = {
  (char*)"<",
  (char*)"YY",
  (char*)"MM",
  (char*)"DD",
  (char*)"hh",
  (char*)"mm"
};

char ap[] = "AP";                       // AM/PM
byte A = 0;
byte P = 1;
byte AP;

#define Waiting 0                       // the values of each State
#define CheckedIn 1
#define Alarm 2
#define PushFail1 3
#define PushFail2 4
#define PushSent 5
#define Paused 6
#define DoneToday 7

char *stateText1[] = {                  // display current State - 1st line
  (char*)"Waiting for",
  (char*)"Checked in",
  (char*)"ALARM!!!",
  (char*)"PushSend FAILED1",
  (char*)"PushSend FAILED2",
  (char*)"Check-in FAILED!",
  (char*)"Paused---",
  (char*)"Done for today"
};
char *stateText2[] = {                  // display current State - 2nd line
  (char*)"chk-in by ",
  (char*)"for ",
  (char*)"PLEASE CHECK IN!",
  (char*)"PLEASE CHECK IN!",
  (char*)"PLEASE CHECK IN!",
  (char*)"Push Notice SENT"
};

void Menu0();                           // declare functions for the Menu options
void Menu1();
void Menu2();
void Menu3();
void Menu4();
void Menu5();
void Menu6();
void Menu7();
void Menu8();
void Menu9();
void Menu10();

void (*menuArray[]) () = {Menu0, Menu1, Menu2, Menu3, Menu4,
                          Menu5, Menu6, Menu7, Menu8, Menu9, Menu10
                         };

char *menuText[] = {                    // pointers to Menu options
  (char*)"SET DATE/TIME",               // set RTC date and time
  (char*)"SET CHKIN HOURS",             // set one to three daily checkin hours
  (char*)"AUTO ADJUST DST",             // enable auto adjust for daylight saving
  (char*)"SET BUZZER TIME",             // set alarm buzzer time if check-in missed
  (char*)"SET QUICK TRIES",             // number of immediate tries to send PUSH
  (char*)"ONE-HOUR RETRIES",            // number of retries on successive hours if PUSH still fails
  (char*)"SET WIFI & KEYS",             // set WiFi and Pushover keys
  (char*)"TESTPUSH TO ME",              // send test PUSH notification to my phone
  (char*)"TESTPUSH TO ALL",             // send to group
  (char*)"TRIM RTC SPEED",              // adjust RTC oscillation speed
  (char*)"COINCELL VOLTAGE",            // check voltage of RTC backup coin cell
  (char*)"EXIT MENU"
};

byte menuIndex;
const byte menuMax = 11;                // highest Menu index
byte column[6] = {0, 1, 4, 7, 10, 13};  // location of timestamp segments
byte minn[6] = {0, 26, 1, 1, 0, 0};     // min & max of timestamp items
byte maxx[6] = {5, 99, 12, 31, 23, 59};
byte daysNmonth[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

byte value[6] = {0, 26, 1, 1, 0, 0};    // default timestamp item values
//               0,Year,Month,Day,hour,minute

byte t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};  //Used to calculate DOW (Mon = 1)
byte i, row, DOW;                       // display row, day-of-the-week (Mon = 1)
const byte early = 6;                   // wake up at 6AM for housekeeping only
const byte debTime = 100;               // debounce time for push buttons

// Globals and defines for Lolin D1 Mini

const byte whatPin = 8;                 // set 'what' push notice to send 1 = test, 0 = real
const byte whoPin = 9;                  // set "to whom" push notice to send 1 = me, 0 = Group
const byte POSTresultPin = 7;           // do POST(1) or ConfigAP(0), then result, 0 = success
const byte D1powerPin = A3;             // switch power to D1 Mini, low = on
byte what, who;
bool success;

// Globals and Defines for Checkin and Pause buttons, and buzzer

const byte checkinPin = 2;              // checkin pin
const byte pausePin = 12;               // pause/resume pin
const byte pauseBit = pausePin - 8;     // bit position of D12 within its port
const byte buzzPin = 10;                // alarm output pin
volatile bool checkinFlag;              // checkin button has been pressed
volatile bool pauseFlag;                // pause/resume button has been pressed
bool stopFlag;
byte buzzState;

// Globals and Defines for rotary encoder
// Encoder uses pin change interrupts using the method described in:
// https://github.com/gbhug5a/Rotary-Encoder-Servicing-Routines/blob/master/Arduino/Alternate_PCInterrupt.ino

const byte aPIN = 4;                    // encoder pins for CW/CCW turning
const byte bPIN = 5;
const byte button = 6;                  // encoder push button

const byte encoderType = 0;             // encoder with equal # of detents & pulses per rev
//const byte encoderType = 1;           // encoder with  pulses = detents/2. pick one, commment out the other

const int THRESH = (4 - (2 * encoderType)); // transitions needed to recognize a tick - type 0 = 4, type 1 = 2

const byte CWPIN = bit(aPIN);           // bit value for switch that leads on CW rotation
const byte CCWPIN = bit(bPIN);          // bit value for switch that leads on CCW rotation
const byte BUTTON = bit(button);        // bit value for push button switch on encoder
const byte PINS = CWPIN + CCWPIN;       // sum of bit values of the encoder pins
const byte ZEERO = 0x80;                // "byte" data type doesn't do negative, so make 128 = zero

byte EDGE;                              // the edge direction of the next pin change interrupt
byte CURRENT;                           // the current state of the switches
byte TOTAL = ZEERO;                     // accumulated transitions since last tick (0x80 = none)
byte INDEX = 0;                         // Index into lookup state table
volatile byte change = 0;               // indicates a tick has taken place
volatile byte buttonFlag = 0;           // indicates the encoder button has been pressed
int8_t ENCTABLE[]  = {0, 1, 0, -2, -1, 0, 2, 0, 0, 2, 0, -1, -2, 0, 1, 0,
                      0, 0, -1, 2, 0, 0, -2, 1, 1, -2, 0, 0, 2, -1, 0, 0
                     };
byte maskSave;                          // save state of PortD PCI mask - PCMSK2

// Globals and defines for RTC
// Note - the current State, check-in hours, and four other settings are backed up in
// registers 0x07 - 0x0A of the battery-backed RTC, so preserved across a power failure

const byte cellCtrlPin = A1;            // switches transistor to read coin cell voltage
const byte cellReadPin = A2;            // read voltage here
const byte alarmPin = 3;                // pin to receive RTC alarm signal
volatile bool alarmFlag;                // indicates RTC alarm trigger has triggered
const int RTCaddr = 0x68;               // I2C address of RTC
const int Control = 0x0E;               // Control register address
const int Status = 0x0F;                // Status register address

byte State, hour1, hour2, hour3, DST, quickTries, oneHourRetries, buzzerTime;
byte* setting[] = {&State, &hour1, &hour2, &hour3, &DST, &quickTries, &oneHourRetries, &buzzerTime};
byte settMin[] =     {0,  7,  8,  9, 0, 1, 0,   1};
byte settMax[] =     {7, 23, 24, 24, 1, 3, 2, 250};
byte settDefault[] = {0,  7, 24, 24, 1, 3, 1, 110};
byte packed[4];                          // the above 8 settings packed into 4 bytes -> RTC

byte cReg, sReg, currentHour, pwrFlag,
     saveState, alarmHour, nextAlarm, thisAlarm, changeFlag;
bool ramGood;                           // backup copies of settings are valid
int8_t Aging;                           // value of RTC aging register
int8_t ageMin = -128;                   // max/min values of Aging
int8_t ageMax = 127;
float volts;                            // coin cell voltage
int IRV;                                // internal reference voltage = bandgap

/*
*******************  ISRs  ***********************
*/

ISR (PCINT2_vect) {                     // pin change interrupt service routine
  // for rotary encoder - see link above

  CURRENT = PIND & (PINS + BUTTON);     // read the entire port, mask out all but our pins
  if (!(CURRENT & BUTTON)) {            // check for button push - bit = low
    buttonFlag = 1;
    PCMSK2 &= ~BUTTON;                  // disable further button interrupt
    PCIFR |= bit(PCIF2);                // clear flag if any by writing a 1
    return;
  }

  CURRENT &= ~PCMSK2;                     // clear the bit that just caused the interrupt
  CURRENT |= (EDGE & PCMSK2);             // OR the EDGE value back in

  INDEX     = INDEX << 2;                 // Shift previous state left 2 bits (0 in)
  if (CURRENT & CWPIN) bitSet(INDEX, 0);  // If CW is high, set INDEX bit 0
  if (CURRENT & CCWPIN) bitSet(INDEX, 1); // If CCW is high, set INDEX bit 1
  INDEX &= 15;                            // Mask out all but prev and current.  bit 4 now zero
  if (PCMSK2 & CCWPIN) bitSet(INDEX, 4);  // if CCWPIN is the current enabled interrupt, set bit 4

  // INDEX is now a five-bit index into the 32-byte ENCTABLE state table.

  TOTAL += ENCTABLE[INDEX];               // Accumulate transitions


  if ((CURRENT == PINS) || ((CURRENT == 0) && encoderType)) { // has detent been reached?

    if (TOTAL == (ZEERO + THRESH)) {
      change = 1;
    }

    else if (TOTAL == (ZEERO - THRESH)) {
      change = 0xFF;
    }
    TOTAL = ZEERO;                      // Always reset TOTAL to 0x80 at detent
  }

  if (CURRENT == EDGE) EDGE ^= PINS;    // having reached EDGE state, now switch EDGE to opposite
  PCMSK2 ^= PINS;                       // switch interrupt to other pin
  PCIFR |= bit(PCIF2);                  // clear flag if any by writing a 1
}                                       // end of ISR - interrupts automatically re-enabled

void checkinISR() {                     // ISR for check-in button on D2
  checkinFlag = true;                   // turn on flag
  EIMSK &= ~1;                          // disable further interrupts
  EIFR = 1;                             // clear EI flag
}

void alarmISR() {                       // ISR for RTC alarm  on D3
  alarmFlag = true;                     // turn on flag
  EIMSK &= ~2;                          // disable further interrupts
  EIFR = 2;                             // clear EI flag
}

ISR (PCINT0_vect) {                     // ISR for pause/resume button on D12
  pauseFlag = true;                     // turn on flag
  PCMSK0 &= ~bit(pauseBit);             // disable interrupt
  PCIFR = bit(PCIF0);                   // clear interrupt flag if any
}

/*
****************  Regular functions ****************
*/

void newHour() {                        // set up alarm and State for next checkin hour
  readTime();                           // read in date/time from RTC
  saveState = State;
  nextHour();                           // calculate next alarm hour, new State
  if ((saveState == Paused) ||
      (saveState == PushSent)) State = saveState; // restore State if one of these
  writeAlarm();                         // set next alarm on RTC
  newState();                           // save State, display it
}

void nextHour() {                       // find next checkin hour or done for today
  currentHour = value[4];
  for (i = 1; i < 4; i++) {
    if ((currentHour < *setting[i]) && (*setting[i] != 24)) break;
  }
  if (i == 4) {
    State = DoneToday;
    nextAlarm = early;                  // set 6AM alarm to begin business
    checkDST();                         // or 3AM tomorrow if DST change
  }
  else {                                // or next checkin hour if not done today
    State = Waiting;
    nextAlarm = *setting[i];
  }
}

void checkDST() {                       // Sat before 2nd Sunday Mar or 1st Sunday Nov
  if (DST == 0) return;                 // not adjusting for DST
  if (DOW != 6) return;                 // not Saturday
  if (((value[2] == 3) && (value[3] > 6) && (value[3] < 14)) ||  // Sat Mar 7-13, or
      ((value[2] == 10) && (value[3] == 31)) ||                  // Sat oct 31, or
      ((value[2] == 11) && (value[3] < 7))) {                    // Sat nov 1-6
    nextAlarm = 3;                      // adjust for DST at 3AM tomorrow
  }
}

void newState() {                       // save new State to RTC ram, then display it
  writeReg(7, State | DST << 3 | quickTries << 4 | oneHourRetries << 6);  // save to RTC.
  displayState();                       // nextAlarm should still be valid
}

void writeAlarm() {                     // set up next alarm on RTC
  Wire.beginTransmission(RTCaddr);      // address DS3231
  Wire.write(0x0B);                     // select register (Alarm2)
  Wire.write(0);                        // write zero to minutes register
  Wire.write(dec2bcd(nextAlarm));       // write hour to Alarm2 hour register
  if (State == DoneToday) {
    DOW++;                              // and new Alarm2 DOW if needed
    if (DOW == 8) DOW = 1;
  }
  Wire.write(DOW | 64);                 // DOW includes day/date flag in bit 6
  Wire.endTransmission();
}

void displayState() {                   // display current State
  lcd.clear();
  lcd.print(stateText1[State]);
  lcd.setCursor(0, 1);
  if (State < Paused) lcd.print(stateText2[State]);
  if (State < Alarm) {
    zeroFill (clock12(nextAlarm));
    lcd.print(":00");
    lcd.print(ap[AP]);
  }
}

/*
***************  Menu  ************************
*/

void doMenu() {                         // encoder button has been pressed
  PCIFR |= bit(PCIF2);                  // clear interrupt flag if any
  PCMSK2 = maskSave;                    // enable encoder interrupts in mask register

  menuIndex = 0;
  while (1) {
    lcd.clear();
    lcd.print(menuText[menuIndex]);
    while (!(buttonFlag || change));    // wait for something to happen
    if (change == 1) {                  // encoder turned CW
      change = 0;
      if (menuIndex == menuMax) menuIndex = 0; // roll around
      else menuIndex++;
    }
    if (change == 0xFF) {               // encoder turned CCW
      change = 0;
      if (menuIndex == 0) menuIndex = menuMax;
      else menuIndex--;
    }
    if (buttonFlag) {
      debounce();
      if (menuIndex == menuMax) break;  // Exit
      menuArray[menuIndex]();           // execute menu selection
      if ((menuIndex >= 1) && (menuIndex <= 5)) writeRam();   // save changes to ram on RTC
      if (menuIndex < 3) newHour();     // recalculate next checkin hour
    }
  }
  maskSave = PCMSK2;                    // save encoder mask state
  PCMSK2 &= ~PINS;                      // turn off encoder CW and CCW interrupts
}

void debounce () {                      // debounce encoder button
  debmillis = millis();
  while ((millis() - debmillis) < debTime) {
    if (!digitalRead(button)) debmillis = millis();
  }
  PCMSK2 |= BUTTON;                     // re-enable interrupt
  buttonFlag = 0;
}

byte clock12 (byte hour24) {            // convert 24-hour time to 12-hour
  byte hour12;
  if (hour24 == 0) {
    hour12 = 12;
    AP = A;
  }
  else if (hour24 == 12) {
    hour12 = 12;
    AP = P;
  }
  else if (hour24 > 12) {
    hour12 = hour24 - 12;
    AP = P;
  }
  else {
    hour12 = hour24;
    AP = A;
  }
  return hour12;
}

void Menu0() {                          // set date and time
  if (!pwrFlag) readTime();             // if coin cell still good, read in current time from RTC
  lcd.clear();
  lcd.print(dateTime);
  changeFlag = 0;                       // have we changed the time at all
  i = 0;  row = 0;                      // start at "<" exit position

TimeStamp:                              // Goto label - redo if date is wrong
  while (1) {
    displayValues();
    blinker();
    if (buttonFlag) {                   // push button switches row
      if ((i == 0) && (row == 0)) break;
      debounce();
      row ^= 1;
    }
    if (change == 0xFF) {               // increase/decrease value
      change = 0;
      if (row == 0) {                   // if 1st row, go to next Menu item
        if (i == minn[0]) i = maxx[0];
        else i--;
      }
      if (row == 1) {                   // if second row, -- this item
        if (i == 3) fixDays();
        if (value[i] == minn[i]) value[i] = maxx[i];
        else value[i]--;
        changeFlag = 1;
      }
    }
    if (change == 1) {
      change = 0;
      if (row == 0) {
        if (i == maxx[0]) i = minn[0];
        else i++;
      }
      if (row == 1) {
        if (i == 3) fixDays();
        if (value[i] >= maxx[i]) value[i] = minn[i];
        else value[i]++;
        changeFlag = 1;
      }
    }
  }
  if (changeFlag) {
    fixDays();
    if (value[3] > maxx[3]) {           // make sure date is right for month
      i = 3;
      row = 1;
      debounce();
      goto TimeStamp;
    }
    int y = value[1] + 2000; byte m = value[2];      // calculate DOW
    if ( m < 3 ) y -= 1;
    DOW = (y + y / 4 - y / 100 + y / 400 + t[m - 1] + value[3]) % 7;
    writeTime();                        //save to RTC
  }
  debounce();
}

void fixDays() {                        // February = 29 days in leap years
  maxx[3] = daysNmonth[value[2]];
  if ((value[2] == 2) && (value[1] % 4 == 0)) maxx[3] = 29;
}

void displayValues() {                  // display timestamp in format
  lcd.setCursor(1, 1);
  zeroFill(value[1]);
  lcd.print("/");
  zeroFill(value[2]);
  lcd.print("/");
  zeroFill(value[3]);
  lcd.print(" ");
  zeroFill (clock12(value[4]));
  lcd.print(":");
  zeroFill(value[5]);
  lcd.print(ap[AP]);
}

void zeroFill (byte digit) {            // add leading zeroes when needed
  if (digit < 10) lcd.print("0");
  lcd.print (digit);
}

void blinker() {                        // switch between value and blank
  oldmillis = millis();
  spaceFlag = false;
  while (1) {
    if (millis() - oldmillis > 250) {
      oldmillis = millis();
      lcd.setCursor(column[i], row);
      if (spaceFlag) {
        if (row == 0) lcd.print(segment[i]);
        else {
          if (i == 4) zeroFill(clock12(value[i]));
          else zeroFill(value[i]);
        }
        spaceFlag = false;
      }
      else {
        if (column[i] == 0) lcd.print(" ");
        else lcd.print ("  ");
        spaceFlag = true;
      }
    }
    if (buttonFlag || change) {
      lcd.setCursor(column[i], row);
      if (row == 0) lcd.print(segment[i]);
      else {
        if (i == 4) zeroFill(clock12(value[i]));
        else zeroFill(value[i]);
      }
      break;
    }
  }
}

void Menu1() {                          // set three check-in hours between 7:00AM and 11:00PM
Again:                                  // Goto label - hours not right
  lcd.clear();
  for (i = 1; i < 4; i++) {
    lcd.setCursor(0, 0);
    lcd.print("Check-in hour "); lcd.print(i);  // 24-hour internally, but dislayed as 12A/P
    while (1) {
      lcd.setCursor(0, 1);
      if (*setting[i] == 24) lcd.print("--    ");
      else {
        zeroFill(clock12(*setting[i]));
        lcd.print(":00");
        lcd.print(ap[AP]);
      }
      if (change == 0xFF) {
        change = 0;
        if (*setting[i] == (early + 1)) *setting[i] = 24;
        else (*setting[i])--;
      }
      if (change == 1) {
        change = 0;
        if (*setting[i] == 24) *setting[i] = early + 1;
        else (*setting[i])++;
      }
      if (buttonFlag) {
        debounce();
        break;
      }
    }
  }
  if  (hour1 == 24) goto Again;                             // 1st can't be 24
  if ((hour2 <= hour1) && (hour2 != 24)) goto Again;        // 2nd must be > 1st
  if ((hour3 <= hour2) && (hour3 != 24)) goto Again;        // 3rd must be > 2nd
}

void Menu2() {                               // Auto-adjust for DST?
  lcd.clear();                               // default = yes
  lcd.print("Auto adjust DST");
  while (1) {
    lcd.setCursor(0, 1);
    if (DST) lcd.print("Yes");          // 1 = yes, 0 = no
    else lcd.print("No ");
    while (!(buttonFlag || change));
    if (change) {
      change = 0;
      DST ^= 1;
    }
    if (buttonFlag) {
      debounce();
      break;
    }
  }
}

void Menu3() {                               // set buzzer alarm time
  lcd.clear();
  lcd.print("Buzzer Time");
  lcd.setCursor(0,1);
  lcd.print("    seconds");
  while (1) {
    lcd.setCursor(0, 1);
    if (buzzerTime < 100) lcd.print(" ");
    if (buzzerTime < 10) lcd.print(" ");
    lcd.print (buzzerTime);
    while (!(buttonFlag || change));
    if (change == 1) {
      change = 0;
      if (buzzerTime >= 250) buzzerTime = 1;
      else {
        if (buzzerTime >= 110) buzzerTime += 20;
        else if (buzzerTime >= 50) buzzerTime += 10;
        else if (buzzerTime >= 10) buzzerTime += 5;
        else buzzerTime++;
      }
    }
    if (change == 0xFF) {
      change = 0;
      if (buzzerTime == 1) buzzerTime = 250;
      else {
        if (buzzerTime >= 130) buzzerTime -= 20;
        else if (buzzerTime >= 60) buzzerTime -= 10;
        else if (buzzerTime >= 15) buzzerTime -= 5;
        else buzzerTime--;
      }
    }
    if (buttonFlag) {
      debounce();
      break;
    }
  }
}

void Menu4() {
  lcd.clear();
  lcd.print("Quick Tries");
  while (1) {
    lcd.setCursor(0, 1);
    lcd.print (quickTries);
    while (!(buttonFlag || change));
    if (change == 1) {
      change = 0;
      if (quickTries == 3) quickTries = 1;
      else quickTries++;
    }
    if (change == 0xFF) {
      change = 0;
      if (quickTries == 1) quickTries = 3;
      else quickTries--;
    }
    if (buttonFlag) {
      debounce();
      break;
    }
  }
}

void Menu5() {
  lcd.clear();
  lcd.print("One Hour Retries");
  while (1) {
    lcd.setCursor(0, 1);
    lcd.print (oneHourRetries);
    while (!(buttonFlag || change));
    if (change == 1) {
      change = 0;
      if (oneHourRetries == 2) oneHourRetries = 0;
      else oneHourRetries++;
    }
    if (change == 0xFF) {
      change = 0;
      if (oneHourRetries == 0) oneHourRetries = 2;
      else oneHourRetries--;
    }
    if (buttonFlag) {
      debounce();
      break;
    }
  }
}

void Menu6() {                              // open access point to set wifi and keys
  success = false; stopFlag = false;
  pinMode(POSTresultPin, OUTPUT);            // will be LOW to open config AP
  digitalWrite(D1powerPin, LOW);             // turn on power to D1 Mini
  lcd.clear();
  lcd.print("Connect to WiFi:");
  lcd.setCursor(0, 1);
  lcd.print("'CHECKIN_KEYS'");
  delay(4000);                               // time for D! Mini to boot and settle
  pinMode(POSTresultPin, INPUT);             // convert to input
  while (1) {
    if (buttonFlag || checkinFlag || pauseFlag) {  // any button press stops process
      clearPins();
      break;
    }
    delay(1000);                             // check each second
    pinMode(POSTresultPin, INPUT_PULLUP);
    delay(2);
    byte resultState = digitalRead(POSTresultPin);
    pinMode(POSTresultPin, INPUT);
    if (resultState == LOW) {
      success = true;
      break;
    }
  }
  digitalWrite(D1powerPin, HIGH);            // turn off D1 Mini
  lcd.clear();
  if (success) lcd.print ("...Success");
  else if (stopFlag) lcd.print ("...Stopped");
  else lcd.print("...Failed");
  delay(3000);                               // display for 3 seconds
}

void Menu7() {                               // send test push notice to me
  what = 1;                                  // 1 = test push. 0 = real push
  who = 1;                                   // 1 = me, 0 = all
  sendPush();
}

void Menu8() {                               // send test push notice to all
  what = 1;
  who = 0;
  sendPush();
}

void sendPush() {                            // send push notice
  success = false; stopFlag = false;
  lcd.clear();
  lcd.print("Sending..");
  if (!what) pinMode(whatPin, OUTPUT);       // will be LOW
  if (!who) pinMode(whoPin, OUTPUT);
  digitalWrite(D1powerPin, LOW);             // turn on power to D1 Mini
  delay(4000);
  oldmillis = millis();
  while ((millis() - oldmillis) < 30000) {   // allow 60 seconds
    delay(1000);                             // check each second
    if (buttonFlag || checkinFlag || pauseFlag) {  // any button press stops process
      clearPins();
      break;
    }
    pinMode(POSTresultPin, INPUT_PULLUP);
    delay(2);
    byte resultState = digitalRead(POSTresultPin);
    pinMode(POSTresultPin, INPUT);
    if (resultState == LOW) {                // success
      success = true;
      break;
    }
  }
  pinMode(whatPin, INPUT);
  pinMode(whoPin, INPUT);
  digitalWrite(D1powerPin, HIGH);            // turn off D1 Mini
  lcd.clear();
  if (success) lcd.print ("...Success");
  else if (stopFlag) lcd.print ("...Stopped");
  else lcd.print("...Failed");
  delay (3000);
}

void Menu9() {                               // adjust Aging register on RTC
  Aging = readReg(0x10);                     // intuitive + = faster
  lcd.clear();                               // (opposite of Aging register)
  lcd.print("SETTING = "); lcd.print(-Aging);
  oldmillis = millis();
  while (1) {
    if (change == 0xFF) {                    // opposite of usual turn direction
      if (Aging != ageMax) Aging++;
    }
    if (change == 1) {
      if (Aging != ageMin) Aging--;
    }
    if (change) {
      change = 0;
      lcd.clear();
      lcd.print("SETTING = "); lcd.print(-Aging);
    }
    if (buttonFlag) {
      debounce();
      break;
    }
    if ((millis() - oldmillis) >= 100) {     // display minutes:seconds
      oldmillis = millis();
      Wire.beginTransmission(RTCaddr);       // address DS3231
      Wire.write(0);                         // begin at seconds register
      Wire.endTransmission();
      delay(2);
      Wire.requestFrom(RTCaddr, 2);
      byte secs = bcd2dec(Wire.read());
      byte mins = bcd2dec(Wire.read());
      lcd.setCursor(0, 1);
      zeroFill(mins);
      lcd.print(":");
      zeroFill(secs);
    }
  }
  writeReg(0x10, Aging);
}

void Menu10() {                               // display/adjust RTC coin cell voltage
  EEPROM.get (0, IRV);                       // calibrated IRV stored in EEPROM
  int eepromIRV = IRV;
  if ((IRV < 980) || (IRV > 1200)) IRV = 1080;
  while (1) {
    int previousIRV = IRV;
    doVoltage();                             // compare results to your meter,
    lcd.clear();                             //   and adjust IRV to match meter
    delay(1000);
    lcd.print("BandGap = "); lcd.print(IRV); lcd.print("mV");
    lcd.setCursor(0, 1);
    lcd.print("CoinBty = "); lcd.print(volts, 3); lcd.print("V");
    while (1) {                              // change IRV value
      while (!(buttonFlag || change));
      if (change == 0xFF) {
        change = 0;
        if (IRV > 980) IRV--;
        lcd.setCursor(10, 0); lcd.print(IRV);
        continue;
      }
      if (change == 1) {
        change = 0;
        if (IRV < 1200) IRV++;
        lcd.setCursor(10, 0); lcd.print(IRV);
        continue;
      }
      if (buttonFlag) {                      // button locks in current IRV
        debounce();
        break;
      }
    }
    if (IRV == previousIRV) break;           // button pressed with no change = get out
  }
  if (IRV != eepromIRV) EEPROM.put(0, IRV);  // if changed, save new IRV to EEPROM
}

void doVoltage() {                           // calculate VCC with respect to IRV
  byte savADMUX = ADMUX;
  ADMUX = (1 << REFS0) | (1 << MUX3) | (1 << MUX2) | (1 << MUX1);
  delay(50);
  ADCSRA |= _BV( ADSC );                     // Start a conversion
  while ((ADCSRA & (1 << ADSC)) != 0 );      // Wait for it to complete
  float VCC = (float)(((IRV * 1024.0) / ADC) / 1000.0);    // Scale the value
  ADMUX = savADMUX;
  delay(50);

  digitalWrite(cellCtrlPin, HIGH);           // calculate coin voltage with respect to Vcc
  delay(5);
  analogRead(cellReadPin);                   // toss first reading
  volts = (float)analogRead(cellReadPin) * VCC / 1024.0;   // read battery voltage
  digitalWrite(cellCtrlPin, LOW);
}

byte bcd2dec(byte n) {                       // binary coded decimal to decimal
  n &= 0x7F;                                 // mask out Century bit
  return n - 6 * (n >> 4);
}
byte dec2bcd(byte n) {                       // decimal to BDC
  byte b = (n * 103) >> 10;
  return (n + b * 6);
}

byte readReg(byte regAddr) {                 // read RTC register
  Wire.beginTransmission(RTCaddr);           // I2C address of DS3231
  Wire.write(regAddr);                       // select register
  Wire.endTransmission();
  delay(10);
  Wire.requestFrom(RTCaddr, 1);
  return (Wire.read());
}

void writeReg(byte reg, byte val) {          // write to RTC register
  Wire.beginTransmission(RTCaddr);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

void readTime () {                           // read all 7 date/time registers
  Wire.beginTransmission(RTCaddr);           // address DS3231
  Wire.write(0);                             // select register to begin
  Wire.endTransmission();
  delay(10);
  Wire.requestFrom(RTCaddr, 7);              // the 7 date/time registers
  Wire.read();                               // read and toss seconds
  for (i = 1; i < 6; i++) {                  // read rest
    value[6 - i] = bcd2dec(Wire.read());     // store in value[]
    if (i == 2) DOW = Wire.read();           // day of week
  }
}

void writeTime () {                          // update all 7 date/time registers
  Wire.beginTransmission(RTCaddr);           // address DS3231
  Wire.write(0);                             // select register
  Wire.write(0);                             // write zero to seconds register
  for (i = 1; i < 6; i++) {                  // now minutes and rest
    Wire.write (dec2bcd(value[6 - i]));
    if (i == 2) Wire.write(DOW);
  }
  Wire.endTransmission();
}

void readRam() {                             // read 4 Alarm1 registers
  Wire.beginTransmission(RTCaddr);           // address DS3231
  Wire.write(7);                             // select register to begin
  Wire.endTransmission();
  delay(10);
  Wire.requestFrom(RTCaddr, 4);
  ramGood = true;
  for (i = 0; i < 4; i++) {
    packed[i] = Wire.read();
  }
  State = packed[0] & 7;
  DST = (packed[0] & 8) >> 3;
  quickTries = (packed[0] & 0x30) >> 4;
  oneHourRetries = (packed[0] & 0xC0) >> 6;
  buzzerTime = packed[1];
  unsigned int hours = (unsigned int) (packed[2] | (packed[3] << 8));
  hour1 = hours & 0x1F;
  hour2 = (hours >> 5) & 0x1F;
  hour3 = (hours >> 10) & 0x1F;
  for (i = 0; i < 8; i++) {
    if (!((*setting[i] >= settMin[i]) && (*setting[i] <= settMax[i]))) {
      ramGood = false;
      break;
    }
  }
}

void writeRam() {
  packed[0] = State | DST << 3 | quickTries << 4 | oneHourRetries << 6;
  packed[1] = buzzerTime;
  unsigned int hours = (unsigned int) (hour1 | hour2 << 5 | hour3 << 10);
  packed[2] = lowByte(hours);
  packed[3] = highByte(hours);
  Wire.beginTransmission(RTCaddr);           // address DS3231
  Wire.write(7);                             // select register to begin
  for (i = 0; i < 4; i++) {
    Wire.write(packed[i]);
  }
  Wire.endTransmission();
}

void debounceSwitch(byte thisPin) {          // debounce for check-in and pause pins
  debmillis = millis();
  while ((millis() - debmillis) < debTime) {
    if (!digitalRead(thisPin)) debmillis = millis();
  }
}

void incAlarm() {
  byte addHour = bcd2dec(readReg(0x0C)) + 1;  // current alarm hour + 1
  if (addHour == 24) {                        // roll to zero at 24
    addHour = 0;
    byte DOWn = ((readReg(0x0D) & 7) + 1);    // and bump DOW
    if (DOWn == 8) DOWn = 1;
    writeReg(0x0D, DOWn | 64);                // new DOW if changed
  }
  writeReg(0x0C, dec2bcd(addHour));           // next alarm one hour later
}

bool oneHour (int8_t delta) {                        // TOD same or 1hr vs alarm time
  byte oneDay = readReg(0x0D) & 7;                   // alarm day
  int8_t oneHour = bcd2dec(readReg(0x0C)) + delta;   // alarm hour +1 or -1, or same
  if (oneHour == 24) {
    oneHour = 0;
    oneDay++;
    if (oneDay == 8) oneDay = 1;
  }
  if (oneHour == -1) {
    oneHour = 23;
    oneDay--;
    if (oneDay == 0) oneDay = 7;
  }
  bool match = ((oneHour == bcd2dec(readReg(2))) &&
                (oneDay == readReg(3)));
  return match;
}

void clearPins() {
  if (buttonFlag) {
    debounce();
    stopFlag = true;
  }
  if (checkinFlag) {                   // still possible to check in
    debounceSwitch(checkinPin);
    EIFR = 1;                          // clear hardware flag
    EIMSK |= 1;                        // re-enable interrupt
    checkinFlag = false;               // clear software flag
    stopFlag = true;
  }
  if (pauseFlag) {                     // pause button also checks in
    debounceSwitch(pausePin);
    PCIFR = bit(PCIF0);                // clear hardware flag
    PCMSK0 |= bit(pauseBit);           // re-enable interrupt
    pauseFlag = false;                 // clear software flag
    stopFlag = true;
  }
}

void setup() {

  // Setup for buttons

  pinMode(checkinPin, INPUT);                // initialize pins
  pinMode(pausePin, INPUT);
  pinMode(buzzPin, OUTPUT);
  digitalWrite(buzzPin, LOW);

  // Setup for Lolin D1 Mini

  digitalWrite(D1powerPin, HIGH);            // turn off D1 Mini (high)
  pinMode(D1powerPin, OUTPUT);
  pinMode(whatPin, INPUT);                   // these either INPUT, or OUTPUT LOW
  pinMode(whoPin, INPUT);
  pinMode(POSTresultPin, INPUT);             // ESP8266 will set pullup

  // Setup for LCD display

  lcd.init();                                // initialize the lcd
  lcd.backlight();
  lcd.clear();

  // Setup for rotary encoder

  pinMode(aPIN, INPUT);                 // set up encoder pins as INPUT.  Assumes external 10K pullups
  pinMode(bPIN, INPUT);
  pinMode(button, INPUT);

  EDGE = PINS;                          // identifies next pin-change interrupt as falling or rising
  //    assume current state is low, so any change will be rising
  if (PIND & PINS) {                    // but if actual current state is already high,
    EDGE = 0;                           //    make EDGE low
    INDEX = 3;                          //    and make "current" bits of INDEX match the current high state
  }

  PCMSK2 |= (CWPIN + BUTTON);           // enable only CWPIN and switch interrupts in mask register
  PCIFR |= bit(PCIF2);                  // clear interrupt flag if any
  PCICR |= bit(PCIE2);                  // enable interrupts on Port D

  // setup for RTC

  pinMode(alarmPin, INPUT);             // RTC will pull low on alarm. assumes external pullup
  pinMode(cellReadPin, INPUT);          // coin cell positive terminal
  DIDR0 |= (1 << ADC2D);                // disable input buffers on cellReadPin

  digitalWrite(cellCtrlPin, LOW);       // N-channel mosfet gate to read coin cell voltage
  pinMode(cellCtrlPin, OUTPUT);

  // See if valid data in RTC, and initialize everything

  pwrFlag = readReg(Status) & 0x80;     // OSF flag high if RTC power has been lost (coin cell dead)
  cReg = 0b01000100;                    // clear /EOSC, RS1&2 and alarm enables, set BBSQW and INTCN
  writeReg(Control, cReg);
  sReg = 0;                             // clear OSF, EN32kHz and alarm flags
  writeReg(Status, sReg);
  if (pwrFlag) {                        // if RTC has lost power, init day/time
    Menu0();                            // enter current date/time
    pwrFlag = 0;
  }
  readTime();                           // read date/time into value[]
  readRam();                            // reads and tests for valid format for saved bytes

  lcd.clear();
  alarmHour = bcd2dec(readReg(0x0C));   // latest alarm setting
  if (ramGood == false) {              // if saved bytes not valid
    for (i = 0; i < 8; i++) {
      *setting[i] = settDefault[i];
    }
    Menu1();                            // set up checkin hours
    Menu2();                            // DST setting
    Menu3();                            // Buzzer time
    Menu4();                            // Quick tries
    Menu5();                            // One Hour Retries
    nextHour();                         // find next alarm hour, set State, display
    writeRam();                         // and save all this back to RTC
    writeAlarm();                       // set new alarm hour
    displayState();                     // save and display State
  }
  else if (State == Alarm) {
    if (oneHour(0)) {                   // TOD still = alarm time
      incAlarm();                       // set alarm one hour later
      State = PushFail1;                // set to next state
      ramGood = false;
    }
    else if (oneHour(1)) {              // TOD one hour after alarm time
      incAlarm();                       // set alarm two hours later
      incAlarm();
      State = PushFail2;                // set to next stage
      ramGood = false;
    }
  }
  else if (State == PushFail1) {
    if (oneHour(-1)) {
      ramGood = false;
    }
    else if (oneHour(0)) {
      incAlarm();
      State = PushFail2;
      ramGood = false;
    }
  }
  else if (State == PushFail2) {
    if (oneHour(-1)) {
      ramGood = false;
    }
  }
  if (ramGood == false) newState();

  if (ramGood) {                                 // how long has power been off
    saveState = State;
    nextHour();                                   // calculate next check-in hour
    if ((saveState == PushSent) || (saveState == Paused)) {
      State = saveState;                          // but retain these states
    }
    if ((saveState == CheckedIn) &&               // preserve "CheckedIn" State if:
        (readReg(3) == (readReg(0x0D) & 7)) &&    //  today is same as alarm day
        (currentHour < alarmHour) &&              //  current hour less than next alarm
        (alarmHour == nextAlarm)) {               //  current alarm hour same as next alarm
      State = CheckedIn;                          //  = stay checked in if brief power loss
    }
    writeAlarm();                         // set new alarm hour
    newState();
  }

  // Set up Checkin and Pause buttons, and RTC Alarm, interrupts

  sReg = 0;                             // clear RTC alarm flags
  writeReg(Status, sReg);
  cReg |= 0b10;                         // enable RTC alarm2
  writeReg(Control, cReg);

  noInterrupts();
  EIFR = 3;                             // clear any flags on both pins
  attachInterrupt(digitalPinToInterrupt(checkinPin), checkinISR, FALLING);  // checkinPin
  attachInterrupt(digitalPinToInterrupt(alarmPin), alarmISR, FALLING);      // pausePin
  EIFR = 3;                             // clear any flags on both pins

  PCIFR = bit(PCIF0);                   // clear pausePin interrupt flag if any
  PCMSK0 |= bit(pauseBit);              // enable only pause pin interrupt in mask register
  PCICR |= bit(PCIE0);                  // enable interrupts on Port B
  PCIFR = bit(PCIF0);                   // clear interrupt flag if any

  maskSave = PCMSK2;                    // save encoder mask state
  PCMSK2 &= ~PINS;                      // turn off encoder CW and CCW interrupts (not in Menu)
  PCIFR |= bit(PCIF2);                  // clear interrupt flag if any

  checkinFlag = false;                  // clear all software flags
  pauseFlag = false;
  alarmFlag = false;
  buttonFlag = 0;

  interrupts();
}

void loop() {
  if (!(checkinFlag || pauseFlag || alarmFlag || buttonFlag )) {
    set_sleep_mode (SLEEP_MODE_PWR_DOWN); // Deep sleep
    sleep_enable();
    sleep_bod_disable();                // disable brownout detector during sleep
    sleep_cpu();                        // now go to sleep
  }

  // waked up by a button press or RTC alarm

  if (buttonFlag) {                   // trigger Menu
    debounce();
    doMenu();                         // enter Menu
    displayState();                   // back to current State
  }

  if (checkinFlag) {
    if (State == Waiting) {             // ignore if any other state
      State = CheckedIn;
      newState();                       // save to RTC, then display state
    }
    else if ((State == Alarm) ||
             (State == PushFail1) ||
             (State == PushFail2)) {
      newHour();
    }
    debounceSwitch(checkinPin);
    EIFR = 1;
    EIMSK |= 1;                         // re-enable interrupt
    checkinFlag = false;                // clear flag
  }

  if (pauseFlag) {                      // either pause or resume
    if ((State == Waiting) ||           // pause from these
        (State == CheckedIn) ||
        (State == DoneToday)) {
      State = Paused;
      newState();
    }
    else {                              // resume from Paused, Alarm, PushSent or PushFailedN
      readTime();
      nextHour();                       // find next alarm, find State
      writeAlarm();                     // new alarm to RTC
      if (currentHour < early) State = DoneToday;   // DoneToday also includes before early time
      if ((State == Waiting) && (((*setting[i]) - currentHour) < 3)) State = CheckedIn;
      newState();
    }
    debounceSwitch(pausePin);
    PCIFR = bit(PCIF0);
    PCMSK0 |= bit(pauseBit);            // enable interrupt
    pauseFlag = false;                  // clear flag
  }

  if (alarmFlag) {                      // RTC alarm triggered this wakeup
    thisAlarm = bcd2dec(readReg(0x0C)); // read in alarm hour, which also = current hour
    if (thisAlarm == 3) {               // if 3AM, adjust for DST
      if (bcd2dec(readReg(5) & 0x7F) == 3) thisAlarm++;     // March = Spring forward
      else thisAlarm--;                                     // else Fall back
      writeReg(2, dec2bcd(thisAlarm));                      // adjust time hour
      writeReg(0x0C, early);            // set alarm for early
    }

    else if (thisAlarm == early) {      // find first check-in hour of the day
      newHour();
    }

    else if ((State == Waiting) ||             // alarm caused by missed check-in
             (State == Alarm) ||
             (State == PushFail1) ||           //   or failed push notice
             (State == PushFail2)) {
      if (State == Waiting) {
        State = Alarm;
        newState();
      }
      stopFlag = false; success = false;
      unsigned long buzzTime = (unsigned long) (buzzerTime * 1000);
      for (i = 0; i < quickTries; i++) {                // try buzzer and push notice again
        displayState();
        oldmillis = millis(); pulsemillis = oldmillis; buzzState = 1;
        digitalWrite(buzzPin, buzzState);      // turn on buzzer
        while ((millis() - oldmillis) < buzzTime) {  // default 2 minutes
          clearPins();                         // test button, checkin and pause pins
          if (stopFlag) break;
          if ((millis() - pulsemillis) > 250) {  // beep buzzer at 1Hz
            buzzState = !buzzState;
            digitalWrite(buzzPin, buzzState);
            pulsemillis = millis();
          }
        }
        digitalWrite(buzzPin, LOW);             // turn off buzzer
        if (stopFlag == false) {                // no response to buzzer
          what = 0; who = 0;                    // send real message to all
          sendPush();
        }
        if ((stopFlag == true) || (success == true)) break;
      }                                         // for loop - try 5 times
      if (stopFlag == true) newHour();          // terminated by button push
      else if (success == true) {               // not terminated,
        State = PushSent;                       // but push was successfull
        newHour();
      }
      else {                                    // no stop or success
        if (State >= Alarm + oneHourRetries) {  // give up after two more tries, one hour apart
          State = Waiting;
          newHour();
        }
        else {
          if (State == PushFail1) State = PushFail2;
          if (State == Alarm) State = PushFail1;
          incAlarm();                            // set alarm for one hour later
          newState();                            // set new State
        }
      }
    }
    else newHour();                   // alarm, but still Paused, or Checked In

    writeReg(Status, sReg);           // clear RTC alarm2 flag - write to zero
    delay(100);
    alarmFlag = false;
    EIFR = 1;
    EIMSK |= 2;                       // re-enable alarm interrupt
  }                                   // alarmFlag caused wakeup
}
