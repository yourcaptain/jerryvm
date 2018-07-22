#include <EEPROM.h>

const bool __ENABLE_DEBUG__ = true;

// CONST
const int MAX_INSTRUCTIONS = 20;// at most 100 instructions
const int MAX_PC = 100;
const int MAX_OPERA_STACK = 10;

const short PIN_STATE = 13;
const int SERIAL_BOND = 9600;
const byte CONTROL_START = 0xEF;
const byte CONTROL_END = 0xFE;
const short BYTES_TO_READ = 10;
const short USER_CODE_BUFFER_LEN = 10;
const int EEPROM_SIZE = 512;// bytes

// variables for reading from Serial
bool readingUserCode = false;
byte userCodeBuffer[USER_CODE_BUFFER_LEN];

// variables for writing to EEPROM
int eeAddress = 0;

// program count
short pc = 0;

// interpret and executing
byte *userCodeUnderExecuting = NULL;
bool firstExecuting = false; // set true when read from serial finished, set false when read from ROM

// opera stack
byte OPERA_STACK[MAX_OPERA_STACK];

//pc stack
struct _PC{
  short pcCount;
  byte instruction;
  short valLen;
  byte vals[2];
} PC_STACK[MAX_PC];

// instructions
void __delay();
void __digital_write();
typedef void (*__instruction_ptr)();
struct _INSTRUCTION{
  byte ins;
  __instruction_ptr func;
} INSTRUCTIONS[MAX_INSTRUCTIONS];

void setup() {
  // put your setup code here, to run once:
  pinMode(PIN_STATE, OUTPUT);

  clearEEPROM();

  Serial.begin(SERIAL_BOND);
  // do not need to wait for serial port as serial may connect at any time
//    while (!Serial) {
//      ; // wait for serial port to connect. Needed for native USB port only
//    }

  prepareInstructionStack();
  
  digitalWrite(PIN_STATE, LOW);
}

void loop() {
  checkAndReadUserCodeFromSerial();
  if (readingUserCode) {
    resetPC();
    return;
  }

  if (!checkIfUserCodeInEEPROM()) {
    return;
  }

  if (firstExecuting) {
    readUserCodeFromROM();  
    preparePcStack();
  }

  executeUserCode();
  
}

void executeUserCode() {
  if (!userCodeUnderExecuting){
    return;  
  }
  // interpret
//  debug("executing code: ", "OVER", userCodeUnderExecuting, eeAddress);
  struct _PC _pc = PC_STACK[pc];
  struct _INSTRUCTION instruction = INSTRUCTIONS[_pc.instruction];

  for (int i=0; i<_pc.valLen; i++){
    __push(_pc.vals[i]);  
  }

  instruction.func();

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
  for (int index = 0 ; index < eeAddress ; index++) {
    //Add one to each cell in the EEPROM
    userCodeUnderExecuting[index] = EEPROM.read(index);
  }  
  debug("Read from ROM: ", "OVER", userCodeUnderExecuting, eeAddress);
  firstExecuting = false;
}

// 
void preparePcStack() {
  debug("preparePcStack start");
  int pcCount=0;
  int index=0;
  byte code = getNextCode(index);
  while(!code && index<eeAddress ){
    struct _PC pc;
    switch(code) {
      case 0x01:  
        pc.pcCount=pcCount;
        pc.instruction=code;
        index++;
        pc.vals[0]=(int)getNextCode(index);
        pc.valLen=1;
        PC_STACK[pcCount]=pc;

        pcCount++;
        break;
      case 0x10:
        pc.pcCount=pcCount;
        pc.instruction=code;
        index++;
        pc.vals[0]=(int)getNextCode(index);
        index++;
        pc.vals[1]=(int)getNextCode(index);
        pc.valLen=2;
        PC_STACK[pcCount]=pc;

        pcCount++;
        break;
      default:
        return;
    }
  }
}

void prepareInstructionStack() {
  constructInstruction(0x01);
  constructInstruction(0x10);
}

void constructInstruction(byte ins){
  struct _INSTRUCTION instruction;
  switch(ins) {
      case 0x01:
        instruction.ins=ins;
        instruction.func = __delay;
        INSTRUCTIONS[ins]=instruction;
        break;
      case 0x10:
        instruction.ins=ins;
        instruction.func = __digital_write;
        INSTRUCTIONS[ins]=instruction;
        break;
      default:
        return;
    }
}

byte getNextCode(int index){
  return userCodeUnderExecuting[index];
}

bool checkIfUserCodeInEEPROM() {
  byte code = EEPROM.read(0);
  if (code) {
    return true;
  }

  return false;
}

void checkAndReadUserCodeFromSerial() {
  size_t userCodeBufferLen=0;
  memclear(userCodeBuffer, USER_CODE_BUFFER_LEN);
  // reply only when you receive data:
  if (Serial.available() > 0) {
    userCodeBufferLen = Serial.readBytes(userCodeBuffer, BYTES_TO_READ);
    debug("Read user code's size:" + String(userCodeBufferLen) + ", Read user codes: ", "OVER", userCodeBuffer, userCodeBufferLen);

    if (!readingUserCode) { // waiting for CONTROL_START single
      if (userCodeBufferLen >= 1) {
        if (userCodeBuffer[0] == CONTROL_START) { // CONTROL_START single came?
          doSomethingWhenStartReadingUserCode();
        }
      }
      else {// waiting for CONTROL_START single but no recognized single came, do nothing
        // do nothing
      }
    }

    if (readingUserCode) {
      if (userCodeBufferLen > 1) {
        for (int i=0; i<userCodeBufferLen-1; i++) {
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
        
        if (userCodeBuffer[userCodeBufferLen - 1] == CONTROL_END) {
          for (int j=0; j<userCodeBufferLen - 1; j++) {
            EEPROM.write(eeAddress, userCodeBuffer[j]);
            eeAddress++;
          }
          
          doSomethingWhenFinishReadingUserCode();// reset pc, reset readingUserCode flag
          debug("eeAddress is " + String(eeAddress) + ", Write codes to ROM: ", " Over", userCodeBuffer, userCodeBufferLen - 1);
        }
        else {
          for (int j=0; j<userCodeBufferLen; j++) {
            EEPROM.write(eeAddress, userCodeBuffer[j]);
            eeAddress++;
          }

          debug("eeAddress is " + String(eeAddress) + ", Write codes to ROM: ", " Over", userCodeBuffer, userCodeBufferLen);
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
}

void resetPC() {
  pc = 0;
}

void clearEEPROM() {
  for (int i = 0 ; i < EEPROM.length() ; i++) { // clear EEPOM
    EEPROM.write(i, 0);
  }
}

void debug(String content) {
  if (__ENABLE_DEBUG__) {
    Serial.println(content); 
  }
}

void debug(String prefix, String content, String suffix) {
  if (__ENABLE_DEBUG__) {
    String output = prefix + content + suffix;
    Serial.println(output);
  }
}

void debug(String prefix, String suffix, byte buf[], const int len) {
  if (__ENABLE_DEBUG__) {
    String output = prefix + "0x";
    for (int i=0; i<len; i++){
      output = output + String(buf[i], HEX);
    }
    output = output + " " +suffix;

    Serial.println(output);
  }
}

void debug(String prefix, String suffix, byte b) {
  if (__ENABLE_DEBUG__) {
    String output = prefix + " 0x" + String(b, HEX) + " " + suffix;
    Serial.println(output);
  }
}


/*****************************************************
 * 
 * call back
 * 
 ****************************************************/

void __delay(){
  byte ms = __pop();
  delay((int)ms);
}

void __digital_write(){
  byte pin = __pop();
  byte val = __pop();
  if (val==0) {
    digitalWrite((int)pin, LOW);
  }
  else {
    digitalWrite((int)pin, HIGH);
  }
}

void __push(byte val){
  for (int i=MAX_OPERA_STACK-1; i>= 1; i--)  {
    OPERA_STACK[i]=OPERA_STACK[i-1];
  }
  OPERA_STACK[0]=val;
}

byte __pop(){
  byte ret = OPERA_STACK[0];
  for (int i=0; i= MAX_OPERA_STACK-1; i++)  {
    OPERA_STACK[i]=OPERA_STACK[i+1];
  }
  OPERA_STACK[MAX_OPERA_STACK-1]=0x0;

  return ret;
}


