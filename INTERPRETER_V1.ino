#include <EEPROM.h>

const bool __ENABLE_DEBUG__ = true;

// CONST
const int MAX_INSTRUCTIONS = 6;// 最多5种指令类型
const int MAX_PC = 20; //最多20条指令 todo 如何动态增加PC？
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
typedef int (*__getcode_ptr)(const int index, struct _PC *const pc);
struct _INSTRUCTION{
  byte ins;
  __instruction_ptr func;
  __getcode_ptr getcode_fun;
} INSTRUCTIONS[MAX_INSTRUCTIONS];

void setup() {
  // put your setup code here, to run once:
  pinMode(PIN_STATE, OUTPUT);

  memclear(OPERA_STACK, MAX_OPERA_STACK);

// DO NOT CLEAR ROM because user code that stored in ROM will be executed after reboot
//  clearEEPROM();

  Serial.begin(SERIAL_BOND);
  // wait 2.5s for serial connected
  for (int i=0; i<10; i++) {
    if (Serial) break; // wait for serial port to connect. Needed for native USB port only
    if (!Serial) delay(500);
  }

  prepareInstructionStack();

//  checkAndReadUserCodeFromSerial();
  // 
  if (checkIfUserCodeInEEPROM()) {
    int userCodeBufferLen = readUserCodeFromROM();
    preparePcStack(userCodeBufferLen);
  }
  
  digitalWrite(PIN_STATE, LOW);
}

void loop() {
  // 检查是否在下载代码
  checkAndReadUserCodeFromSerial();

  if (downloadingUserCode) {
    debug("downloading user code ...");
    resetPC();
    return;
  }
  // 检查结束

  if (!checkIfUserCodeInEEPROM()) {
    return;
  }

  // 检查是否在下载代码
  checkAndReadUserCodeFromSerial();

  if (downloadingUserCode) {
    debug("downloading user code ...");
    resetPC();
    return;
  }
  // 检查结束

  debug("firstExecuting = " + String(firstExecuting));
  if (firstExecuting) {
    int userCodeBufferLen = readUserCodeFromROM();  
    preparePcStack(userCodeBufferLen);

    firstExecuting = false;
  }

  // 检查是否在下载代码
  checkAndReadUserCodeFromSerial();

  if (downloadingUserCode) {
    debug("downloading user code ...");
    resetPC();
    return;
  }
  // 检查结束

  executeUserCode();
  
}

void executeUserCode() {
  if (!userCodeBuffer){// no user code to execute
    debug("user code buffer is NULL");
    return;  
  }

  debug("executing code: ", " pc: "+ String(pc)+" START", userCodeBuffer, eeAddress);
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
  for (int index = USER_CODE_START_POS_IN_ROM ; index < len+USER_CODE_START_POS_IN_ROM ; index++) {
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
    int indexStepForward;
    pc.pcCount=pcCount;
    pc.instruction=code;
    switch(code) {
      case 0x01:
      case 0x02:
      case 0x03:
      case 0x04:
      case 0x05:
        debug("getcode_fun(" + String(index) + "0");
        indexStepForward = INSTRUCTIONS[code].getcode_fun(index, &pc);
        debug("indexStepForward=" + String(indexStepForward));
        index += indexStepForward;
        PC_STACK[pcCount]=pc;
        debug("PC_STACK[" + String(pcCount) + "] vals[0]=0x" + String(pc.vals[0], HEX) + " vals[1]=0x" + String(pc.vals[1], HEX));
        pcCount++;
        totalPc++;
        break;
      default:
        error("instruction is undefined. 0x" + String(code, HEX));
        return;
    }

    index++;
    code = getNextCode(index);
  }
}

int getUnitaryCode(const int index, struct _PC *const pc){
  int i = index;
  i++;
  pc->vals[0]=(int)getNextCode(i);
  pc->valLen=1; 

  return 1;
}

int getBinaryCode(const int index, struct _PC *const pc){
  int i = index;
  i++;
  pc->vals[0]=(int)getNextCode(i);
  i++;
  pc->vals[1]=(int)getNextCode(i);
  pc->valLen=2;   

  return 2;
}

void prepareInstructionStack() {
  constructInstruction(0x01);
  constructInstruction(0x02);
  constructInstruction(0x03);
  constructInstruction(0x04);
  constructInstruction(0x05);
}

void constructInstruction(byte ins){
  struct _INSTRUCTION instruction;

  if (ins >= MAX_INSTRUCTIONS) {
    debug("instruction CODE should not be great than MAX_INSTRUCTIONS(" + String(MAX_INSTRUCTIONS) + ")");
    return;  
  }
  
  instruction.ins=ins;
  switch(ins) {
      case 0x01:
        instruction.func = __delay;
        instruction.getcode_fun = getBinaryCode;
        break;
      case 0x02:
        instruction.func = __digital_write;
        instruction.getcode_fun = getBinaryCode;
        break;
      case 0x03:
        instruction.func = __pin_mode;
        instruction.getcode_fun = getBinaryCode;
        break;
      case 0x04:
        instruction.func = __goto;
        instruction.getcode_fun = getUnitaryCode;
        break;
      case 0x05:
        instruction.func = __compare;
        instruction.getcode_fun = getBinaryCode;
        break;
      default:
        return;
    }
    INSTRUCTIONS[ins]=instruction;
}

byte getNextCode(int index){
  byte val = userCodeBuffer[index];
  debug("end getNextCode(" + String(index) + ") is 0x" + String(val, HEX));
  return val;
}

bool checkIfUserCodeInEEPROM() {
  int len = EEPROM.read(0);
  if (len > 0) {
    debug("checkIfUserCodeInEEPROM: user codes length that saved in ROM is: " + String(len));
    return true;
  }

  debug("checkIfUserCodeInEEPROM: user codes length that saved in ROM is: 0");
  return false;
}

void checkAndReadUserCodeFromSerial() {
  debug("checkAndReadUserCodeFromSerial");
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

        //todo 结尾控制符号可能含在读取的内容中间而非恰好是在末尾，是否需要处理？
        //     一般正规jerry编译器可以确保结尾控制符处于末尾
        //todo 结尾控制符选择EF并非最好，需要另外选择其它符号
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
  else {
    debug("!! Serial is not avalable yet!!");  
  }
}

void memclear(byte mem[], const int len) {
  for (int i; i<len; i++)
    mem[i]=0;
}

void doSomethingWhenStartReadingUserCode() {
  debug("doSomethingWhenStartReadingUserCode");

  eeAddress = USER_CODE_START_POS_IN_ROM;
  debug("reset eeAddress=" + String(USER_CODE_START_POS_IN_ROM));
  downloadingUserCode = true; // yes, set start reading user code flag
  clearEEPROM();
}

void doSomethingWhenFinishReadingUserCode() {
  debug("doSomethingWhenFinishReadingUserCode");

  resetPC();
  downloadingUserCode = false;

  EEPROM.write(0, eeAddress-1);// save last eeAddress to pos 0
  debug("write user code length " + String(eeAddress-1, HEX) + " to ROM at pos 0");

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

void __pin_mode(){
  byte pin = __pop();
  byte val = __pop();
  
  if (val==0) {
    pinMode((int)pin, LOW);
  }
  else {
    pinMode((int)pin, HIGH);
  }

  debug("pinMode() pin: 0x" + String(pin, HEX) + " val: 0x" + String(val, HEX));
}

void __goto(){
  byte _pc = __pop();
  pc = _pc;

  debug("__goto() pc: 0x" + String(_pc, HEX));
}

void __compare(){
  byte val2 = __pop();
  byte val1 = __pop();
  
  int c = val1 - val2;
  __push((byte)c);

  debug("__compare() val1: 0x" + String(val1, HEX) + " val2: 0x" + String(val2, HEX));
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


