#include <EEPROM.h>

const bool __ENABLE_DEBUG__ = true;

// CONST
const int PIN_STATE = 13;
const int SERIAL_BOND = 9600;
const byte CONTROL_START = 0xEF;
const byte CONTROL_END = 0xFE;
const int BYTES_TO_READ = 10;
const int USER_CODE_BUFFER_LEN = 10;
const int EEPROM_SIZE = 512;// bytes

// variables for reading from Serial
bool readingUserCode = false;
bool userCodeReadFinished = false;
byte userCodeBuffer[USER_CODE_BUFFER_LEN];
size_t userCodeBufferLen = 0;

// variables for writing to EEPROM
int eeAddress = 0;

// program count
int pc = 0;

// interpret and executing
byte *userCodeUnderExecuting = 0;
bool firstExecuting = false; // set true when read from serial finished, set false when read from ROM

void setup() {
  // put your setup code here, to run once:
  pinMode(PIN_STATE, OUTPUT);

  clearEEPROM();

  Serial.begin(SERIAL_BOND);
  // do not need to wait for serial port as serial may connect at any time
  //  while (!Serial) {
  //    ; // wait for serial port to connect. Needed for native USB port only
  //  }

  digitalWrite(PIN_STATE, LOW);
}

void loop() {

  checkAndReadUserCode();
  if (readingUserCode) {
//    Serial.println("loop return, as reading user code.");
    resetPC();
    return;
  }

  if (!checkIfUserCodeInEEPROM()) {
    return;
  }

  if (firstExecuting) {
    readUserCodeFromROM();  
  }

  executeUserCode();
  
}

void executeUserCode() {
  if (!userCodeUnderExecuting){
    return;  
  }
  // interpret
  debug("executing code: ", "OVER", userCodeUnderExecuting, eeAddress);

  pc++;
  if (pc >= EEPROM.length())
    pc = 0;
}

void readUserCodeFromROM() {
  // release memory which is used by previous user codes
  if (!userCodeUnderExecuting){
    delete userCodeUnderExecuting;  
  }

  // no user code to be read
  if (eeAddress <= 0) {
    userCodeUnderExecuting = 0;
    return;
  }

  // read from ROM
  userCodeUnderExecuting = new byte[eeAddress];
  for (int index = 0 ; index <= eeAddress ; index++) {
    byte code = EEPROM.read(index);
    debug("Read from ROM: ", "OVER", code);
    //Add one to each cell in the EEPROM
    userCodeUnderExecuting[index] = code;
  }  

  firstExecuting = false;
}

bool checkIfUserCodeInEEPROM() {
  byte code = EEPROM.read(0);
  if (code) {
    return true;
  }

  return false;
}

void checkAndReadUserCode() {
  memclear(userCodeBuffer, USER_CODE_BUFFER_LEN);
  // reply only when you receive data:
  if (Serial.available() > 0) {
    userCodeBufferLen = Serial.readBytes(userCodeBuffer, BYTES_TO_READ);
    debug("Read user code's size:", String(userCodeBufferLen), " BYTES, Over");
    debug("Read user codes: ", "OVER", userCodeBuffer, userCodeBufferLen);

    if (!readingUserCode) { // waiting for CONTROL_START single
      if (userCodeBufferLen >= 1) {
        byte fistByte = userCodeBuffer[0];
        debug("Got fistByte: ", "Over", fistByte);
        if (fistByte == CONTROL_START) { // CONTROL_START single came?
          debug("Got CONTROL_START: ", "Over", fistByte);
          doSomethingWhenStartReadingUserCode();
        }
      }
      else {// waiting for CONTROL_START single but no recognized single came, do nothing
        // do nothing
      }
    }

    if (readingUserCode) {
      if (userCodeBufferLen > 1) {
        for (int i=0; i<userCodeBufferLen; i++) {
          userCodeBuffer[i] = userCodeBuffer[i+1];
        }
        userCodeBuffer[userCodeBufferLen-1] = 0x00;
        userCodeBufferLen--;

        debug("process remain codes.");
      }
      else {
        userCodeBufferLen = Serial.readBytes(userCodeBuffer, BYTES_TO_READ);
        debug("Read user codes: ", "OVER", userCodeBuffer, userCodeBufferLen);
      }

      if (userCodeBufferLen > 0 ) {
        // check if CONTROL_END single contained
        // if contained, do not save it to EEPROM
        byte lastByte = userCodeBuffer[userCodeBufferLen - 1];
        
        bool controlEndSingleContained = false;
        if (lastByte == CONTROL_END) {
          debug("Got CONTROL_END: ", "Over", lastByte);
          controlEndSingleContained = true;
        }

        if (controlEndSingleContained) {
          byte* bufferWithoutEndCtrl = new byte[userCodeBufferLen - 1];
          for (int j=0; j<userCodeBufferLen - 1; j++) {
            bufferWithoutEndCtrl[j] = userCodeBuffer[j];
            EEPROM.write(eeAddress, bufferWithoutEndCtrl[j]);
            eeAddress++;
          }
          
          debug("Write codes to ROM: ", " Over", bufferWithoutEndCtrl, userCodeBufferLen - 1);
          debug("eeAddress is ", "", String(eeAddress));

          delete bufferWithoutEndCtrl;

          doSomethingWhenFinishReadingUserCode();// reset pc, reset readingUserCode flag
        }
        else {
          for (int j=0; j<userCodeBufferLen; j++) {
            EEPROM.write(eeAddress, userCodeBuffer[j]);
            eeAddress++;
          }

          debug("Write codes to ROM: ", " Over", userCodeBuffer, userCodeBufferLen);
          debug("eeAddress is ", "", String(eeAddress));
          
          if (eeAddress >= EEPROM_SIZE -1) {
            doSomethingWhenFinishReadingUserCode();
            clearEEPROM();
          }
        }
      }
      else {
        debug("readingUserCode, but no code read");
      }
    }
  }
}

void memclear(byte mem[], const int len) {
  for (int i; i<len; i++)
    mem[i]=0;
}

void doSomethingWhenStartReadingUserCode() {
  debug("doSomethingWhenStartReadingUserCode");

  eeAddress = 0;
  readingUserCode = true; // yes, set start reading user code flag
  clearEEPROM();
}

void doSomethingWhenFinishReadingUserCode() {
  debug("doSomethingWhenFinishReadingUserCode");

  resetPC();
  readingUserCode = false;

  firstExecuting = true;

  unsetLight();
}

void resetPC() {
  pc = 0;
}

void clearEEPROM() {
  for (int i = 0 ; i < EEPROM.length() ; i++) { // clear EEPOM
    EEPROM.write(i, 0);
  }
}

// - - - - - _
void lightInitState() {
  // put your main code here, to run repeatedly:
  digitalWrite(PIN_STATE, HIGH);
  delay(5000);

  digitalWrite(PIN_STATE, LOW);
  delay(500);
}

// - _ - _
void lightReadingUserCode() {
  // put your main code here, to run repeatedly:
  digitalWrite(PIN_STATE, HIGH);
  delay(50);
  digitalWrite(PIN_STATE, LOW);
  delay(50);
}

// - - _
void lightExecutingUserCodeState() {
  // put your main code here, to run repeatedly:
  digitalWrite(PIN_STATE, HIGH);
  delay(500);
  digitalWrite(PIN_STATE, LOW);
  delay(50);
}

void unsetLight() {
  digitalWrite(PIN_STATE, LOW);
}

void debug(String content) {
  if (__ENABLE_DEBUG__) {
    Serial.println(content); 
  }
}

void debug(String prefix, String content, String suffix) {
  if (__ENABLE_DEBUG__) {
    Serial.print(prefix);
    Serial.print(content);
    Serial.println(suffix);
  }
}

void debug(String prefix, String suffix, byte buf[], const int len) {
  if (__ENABLE_DEBUG__) {
    Serial.print(prefix);
    for (int i=0; i<len; i++){
      Serial.print(buf[i], HEX);
    }
    Serial.print(" ");
    Serial.println(suffix);
  }
}

void debug(String prefix, String suffix, byte b) {
  if (__ENABLE_DEBUG__) {
    Serial.print(prefix);
    Serial.print(" 0x");
    Serial.print(b, HEX);
    Serial.print(" ");
    Serial.println(suffix);
  }
}
