#include <EEPROM.h>

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

  if (!firstExecuting) {
    readUserCodeFromROM();  
  }

  executeUserCode();
  
}

void executeUserCode() {
  // interpret
  for (int i=0; i<=eeAddress; i++){
    Serial.print(*(userCodeUnderExecuting+i), HEX);  
  }
  Serial.println(" :=> OVER");

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
    //Add one to each cell in the EEPROM
    *(userCodeUnderExecuting+index) = EEPROM.read(index);
  }  

  firstExecuting = false;
}

bool checkIfUserCodeInEEPROM() {
  byte code = EEPROM.read(0);
  Serial.print("EEPROM code first byte: 0x");Serial.println(code, HEX);
  if (code) {
//    Serial.println("user codes exist in EEPROM");
    return true;
  }

//  Serial.println("no user codes exist in EEPROM");
  return false;
}

void checkAndReadUserCode() {
  // reply only when you receive data:
  if (Serial.available() > 0) {
    userCodeBufferLen = Serial.readBytes(userCodeBuffer, BYTES_TO_READ);

    if (!readingUserCode) { // waiting for CONTROL_START single
      if (userCodeBufferLen >= 1) {
        byte fistByte = userCodeBuffer[0];
        if (!fistByte ^ CONTROL_START) { // CONTROL_START single came?
          Serial.print("fistByte: 0x"); Serial.println(fistByte, HEX);
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
      }
      else {
        userCodeBufferLen = Serial.readBytes(userCodeBuffer, BYTES_TO_READ);
      }

      if (userCodeBufferLen > 0 ) {
        // check if CONTROL_END single contained
        // if contained, do not save it to EEPROM
        byte lastByte = userCodeBuffer[userCodeBufferLen - 1];
        
        bool controlEndSingleContained = false;
        if (!lastByte ^ CONTROL_END) {
          Serial.print("lastbyte: 0x");Serial.println(lastByte, HEX);
          controlEndSingleContained = true;
        }

        if (controlEndSingleContained) {
          byte* bufferWithoutEndCtrl = new byte[userCodeBufferLen - 1];
          for (int j=0; j<userCodeBufferLen - 1; j++) {
            bufferWithoutEndCtrl[j] = userCodeBuffer[j];
          }
          EEPROM.put(eeAddress, bufferWithoutEndCtrl);
          eeAddress += (userCodeBufferLen - 1);
          delete bufferWithoutEndCtrl;

          doSomethingWhenFinishReadingUserCode();// reset pc, reset readingUserCode flag
        }
        else {
          EEPROM.put(eeAddress, userCodeBuffer);
          eeAddress += userCodeBufferLen;

          if (eeAddress >= EEPROM_SIZE -1) {
            doSomethingWhenFinishReadingUserCode();
            clearEEPROM();
          }
        }
      }
      else {
        Serial.println("readingUserCode, but no code read");
      }
    }
  }
}

void doSomethingWhenStartReadingUserCode() {
  Serial.println("doSomethingWhenStartReadingUserCode");

  eeAddress = 0;
  readingUserCode = true; // yes, set start reading user code flag
  clearEEPROM();
}

void doSomethingWhenFinishReadingUserCode() {
  Serial.println("doSomethingWhenFinishReadingUserCode");

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
