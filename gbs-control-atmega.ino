#include <Wire.h>
#include <EEPROM.h>
#include "minimal_startup.h"
#include "rgbhv.h"
//#include "ypbpr_1080i.h"
#include "ofw_ypbpr.h"
//#include "ntsc_240p.h"
//#include "pal_240p.h"
#include "pal_widescreen.h"
#include "pal_fullscreen.h"
#include "pal_feedbackclock.h"
#include "ntsc_widescreen.h"
#include "ntsc_fullscreen.h"
#include "ntsc_feedbackclock.h"

#define LEDON  digitalWrite(LED_BUILTIN, HIGH)
#define LEDOFF digitalWrite(LED_BUILTIN, LOW)
#define vsyncInPin 10
#define BUTTON1 2
#define BUTTON2 3
#define BUTTON3 4
#define SWITCH1 8

#define GBS_ADDR 0x17


//#define REGISTER_DUMP

// runTimeOptions holds system variables
struct runTimeOptions {
  boolean inputIsYpBpR;
  boolean syncWatcher;
  uint8_t videoStandardInput : 4; // 0 - unknown, 1 - NTSC like, 2 - PAL like, 3 480p NTSC, 4 576p PAL
  uint8_t phaseSP;
  uint8_t phaseADC;
  uint8_t samplingStart;
  uint8_t currentLevelSOG;
  boolean deinterlacerWasTurnedOff;
  boolean modeDetectInReset;
  boolean syncLockEnabled;
  boolean syncLockFound;
  boolean VSYNCconnected;
  boolean IFdown; // push button support example using an interrupt
  boolean printInfos;
  boolean sourceDisconnected;
} rtos;
struct runTimeOptions *rto = &rtos;

// userOptions holds user preferences / customizations
struct userOptions {
  uint8_t presetPreference; // 0 - normal, 1 - feedback clock, 2 - customized
} uopts;
struct userOptions *uopt = &uopts;

uint8_t imageFunctionToggle = 0;
bool widescreenSwitchEnabled = false;
char globalCommand;

void nopdelay(unsigned int times) {
  while (times-- > 0)
    __asm__("nop\n\t");
}

void writeOneByte(uint8_t slaveRegister, uint8_t value) {
  writeBytes(slaveRegister, &value, 1);
}

void writeBytes(uint8_t slaveAddress, uint8_t slaveRegister, uint8_t* values, uint8_t numValues) {
  Wire.beginTransmission(slaveAddress);
  Wire.write(slaveRegister);
  int sentBytes = Wire.write(values, numValues);
  Wire.endTransmission();

  if (sentBytes != numValues) {
    Serial.print(F("i2c error\n"));
  }
}

void writeBytes(uint8_t slaveRegister, uint8_t* values, int numValues) {
  writeBytes(GBS_ADDR, slaveRegister, values, numValues);
}

void writeProgramArray(const uint8_t* programArray) {
  for (int y = 0; y < 6; y++) {
    writeOneByte(0xF0, (uint8_t)y );
    for (int z = 0; z < 16; z++) {
      uint8_t bank[16];
      for (int w = 0; w < 16; w++) {
        bank[w] = pgm_read_byte(programArray + (y * 256 + z * 16 + w));
      }
      writeBytes(z * 16, bank, 16);
    }
  }
}

void writeProgramArrayNew(const uint8_t* programArray) {
  int index = 0;
  uint8_t bank[16];

  // programs all valid registers (the register map has holes in it, so it's not straight forward)
  // 'index' keeps track of the current preset data location.
  writeOneByte(0xF0, 0);
  writeOneByte(0x46, 0x00); // reset controls 1
  writeOneByte(0x47, 0x00); // reset controls 2

  // 498 = s5_12, 499 = s5_13
  writeOneByte(0xF0, 5);
  writeOneByte(0x11, 0x11); // Initial VCO control voltage
  writeOneByte(0x13, getSingleByteFromPreset(programArray, 499)); // load PLLAD divider high bits first (tvp7002 manual)
  writeOneByte(0x12, getSingleByteFromPreset(programArray, 498));
  writeOneByte(0x16, getSingleByteFromPreset(programArray, 502)); // might as well
  writeOneByte(0x17, getSingleByteFromPreset(programArray, 503)); // charge pump current
  writeOneByte(0x18, 0); writeOneByte(0x19, 0); // adc / sp phase reset

  for (int y = 0; y < 6; y++) {
    writeOneByte(0xF0, (uint8_t)y );
    switch (y) {
      case 0:
        for (int j = 0; j <= 1; j++) { // 2 times
          for (int x = 0; x <= 15; x++) {
            // reset controls are at 0x46, 0x47
            if (j == 0 && (x == 6 || x == 7)) {
              // keep reset controls active
              bank[x] = 0;
            }
            else {
              // use preset values
              bank[x] = pgm_read_byte(programArray + index);
            }

            index++;
          }
          writeBytes(0x40 + (j * 16), bank, 16);
        }
        for (int x = 0; x <= 15; x++) {
          bank[x] = pgm_read_byte(programArray + index);
          index++;
        }
        writeBytes(0x90, bank, 16);
        break;
      case 1:
        for (int j = 0; j <= 8; j++) { // 9 times
          for (int x = 0; x <= 15; x++) {
            bank[x] = pgm_read_byte(programArray + index);
            index++;
          }
          writeBytes(j * 16, bank, 16);
        }
        break;
      case 2:
        for (int j = 0; j <= 3; j++) { // 4 times
          for (int x = 0; x <= 15; x++) {
            bank[x] = pgm_read_byte(programArray + index);
            index++;
          }
          writeBytes(j * 16, bank, 16);
        }
        break;
      case 3:
        for (int j = 0; j <= 7; j++) { // 8 times
          for (int x = 0; x <= 15; x++) {
            bank[x] = pgm_read_byte(programArray + index);
            index++;
          }
          writeBytes(j * 16, bank, 16);
        }
        break;
      case 4:
        for (int j = 0; j <= 5; j++) { // 6 times
          for (int x = 0; x <= 15; x++) {
            bank[x] = pgm_read_byte(programArray + index);
            index++;
          }
          writeBytes(j * 16, bank, 16);
        }
        break;
      case 5:
        for (int j = 0; j <= 6; j++) { // 7 times
          for (int x = 0; x <= 15; x++) {
            bank[x] = pgm_read_byte(programArray + index);
            if (index == 482) { // s5_02 bit 6+7 = input selector (only bit 6 is relevant)
              if (rto->inputIsYpBpR)bitClear(bank[x], 6);
              else bitSet(bank[x], 6);
            }
            index++;
          }
          writeBytes(j * 16, bank, 16);
        }
        break;
    }
  }

  setParametersSP();

  writeOneByte(0xF0, 1);
  writeOneByte(0x60, 0x81); // MD H unlock / lock
  writeOneByte(0x61, 0x81); // MD V unlock / lock
  writeOneByte(0x80, 0xa9); // MD V nonsensical custom mode
  writeOneByte(0x81, 0x2e); // MD H nonsensical custom mode
  writeOneByte(0x82, 0x35); // MD H / V timer detect enable, auto detect enable
  writeOneByte(0x83, 0x10); // MD H / V unstable estimation lock value medium

  //update rto phase variables
  uint8_t readout = 0;
  writeOneByte(0xF0, 5);
  readFromRegister(0x18, 1, &readout);
  rto->phaseADC = ((readout & 0x3e) >> 1);
  readFromRegister(0x19, 1, &readout);
  rto->phaseSP = ((readout & 0x3e) >> 1);
  Serial.print(rto->phaseADC);
  Serial.print("\n");
  Serial.print(rto->phaseSP);
  Serial.print("\n");

  //reset rto sampling start variable
  //writeOneByte(0xF0, 1);
  //readFromRegister(0x26, 1, &readout);
  //rto->samplingStart = readout;
  //Serial.print(rto->samplingStart);
  //Serial.print("\n");

  writeOneByte(0xF0, 0);
  writeOneByte(0x46, 0x3f); // reset controls 1 // everything on except VDS display output
  writeOneByte(0x47, 0x17); // all on except HD bypass
}

void writeProgramArraySection(const uint8_t* programArray, byte section, byte subsection = 0) {
  // section 1: index = 48
  uint8_t bank[16];
  int index = 0;

  if (section == 0) {
    index = 0;
    writeOneByte(0xF0, 0);
    for (int j = 0; j <= 1; j++) { // 2 times
      for (int x = 0; x <= 15; x++) {
        bank[x] = pgm_read_byte(programArray + index);
        index++;
      }
      writeBytes(0x40 + (j * 16), bank, 16);
    }
    for (int x = 0; x <= 15; x++) {
      bank[x] = pgm_read_byte(programArray + index);
      index++;
    }
    writeBytes(0x90, bank, 16);
  }
  if (section == 1) {
    index = 48;
    writeOneByte(0xF0, 1);
    for (int j = 0; j <= 8; j++) { // 9 times
      for (int x = 0; x <= 15; x++) {
        bank[x] = pgm_read_byte(programArray + index);
        index++;
      }
      writeBytes(j * 16, bank, 16);
    }
  }
  if (section == 2) {
    index = 192;
    writeOneByte(0xF0, 2);
    for (int j = 0; j <= 3; j++) { // 4 times
      for (int x = 0; x <= 15; x++) {
        bank[x] = pgm_read_byte(programArray + index);
        index++;
      }
      writeBytes(j * 16, bank, 16);
    }
  }
  if (section == 3) {
    index = 256;
    writeOneByte(0xF0, 3);
    for (int j = 0; j <= 7; j++) { // 8 times
      for (int x = 0; x <= 15; x++) {
        bank[x] = pgm_read_byte(programArray + index);
        index++;
      }
      writeBytes(j * 16, bank, 16);
    }
  }
  if (section == 4) {
    index = 384;
    writeOneByte(0xF0, 4);
    for (int j = 0; j <= 5; j++) { // 6 times
      for (int x = 0; x <= 15; x++) {
        bank[x] = pgm_read_byte(programArray + index);
        index++;
      }
      writeBytes(j * 16, bank, 16);
    }
  }
  if (section == 5) {
    index = 480;
    int j = 0;
    if (subsection == 1) {
      index = 512;
      j = 2;
    }
    writeOneByte(0xF0, 5);
    for (; j <= 6; j++) {
      for (int x = 0; x <= 15; x++) {
        bank[x] = pgm_read_byte(programArray + index);
        if (index == 482) { // s5_02 bit 6+7 = input selector (only 6 is relevant)
          if (rto->inputIsYpBpR)bitClear(bank[x], 6);
          else bitSet(bank[x], 6);
        }
        index++;
      }
      writeBytes(j * 16, bank, 16);
    }
    resetPLL(); resetPLLAD();// only for section 5
  }
}

void fuzzySPWrite() {
  writeOneByte(0xF0, 5);
  for (uint8_t reg = 0x21; reg <= 0x34; reg++) {
    writeOneByte(reg, random(0x00, 0xFF));
  }
}

void setParametersSP() {
  writeOneByte(0xF0, 5);

  if (rto->videoStandardInput == 3) {
    writeOneByte(0x00, 0xd0);
    writeOneByte(0x16, 0x1f);
    writeOneByte(0x37, 0x04); // need to work on this
    writeOneByte(0x3b, 0x11);
    writeOneByte(0x3f, 0x1b);
    writeOneByte(0x40, 0x20);
    writeOneByte(0x50, 0x00);
  }
  else if (rto->videoStandardInput == 4) {
    writeOneByte(0x00, 0xd0);
    writeOneByte(0x16, 0x1f);
    writeOneByte(0x37, 0x04);
    writeOneByte(0x3b, 0x11);
    writeOneByte(0x3f, 0x58);
    writeOneByte(0x40, 0x5b);
    writeOneByte(0x50, 0x00);
  }
  else {
    writeOneByte(0x37, 0x58); // need to work on this
  }

  writeOneByte(0x20, 0x12); // was 0xd2 // keep jitter sync on! (snes, check debug vsync)(auto correct sog polarity, sog source = ADC)
  // H active detect control
  writeOneByte(0x21, 0x1b); // SP_SYNC_TGL_THD    H Sync toggle times threshold  0x20
  writeOneByte(0x22, 0x0f); // SP_L_DLT_REG       Sync pulse width different threshold (little than this as equal).
  writeOneByte(0x24, 0x40); // SP_T_DLT_REG       H total width different threshold rgbhv: b // try reducing to 0x0b again
  writeOneByte(0x25, 0x00); // SP_T_DLT_REG
  writeOneByte(0x26, 0x05); // SP_SYNC_PD_THD     H sync pulse width threshold // try increasing to ~ 0x50
  writeOneByte(0x27, 0x00); // SP_SYNC_PD_THD
  writeOneByte(0x2a, 0x0f); // SP_PRD_EQ_THD      How many continue legal line as valid
  // V active detect control
  writeOneByte(0x2d, 0x04); // SP_VSYNC_TGL_THD   V sync toggle times threshold
  writeOneByte(0x2e, 0x04); // SP_SYNC_WIDTH_DTHD V sync pulse width threshod // the 04 is a test
  writeOneByte(0x2f, 0x04); // SP_V_PRD_EQ_THD    How many continue legal v sync as valid  0x04
  writeOneByte(0x31, 0x2f); // SP_VT_DLT_REG      V total different threshold
  // Timer value control
  writeOneByte(0x33, 0x28); // SP_H_TIMER_VAL     H timer value for h detect (hpw 148 typical, need a little slack > 160/4 = 40 (0x28)) (was 0x28)
  writeOneByte(0x34, 0x03); // SP_V_TIMER_VAL     V timer for V detect       (?typical vtotal: 259. times 2 for 518. ntsc 525 - 518 = 7. so 0x08?)

  // Sync separation control
  writeOneByte(0x35, 0xb0); // SP_DLT_REG [7:0]   Sync pulse width difference threshold  (tweak point) (b0 seems best from experiments. above, no difference)
  writeOneByte(0x36, 0x00); // SP_DLT_REG [11:8]

  writeOneByte(0x38, 0x07); // h coast pre (psx starts eq pulses around 4 hsyncs before vs pulse) rgbhv: 7
  writeOneByte(0x39, 0x03); // h coast post (psx stops eq pulses around 4 hsyncs after vs pulse) rgbhv: 12
  // note: the pre / post lines number probably depends on the vsync pulse delay, ie: sync stripper vsync delay

  writeOneByte(0x3a, 0x0a); // 0x0a rgbhv: 20
  //writeOneByte(0x3f, 0x03); // 0x03
  //writeOneByte(0x40, 0x0b); // 0x0b

  //writeOneByte(0x3e, 0x00); // problems with snes 239 line mode, use 0x00  0xc0 rgbhv: f0

  // clamp position
  // in RGB mode, should use sync tip clamping: s5s41s80 s5s43s90 s5s42s06 s5s44s06
  // in YUV mode, should use back porch clamping: s5s41s70 s5s43s98 s5s42s00 s5s44s00
  // tip: see clamp pulse in RGB signal with clamp start > clamp end (scope trigger on sync in, show one of the RGB lines)
  writeOneByte(0x41, 0x19); writeOneByte(0x43, 0x27); // 0x70, 0x98
  writeOneByte(0x42, 0x00); writeOneByte(0x44, 0x00); // 0x00 0x05

  // 0x45 to 0x48 set a HS position just for Mode Detect. it's fine at start = 0 and stop = 1 or above
  // Update: This is the retiming module. It can be used for SP processing with t5t57t6
  writeOneByte(0x45, 0x00); // 0x00 // retiming SOG HS start
  writeOneByte(0x46, 0x00); // 0xc0 // retiming SOG HS start
  writeOneByte(0x47, 0x02); // 0x05 // retiming SOG HS stop // align with 1_26 (same value) seems good for phase
  writeOneByte(0x48, 0x00); // 0xc0 // retiming SOG HS stop
  writeOneByte(0x49, 0x04); // 0x04 rgbhv: 20
  writeOneByte(0x4a, 0x00); // 0xc0
  writeOneByte(0x4b, 0x44); // 0x34 rgbhv: 50
  writeOneByte(0x4c, 0x00); // 0xc0

  // h coast start / stop positions
  // try these values and t5t3et2 when using cvid sync / no sync stripper
  // appears start should be around 0x70, stop should be htotal - 0x70
  //writeOneByte(0x4e, 0x00); writeOneByte(0x4d, 0x70); //  | rgbhv: 0 0
  //writeOneByte(0x50, 0x06); // rgbhv: 0 // is 0x06 for comment below
  writeOneByte(0x4f, 0x9a); // rgbhv: 0 // psx with 54mhz osc. > 0xa4 too much, 0xa0 barely ok, > 0x9a!

  writeOneByte(0x51, 0x02); // 0x00 rgbhv: 2
  writeOneByte(0x52, 0x00); // 0xc0
  writeOneByte(0x53, 0x06); // 0x05 rgbhv: 6
  writeOneByte(0x54, 0x00); // 0xc0

  //writeOneByte(0x55, 0x50); // auto coast off (on = d0, was default)  0xc0 rgbhv: 0 but 50 is fine
  //writeOneByte(0x56, 0x0d); // sog mode on, clamp source pixclk, no sync inversion (default was invert h sync?)  0x21 rgbhv: 36
  if (rto->videoStandardInput == 3 || rto->videoStandardInput == 4) {
    writeOneByte(0x56, 0x01); // 0x01 for 480p over component
  }
  else {
    writeOneByte(0x56, 0x05); // update: one of the new bits causes clamp glitches, check with checkerboard pattern
  }
  //writeOneByte(0x57, 0xc0); // 0xc0 rgbhv: 44 // set to 0x80 for retiming

  writeOneByte(0x58, 0x05); //rgbhv: 0
  writeOneByte(0x59, 0x00); //rgbhv: c0
  writeOneByte(0x5a, 0x01); //rgbhv: 0 // was 0x05 but 480p ps2 doesnt like it
  writeOneByte(0x5b, 0x00); //rgbhv: c8
  writeOneByte(0x5c, 0x06); //rgbhv: 0
  writeOneByte(0x5d, 0x00); //rgbhv: 0
}

// Sync detect resolution: 5bits; comparator voltage range 10mv~320mv.
// -> 10mV per step; recommended 120mV = 0x59 / 0x19 (snes likes 100mV, fine.)
void setSOGLevel(uint8_t level) {
  uint8_t reg_5_02 = 0;
  writeOneByte(0xF0, 5);
  readFromRegister(0x02, 1, &reg_5_02);
  reg_5_02 = (reg_5_02 & 0xc1) | (level << 1);
  writeOneByte(0x02, reg_5_02);
  rto->currentLevelSOG = level;
  Serial.print(" SOG lvl "); Serial.print(rto->currentLevelSOG);
  Serial.print("\n");
}

void inputAndSyncDetect() {
  // GBS boards have 2 potential sync sources:
  // - 3 plug RCA connector
  // - VGA input / 5 pin RGBS header / 8 pin VGA header (all 3 are shared electrically)
  // This routine finds the input that has a sync signal, then stores the result for global use.
  // Note: It is assumed that the 5725 has a preset loaded!
  uint8_t readout = 0;
  uint8_t previous = 0;
  byte timeout = 0;
  boolean syncFound = false;

  setParametersSP();

  writeOneByte(0xF0, 5);
  writeOneByte(0x02, 0x15); // SOG on, slicer level 100mV, input 00 > R0/G0/B0/SOG0 as input (YUV)
  writeOneByte(0xF0, 0);
  timeout = 6; // try this input a few times and look for a change
  readFromRegister(0x19, 1, &readout); // in hor. pulse width
  while (timeout-- > 0) {
    previous = readout;
    readFromRegister(0x19, 1, &readout);
    if (previous != readout) {
      rto->inputIsYpBpR = 1;
      syncFound = true;
      break;
    }
    delay(1);
  }

  if (syncFound == false) {
    writeOneByte(0xF0, 5);
    writeOneByte(0x02, 0x55); // SOG on, slicer level 100mV, input 01 > R1/G1/B1/SOG1 as input (RGBS)
    writeOneByte(0xF0, 0);
    timeout = 6; // try this input a few times and look for a change
    readFromRegister(0x19, 1, &readout); // in hor. pulse width
    while (timeout-- > 0) {
      previous = readout;
      readFromRegister(0x19, 1, &readout);
      if (previous != readout) {
        rto->inputIsYpBpR = 0;
        syncFound = true;
        break;
      }
      delay(1);
    }
  }

  if (!syncFound) {
    Serial.print(F("no input with sync found\n"));
    writeOneByte(0xF0, 0);
    byte a = 0;
    for (byte b = 0; b < 100; b++) {
      readFromRegister(0x17, 1, &readout); // input htotal
      a += readout;
    }
    if (a == 0) {
      rto->sourceDisconnected = true;
      Serial.print(F("source is off\n"));
    }
  }

  if (syncFound && rto->inputIsYpBpR == true) {
    Serial.print(F("using RCA inputs\n"));
    rto->sourceDisconnected = false;
    applyYuvPatches();
  }
  else if (syncFound && rto->inputIsYpBpR == false) {
    Serial.print(F("using RGBS inputs\n"));
    rto->sourceDisconnected = false;
  }
}

uint8_t getSingleByteFromPreset(const uint8_t* programArray, unsigned int offset) {
  return pgm_read_byte(programArray + offset);
}

void zeroAll() {
  // turn processing units off first
  writeOneByte(0xF0, 0);
  writeOneByte(0x46, 0x00); // reset controls 1
  writeOneByte(0x47, 0x00); // reset controls 2

  // zero out entire register space
  for (int y = 0; y < 6; y++) {
    writeOneByte(0xF0, (uint8_t)y );
    for (int z = 0; z < 16; z++) {
      uint8_t bank[16];
      for (int w = 0; w < 16; w++) {
        bank[w] = 0;
      }
      writeBytes(z * 16, bank, 16);
    }
  }
}

void readFromRegister(uint8_t segment, uint8_t reg, int bytesToRead, uint8_t* output) {
  writeOneByte(0xF0, segment);
  readFromRegister(reg, bytesToRead, output);
}

void readFromRegister(uint8_t reg, int bytesToRead, uint8_t* output) {
  Wire.beginTransmission(GBS_ADDR);
  if (!Wire.write(reg)) {
    Serial.print(F("i2c error\n"));
  }

  Wire.endTransmission();
  Wire.requestFrom(GBS_ADDR, bytesToRead, true);
  int rcvBytes = 0;
  while (Wire.available()) {
    output[rcvBytes++] =  Wire.read();
  }

  if (rcvBytes != bytesToRead) {
    Serial.print(F("i2c error\n"));
  }
}

// dumps the current chip configuration in a format that's ready to use as new preset :)
void dumpRegisters(byte segment) {
  uint8_t readout = 0;
  if (segment > 5) return;
  writeOneByte(0xF0, segment);

  switch (segment) {
    case 0:
      for (int x = 0x40; x <= 0x5F; x++) {
        readFromRegister(x, 1, &readout);
        Serial.print(readout);
        Serial.println(",");
      }
      for (int x = 0x90; x <= 0x9F; x++) {
        readFromRegister(x, 1, &readout);
        Serial.print(readout);
        Serial.println(",");
      }
      break;
    case 1:
      for (int x = 0x0; x <= 0x8F; x++) {
        readFromRegister(x, 1, &readout);
        Serial.print(readout);
        Serial.println(",");
      }
      break;
    case 2:
      for (int x = 0x0; x <= 0x3F; x++) {
        readFromRegister(x, 1, &readout);
        Serial.print(readout);
        Serial.println(",");
      }
      break;
    case 3:
      for (int x = 0x0; x <= 0x7F; x++) {
        readFromRegister(x, 1, &readout);
        Serial.print(readout);
        Serial.println(",");
      }
      break;
    case 4:
      for (int x = 0x0; x <= 0x5F; x++) {
        readFromRegister(x, 1, &readout);
        Serial.print(readout);
        Serial.println(",");
      }
      break;
    case 5:
      for (int x = 0x0; x <= 0x6F; x++) {
        readFromRegister(x, 1, &readout);
        Serial.print(readout);
        Serial.println(",");
      }
      break;
  }
}

// required sections for reduced sets:
// S0_40 - S0_59 "misc"
// S1_00 - S1_2a "IF"
// S3_00 - S3_74 "VDS"
void dumpRegistersReduced() {
  uint8_t readout = 0;

  writeOneByte(0xF0, 0);
  for (int x = 0x40; x <= 0x59; x++) {
    readFromRegister(x, 1, &readout);
    Serial.print(readout);
    Serial.println(",");
  }

  writeOneByte(0xF0, 1);
  for (int x = 0x0; x <= 0x2a; x++) {
    readFromRegister(x, 1, &readout);
    Serial.print(readout);
    Serial.println(",");
  }

  writeOneByte(0xF0, 3);
  for (int x = 0x0; x <= 0x74; x++) {
    readFromRegister(x, 1, &readout);
    Serial.print(readout);
    Serial.println(",");
  }
}

void resetPLLAD() {
  uint8_t readout = 0;
  writeOneByte(0xF0, 5);
  readFromRegister(0x11, 1, &readout);
  readout &= ~(1 << 7); // latch off
  readout |= (1 << 0); // init vco voltage on
  readout &= ~(1 << 1); // lock off
  writeOneByte(0x11, readout);
  readFromRegister(0x11, 1, &readout);
  readout |= (1 << 7); // latch on
  readout &= 0xfe; // init vco voltage off
  writeOneByte(0x11, readout);
  readFromRegister(0x11, 1, &readout);
  readout |= (1 << 1); // lock on
  delay(2);
  writeOneByte(0x11, readout);
}

void resetPLL() {
  uint8_t readout = 0;
  writeOneByte(0xF0, 0);
  readFromRegister(0x43, 1, &readout);
  readout |= (1 << 2); // low skew
  readout &= ~(1 << 5); // initial vco voltage off
  writeOneByte(0x43, (readout & ~(1 << 5)));
  readFromRegister(0x43, 1, &readout);
  readout |= (1 << 4); // lock on
  delay(2);
  writeOneByte(0x43, readout); // main pll lock on
}

// soft reset cycle
// This restarts all chip units, which is sometimes required when important config bits are changed.
// Note: This leaves the main PLL uninitialized so issue a resetPLL() after this!
void resetDigital() {
  writeOneByte(0xF0, 0);
  writeOneByte(0x46, 0); writeOneByte(0x47, 0);
  writeOneByte(0x43, 0x20); delay(10); // initial VCO voltage
  resetPLL(); delay(10);
  writeOneByte(0x46, 0x3f); // all on except VDS (display enable)
  writeOneByte(0x47, 0x17); // all on except HD bypass
  Serial.print(F("resetDigital\n"));
}

// returns true when all SP parameters are reasonable
// This needs to be extended for supporting more video modes.
boolean getSyncProcessorSignalValid() {
  uint8_t register_low, register_high = 0;
  uint16_t register_combined = 0;
  boolean returnValue = false;
  boolean horizontalOkay = false;
  boolean verticalOkay = false;
  boolean hpwOkay = false;

  writeOneByte(0xF0, 0);
  readFromRegister(0x07, 1, &register_high); readFromRegister(0x06, 1, &register_low);
  register_combined =   (((uint16_t)register_high & 0x0001) << 8) | (uint16_t)register_low;

  // pal: 432, ntsc: 428, hdtv: 214?
  if (register_combined > 422 && register_combined < 438) {
    horizontalOkay = true;  // pal, ntsc 428-432
  }
  else if (register_combined > 205 && register_combined < 225) {
    horizontalOkay = true;  // hdtv 214
  }
  //else Serial.print("hor bad\n");

  readFromRegister(0x08, 1, &register_high); readFromRegister(0x07, 1, &register_low);
  register_combined = (((uint16_t(register_high) & 0x000f)) << 7) | (((uint16_t)register_low & 0x00fe) >> 1);
  if ((register_combined > 518 && register_combined < 530) && (horizontalOkay == true) ) {
    verticalOkay = true;  // ntsc
  }
  else if ((register_combined > 620 && register_combined < 632) && (horizontalOkay == true) ) {
    verticalOkay = true;  // pal
  }
  //else Serial.print("ver bad\n");

  readFromRegister(0x1a, 1, &register_high); readFromRegister(0x19, 1, &register_low);
  register_combined = (((uint16_t(register_high) & 0x000f)) << 8) | (uint16_t)register_low;
  if ( (register_combined < 180) && (register_combined > 5)) {
    hpwOkay = true;
  }
  else {
    //Serial.print("hpw bad: ");
    Serial.print(register_combined);
    Serial.print("\n");
  }

  if ((horizontalOkay == true) && (verticalOkay == true) && (hpwOkay == true)) {
    returnValue = true;
  }

  return returnValue;
}

void switchInputs() {
  uint8_t readout = 0;
  writeOneByte(0xF0, 5); readFromRegister(0x02, 1, &readout);
  writeOneByte(0x02, (readout & ~(1 << 6)));
}

void SyncProcessorOffOn() {
  uint8_t readout = 0;
  disableDeinterlacer(); delay(5); // hide the glitching
  writeOneByte(0xF0, 0);
  readFromRegister(0x47, 1, &readout);
  writeOneByte(0x47, readout & ~(1 << 2));
  readFromRegister(0x47, 1, &readout);
  writeOneByte(0x47, readout | (1 << 2));
  enableDeinterlacer();
}

void resetModeDetect() {
  uint8_t readout = 0, backup = 0;
  writeOneByte(0xF0, 1);
  readFromRegister(0x63, 1, &readout);
  backup = readout;
  writeOneByte(0x63, readout & ~(1 << 6));
  writeOneByte(0x63, readout | (1 << 6));
  writeOneByte(0x63, readout & ~(1 << 7));
  writeOneByte(0x63, readout | (1 << 7));
  writeOneByte(0x63, backup);
}

void shiftHorizontal(uint16_t amountToAdd, bool subtracting) {

  uint8_t hrstLow = 0x00;
  uint8_t hrstHigh = 0x00;
  uint16_t htotal = 0x0000;
  uint8_t hbstLow = 0x00;
  uint8_t hbstHigh = 0x00;
  uint16_t Vds_hb_st = 0x0000;
  uint8_t hbspLow = 0x00;
  uint8_t hbspHigh = 0x00;
  uint16_t Vds_hb_sp = 0x0000;

  // get HRST
  readFromRegister(0x03, 0x01, 1, &hrstLow);
  readFromRegister(0x02, 1, &hrstHigh);
  htotal = ( ( ((uint16_t)hrstHigh) & 0x000f) << 8) | (uint16_t)hrstLow;

  // get HBST
  readFromRegister(0x04, 1, &hbstLow);
  readFromRegister(0x05, 1, &hbstHigh);
  Vds_hb_st = ( ( ((uint16_t)hbstHigh) & 0x000f) << 8) | (uint16_t)hbstLow;

  // get HBSP
  hbspLow = hbstHigh;
  readFromRegister(0x06, 1, &hbspHigh);
  Vds_hb_sp = ( ( ((uint16_t)hbspHigh) & 0x00ff) << 4) | ( (((uint16_t)hbspLow) & 0x00f0) >> 4);

  // Perform the addition/subtraction
  if (subtracting) {
    Vds_hb_st -= amountToAdd;
    Vds_hb_sp -= amountToAdd;
  } else {
    Vds_hb_st += amountToAdd;
    Vds_hb_sp += amountToAdd;
  }

  // handle the case where hbst or hbsp have been decremented below 0
  if (Vds_hb_st & 0x8000) {
    Vds_hb_st = htotal % 2 == 1 ? (htotal + Vds_hb_st) + 1 : (htotal + Vds_hb_st);
  }
  if (Vds_hb_sp & 0x8000) {
    Vds_hb_sp = htotal % 2 == 1 ? (htotal + Vds_hb_sp) + 1 : (htotal + Vds_hb_sp);
  }

  // handle the case where hbst or hbsp have been incremented above htotal
  if (Vds_hb_st > htotal) {
    Vds_hb_st = htotal % 2 == 1 ? (Vds_hb_st - htotal) - 1 : (Vds_hb_st - htotal);
  }
  if (Vds_hb_sp > htotal) {
    Vds_hb_sp = htotal % 2 == 1 ? (Vds_hb_sp - htotal) - 1 : (Vds_hb_sp - htotal);
  }

  writeOneByte(0x04, (uint8_t)(Vds_hb_st & 0x00ff));
  writeOneByte(0x05, ((uint8_t)(Vds_hb_sp & 0x000f) << 4) | ((uint8_t)((Vds_hb_st & 0x0f00) >> 8)) );
  writeOneByte(0x06, (uint8_t)((Vds_hb_sp & 0x0ff0) >> 4) );
}

void shiftHorizontalLeft() {
  shiftHorizontal(4, true);
}

void shiftHorizontalRight() {
  shiftHorizontal(4, false);
}

void scaleHorizontalAbsolute(uint16_t value) {
  uint8_t high = 0x00;
  uint8_t newHigh = 0x00;
  uint8_t low = 0x00;
  uint8_t newLow = 0x00;

  readFromRegister(0x03, 0x16, 1, &low);
  readFromRegister(0x17, 1, &high);

  Serial.print(F("Scale Hor: "));
  Serial.print(value);
  Serial.print("\n");
  newHigh = (high & 0xfc) | (uint8_t)( (value / 256) & 0x0003);
  newLow = (uint8_t)(value & 0x00ff);

  writeOneByte(0x16, newLow);
  writeOneByte(0x17, newHigh);
}


void scaleHorizontal(uint16_t amountToAdd, bool subtracting) {
  uint8_t high = 0x00;
  uint8_t newHigh = 0x00;
  uint8_t low = 0x00;
  uint8_t newLow = 0x00;
  uint16_t newValue = 0x0000;

  readFromRegister(0x03, 0x16, 1, &low);
  readFromRegister(0x17, 1, &high);

  newValue = ( ( ((uint16_t)high) & 0x0003) * 256) + (uint16_t)low;

  if (subtracting && ((newValue - amountToAdd) > 0)) {
    newValue -= amountToAdd;
  } else if ((newValue + amountToAdd) <= 1023) {
    newValue += amountToAdd;
  }

  Serial.print(F("Scale Hor: "));
  Serial.print(newValue);
  Serial.print("\n");
  newHigh = (high & 0xfc) | (uint8_t)( (newValue / 256) & 0x0003);
  newLow = (uint8_t)(newValue & 0x00ff);

  writeOneByte(0x16, newLow);
  writeOneByte(0x17, newHigh);
}

void scaleHorizontalSmaller() {
  scaleHorizontal(1, false);
}

void scaleHorizontalLarger() {
  scaleHorizontal(1, true);
}

void moveHS(uint16_t amountToAdd, bool subtracting) {
  uint8_t high, low;
  uint16_t newST, newSP;

  writeOneByte(0xf0, 3);
  readFromRegister(0x0a, 1, &low);
  readFromRegister(0x0b, 1, &high);
  newST = ( ( ((uint16_t)high) & 0x000f) << 8) | (uint16_t)low;
  readFromRegister(0x0b, 1, &low);
  readFromRegister(0x0c, 1, &high);
  newSP = ( (((uint16_t)high) & 0x00ff) << 4) | ( (((uint16_t)low) & 0x00f0) >> 4);

  if (subtracting) {
    newST -= amountToAdd;
    newSP -= amountToAdd;
  } else {
    newST += amountToAdd;
    newSP += amountToAdd;
  }
  Serial.print("HSST: ");
  Serial.print(newST);
  Serial.print("\n");
  Serial.print(" HSSP: ");
  Serial.print(newSP);
  Serial.print("\n");

  writeOneByte(0x0a, (uint8_t)(newST & 0x00ff));
  writeOneByte(0x0b, ((uint8_t)(newSP & 0x000f) << 4) | ((uint8_t)((newST & 0x0f00) >> 8)) );
  writeOneByte(0x0c, (uint8_t)((newSP & 0x0ff0) >> 4) );
}

void moveVS(uint16_t amountToAdd, bool subtracting) {
  uint8_t regHigh, regLow;
  uint16_t newST, newSP, VDS_DIS_VB_ST, VDS_DIS_VB_SP;

  writeOneByte(0xf0, 3);
  // get VBST
  readFromRegister(3, 0x13, 1, &regLow);
  readFromRegister(3, 0x14, 1, &regHigh);
  VDS_DIS_VB_ST = (((uint16_t)regHigh & 0x0007) << 8) | ((uint16_t)regLow) ;
  // get VBSP
  readFromRegister(3, 0x14, 1, &regLow);
  readFromRegister(3, 0x15, 1, &regHigh);
  VDS_DIS_VB_SP = ((((uint16_t)regHigh & 0x007f) << 4) | ((uint16_t)regLow & 0x00f0) >> 4) ;

  readFromRegister(0x0d, 1, &regLow);
  readFromRegister(0x0e, 1, &regHigh);
  newST = ( ( ((uint16_t)regHigh) & 0x000f) << 8) | (uint16_t)regLow;
  readFromRegister(0x0e, 1, &regLow);
  readFromRegister(0x0f, 1, &regHigh);
  newSP = ( (((uint16_t)regHigh) & 0x00ff) << 4) | ( (((uint16_t)regLow) & 0x00f0) >> 4);

  if (subtracting) {
    if ((newST - amountToAdd) > VDS_DIS_VB_ST) {
      newST -= amountToAdd;
      newSP -= amountToAdd;
    } else Serial.print(F("limit!\n"));
  } else {
    if ((newSP + amountToAdd) < VDS_DIS_VB_SP) {
      newST += amountToAdd;
      newSP += amountToAdd;
    } else Serial.print(F("limit!\n"));
  }
  Serial.print("VSST: ");
  Serial.print(newST);
  Serial.print("\n");
  Serial.print(" VSSP: ");
  Serial.print(newSP);
  Serial.print("\n");

  writeOneByte(0x0d, (uint8_t)(newST & 0x00ff));
  writeOneByte(0x0e, ((uint8_t)(newSP & 0x000f) << 4) | ((uint8_t)((newST & 0x0f00) >> 8)) );
  writeOneByte(0x0f, (uint8_t)((newSP & 0x0ff0) >> 4) );
}

void invertHS() {
  uint8_t high, low;
  uint16_t newST, newSP;

  writeOneByte(0xf0, 3);
  readFromRegister(0x0a, 1, &low);
  readFromRegister(0x0b, 1, &high);
  newST = ( ( ((uint16_t)high) & 0x000f) << 8) | (uint16_t)low;
  readFromRegister(0x0b, 1, &low);
  readFromRegister(0x0c, 1, &high);
  newSP = ( (((uint16_t)high) & 0x00ff) << 4) | ( (((uint16_t)low) & 0x00f0) >> 4);

  uint16_t temp = newST;
  newST = newSP;
  newSP = temp;

  writeOneByte(0x0a, (uint8_t)(newST & 0x00ff));
  writeOneByte(0x0b, ((uint8_t)(newSP & 0x000f) << 4) | ((uint8_t)((newST & 0x0f00) >> 8)) );
  writeOneByte(0x0c, (uint8_t)((newSP & 0x0ff0) >> 4) );
}

void invertVS() {
  uint8_t high, low;
  uint16_t newST, newSP;

  writeOneByte(0xf0, 3);
  readFromRegister(0x0d, 1, &low);
  readFromRegister(0x0e, 1, &high);
  newST = ( ( ((uint16_t)high) & 0x000f) << 8) | (uint16_t)low;
  readFromRegister(0x0e, 1, &low);
  readFromRegister(0x0f, 1, &high);
  newSP = ( (((uint16_t)high) & 0x00ff) << 4) | ( (((uint16_t)low) & 0x00f0) >> 4);

  uint16_t temp = newST;
  newST = newSP;
  newSP = temp;

  writeOneByte(0x0d, (uint8_t)(newST & 0x00ff));
  writeOneByte(0x0e, ((uint8_t)(newSP & 0x000f) << 4) | ((uint8_t)((newST & 0x0f00) >> 8)) );
  writeOneByte(0x0f, (uint8_t)((newSP & 0x0ff0) >> 4) );
}

void scaleVerticalAbsolute(uint16_t value) {
  uint8_t high = 0x00;
  uint8_t newHigh = 0x00;
  uint8_t low = 0x00;
  uint8_t newLow = 0x00;

  readFromRegister(0x03, 0x18, 1, &high);
  readFromRegister(0x03, 0x17, 1, &low);
  Serial.print(F("Scale Vert: "));
  Serial.print(value);
  Serial.print("\n");
  newHigh = (uint8_t)(value >> 4);
  newLow = (low & 0x0f) | (((uint8_t)(value & 0x00ff)) << 4) ;
  writeOneByte(0x17, newLow);
  writeOneByte(0x18, newHigh);
}

void scaleVertical(uint16_t amountToAdd, bool subtracting) {
  uint8_t high = 0x00;
  uint8_t newHigh = 0x00;
  uint8_t low = 0x00;
  uint8_t newLow = 0x00;
  uint16_t newValue = 0x0000;

  readFromRegister(0x03, 0x18, 1, &high);
  readFromRegister(0x03, 0x17, 1, &low);
  newValue = ( (((uint16_t)high) & 0x007f) << 4) | ( (((uint16_t)low) & 0x00f0) >> 4);

  if (subtracting && ((newValue - amountToAdd) > 0)) {
    newValue -= amountToAdd;
  } else if ((newValue + amountToAdd) <= 1023) {
    newValue += amountToAdd;
  }

  Serial.print(F("Scale Vert: "));
  Serial.print(newValue);
  Serial.print("\n");
  newHigh = (uint8_t)(newValue >> 4);
  newLow = (low & 0x0f) | (((uint8_t)(newValue & 0x00ff)) << 4) ;

  writeOneByte(0x17, newLow);
  writeOneByte(0x18, newHigh);
}

void shiftVertical(uint16_t amountToAdd, bool subtracting) {

  uint8_t vrstLow;
  uint8_t vrstHigh;
  uint16_t vrstValue;
  uint8_t vbstLow;
  uint8_t vbstHigh;
  uint16_t vbstValue;
  uint8_t vbspLow;
  uint8_t vbspHigh;
  uint16_t vbspValue;

  // get VRST
  readFromRegister(0x03, 0x02, 1, &vrstLow);
  readFromRegister(0x03, 1, &vrstHigh);
  vrstValue = ( (((uint16_t)vrstHigh) & 0x007f) << 4) | ( (((uint16_t)vrstLow) & 0x00f0) >> 4);

  // get VBST
  readFromRegister(0x07, 1, &vbstLow);
  readFromRegister(0x08, 1, &vbstHigh);
  vbstValue = ( ( ((uint16_t)vbstHigh) & 0x0007) << 8) | (uint16_t)vbstLow;

  // get VBSP
  vbspLow = vbstHigh;
  readFromRegister(0x09, 1, &vbspHigh);
  vbspValue = ( ( ((uint16_t)vbspHigh) & 0x007f) << 4) | ( (((uint16_t)vbspLow) & 0x00f0) >> 4);

  if (subtracting) {
    vbstValue -= amountToAdd;
    vbspValue -= amountToAdd;
  } else {
    vbstValue += amountToAdd;
    vbspValue += amountToAdd;
  }

  // handle the case where hbst or hbsp have been decremented below 0
  if (vbstValue & 0x8000) {
    vbstValue = vrstValue + vbstValue;
  }
  if (vbspValue & 0x8000) {
    vbspValue = vrstValue + vbspValue;
  }

  // handle the case where vbst or vbsp have been incremented above vrstValue
  if (vbstValue > vrstValue) {
    vbstValue = vbstValue - vrstValue;
  }
  if (vbspValue > vrstValue) {
    vbspValue = vbspValue - vrstValue;
  }

  writeOneByte(0x07, (uint8_t)(vbstValue & 0x00ff));
  writeOneByte(0x08, ((uint8_t)(vbspValue & 0x000f) << 4) | ((uint8_t)((vbstValue & 0x0700) >> 8)) );
  writeOneByte(0x09, (uint8_t)((vbspValue & 0x07f0) >> 4) );
}

void shiftVerticalUp() {
  shiftVertical(4, true);
}

void shiftVerticalDown() {
  shiftVertical(4, false);
}

void setMemoryHblankStartPosition(uint16_t value) {
  uint8_t regLow, regHigh;
  regLow = (uint8_t)value;
  readFromRegister(3, 0x05, 1, &regHigh);
  regHigh = (regHigh & 0xf0) | (uint8_t)((value & 0x0f00) >> 8);
  writeOneByte(0x04, regLow);
  writeOneByte(0x05, regHigh);
}

void setMemoryHblankStopPosition(uint16_t value) {
  uint8_t regLow, regHigh;
  readFromRegister(3, 0x05, 1, &regLow);
  regLow = (regLow & 0x0f) | (uint8_t)((value & 0x000f) << 4);
  regHigh = (uint8_t)((value & 0x0ff0) >> 4);
  writeOneByte(0x05, regLow);
  writeOneByte(0x06, regHigh);
}

void getVideoTimings() {
  uint8_t  regLow;
  uint8_t  regHigh;

  uint16_t Vds_hsync_rst;
  uint16_t VDS_HSCALE;
  uint16_t Vds_vsync_rst;
  uint16_t VDS_VSCALE;
  uint16_t vds_dis_hb_st;
  uint16_t vds_dis_hb_sp;
  uint16_t VDS_HS_ST;
  uint16_t VDS_HS_SP;
  uint16_t VDS_DIS_VB_ST;
  uint16_t VDS_DIS_VB_SP;
  uint16_t VDS_DIS_VS_ST;
  uint16_t VDS_DIS_VS_SP;
  uint16_t MD_pll_divider;

  // get HRST
  readFromRegister(3, 0x01, 1, &regLow);
  readFromRegister(3, 0x02, 1, &regHigh);
  Vds_hsync_rst = (( ( ((uint16_t)regHigh) & 0x000f) << 8) | (uint16_t)regLow);
  Serial.print(F("htotal: "));
  Serial.print(Vds_hsync_rst);
  Serial.print("\n");

  // get horizontal scale up
  readFromRegister(3, 0x16, 1, &regLow);
  readFromRegister(3, 0x17, 1, &regHigh);
  VDS_HSCALE = (( ( ((uint16_t)regHigh) & 0x0003) << 8) | (uint16_t)regLow);
  Serial.print(F("VDS_HSCALE: "));
  Serial.print(VDS_HSCALE);
  Serial.print("\n");

  // get HS_ST
  readFromRegister(3, 0x0a, 1, &regLow);
  readFromRegister(3, 0x0b, 1, &regHigh);
  VDS_HS_ST = (( ( ((uint16_t)regHigh) & 0x000f) << 8) | (uint16_t)regLow);
  Serial.print(F("HS ST: "));
  Serial.print(VDS_HS_ST);
  Serial.print("\n");

  // get HS_SP
  readFromRegister(3, 0x0b, 1, &regLow);
  readFromRegister(3, 0x0c, 1, &regHigh);
  VDS_HS_SP = ( (((uint16_t)regHigh) << 4) | ((uint16_t)regLow & 0x00f0) >> 4);
  Serial.print(F("HS SP: "));
  Serial.print(VDS_HS_SP);
  Serial.print("\n");

  // get HBST
  readFromRegister(3, 0x10, 1, &regLow);
  readFromRegister(3, 0x11, 1, &regHigh);
  vds_dis_hb_st = (( ( ((uint16_t)regHigh) & 0x000f) << 8) | (uint16_t)regLow);
  Serial.print(F("HB ST (display): "));
  Serial.print(vds_dis_hb_st);
  Serial.print("\n");

  // get HBSP
  readFromRegister(3, 0x11, 1, &regLow);
  readFromRegister(3, 0x12, 1, &regHigh);
  vds_dis_hb_sp = ( (((uint16_t)regHigh) << 4) | ((uint16_t)regLow & 0x00f0) >> 4);
  Serial.print(F("HB SP (display): "));
  Serial.print(vds_dis_hb_sp);
  Serial.print("\n");

  // get HBST(memory)
  readFromRegister(3, 0x04, 1, &regLow);
  readFromRegister(3, 0x05, 1, &regHigh);
  vds_dis_hb_st = (( ( ((uint16_t)regHigh) & 0x000f) << 8) | (uint16_t)regLow);
  Serial.print(F("HB ST (memory): "));
  Serial.print(vds_dis_hb_st);
  Serial.print("\n");

  // get HBSP(memory)
  readFromRegister(3, 0x05, 1, &regLow);
  readFromRegister(3, 0x06, 1, &regHigh);
  vds_dis_hb_sp = ( (((uint16_t)regHigh) << 4) | ((uint16_t)regLow & 0x00f0) >> 4);
  Serial.print(F("HB SP (memory): "));
  Serial.print(vds_dis_hb_sp);
  Serial.print("\n");

  Serial.print(F("----\n"));
  // get VRST
  readFromRegister(3, 0x02, 1, &regLow);
  readFromRegister(3, 0x03, 1, &regHigh);
  Vds_vsync_rst = ( (((uint16_t)regHigh) & 0x007f) << 4) | ( (((uint16_t)regLow) & 0x00f0) >> 4);
  Serial.print(F("vtotal: "));
  Serial.print(Vds_vsync_rst);
  Serial.print("\n");

  // get vertical scale up
  readFromRegister(3, 0x17, 1, &regLow);
  readFromRegister(3, 0x18, 1, &regHigh);
  VDS_VSCALE = ( (((uint16_t)regHigh) & 0x007f) << 4) | ( (((uint16_t)regLow) & 0x00f0) >> 4);
  Serial.print(F("VDS_VSCALE: "));
  Serial.print(VDS_VSCALE);
  Serial.print("\n");

  // get V Sync Start
  readFromRegister(3, 0x0d, 1, &regLow);
  readFromRegister(3, 0x0e, 1, &regHigh);
  VDS_DIS_VS_ST = (((uint16_t)regHigh & 0x0007) << 8) | ((uint16_t)regLow) ;
  Serial.print(F("VS ST: "));
  Serial.print(VDS_DIS_VS_ST);
  Serial.print("\n");

  // get V Sync Stop
  readFromRegister(3, 0x0e, 1, &regLow);
  readFromRegister(3, 0x0f, 1, &regHigh);
  VDS_DIS_VS_SP = ((((uint16_t)regHigh & 0x007f) << 4) | ((uint16_t)regLow & 0x00f0) >> 4) ;
  Serial.print(F("VS SP: "));
  Serial.print(VDS_DIS_VS_SP);
  Serial.print("\n");

  // get VBST
  readFromRegister(3, 0x13, 1, &regLow);
  readFromRegister(3, 0x14, 1, &regHigh);
  VDS_DIS_VB_ST = (((uint16_t)regHigh & 0x0007) << 8) | ((uint16_t)regLow) ;
  Serial.print(F("VB ST (display): "));
  Serial.print(VDS_DIS_VB_ST);
  Serial.print("\n");

  // get VBSP
  readFromRegister(3, 0x14, 1, &regLow);
  readFromRegister(3, 0x15, 1, &regHigh);
  VDS_DIS_VB_SP = ((((uint16_t)regHigh & 0x007f) << 4) | ((uint16_t)regLow & 0x00f0) >> 4) ;
  Serial.print(F("VB SP (display): "));
  Serial.print(VDS_DIS_VB_SP);
  Serial.print("\n");

  // get VBST (memory)
  readFromRegister(3, 0x07, 1, &regLow);
  readFromRegister(3, 0x08, 1, &regHigh);
  VDS_DIS_VB_ST = (((uint16_t)regHigh & 0x0007) << 8) | ((uint16_t)regLow) ;
  Serial.print(F("VB ST (memory): "));
  Serial.print(VDS_DIS_VB_ST);
  Serial.print("\n");

  // get VBSP (memory)
  readFromRegister(3, 0x08, 1, &regLow);
  readFromRegister(3, 0x09, 1, &regHigh);
  VDS_DIS_VB_SP = ((((uint16_t)regHigh & 0x007f) << 4) | ((uint16_t)regLow & 0x00f0) >> 4) ;
  Serial.print(F("VB SP (memory): "));
  Serial.print(VDS_DIS_VB_SP);
  Serial.print("\n");

  // get Pixel Clock -- MD[11:0] -- must be smaller than 4096 --
  readFromRegister(5, 0x12, 1, &regLow);
  readFromRegister(5, 0x13, 1, &regHigh);
  MD_pll_divider = (( ( ((uint16_t)regHigh) & 0x000f) << 8) | (uint16_t)regLow);
  Serial.print(F("PLLAD divider: "));
  Serial.print(MD_pll_divider);
  Serial.print("\n");
}

//s0s41s85 wht 1800 wvt 1200 | pal 1280x???
//s0s41s85 wht 1800 wvt 1000 | ntsc 1280x1024
// memory blanking ntsc 1280x960 (htotal 1803): whbst 0 whbsp 314, then shift into frame
void set_htotal(uint16_t htotal) {
  uint8_t regLow, regHigh;

  // ModeLine "1280x960" 108.00 1280 1376 1488 1800 960 961 964 1000 +HSync +VSync
  // front porch: H2 - H1: 1376 - 1280
  // back porch : H4 - H3: 1800 - 1488
  // sync pulse : H3 - H2: 1488 - 1376
  // HB start: 1280 / 1800 = (32/45)
  // HB stop:  1800        = htotal
  // HS start: 1376 / 1800 = (172/225)
  // HS stop : 1488 / 1800 = (62/75)

  // hbst (memory) should always start before or exactly at hbst (display) for interlaced sources to look nice
  // .. at least in PAL modes. NTSC doesn't seem to be affected
  uint16_t h_blank_start_position = (uint16_t)((htotal * (32.0f / 45.0f)) + 1) & 0xfffe;
  uint16_t h_blank_stop_position =  0;  //(uint16_t)htotal;  // it's better to use 0 here, allows for easier masking
  uint16_t h_sync_start_position =  (uint16_t)((htotal * (172.0f / 225.0f)) + 1) & 0xfffe;
  uint16_t h_sync_stop_position =   (uint16_t)((htotal * (62.0f / 75.0f)) + 1) & 0xfffe;

  // Memory fetch locations should somehow be calculated with settings for line length in IF and/or buffer sizes in S4 (Capture Buffer)
  // just use something that works for now
  uint16_t h_blank_memory_start_position = h_blank_start_position - 1;
  uint16_t h_blank_memory_stop_position =  (uint16_t)htotal * (41.0f / 45.0f);

  writeOneByte(0xF0, 3);

  // write htotal
  regLow = (uint8_t)htotal;
  readFromRegister(3, 0x02, 1, &regHigh);
  regHigh = (regHigh & 0xf0) | (htotal >> 8);
  writeOneByte(0x01, regLow);
  writeOneByte(0x02, regHigh);

  // HS ST
  regLow = (uint8_t)h_sync_start_position;
  regHigh = (uint8_t)((h_sync_start_position & 0x0f00) >> 8);
  writeOneByte(0x0a, regLow);
  writeOneByte(0x0b, regHigh);

  // HS SP
  readFromRegister(3, 0x0b, 1, &regLow);
  regLow = (regLow & 0x0f) | ((uint8_t)(h_sync_stop_position << 4));
  regHigh = (uint8_t)((h_sync_stop_position) >> 4);
  writeOneByte(0x0b, regLow);
  writeOneByte(0x0c, regHigh);

  // HB ST
  regLow = (uint8_t)h_blank_start_position;
  regHigh = (uint8_t)((h_blank_start_position & 0x0f00) >> 8);
  writeOneByte(0x10, regLow);
  writeOneByte(0x11, regHigh);
  // HB ST(memory fetch)
  regLow = (uint8_t)h_blank_memory_start_position;
  regHigh = (uint8_t)((h_blank_memory_start_position & 0x0f00) >> 8);
  writeOneByte(0x04, regLow);
  writeOneByte(0x05, regHigh);

  // HB SP
  regHigh = (uint8_t)(h_blank_stop_position >> 4);
  readFromRegister(3, 0x11, 1, &regLow);
  regLow = (regLow & 0x0f) | ((uint8_t)(h_blank_stop_position << 4));
  writeOneByte(0x11, regLow);
  writeOneByte(0x12, regHigh);
  // HB SP(memory fetch)
  readFromRegister(3, 0x05, 1, &regLow);
  regLow = (regLow & 0x0f) | ((uint8_t)(h_blank_memory_stop_position << 4));
  regHigh = (uint8_t)(h_blank_memory_stop_position >> 4);
  writeOneByte(0x05, regLow);
  writeOneByte(0x06, regHigh);
}

void set_vtotal(uint16_t vtotal) {
  uint8_t regLow, regHigh;
  // ModeLine "1280x960" 108.00 1280 1376 1488 1800 960 961 964 1000 +HSync +VSync
  // front porch: V2 - V1: 961 - 960 = 1
  // back porch : V4 - V3: 1000 - 964 = 36
  // sync pulse : V3 - V2: 964 - 961 = 3
  // VB start: 960 / 1000 = (24/25)
  // VB stop:  1000        = vtotal
  // VS start: 961 / 1000 = (961/1000)
  // VS stop : 964 / 1000 = (241/250)

  uint16_t v_blank_start_position = vtotal * (24.0f / 25.0f);
  uint16_t v_blank_stop_position = vtotal;
  uint16_t v_sync_start_position = vtotal * (961.0f / 1000.0f);
  uint16_t v_sync_stop_position = vtotal * (241.0f / 250.0f);

  // write vtotal
  writeOneByte(0xF0, 3);
  regHigh = (uint8_t)(vtotal >> 4);
  readFromRegister(3, 0x02, 1, &regLow);
  regLow = ((regLow & 0x0f) | (uint8_t)(vtotal << 4));
  writeOneByte(0x03, regHigh);
  writeOneByte(0x02, regLow);

  // NTSC 60Hz: 60 kHz ModeLine "1280x960" 108.00 1280 1376 1488 1800 960 961 964 1000 +HSync +VSync
  // V-Front Porch: 961-960 = 1  = 0.1% of vtotal. Start at v_blank_start_position = vtotal - (vtotal*0.04) = 960
  // V-Back Porch:  1000-964 = 36 = 3.6% of htotal (black top lines)
  // -> vbi = 3.7 % of vtotal | almost all on top (> of 0 (vtotal+1 = 0. It wraps.))
  // vblank interval PAL would be more

  regLow = (uint8_t)v_sync_start_position;
  regHigh = (uint8_t)((v_sync_start_position & 0x0700) >> 8);
  writeOneByte(0x0d, regLow); // vs mixed
  writeOneByte(0x0e, regHigh); // vs stop
  readFromRegister(3, 0x0e, 1, &regLow);
  readFromRegister(3, 0x0f, 1, &regHigh);
  regLow = regLow | (uint8_t)(v_sync_stop_position << 4);
  regHigh = (uint8_t)(v_sync_stop_position >> 4);
  writeOneByte(0x0e, regLow); // vs mixed
  writeOneByte(0x0f, regHigh); // vs stop

  // VB ST
  regLow = (uint8_t)v_blank_start_position;
  readFromRegister(3, 0x14, 1, &regHigh);
  regHigh = (uint8_t)((regHigh & 0xf8) | (uint8_t)((v_blank_start_position & 0x0700) >> 8));
  writeOneByte(0x13, regLow);
  writeOneByte(0x14, regHigh);
  //VB SP
  regHigh = (uint8_t)(v_blank_stop_position >> 4);
  readFromRegister(3, 0x14, 1, &regLow);
  regLow = ((regLow & 0x0f) | (uint8_t)(v_blank_stop_position << 4));
  writeOneByte(0x15, regHigh);
  writeOneByte(0x14, regLow);

  // VB ST (memory) to v_blank_start_position, VB SP (memory): v_blank_stop_position - 2
  // guide says: if vscale enabled, vb (memory) stop -=2, else keep it | scope readings look best with -= 2.
  regLow = (uint8_t)v_blank_start_position;
  regHigh = (uint8_t)(v_blank_start_position >> 8);
  writeOneByte(0x07, regLow);
  writeOneByte(0x08, regHigh);
  readFromRegister(3, 0x08, 1, &regLow);
  regLow = (regLow & 0x0f) | (uint8_t)(v_blank_stop_position - 2) << 4;
  regHigh = (uint8_t)((v_blank_stop_position - 2) >> 4);
  writeOneByte(0x08, regLow);
  writeOneByte(0x09, regHigh);
}

void aquireSyncLock() {
  long outputLength = 1;
  long inputLength = 1;
  long difference = 99999; // shortcut
  long prev_difference;
  uint8_t regLow, regHigh, readout;
  uint16_t hbsp, htotal, backupHTotal, bestHTotal = 1;

  // test if we get the vsync signal (wire is connected, display output is working)
  // this remembers a positive result via VSYNCconnected
  if (rto->VSYNCconnected == false) {
    if (pulseIn(vsyncInPin, HIGH, 100000) != 0) {
      if  (pulseIn(vsyncInPin, LOW, 100000) != 0) {
        rto->VSYNCconnected = true;
      }
    }
    else {
      Serial.print(F("VSYNC not connected \n"));
      rto->VSYNCconnected = false;
      rto->syncLockEnabled = false;
      return;
    }
  }

  writeOneByte(0xF0, 0);
  readFromRegister(0x4f, 1, &readout);
  writeOneByte(0x4f, readout | (1 << 7));
  delay(2);

  long highTest1, highTest2;
  long lowTest1, lowTest2;

  // input field time
  noInterrupts();
  highTest1 = pulseIn(vsyncInPin, HIGH, 90000);
  highTest2 = pulseIn(vsyncInPin, HIGH, 90000);
  lowTest1 = pulseIn(vsyncInPin, LOW, 90000);
  lowTest2 = pulseIn(vsyncInPin, LOW, 90000);
  interrupts();

  inputLength = ((highTest1 + highTest2) / 2);
  inputLength += ((lowTest1 + lowTest2) / 2);

  writeOneByte(0xF0, 0);
  readFromRegister(0x4f, 1, &readout);
  writeOneByte(0x4f, readout & ~(1 << 7));
  delay(2);

  // current output field time
  noInterrupts();
  lowTest1 = pulseIn(vsyncInPin, LOW, 90000);
  lowTest2 = pulseIn(vsyncInPin, LOW, 90000);
  highTest1 = pulseIn(vsyncInPin, HIGH, 90000); // now these are short pulses
  highTest2 = pulseIn(vsyncInPin, HIGH, 90000);
  interrupts();

  long highPulse = ((highTest1 + highTest2) / 2);
  long lowPulse = ((lowTest1 + lowTest2) / 2);
  outputLength = lowPulse + highPulse;

  Serial.print(F("in field time: "));
  Serial.print(inputLength);
  Serial.print("\n");
  Serial.print(F("out field time: "));
  Serial.print(outputLength);
  Serial.print("\n");

  // shortcut to exit if in and out are close
  int inOutDiff = outputLength - inputLength;
  if ( abs(inOutDiff) < 7) {
    rto->syncLockFound = true;
    return;
  }

  writeOneByte(0xF0, 3);
  readFromRegister(3, 0x01, 1, &regLow);
  readFromRegister(3, 0x02, 1, &regHigh);
  htotal = (( ( ((uint16_t)regHigh) & 0x000f) << 8) | (uint16_t)regLow);
  backupHTotal = htotal;
  Serial.print(F(" Start HTotal: "));
  Serial.print(htotal);
  Serial.print("\n");

  // start looking at an htotal value at or slightly below anticipated target
  htotal = ((float)(htotal) / (float)(outputLength)) * (float)(inputLength);

  uint8_t attempts = 40;
  while (attempts-- > 0) {
    writeOneByte(0xF0, 3);
    regLow = (uint8_t)htotal;
    readFromRegister(3, 0x02, 1, &regHigh);
    regHigh = (regHigh & 0xf0) | (htotal >> 8);
    writeOneByte(0x01, regLow);
    writeOneByte(0x02, regHigh);
    delay(1);
    noInterrupts();
    outputLength = pulseIn(vsyncInPin, LOW, 90000) + highPulse;
    interrupts();
    prev_difference = difference;
    difference = (outputLength > inputLength) ? (outputLength - inputLength) : (inputLength - outputLength);
    Serial.print(htotal);
    Serial.print(": ");
    Serial.print(difference);
    Serial.print("\n");

    if (difference == prev_difference) {
      // best value is last one, exit
      bestHTotal = htotal - 1;
      break;
    }
    else if (difference < prev_difference) {
      bestHTotal = htotal;
    }
    else {
      // increasing again? we have the value, exit
      break;
    }

    htotal += 1;
  }

  writeOneByte(0xF0, 3);
  regLow = (uint8_t)bestHTotal;
  readFromRegister(3, 0x02, 1, &regHigh);
  regHigh = (regHigh & 0xf0) | (bestHTotal >> 8);
  writeOneByte(0x01, regLow);
  writeOneByte(0x02, regHigh);

  // changing htotal shifts the canvas with in the frame. Correct this now.
  int toShiftPixels = backupHTotal - bestHTotal;
  if (toShiftPixels >= 0 && toShiftPixels < 80) {
    Serial.print("shifting ");
    Serial.print(toShiftPixels);
    Serial.print(" pixels left\n");
    shiftHorizontal(toShiftPixels, true); // true = left
  }
  else if (toShiftPixels < 0 && toShiftPixels > -80) {
    Serial.print("shifting ");
    Serial.print(-toShiftPixels);
    Serial.print(" pixels right\n");
    shiftHorizontal(-toShiftPixels, false); // false = right
  }

  // HTotal might now be outside horizontal blank pulse
  readFromRegister(3, 0x01, 1, &regLow);
  readFromRegister(3, 0x02, 1, &regHigh);
  htotal = (( ( ((uint16_t)regHigh) & 0x000f) << 8) | (uint16_t)regLow);
  // safety
  if (htotal > backupHTotal) {
    if ((htotal - backupHTotal) > 400) { // increased from 30 to 400 (54mhz psx)
      Serial.print("safety triggered upper ");
      Serial.print(htotal - backupHTotal);
      Serial.print("\n");
      regLow = (uint8_t)backupHTotal;
      readFromRegister(3, 0x02, 1, &regHigh);
      regHigh = (regHigh & 0xf0) | (backupHTotal >> 8);
      writeOneByte(0x01, regLow);
      writeOneByte(0x02, regHigh);
      htotal = backupHTotal;
    }
  }
  else if (htotal < backupHTotal) {
    if ((backupHTotal - htotal) > 400) { // increased from 30 to 400 (54mhz psx)
      Serial.print("safety triggered lower ");
      Serial.print(backupHTotal - htotal);
      Serial.print("\n");
      regLow = (uint8_t)backupHTotal;
      readFromRegister(3, 0x02, 1, &regHigh);
      regHigh = (regHigh & 0xf0) | (backupHTotal >> 8);
      writeOneByte(0x01, regLow);
      writeOneByte(0x02, regHigh);
      htotal = backupHTotal;
    }
  }

  readFromRegister(3, 0x11, 1, &regLow);
  readFromRegister(3, 0x12, 1, &regHigh);
  hbsp = ( (((uint16_t)regHigh) << 4) | ((uint16_t)regLow & 0x00f0) >> 4);

  Serial.print(F(" End HTotal: "));
  Serial.print(htotal);
  Serial.print("\n");

  if ( htotal <= hbsp  ) {
    hbsp = htotal - 1;
    hbsp &= 0xfffe;
    regHigh = (uint8_t)(hbsp >> 4);
    readFromRegister(3, 0x11, 1, &regLow);
    regLow = (regLow & 0x0f) | ((uint8_t)(hbsp << 4));
    writeOneByte(0x11, regLow);
    writeOneByte(0x12, regHigh);
    //setMemoryHblankStartPosition( Vds_hsync_rst - 8 );
    //setMemoryHblankStopPosition( (Vds_hsync_rst  * (73.0f / 338.0f) + 2 ) );
  }

  rto->syncLockFound = true;
}

void enableDebugPort() {
  writeOneByte(0xf0, 0);
  writeOneByte(0x48, 0xeb);
  writeOneByte(0x4D, 0x2a);
  writeOneByte(0xf0, 0x05);
  writeOneByte(0x63, 0x0f);
}

void doPostPresetLoadSteps() {
  if (rto->inputIsYpBpR == true) {
    Serial.print("(YUV)");
    applyYuvPatches();
    rto->currentLevelSOG = 12; // do this here, gets applied next line
  }
  if (rto->videoStandardInput == 3) {
    Serial.print(F("HDTV mode \n"));
    scaleVerticalAbsolute(1023); // temporary
    scaleHorizontalAbsolute(708); // temporary
    shiftHorizontal(4 * 22, false);
  }
  if (rto->videoStandardInput == 4) {
    Serial.print(F("HDTV mode \n"));
    scaleVerticalAbsolute(1023); // temporary
    scaleHorizontalAbsolute(573); // temporary
    shiftHorizontal(4 * 22, false);
  }
  setSOGLevel( rto->currentLevelSOG );
  resetDigital();
  delay(50);
  byte result = getVideoMode();
  byte timeout = 255;
  while (result == 0 && --timeout > 0) {
    result = getVideoMode();
    delay(2);
  }
  if (timeout == 0) {
    Serial.print(F("sync lost \n"));
    rto->videoStandardInput = 0;
    return;
  }
  setClampPosition();
  enableDebugPort();
  resetPLL();
  enableVDS();
  delay(10);
  resetPLLAD();
  delay(10);
  resetSyncLock();
  rto->modeDetectInReset = false;
  LEDOFF; // in case LED was on
  Serial.print(F("post preset done \n"));
}

void applyPresets(byte videoMode) {
  if (videoMode == 1) {
    Serial.print(F("NTSC timing \n"));
    if (uopt->presetPreference == 0) {
      if (widescreenSwitchEnabled == true) {
        writeProgramArrayNew(ntsc_widescreen);
      } else {
        writeProgramArrayNew(ntsc_fullscreen);
      }
    } else if (uopt->presetPreference == 1) {
      writeProgramArrayNew(ntsc_feedbackclock);
    }
    rto->videoStandardInput = 1;
    doPostPresetLoadSteps();
  } else if (videoMode == 2) {
    Serial.print(F("PAL timing \n"));
    if (uopt->presetPreference == 0) {
      if (widescreenSwitchEnabled == true) {
        writeProgramArrayNew(pal_widescreen);
      } else {
        writeProgramArrayNew(pal_fullscreen);
      }
    } else if (uopt->presetPreference == 1) {
      writeProgramArrayNew(pal_feedbackclock);
    }
    rto->videoStandardInput = 2;
    doPostPresetLoadSteps();
  } else if (videoMode == 1) {
    Serial.print(F("NTSC timing \n"));
    if (uopt->presetPreference == 0) {
      if (widescreenSwitchEnabled == true) {
        writeProgramArrayNew(ntsc_widescreen);
      } else {
        writeProgramArrayNew(ntsc_fullscreen);
      }
    }
    else if (uopt->presetPreference == 1) {
      writeProgramArrayNew(ntsc_feedbackclock);
    }
    rto->videoStandardInput = 1;
    doPostPresetLoadSteps();
  } else if (videoMode == 3) {
    Serial.print(F("HDTV NTSC timing \n"));
    if (uopt->presetPreference == 0) {
      if (widescreenSwitchEnabled == true) {
        writeProgramArrayNew(ntsc_widescreen);
      } else {
        writeProgramArrayNew(ntsc_fullscreen);
      }
    }
    else if (uopt->presetPreference == 1) {
      writeProgramArrayNew(ntsc_feedbackclock);
    }
    rto->videoStandardInput = 3;
    doPostPresetLoadSteps();
  } else if (videoMode == 4) {
    Serial.print(F("HDTV NTSC timing \n"));
    if (uopt->presetPreference == 0) {
      if (widescreenSwitchEnabled == true) {
        writeProgramArrayNew(pal_widescreen);
      } else {
        writeProgramArrayNew(pal_fullscreen);
      }
    }
    else if (uopt->presetPreference == 1) {
      writeProgramArrayNew(pal_feedbackclock);
    }
    rto->videoStandardInput = 4;
    doPostPresetLoadSteps();
  } else {
    Serial.print(F("Unknown timing! \n"));
    inputAndSyncDetect();
    setSOGLevel( random(0, 31) ); // try a random(min, max) sog level, hopefully find some sync
    resetModeDetect();
    delay(300); // and give MD some time
    rto->videoStandardInput = 0; // mark as "no sync" for syncwatcher
  }
}

void enableDeinterlacer() {
  uint8_t readout = 0;
  writeOneByte(0xf0, 0);
  readFromRegister(0x46, 1, &readout);
  writeOneByte(0x46, readout | (1 << 1));
  rto->deinterlacerWasTurnedOff = false;
}

void disableDeinterlacer() {
  uint8_t readout = 0;
  writeOneByte(0xf0, 0);
  readFromRegister(0x46, 1, &readout);
  writeOneByte(0x46, readout & ~(1 << 1));
  rto->deinterlacerWasTurnedOff = true;
}

void disableVDS() {
  uint8_t readout = 0;
  writeOneByte(0xf0, 0);
  readFromRegister(0x46, 1, &readout);
  writeOneByte(0x46, readout & ~(1 << 6));
}

void enableVDS() {
  uint8_t readout = 0;
  writeOneByte(0xf0, 0);
  readFromRegister(0x46, 1, &readout);
  writeOneByte(0x46, readout | (1 << 6));
}

// example for using the gbs8200 onboard buttons in an interrupt routine
void IFdown() {
  rto->IFdown = true;
  delay(45); // debounce
}

void resetSyncLock() {
  if (rto->syncLockEnabled) {
    rto->syncLockFound = false;
  }
}

static byte getVideoMode() {
  writeOneByte(0xF0, 0);
  byte detectedMode = 0;
  readFromRegister(0x00, 1, &detectedMode);
  //return detectedMode;
  if (detectedMode & 0x08) return 1; // ntsc
  if (detectedMode & 0x20) return 2; // pal
  if (detectedMode & 0x10) return 3; // hdtv ntsc progressive
  if (detectedMode & 0x40) return 4; // hdtv pal progressive
  return 0; // unknown mode
}

boolean getSyncStable() {
  uint8_t readout = 0;
  writeOneByte(0xF0, 0);
  readFromRegister(0x00, 1, &readout);
  if (readout & 0x04) { // H + V sync both stable
    return true;
  }
  return false;
}

void setParametersIF() {
  uint16_t register_combined;
  uint8_t register_high, register_low;
  writeOneByte(0xF0, 0);
  // get detected vlines (will be around 625 PAL / 525 NTSC)
  readFromRegister(0x08, 1, &register_high); readFromRegister(0x07, 1, &register_low);
  register_combined = (((uint16_t(register_high) & 0x000f)) << 7) | (((uint16_t)register_low & 0x00fe) >> 1);

  // update IF vertical blanking stop position
  register_combined -= 2; // but leave some line as safety (black screen glitch protection)
  writeOneByte(0xF0, 1);
  writeOneByte(0x1e, (uint8_t)register_combined);
  writeOneByte(0x1f, (uint8_t)(register_combined >> 8));

  // IF vertical blanking start position should be in the loaded preset
}

void setSamplingStart(uint8_t samplingStart) {
  writeOneByte(0xF0, 1);
  writeOneByte(0x26, samplingStart);
}

void advancePhase() {
  uint8_t readout;
  writeOneByte(0xF0, 5);
  readFromRegister(0x18, 1, &readout);
  readout &= ~(1 << 7); // latch off
  writeOneByte(0x18, readout);
  readFromRegister(0x18, 1, &readout);
  byte level = (readout & 0x3e) >> 1;
  level += 4; level &= 0x1f;
  readout = (readout & 0xc1) | (level << 1); readout |= (1 << 0);
  writeOneByte(0x18, readout);

  readFromRegister(0x18, 1, &readout);
  readout |= (1 << 7);
  writeOneByte(0x18, readout);
  readFromRegister(0x18, 1, &readout);
  Serial.print(F("ADC phase: "));
  Serial.print(readout, HEX);
  Serial.print("\n");
}

void setPhaseSP() {

  uint8_t readout = 0;
  uint8_t complete = 0;

  writeOneByte(0xF0, 5);
  readFromRegister(0x19, 1, &readout);
  readout &= ~(1 << 7); // latch off
  writeOneByte(0x19, readout);

  readout = rto->phaseSP << 1;
  readout |= (1 << 0);
  writeOneByte(0x19, readout); // write this first
  // new phase is now ready. it will go in effect when the latch bit gets toggled
  readFromRegister(0x19, 1, &readout);
  readout |= (1 << 7);

  writeOneByte(0xF0, 0);
  uint16_t timeout = 3000;
  do {
    readFromRegister(0x10, 1, &complete);
    timeout--;
  } while (((complete & 0x10) == 0) && timeout > 0);

  writeOneByte(0xF0, 5);
  writeOneByte(0x19, readout);
  if (timeout == 0) {
    Serial.print("timeout in setPhaseSP\n");
  }
}

void setPhaseADC() {

  uint8_t readout = 0;
  uint8_t complete = 0;

  writeOneByte(0xF0, 5);
  readFromRegister(0x18, 1, &readout);
  readout &= ~(1 << 7); // latch off
  writeOneByte(0x18, readout);

  readout = rto->phaseADC << 1;
  readout |= (1 << 0);
  writeOneByte(0x18, readout); // write this first
  // new phase is now ready. it will go in effect when the latch bit gets toggled
  readFromRegister(0x18, 1, &readout);
  readout |= (1 << 7);

  writeOneByte(0xF0, 0);
  uint16_t timeout = 3000;
  do {
    readFromRegister(0x10, 1, &complete);
    timeout--;
  } while (((complete & 0x10) == 0) && timeout > 0);

  writeOneByte(0xF0, 5);
  writeOneByte(0x18, readout);
  if (timeout == 0) {
    Serial.print("timeout in setPhaseADC\n");
  }
}

void setClampPosition() {
  if (rto->inputIsYpBpR) {
    return;
  } else {
    uint8_t register_high, register_low;
    uint16_t hpw, htotal, clampPositionStart, clampPositionStop;

    writeOneByte(0xF0, 0);
    readFromRegister(0x1a, 1, &register_high); readFromRegister(0x19, 1, &register_low);
    hpw = (((uint16_t(register_high) & 0x000f)) << 8) | (uint16_t)register_low;
    readFromRegister(0x18, 1, &register_high); readFromRegister(0x17, 1, &register_low);
    htotal = (((uint16_t(register_high) & 0x000f)) << 8) | (uint16_t)register_low;

    clampPositionStart = ((htotal - hpw) + 20) & 0xfff8;
    clampPositionStop = (htotal - 20) & 0xfff8;
    Serial.print(" clampPositionStart: ");
    Serial.print(clampPositionStart);
    Serial.print("\n");
    Serial.print(" clampPositionStop: ");
    Serial.print(clampPositionStop);
    Serial.print("\n");
    register_high = clampPositionStart >> 8;
    register_low = (uint8_t)clampPositionStart;
    writeOneByte(0xF0, 5);
    writeOneByte(0x41, register_low); writeOneByte(0x42, register_high);

    register_high = clampPositionStop >> 8;
    register_low = (uint8_t)clampPositionStop;
    writeOneByte(0x43, register_low); writeOneByte(0x44, register_high);
  }
}

void applyYuvPatches() {   // also does color mixing changes
  uint8_t readout;

  writeOneByte(0xF0, 5);
  readFromRegister(0x03, 1, &readout);
  writeOneByte(0x03, readout | (1 << 1)); // midlevel clamp red
  readFromRegister(0x03, 1, &readout);
  writeOneByte(0x03, readout | (1 << 3)); // midlevel clamp blue
  writeOneByte(0x56, 0x01); //sog mode on, clamp source 27mhz, no sync inversion, clamp manual off! (for yuv only, bit 2)
  writeOneByte(0x06, 0x3f); //adc R offset
  writeOneByte(0x07, 0x3f); //adc G offset
  writeOneByte(0x08, 0x3f); //adc B offset

  writeOneByte(0xF0, 1);
  readFromRegister(0x00, 1, &readout);
  writeOneByte(0x00, readout | (1 << 1)); // rgb matrix bypass

  writeOneByte(0xF0, 3); // for colors
  writeOneByte(0x35, 0x7a); writeOneByte(0x3a, 0xfa); writeOneByte(0x36, 0x18);
  writeOneByte(0x3b, 0x02); writeOneByte(0x37, 0x22); writeOneByte(0x3c, 0x02);
}

// undo yuvpatches if necessary
void applyRGBPatches() {
  //uint8_t readout;
  rto->currentLevelSOG = 10;
  setSOGLevel( rto->currentLevelSOG );
}

void setup() {
  Serial.begin(57600); // up from 57600
  Serial.setTimeout(10);
  Serial.print(F("starting...\n"));

  pinMode(BUTTON1, INPUT);
  pinMode(BUTTON2, INPUT);
  pinMode(BUTTON3, INPUT);
  pinMode(SWITCH1, INPUT);
  digitalWrite(BUTTON1, HIGH);
  digitalWrite(BUTTON2, HIGH);
  digitalWrite(BUTTON3, HIGH);
  digitalWrite(SWITCH1, HIGH);

  // user options // todo: save/load from EEPROM
  uopt->presetPreference = 0;
  // run time options
  rto->syncLockEnabled = true;  // automatically find the best horizontal total pixel value for a given input timing
  rto->syncWatcher = true;  // continously checks the current sync status. issues resets if necessary
  rto->phaseADC = 16; // 0 to 31
  rto->phaseSP = 10; // 0 to 31
  rto->samplingStart = 3; // holds S1_26

  // the following is just run time variables. don't change!
  rto->currentLevelSOG = 10;
  rto->inputIsYpBpR = false;
  rto->videoStandardInput = 0;
  rto->deinterlacerWasTurnedOff = false;
  rto->modeDetectInReset = false;
  rto->syncLockFound = false;
  rto->VSYNCconnected = false;
  rto->IFdown = false;
  rto->printInfos = false;
  rto->sourceDisconnected = false;

  pinMode(vsyncInPin, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  LEDON; // enable the LED, lets users know the board is starting up

  // example for using the gbs8200 onboard buttons in an interrupt routine
  //pinMode(2, INPUT); // button for IFdown
  //attachInterrupt(digitalPinToInterrupt(2), IFdown, FALLING);

  delay(1000); // give the 5725 some time to start up. this adds to the Arduino bootloader delay.

  Wire.begin();
  // The i2c wire library sets pullup resistors on by default. Disable this so that 5V MCUs aren't trying to drive the 3.3V bus.
  digitalWrite(SCL, LOW);
  digitalWrite(SDA, LOW);
  Wire.setClock(400000); // TV5725 supports 400kHz
  delay(2);


#ifdef REGISTER_DUMP
  uint8_t temp = 0;
  writeOneByte(0xF0, 1);
  readFromRegister(0xF0, 1, &temp);
  while (temp != 1) { // is the 5725 up yet?
    writeOneByte(0xF0, 1);
    readFromRegister(0xF0, 1, &temp);
    Serial.print(F("5725 not responding\n"));
    delay(500);
  }

  Serial.print("Dumping registers... \n\nconst uint8_t dump[] PROGMEM = {\n");
  for (int segment = 0; segment <= 5; segment++) {
    dumpRegisters(segment);
  }
  Serial.print("};\n");
#endif

#ifndef REGISTER_DUMP
  disableVDS();
  writeProgramArrayNew(minimal_startup); // bring the chip up for input detection
  resetDigital();
  delay(250);
  inputAndSyncDetect();
  delay(500);

  byte videoMode = getVideoMode();
  byte timeout = 255;
  while (videoMode == 0 && --timeout > 0) {
    if ((timeout % 5) == 0) Serial.print(".");
    videoMode = getVideoMode();
    delay(1);
  }

  if (timeout > 0 && videoMode != 0) {
    applyPresets(videoMode);
    delay(1000); // at least 750ms required to become stable
  }

  // prepare for synclock
  videoMode = getVideoMode();
  timeout = 255;
  while (videoMode == 0 && --timeout > 0) {
    if ((timeout % 5) == 0) Serial.print(".");
    videoMode = getVideoMode();
    delay(1);
  }
  // sync should be stable now
  if ((videoMode != 0) && rto->syncLockEnabled == true && rto->syncLockFound == false && rto->videoStandardInput != 0) {
    aquireSyncLock();
  }
#endif

  Serial.print("Loading UserPresets from EEPROM... \n");
  imageFunctionToggle = EEPROM.read(0);
  
  if(imageFunctionToggle > 3  || imageFunctionToggle < 0) {
    imageFunctionToggle = 0;
  }

  rto->samplingStart = EEPROM.read(1);
  //if(rto->samplingStart > 6 || rto->samplingStart < 1) {
  //  rto->samplingStart = 1;
  //}
  setSamplingStart(rto->samplingStart);

  globalCommand = 0; // web server uses this to issue commands
  Serial.print(F("done!\nStartup complete! \nMCU: "));
  Serial.print(F_CPU);
  Serial.print("\n");
  LEDOFF; // startup done, disable the LED
}

bool widescreenSwitchEnabledOldValue = false;
bool button1HoldDown = false;

void loop() {

#ifndef REGISTER_DUMP
  static uint8_t readout = 0;
  static uint8_t segment = 0;
  static uint8_t inputRegister = 0;
  static uint8_t inputToogleBit = 0;
  static uint8_t inputStage = 0;
  static uint8_t register_low, register_high = 0;
  static uint16_t register_combined = 0;
  static uint16_t noSyncCounter = 0;
  static uint16_t signalInputChangeCounter = 0;
  static unsigned long lastTimeSyncWatcher = millis();
  static unsigned long lastTimeMDWatchdog = millis();


  bool button1pressed = (digitalRead(BUTTON1) == LOW);
  bool button2pressed = (digitalRead(BUTTON2) == LOW);
  bool button3pressed = (digitalRead(BUTTON3) == LOW);

  widescreenSwitchEnabled = (digitalRead(SWITCH1) == HIGH);

  if ((widescreenSwitchEnabled != widescreenSwitchEnabledOldValue) || (button2pressed && button3pressed)) {
    if (widescreenSwitchEnabled == true) {
      writeProgramArrayNew(pal_widescreen);
    } else {
      writeProgramArrayNew(pal_fullscreen);
    }
    widescreenSwitchEnabledOldValue = widescreenSwitchEnabled;
    doPostPresetLoadSteps();
    button1pressed = false;
    button2pressed = false;
    button3pressed = false;
    delay(500);
  }

  if (button1pressed) {
    button1HoldDown = true; // set it to true, but do not set it back when releasing the button.
  }

  if (button1HoldDown == true && (button2pressed || button3pressed)) {
    if (button2pressed) {
      rto->samplingStart++;
      setSamplingStart(rto->samplingStart);
      Serial.print(F("sampling start: "));
      Serial.print(rto->samplingStart);
      Serial.print("\n");
      button2pressed = false;
      EEPROM.write(1, rto->samplingStart);
    }

    if (button3pressed) {
      rto->samplingStart--;
      setSamplingStart(rto->samplingStart);
      Serial.print(F("sampling start: "));
      Serial.print(rto->samplingStart);
      Serial.print("\n");
      button3pressed = false;
      EEPROM.write(1, rto->samplingStart);
    }
    button1pressed = false;
    button1HoldDown = false;
    LEDON;
    delay(100);
    LEDOFF;
    delay(100);
    LEDON;
    delay(100);
    LEDOFF;
    delay(100);
    LEDON;
    delay(100);
    LEDOFF;
  }


  // is button1 released? AND was pressed before?
  if (button1HoldDown == true && button1pressed == false) {  // toggle between scaling up/down and moving up/down
    imageFunctionToggle++;
    Serial.print("Function: ");
    Serial.print(imageFunctionToggle);
    Serial.print("\n");
    EEPROM.write(0, imageFunctionToggle);
    button1HoldDown = false;
    LEDON;
    delay(100);
    LEDOFF;
    delay(100);
    LEDON;
    delay(100);
    LEDOFF;
    delay(100);
    LEDON;
    delay(100);
    LEDOFF;
  }

  if (imageFunctionToggle > 3) {
    imageFunctionToggle = 0;
  }

  if (button2pressed || button3pressed) {
    switch (imageFunctionToggle) {
      case 0:
        if (button2pressed) {
          shiftVerticalDown();
          delay(100);
        }
        if (button3pressed) {
          shiftVerticalUp();
          delay(100);
        }
        break;
      case 1:
        if (button2pressed) {
          scaleVertical(1, false);
          delay(100);
        }
        if (button3pressed) {
          scaleVertical(1, true);
          delay(100);
        }
        break;
      case 2:
        if (button2pressed) {
          shiftHorizontalLeft();
          delay(100);
        }
        if (button3pressed) {
          shiftHorizontalRight();
          delay(100);
        }
        break;
      case 3:
        if (button2pressed) {
          scaleHorizontalSmaller();
          delay(100);
        }
        if (button3pressed) {
          scaleHorizontalLarger();
          delay(100);
        }
        break;
    }
  }

  if (Serial.available() || globalCommand != 0) {
    switch (globalCommand == 0 ? Serial.read() : globalCommand) {
      case ' ':
        // skip spaces
        inputStage = 0; // reset this as well
        break;
      case 'd':
        for (int segment = 0; segment <= 5; segment++) {
          dumpRegisters(segment);
        }
        Serial.print("};\n");
        break;
      case '+':
        Serial.print(F("shift hor. +\n"));
        shiftHorizontalRight();
        break;
      case '-':
        Serial.print(F("shift hor. -\n"));
        shiftHorizontalLeft();
        break;
      case '*':
        Serial.print(F("shift vert. +\n"));
        shiftVerticalUp();
        break;
      case '/':
        Serial.print(F("shift vert. -\n"));
        shiftVerticalDown();
        break;
      case 'z':
        Serial.print(F("scale+\n"));
        scaleHorizontalLarger();
        break;
      case 'h':
        Serial.print(F("scale-\n"));
        scaleHorizontalSmaller();
        break;
      case 'q':
        resetDigital();
        enableVDS();
        Serial.print(F("resetDigital()\n"));
        break;
      case 'y':
        break;
      case 'p':
        break;
      case 'k':
        {
          static boolean sp_passthrough_enabled = false;
          if (!sp_passthrough_enabled) {
            writeOneByte(0xF0, 0);
            readFromRegister(0x4f, 1, &readout);
            writeOneByte(0x4f, readout | (1 << 7));
            // clock output (for measurment)
            readFromRegister(0x4f, 1, &readout);
            writeOneByte(0x4f, readout | (1 << 4));
            readFromRegister(0x49, 1, &readout);
            writeOneByte(0x49, readout & ~(1 << 1));

            sp_passthrough_enabled = true;
          }
          else {
            writeOneByte(0xF0, 0);
            readFromRegister(0x4f, 1, &readout);
            writeOneByte(0x4f, readout & ~(1 << 7));
            sp_passthrough_enabled = false;
          }
        }
        break;
      case 'e':
        if (widescreenSwitchEnabled == true) {
          Serial.print(F("ntsc preset widescreen\n"));
          writeProgramArrayNew(ntsc_widescreen);
        } else {
          Serial.print(F("ntsc preset fullscreen\n"));
          writeProgramArrayNew(ntsc_fullscreen);
        }
        doPostPresetLoadSteps();
        break;
      case 'r':
        if (widescreenSwitchEnabled == true) {
          Serial.print(F("pal preset widescreen\n"));
          writeProgramArrayNew(pal_widescreen);
        } else {
          Serial.print(F("pal preset fullscreen\n"));
          writeProgramArrayNew(pal_fullscreen);
        }
        doPostPresetLoadSteps();
        break;
      case '.':
        rto->syncLockFound = !rto->syncLockFound;
        break;
      case 'j':
        resetPLL(); resetPLLAD();
        break;
      case 'v':
        fuzzySPWrite();
        SyncProcessorOffOn();
        break;
      case 'b':
        advancePhase(); resetPLLAD();
        break;
      case 'n':
        {
          writeOneByte(0xF0, 5);
          readFromRegister(0x12, 1, &readout);
          writeOneByte(0x12, readout + 1);
          readFromRegister(0x12, 1, &readout);
          Serial.print(F("PLL divider: "));
          Serial.print(readout, HEX);
          Serial.print("\n");
          resetPLLAD();
        }
        break;
      case 'a':
        {
          uint8_t regLow, regHigh;
          uint16_t htotal;
          writeOneByte(0xF0, 3);
          readFromRegister(3, 0x01, 1, &regLow);
          readFromRegister(3, 0x02, 1, &regHigh);
          htotal = (( ( ((uint16_t)regHigh) & 0x000f) << 8) | (uint16_t)regLow);
          htotal++;
          regLow = (uint8_t)(htotal);
          regHigh = (regHigh & 0xf0) | ((htotal) >> 8);
          writeOneByte(0x01, regLow);
          writeOneByte(0x02, regHigh);
          Serial.print(F("HTotal++: "));
          Serial.print(htotal);
          Serial.print("\n");
        }
        break;
      case 'm':
        Serial.print(F("syncwatcher + autoIF "));
        if (rto->syncWatcher == true) {
          rto->syncWatcher = false;
          Serial.print(F("off\n"));
        }
        else {
          rto->syncWatcher = true;
          Serial.print(F("on\n"));
        }
        break;
      case ',':
        Serial.print(F("----\n"));
        getVideoTimings();
        break;
      case 'i':
        rto->printInfos = !rto->printInfos;
        break;
      case 'u':
        break;
      case 'f':
        Serial.print(F("show noise\n"));
        writeOneByte(0xF0, 5);
        writeOneByte(0x03, 1);
        writeOneByte(0xF0, 3);
        writeOneByte(0x44, 0xf8);
        writeOneByte(0x45, 0xff);
        break;
      case 'l':
        Serial.print(F("l - spOffOn\n"));
        SyncProcessorOffOn();
        break;
      case 'Q':
        setPhaseSP();
        break;
      case 'W':
        setPhaseADC();
        break;
      case 'E':
        rto->phaseADC += 1; rto->phaseADC &= 0x1f;
        rto->phaseSP += 1; rto->phaseSP &= 0x1f;
        Serial.print("ADC: ");
        Serial.print(rto->phaseADC);
        Serial.print("\n");
        Serial.print(" SP: ");
        Serial.print(rto->phaseSP);
        Serial.print("\n");
        break;
      case '0':
        moveHS(1, true);
        break;
      case '1':
        moveHS(1, false);
        break;
      case '2':
        //writeProgramArrayNew(vclktest);
        writeProgramArrayNew(pal_feedbackclock); // ModeLine "720x576@50" 27 720 732 795 864 576 581 586 625 -hsync -vsync
        doPostPresetLoadSteps();
        break;
      case '3':
        writeProgramArrayNew(ofw_ypbpr);
        doPostPresetLoadSteps();
        break;
      case '4':
        scaleVertical(1, true);
        break;
      case '5':
        scaleVertical(1, false);
        break;
      case '6':
        moveVS(1, true);
        break;
      case '7':
        moveVS(1, false);
        break;
      case '8':
        Serial.print(F("invert sync\n"));
        invertHS(); invertVS();
        break;
      case '9':
        writeProgramArrayNew(ntsc_feedbackclock);
        //writeProgramArrayNew(rgbhv);
        //writeProgramArrayNew(ypbpr_1080i);
        doPostPresetLoadSteps();
        break;
      case 'o':
        {
          static byte OSRSwitch = 0;
          if (OSRSwitch == 0) {
            Serial.print("OSR 1x\n"); // oversampling ratio
            writeOneByte(0xF0, 5);
            writeOneByte(0x16, 0xa0);
            writeOneByte(0x00, 0xc0);
            writeOneByte(0x1f, 0x07);
            resetPLL(); resetPLLAD();
            OSRSwitch = 1;
          }
          else if (OSRSwitch == 1) {
            Serial.print("OSR 2x\n");
            writeOneByte(0xF0, 5);
            writeOneByte(0x16, 0x6f);
            writeOneByte(0x00, 0xd0);
            writeOneByte(0x1f, 0x05);
            resetPLL(); resetPLLAD();
            OSRSwitch = 2;
          }
          else {
            Serial.print("OSR 4x\n");
            writeOneByte(0xF0, 5);
            writeOneByte(0x16, 0x2f);
            writeOneByte(0x00, 0xd8);
            writeOneByte(0x1f, 0x04);
            resetPLL(); resetPLLAD();
            OSRSwitch = 0;
          }
        }
        break;
      case 'g':
        inputStage++;
        Serial.flush();
        // we have a multibyte command
        if (inputStage > 0) {
          if (inputStage == 1) {
            segment = Serial.parseInt();
            Serial.print("segment: ");
            Serial.print(segment);
            Serial.print("\n");
          }
          else if (inputStage == 2) {
            char szNumbers[3];
            szNumbers[0] = Serial.read(); szNumbers[1] = Serial.read(); szNumbers[2] = '\0';
            char * pEnd;
            inputRegister = strtol(szNumbers, &pEnd, 16);
            Serial.print("register: ");
            Serial.print(inputRegister, HEX);
            Serial.print("\n");
            if (segment <= 5) {
              writeOneByte(0xF0, segment);
              readFromRegister(inputRegister, 1, &readout);
              Serial.print(F("register value is: "));
              Serial.print(readout, HEX);
              Serial.print("\n");
            }
            else {
              Serial.print(F("abort\n"));
            }
            inputStage = 0;
          }
        }
        break;
      case 's':
        inputStage++;
        Serial.flush();
        // we have a multibyte command
        if (inputStage > 0) {
          if (inputStage == 1) {
            segment = Serial.parseInt();
            Serial.print("segment: ");
            Serial.print(segment);
            Serial.print("\n");
          }
          else if (inputStage == 2) {
            char szNumbers[3];
            szNumbers[0] = Serial.read(); szNumbers[1] = Serial.read(); szNumbers[2] = '\0';
            char * pEnd;
            inputRegister = strtol(szNumbers, &pEnd, 16);
            Serial.print("register: ");
            Serial.print(inputRegister);
            Serial.print("\n");
          }
          else if (inputStage == 3) {
            char szNumbers[3];
            szNumbers[0] = Serial.read(); szNumbers[1] = Serial.read(); szNumbers[2] = '\0';
            char * pEnd;
            inputToogleBit = strtol (szNumbers, &pEnd, 16);
            if (segment <= 5) {
              writeOneByte(0xF0, segment);
              readFromRegister(inputRegister, 1, &readout);
              Serial.print("was: ");
              Serial.print(readout, HEX);
              Serial.print("\n");
              writeOneByte(inputRegister, inputToogleBit);
              readFromRegister(inputRegister, 1, &readout);
              Serial.print("is now: ");
              Serial.print(readout, HEX);
              Serial.print("\n");
            }
            else {
              Serial.print(F("abort\n"));
            }
            inputStage = 0;
          }
        }
        break;
      case 't':
        inputStage++;
        Serial.flush();
        // we have a multibyte command
        if (inputStage > 0) {
          if (inputStage == 1) {
            segment = Serial.parseInt();
            Serial.print(F("toggle bit segment: "));
            Serial.print(segment);
            Serial.print("\n");
          }
          else if (inputStage == 2) {
            char szNumbers[3];
            szNumbers[0] = Serial.read(); szNumbers[1] = Serial.read(); szNumbers[2] = '\0';
            char * pEnd;
            inputRegister = strtol (szNumbers, &pEnd, 16);
            Serial.print("toggle bit register: ");
            Serial.print(inputRegister, HEX);
            Serial.print("\n");
          }
          else if (inputStage == 3) {
            inputToogleBit = Serial.parseInt();
            Serial.print(F(" inputToogleBit: "));
            Serial.print(inputToogleBit);
            Serial.print("\n");
            inputStage = 0;
            if ((segment <= 5) && (inputToogleBit <= 7)) {
              writeOneByte(0xF0, segment);
              readFromRegister(inputRegister, 1, &readout);
              Serial.print("was: ");
              Serial.print(readout, HEX);
              Serial.print("\n");
              writeOneByte(inputRegister, readout ^ (1 << inputToogleBit));
              readFromRegister(inputRegister, 1, &readout);
              Serial.print("is now: ");
              Serial.print(readout, HEX);
              Serial.print("\n");
            }
            else {
              Serial.print(F("abort\n"));
            }
          }
        }
        break;
      case 'w':
        {
          inputStage++;
          Serial.flush();
          uint16_t value = 0;
          if (inputStage == 1) {
            String what = Serial.readStringUntil(' ');
            if (what.length() > 4) {
              Serial.print(F("abort\n"));
              inputStage = 0;
              break;
            }
            value = Serial.parseInt();
            if (value < 4096) {
              Serial.print(F("\nset "));
              Serial.print(what);
              Serial.print(" ");
              Serial.print(value);
              Serial.print("\n");
              if (what.equals("ht")) {
                set_htotal(value);
              }
              else if (what.equals("vt")) {
                set_vtotal(value);
              }
              else if (what.equals("hbst")) {
                setMemoryHblankStartPosition(value);
              }
              else if (what.equals("hbsp")) {
                setMemoryHblankStopPosition(value);
              }
              else if (what.equals("sog")) {
                setSOGLevel(value);
              }
            }
            else {
              Serial.print(F("abort\n"));
            }
            inputStage = 0;
          }
        }
        break;
      case 'x':
        rto->samplingStart++;
        setSamplingStart(rto->samplingStart);
        Serial.print(F("sampling start: "));
        Serial.print(rto->samplingStart);
        Serial.print("\n");
        break;
      default:
        Serial.print(F("command not understood\n"));
        inputStage = 0;
        while (Serial.available()) Serial.read(); // eat extra characters
        break;
    }
  }
  globalCommand = 0; // in case the web server had this set

  // poll sync status continously
  if ((rto->sourceDisconnected == false) && (rto->syncWatcher == true) && ((millis() - lastTimeSyncWatcher) > 60)) {
    byte videoMode = getVideoMode();
    boolean doChangeVideoMode = false;

    if (videoMode == 0) {
      noSyncCounter++;
      signalInputChangeCounter = 0; // needs some field testing > seems to be fine!
    }
    else if (videoMode != rto->videoStandardInput) { // ntsc/pal switch or similar
      noSyncCounter = 0;
      signalInputChangeCounter++;
    }
    else if (noSyncCounter > 0) { // videoMode is rto->videoStandardInput
      noSyncCounter--;
    }

    // PAL PSX consoles have a quirky reset cycle. They will boot up in NTSC mode up until right before the logo shows.
    // Avoiding constant mode switches would be good. Set signalInputChangeCounter to above 55 for that.
    if (signalInputChangeCounter >= 3 ) { // video mode has changed
      Serial.print(F("New Input!\n"));
      rto->videoStandardInput = 0;
      signalInputChangeCounter = 0;
      doChangeVideoMode = true;
    }

    // debug
    if (noSyncCounter > 0 ) {
      if (noSyncCounter < 3) {
        Serial.print(".");
      }
      else if (noSyncCounter % 10 == 0) {
        Serial.print(".");
      }
    }

    if (noSyncCounter >= 80 ) { // ModeDetect reports nothing
      Serial.print(F("No Sync!\n"));
      disableVDS();
      inputAndSyncDetect();
      setSOGLevel( random(0, 31) ); // try a random(min, max) sog level, hopefully find some sync
      resetModeDetect();
      delay(300); // and give MD some time
      //rto->videoStandardInput = 0;
      signalInputChangeCounter = 0;
      noSyncCounter = 0; // speed up sog change attempts by not zeroing this here
    }

    if ( (doChangeVideoMode == true) && (rto->videoStandardInput == 0) ) {
      byte temp = 250;
      while (videoMode == 0 && temp-- > 0) {
        delay(1);
        videoMode = getVideoMode();
      }
      boolean isValid = getSyncProcessorSignalValid();
      if (videoMode > 0 && isValid) { // ensures this isn't an MD glitch
        applyPresets(videoMode);
        delay(600);
        noSyncCounter = 0;
      }
      else if (videoMode > 0 && !isValid) Serial.print(F("MD Glitch!\n"));
    }

    // ModeDetect can get stuck in the last mode when console is powered off
    if ((millis() - lastTimeMDWatchdog) > 3000) {
      if ( (rto->videoStandardInput > 0) && !getSyncProcessorSignalValid() && (rto->modeDetectInReset == false) ) {
        delay(40);
        if (!getSyncProcessorSignalValid()) { // check a second time; avoids glitches
          Serial.print("ModeDetect stuck!\n");
          resetModeDetect(); resetModeDetect();
          delay(200);
          byte another_test = 0;
          for ( byte i = 0; i < 10; i++) {
            if (getVideoMode() == 0) { // due to resetModeDetect(), this now works
              another_test++;
            }
          }

          if (another_test > 7) {
            disableVDS(); // pretty sure now that the source has been turned off
            SyncProcessorOffOn();
            rto->videoStandardInput = 0;
            rto->modeDetectInReset = true;
            inputAndSyncDetect();
            //noSyncCounter = 60; // speed up sync detect attempts
          }
        }
      }
      lastTimeMDWatchdog = millis();
    }

    setParametersIF(); // continously update, so offsets match when switching from progressive to interlaced modes

    lastTimeSyncWatcher = millis();
  }

  if (rto->printInfos == true) { // information mode
    writeOneByte(0xF0, 0);

    //horizontal pixels:
    readFromRegister(0x07, 1, &register_high); readFromRegister(0x06, 1, &register_low);
    register_combined =   (((uint16_t)register_high & 0x0001) << 8) | (uint16_t)register_low;
    Serial.print("h:");
    Serial.print(register_combined);
    Serial.print(" ");

    //vertical line number:
    readFromRegister(0x08, 1, &register_high); readFromRegister(0x07, 1, &register_low);
    register_combined = (((uint16_t(register_high) & 0x000f)) << 7) | (((uint16_t)register_low & 0x00fe) >> 1);
    Serial.print("v:");
    Serial.print(register_combined);

    // PLLAD and PLL648 lock indicators
    readFromRegister(0x09, 1, &register_high);
    register_low = (register_high & 0x80) ? 1 : 0;
    register_low |= (register_high & 0x40) ? 2 : 0;
    Serial.print(" PLL:");
    Serial.print(register_low);

    // status
    readFromRegister(0x05, 1, &register_high);
    Serial.print(" status:");
    Serial.print(register_high, HEX);

    // video mode, according to MD
    Serial.print(" mode:");
    Serial.print(getVideoMode(), HEX);

    writeOneByte(0xF0, 5);
    readFromRegister(0x09, 1, &readout);
    Serial.print(" ADC:");
    Serial.print(readout, HEX);

    writeOneByte(0xF0, 0);
    readFromRegister(0x1a, 1, &register_high); readFromRegister(0x19, 1, &register_low);
    register_combined = (((uint16_t(register_high) & 0x000f)) << 8) | (uint16_t)register_low;
    Serial.print(" hpw:");
    Serial.print(register_combined); // horizontal pulse width

    readFromRegister(0x18, 1, &register_high); readFromRegister(0x17, 1, &register_low);
    register_combined = (((uint16_t(register_high) & 0x000f)) << 8) | (uint16_t)register_low;
    Serial.print(" htotal:");
    Serial.print(register_combined);

    readFromRegister(0x1c, 1, &register_high); readFromRegister(0x1b, 1, &register_low);
    register_combined = (((uint16_t(register_high) & 0x0007)) << 8) | (uint16_t)register_low;
    Serial.print(" vtotal:");
    Serial.print(register_combined);

    Serial.print("\n");
  } // end information mode

  if (rto->IFdown == true) {
    rto->IFdown = false;
    writeOneByte(0xF0, 1);
    readFromRegister(0x1e, 1, &readout);
    //if (readout > 0) // just underflow
    {
      writeOneByte(0x1e, readout - 1);
      Serial.print(readout - 1);
      Serial.print("\n");
    }
  }

  // only run this when sync is stable!
  if (rto->syncLockEnabled == true && rto->syncLockFound == false && getSyncStable() && rto->videoStandardInput != 0) {
    aquireSyncLock();
  }

  if (rto->sourceDisconnected == true) { // keep looking for new input
    writeOneByte(0xF0, 0);
    byte a = 0;
    for (byte b = 0; b < 20; b++) {
      readFromRegister(0x17, 1, &readout); // input htotal
      a += readout;
    }
    if (a == 0) {
      rto->sourceDisconnected = true;
    } else {
      rto->sourceDisconnected = false;
    }
  }
#endif
}

