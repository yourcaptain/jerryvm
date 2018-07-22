#include <EEPROM.h>

const bool __ENABLE_DEBUG__ = true;

// CONST
const int MAX_INSTRUCTIONS = 5;// 最多5种指令类型
const int MAX_PC = 20; //最多20条指令
const int MAX_OPERA_STACK = 3;

const int USER_CODE_START_POS_IN_ROM = 1;
const short PIN_STATE = 13;
const int SERIAL_BOND = 9600;
const byte CONTROL_START = 0xEF;
const byte CONTROL_END = 0xFE;
const short BYTES_TO_READ = 10;
const short USER_CODE_BUFFER_LEN = 10;
const int EEPROM_SIZE = 512;// bytes

// variables for reading from Serial
bool downloadingUserCode = false;
byte userCodeDownloadBuffer[USER_CODE_BUFFER_LEN];

// variables for writing to EEPROM
int eeAddress = USER_CODE_START_POS_IN_ROM; // address 0 is reserved for saving length

// program count
short pc = 0;
short totalPc=0;

// interpret and executing
byte *userCodeBuffer = NULL;
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
typedef void (*__instruction_ptr)();
struct _INSTRUCTION{
  byte ins;
  __instruction_ptr func;
} INSTRUCTIONS[MAX_INSTRUCTIONS];

void setup() {
  // put your setup code here, to run once:
  pinMode(PIN_STATE, OUTPUT);

  memclear(OPERA_STACK, MAX_OPERA_STACK);

//  clearEEPROM();

  Serial.begin(SERIAL_BOND);
  // wait 2.5s for serial connected
  for (int i=0; i<10; i++) {
    if (Serial) break; // wait for serial port to connect. Needed for native USB port only
    if (!Serial) delay(500);
  }

  prepareInstructionStack();

  // 
  if (checkIfUserCodeInEEPROM()) {
    int userCodeBufferLen = readUserCodeFromROM();
    preparePcStack(userCodeBufferLen);
  }
  
  digitalWrite(PIN_STATE, LOW);
}

void loop() {
  checkAndReadUserCodeFromSerial();

  if (downloadingUserCode) {
    debug("downloading user code ...");
    resetPC();
    return;
  }

  if (!checkIfUserCodeInEEPROM()) {
    return;
  }

  debug("firstExecuting = " + String(firstExecuting));
  if (firstExecuting) {
    int userCodeBufferLen = readUserCodeFromROM();  
    preparePcStack(userCodeBufferLen);

    firstExecuting = false;
  }

  executeUserCode();
  
}

void executeUserCode() {
  if (!userCodeBuffer){// no user code to execute
    debug("user code buffer is NULL");
    return;  
  }

  debug("executing code: ", "START", userCodeBuffer, eeAddress);
  struct _PC _pc = PC_STACK[pc];
  struct _INSTRUCTION instruction = INSTRUCTIONS[_pc.instruction];

  for (int i=0; i<_pc.valLen; i++){
    __push(_pc.vals[i]);  
  }

  if (_pc.valLen == 1)
    debug("operastack: " + String(OPERA_STACK[0], HEX));
  if (_pc.valLen == 2)
    debug("operastack: " + String(OPERA_STACK[0], HEX) + " "+ String(OPERA_STACK[1], HEX) );
  instruction.func();

  debug("pc=" + String(pc) + " is executed.");

  pc++;
  if (pc > totalPc-1) {
    pc = 0;
  }

  debug("executing code: ", "OVER", userCodeBuffer, eeAddress);
}

// 返回获取的代码userCodeBuffer长度
int readUserCodeFromROM() {
  int len = EEPROM.read(0);
  if (!len){
    debug("read length from ROM is 0");
    return;
  }
  debug("read length from ROM is " + String(len));
  
  // release memory which is used by previous user codes
  if (!userCodeBuffer){
    delete userCodeBuffer;  
  }

  // read from ROM
  userCodeBuffer = new byte[len];
  for (int index = USER_CODE_START_POS_IN_ROM ; index < len ; index++) {
    //Add one to each cell in the EEPROM
    userCodeBuffer[index-1] = EEPROM.read(index);
  }  
  debug("Read from ROM: ", "OVER", userCodeBuffer, len);

  return len;
}

// 
void preparePcStack(int userCodeBufferLen) {
  debug("preparePcStack start, userCodeBufferLen=" + String(userCodeBufferLen));
  int pcCount=0;
  int index=0;
  byte code = getNextCode(index);
  while(code && index<userCodeBufferLen-1 ){
    debug("prepare code:" + String(code, HEX) + " index: "+String(index));
    struct _PC pc;
    switch(code) {
      case 0x01:  
        pc.pcCount=pcCount;
        pc.instruction=code;
        index++;
        pc.vals[0]=(int)getNextCode(index);
        index++;
        pc.vals[1]=(int)getNextCode(index);
        index++;
        pc.valLen=2;
        PC_STACK[pcCount]=pc;

        debug("PC_STACK[" + String(pcCount) + "] vals[0]=0x" + String(pc.vals[0], HEX) + " vals[1]=0x" + String(pc.vals[1], HEX));

        pcCount++;
        totalPc++;
        break;
      case 0x10:
        debug("switch to 0x10");
        pc.pcCount=pcCount;
        pc.instruction=code;
        index++;
        pc.vals[0]=(int)getNextCode(index);
        index++;
        pc.vals[1]=(int)getNextCode(index);
        index++;
        pc.valLen=2;
        PC_STACK[pcCount]=pc;

        debug("PC_STACK[" + String(pcCount) + "] vals[0]=0x" + String(pc.vals[0], HEX) + " vals[1]=0x" + String(pc.vals[1], HEX));

        pcCount++;
        totalPc++;
        break;
      default:
        error("instruction is undefined. 0x" + String(code, HEX));
        return;
    }

    code = getNextCode(index);
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
  byte val = userCodeBuffer[index];
  debug("getNextCode(" + String(index) + ") is 0x" + String(val, HEX));
  return val;
}

bool checkIfUserCodeInEEPROM() {
  byte len = EEPROM.read(0);
  if (len) {
    debug("user codes length that saved in ROM is: 0x" + String(len, HEX));
    return true;
  }

  debug("user codes length that saved in ROM is: 0x" + String(len, HEX));
  return false;
}

void checkAndReadUserCodeFromSerial() {
  size_t userCodeDownloadBufferLen=0;
  memclear(userCodeDownloadBuffer, USER_CODE_BUFFER_LEN);
  
  if (!Serial){
    debug("!! Serial is false !!");
    return;
  }
  
  // reply only when you receive data:
  if (Serial.available() > 0) {
    userCodeDownloadBufferLen = Serial.readBytes(userCodeDownloadBuffer, BYTES_TO_READ);
    debug("Downloading user code's size:" + String(userCodeDownloadBufferLen) + ", Downloading user codes: ", "OVER", userCodeDownloadBuffer, userCodeDownloadBufferLen);

    if (!downloadingUserCode) { // waiting for CONTROL_START single
      if (userCodeDownloadBufferLen >= 1) {
        if (userCodeDownloadBuffer[0] == CONTROL_START) { // CONTROL_START single came?
          doSomethingWhenStartReadingUserCode();
          
          for (int i=0; i<userCodeDownloadBufferLen-1; i++) {
            userCodeDownloadBuffer[i] = userCodeDownloadBuffer[i+1];
          }
          userCodeDownloadBuffer[userCodeDownloadBufferLen-1] = 0x00;
          userCodeDownloadBufferLen--;
        }
      }
      else {// waiting for CONTROL_START single but no recognized single came, do nothing
        // do nothing
      }
    }

    if (downloadingUserCode) {
      if (userCodeDownloadBufferLen > 0 ) {
        // check if CONTROL_END single contained
        // if contained, do not save it to EEPROM
        byte lastByte = userCodeDownloadBuffer[userCodeDownloadBufferLen - 1];
        
        if (userCodeDownloadBuffer[userCodeDownloadBufferLen - 1] == CONTROL_END) {
          for (int j=0; j<userCodeDownloadBufferLen - 1; j++) {
            EEPROM.write(eeAddress, userCodeDownloadBuffer[j]);
            eeAddress++;
          }
          
          doSomethingWhenFinishReadingUserCode();// reset pc, reset readingUserCode flag
          debug("eeAddress is " + String(eeAddress) + ", Write codes to ROM: ", " Over", userCodeDownloadBuffer, userCodeDownloadBufferLen - 1);
        }
        else {
          for (int j=0; j<userCodeDownloadBufferLen; j++) {
            EEPROM.write(eeAddress, userCodeDownloadBuffer[j]);
            eeAddress++;
          }

          debug("eeAddress is " + String(eeAddress) + ", Write codes to ROM: ", " Over", userCodeDownloadBuffer, userCodeDownloadBufferLen);
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

  eeAddress = USER_CODE_START_POS_IN_ROM;
  downloadingUserCode = true; // yes, set start reading user code flag
  clearEEPROM();
}

void doSomethingWhenFinishReadingUserCode() {
  debug("doSomethingWhenFinishReadingUserCode");

  resetPC();
  downloadingUserCode = false;

  EEPROM.write(0, eeAddress);// save last eeAddress to pos 0

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

void error(String content) {
  Serial.println(content); 
  Serial.flush();
}

void debug(String content) {
  if (__ENABLE_DEBUG__) {
    Serial.println(content); 
    Serial.flush();
  }
}

void debug(String prefix, String content, String suffix) {
  if (__ENABLE_DEBUG__) {
    String output = prefix + content + suffix;
    Serial.println(output);
    Serial.flush();
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
    Serial.flush();
  }
}

void debug(String prefix, String suffix, byte b) {
  if (__ENABLE_DEBUG__) {
    String output = prefix + " 0x" + String(b, HEX) + " " + suffix;
    Serial.println(output);
    Serial.flush();
  }
}


/*****************************************************
 * 
 * call back
 * 
 ****************************************************/

void __delay(){
  byte msLOW = __pop();//先pop出整数的低四位
  byte msHIGH = __pop();//再pop出整数高四位
  unsigned long ms = 0;
  
  unsigned long msHIGHlong = msHIGH;
  msHIGHlong = msHIGHlong<<8;

  ms = ms | msLOW;
  ms = ms | msHIGHlong;
  debug("__delay(" + String(ms) + ")");
  delay(ms);
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

  debug("__digital_write() pin: 0x" + String(pin, HEX) + " val: 0x" + String(val, HEX));
}

void __push(byte val){
  for (int i=MAX_OPERA_STACK-1; i>= 1; i--)  {
    OPERA_STACK[i]=OPERA_STACK[i-1];
  }
  OPERA_STACK[0]=val;
}

byte __pop(){
  byte ret = OPERA_STACK[0];
  for (int i=0; i< MAX_OPERA_STACK-2; i++)  {
    OPERA_STACK[i]=OPERA_STACK[i+1];
  }
  OPERA_STACK[MAX_OPERA_STACK-1]=0x0;

  return ret;
}


