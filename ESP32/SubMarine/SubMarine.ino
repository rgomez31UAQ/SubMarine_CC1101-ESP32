// SPIFFS
#include "SPIFFS.h"
#define SPIFFS_FILENAME_RECORDED_SIGNAL "/recordedSignal.txt"
bool spiffsMounted = false;

// BLUETOOTH
#include "Arduino.h"
#include "BluetoothSerial.h"
BluetoothSerial SerialBT;

//INCOMING BLUETOOTH COMMAND
#define INCOMING_BLUETOOTH_COMMAND_HEADER_LENGTH 8
int incomingCommandLength = 0;
bool receivingBluetoothCommand = false;
int incomingCommandStartTime = 0;
int incomingCommandEndTime = 0;

// COMMANDS
#define COMMAND_REPLAY_SIGNAL_FROM_BLUETOOTH_COMMAND "0001" 
#define COMMAND_SET_OPERATION_MODE "0002" 
#define COMMAND_TRANSFER_SIGNAL_OVER_BLUETOOTH "0003"
#define COMMAND_SET_ADAPTER_CONFIGURATION "0004"
#define COMMAND_GET_ADAPTER_CONFIGURATION "0005"
#define COMMAND_SEND_DETECTED_FREQUENCY "0006"
String incomingBluetoothCommandParsed = "";
String incomingBluetoothCommandIdParsed = "";
String incomingBluetoothCommandDataStringParsed = "";

// COMMAND IDS
#define COMMAND_ID_DUMMY "0000"

// SIGNAL FROM BLUETOOTH COMMAND
#define INCOMING_BLUETOOTH_SIGNAL_BUFFER_SIZE 4096
int incomingBluetoothSignal[INCOMING_BLUETOOTH_SIGNAL_BUFFER_SIZE];

// OPERATION MODES
#define OPERATIONMODE_IDLE "0000" 
#define OPERATIONMODE_HANDLE_INCOMING_BLUETOOTH_COMMAND "0001" 
#define OPERATIONMODE_PERISCOPE "0002"
#define OPERATIONMODE_RECORD_SIGNAL "0003"
#define OPERATIONMODE_DETECT_SIGNAL "0004"
String lastExecutedOperationMode = "0000"; 
String operationMode = OPERATIONMODE_DETECT_SIGNAL;//"0000";

// RECORDED SAMPLES BUFFER
#define MAX_LENGHT_RECORDED_SIGNAL 4096
int recordedSignal[MAX_LENGHT_RECORDED_SIGNAL];
int recordedSamples = 0;

// SINGAL DETECTION
#define SIGNAL_DETECTION_FREQUENCIES_LENGTH 16
float signalDetectionFrequencies[SIGNAL_DETECTION_FREQUENCIES_LENGTH] = {300.00, 303.87, 304.25, 310.00, 315.00, 318.00, 390.00, 418.00, 433.07, 433.92, 434.42, 434.77, 438.90, 868.35, 915.00, 925.00};
int detectedRssi = -100;
float detectedFrequency = 0.0;
int signalDetectionMinRssi = -65;

// CC1101
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#define PIN_GDO0 12
#define PIN_GDO2 4

// CC1101 MODULE SETTINGS
#define CC1101_ADAPTER_CONGIRURATION_LENGTH 30
 /*
      Adapter Configuration Structure:
      Bytes:
      0-5 => MHZ
      6 => TX
      7 => MODULATION
      8-10 => DRATE
      11-16 => RX_BW
      17 => PKT_FORMAT
      18-23 => AVG_LQI
      24-29 => AVG_RSSI
*/
float CC1101_MHZ = 433.92;
bool CC1101_TX = false;
int CC1101_MODULATION = 2;
int CC1101_DRATE = 512;
float CC1101_RX_BW = 256;
int CC1101_PKT_FORMAT = 3;
float CC1101_LAST_AVG_LQI = 0;
float CC1101_LAST_AVG_RSSI = 0;

// LED
#define PIN_LED_ONBOARD 2

// BUTTONS
#define THRESHOLD_BTN_CLICK 4000
#define PIN_BTN_1 32
#define PIN_BTN_2 25
#define PIN_BTN_3 27
#define PIN_BTN_4 14

void setup() {
  Serial.begin(1000000);
  //BLUETOOTH SETUP
  initBluetooth();
  //CC1101 SETUP
  initCC1101();
  // INIT SPIFFS
  initSpiffs();
  // PIN MODES
  pinMode(PIN_LED_ONBOARD, OUTPUT);
}

void initBluetooth(){
  SerialBT.begin("Sub Marine"); //Bluetooth device name
  SerialBT.register_callback(btCallback);
}

void initSpiffs(){
  if(SPIFFS.begin(true)){
    Serial.println("SPIFFS mounted successfully");
    spiffsMounted = true;
  } else {
    Serial.println("An Error has occurred while mounting SPIFFS");  
    spiffsMounted = false;  
  }
}

void initCC1101(){
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setGDO(PIN_GDO0, PIN_GDO2);
  ELECHOUSE_cc1101.setMHZ(CC1101_MHZ);                    // Here you can set your basic frequency. The lib calculates the frequency automatically (default = 433.92).The cc1101 can: 300-348 MHZ, 387-464MHZ and 779-928MHZ. Read More info from datasheet.
  if(CC1101_TX){
    ELECHOUSE_cc1101.SetTx();                             // set Transmit on
    pinMode(PIN_GDO0,OUTPUT);  
  } else {
    ELECHOUSE_cc1101.SetRx();                             // set Receive on
    pinMode(PIN_GDO0,INPUT);  
  }
  
  ELECHOUSE_cc1101.setModulation(CC1101_MODULATION);      // set modulation mode. 0 = 2-FSK, 1 = GFSK, 2 = ASK/OOK, 3 = 4-FSK, 4 = MSK.
  ELECHOUSE_cc1101.setDRate(CC1101_DRATE);                // Set the Data Rate in kBaud. Value from 0.02 to 1621.83. Default is 99.97 kBaud!
  ELECHOUSE_cc1101.setRxBW(CC1101_RX_BW);                 // Set the Receive Bandwidth in kHz. Value from 58.03 to 812.50. Default is 812.50 kHz.
  ELECHOUSE_cc1101.setPktFormat(CC1101_PKT_FORMAT);       // Format of RX and TX data. 0 = Normal mode, use FIFOs for RX and TX. 
                                                          // 1 = Synchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins. 
                                                          // 2 = Random TX mode; sends random data using PN9 generator. Used for test. Works as normal mode, setting 0 (00), in RX. 
                                                          // 3 = Asynchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins.

  // Check the CC1101 Spi connection.
  if(!ELECHOUSE_cc1101.getCC1101()){       
    Serial.println("CC1101 Connection Error");
  } else {
    //Serial.println("CC1101 Connection OK");
  }
}


void btCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param){
  if(event == ESP_SPP_SRV_OPEN_EVT){
    Serial.println("Bluetooth Client Connected!");
    delay(100);
  } else if(event == ESP_SPP_DATA_IND_EVT){
    // RECEIVE INCOMING BLUETOOTH COMMAND
    if(receivingBluetoothCommand == false){
      // CLEAR ANY PREVIOUS RESULT
      //memset(incomingBluetoothCommand,0,INCOMING_BLUETOOTH_COMMAND_BUFFER_SIZE*sizeof(int));
      incomingCommandStartTime = millis();      
      //incomingBluetoothCommandByteIndex = 0;
      incomingCommandLength = 0;
      // CLEAR PREVIOUS COMMAND FILE
      SPIFFS.remove("/incomingBluetoothCommand.txt");
      File file = SPIFFS.open("/incomingBluetoothCommand.txt", FILE_WRITE);
      file.close();
    }    
    receivingBluetoothCommand = true;
    File file = SPIFFS.open("/incomingBluetoothCommand.txt", FILE_APPEND);

    while(SerialBT.available()){ 
      int incoming = SerialBT.read();
      char incomingChar = incoming; 

      if(incomingChar == '\n'){
        // TRANSMISSION COMPLETED
        receivingBluetoothCommand = false;
        incomingCommandEndTime = millis();
        file.print(incomingChar);
      } else {
        // ADD BYTE TO COMMAND BUFFER
        incomingCommandLength++;
        file.print(incomingChar);
      }
    }

    //CLOSE THE FILE
    file.close();  

    // LOG INFORMATION, CALL CALLBACK     
     
    if(receivingBluetoothCommand == false){
        Serial.print("Stopped after receiving ");
        Serial.print(incomingCommandLength);
        Serial.println(" Bytes");
        Serial.print("in ");
        Serial.print(incomingCommandEndTime - incomingCommandStartTime);
        Serial.println(" Milliseconds");

        // CALLBACK
        incomingBluetoothCommandReceivedCallback();      
    }
  }
}

void incomingBluetoothCommandReceivedCallback(){
  Serial.println("Incoming Bluetooth Command received successfully.");

   /*
      Command Structure:
      Bytes:
      0-3 => Command
      4-7 => CommandId
      Remaining Bytes => Data      
  */

  String parsedCommand = "";
  String parsedCommandId = "";
  String parsedDataString = "";

  int fileIndex = 0;  
  File file = SPIFFS.open("/incomingBluetoothCommand.txt", FILE_READ);
  while (file.available()) {
    char c = file.read();

    if(fileIndex <= 3){
      parsedCommand += c;
    }

    if(fileIndex > 3 && fileIndex <= 7){
      parsedCommandId += c;
    }

    if(fileIndex > 7){
      parsedDataString += c;
    }
    
    fileIndex++;
  }

  file.close();
  Serial.println("Parsed Command: " + parsedCommand);
  Serial.println("Parsed Command ID: " + parsedCommandId);
  Serial.println("Parsed Data: " + parsedDataString);

  // SET THE CURRENT COMMAND HEADER INFORMATION FOR MAIN THREAD
  incomingBluetoothCommandParsed = parsedCommand;
  incomingBluetoothCommandIdParsed = parsedCommand;
  incomingBluetoothCommandDataStringParsed = parsedDataString;

  setOperationMode(OPERATIONMODE_HANDLE_INCOMING_BLUETOOTH_COMMAND); 
}

void setOperationMode(String opMode){
  operationMode = opMode;
}

String getCC1101Configuration(){
   /*
      Adapter Configuration Structure:
      Bytes:
      0-5 => MHZ
      6 => TX
      7 => MODULATION
      8-10 => DRATE
      11-16 => RX_BW
      17 => PKT_FORMAT
      18-23 => AVG_LQI
      24-29 => AVG_RSSI
  */
  String configurationString = "";
  // MHZ
  String mhz = String(CC1101_MHZ, 2);
  while(mhz.length() < 6){
    mhz = "0" + mhz;
  }
  configurationString += mhz;
  // TX
  if(CC1101_TX){
    configurationString += "1";
  } else {
    configurationString += "0";
  }
  // MODULATION
  configurationString += String(CC1101_MODULATION).substring(0,1);
  //DRATE
  String dRate = String(CC1101_DRATE);
  while(dRate.length() < 3){
    dRate = "0" + dRate;
  }
  configurationString += String(CC1101_DRATE);
  //RX_BW
  String rxBw = String(CC1101_RX_BW, 2);
  while(rxBw.length() < 6){
    rxBw = "0" + rxBw;
  }
  configurationString += rxBw;
  // PKT_FORMAT
  configurationString += String(CC1101_PKT_FORMAT).substring(0,1);

  String lqi = String(CC1101_LAST_AVG_LQI, 2);
  while(lqi.length() < 6){
    lqi = "0" + lqi;
  }
  configurationString += lqi;

  String rssi = String(CC1101_LAST_AVG_RSSI, 2);
  while(rssi.length() < 6){
    rssi = "0" + rssi;
  }
  configurationString += rssi;

  return configurationString;
}

void setCC1101Configuration(String configurationString){
   /*
      Adapter Configuration Structure:
      Bytes:
      0-5 => MHZ
      6 => TX
      7 => MODULATION
      8-10 => DRATE
      11-16 => RX_BW
      17 => PKT_FORMAT
      18-23 => AVG_LQI
      24-29 => AVG_RSSI
  */

  int position = 0;

  String mhz = "";
  bool tx;
  int modulation;
  String dRate = "";
  String rxBw = "";
  int pktFormat;
  String lqi = "";
  String rssi = "";

  for(auto x : configurationString)
  {
    if(position <= 5){
      mhz += x;      
    }

    if(position == 6){
      tx = String(x).toInt() > 0 ? true : false;
    }

    if(position == 7){
      modulation = String(x).toInt();
    }

    if(position > 7 && position <= 10){
      dRate += x;
    }

    if(position > 10 && position <= 16){
      rxBw += x;
    }

    if(position == 17){
      pktFormat = String(x).toInt();
    }

    if(position > 17 && position <= 23){
      lqi += x;
    }

    if(position > 23 && position <= 29){
      rssi += x;
    }

    position++;
  }

  
  Serial.println("DETECTED FROM STRING: ");
  Serial.print("MHZ: ");
  Serial.println(mhz.toFloat());
  Serial.print("TX: ");
  Serial.println(tx);
  Serial.print("MODULATION: ");
  Serial.println(modulation);
  Serial.print("DRATE: ");
  Serial.println(dRate.toInt());
  Serial.print("RX BW: ");
  Serial.println(rxBw.toFloat());
  Serial.print("PKT FORMAT: ");
  Serial.println(pktFormat);
  Serial.print("LQI: ");
  Serial.println(lqi.toFloat());
  Serial.print("RSSI: ");
  Serial.println(rssi);
  

  // SET VALUES:
  CC1101_MHZ = mhz.toFloat();
  CC1101_TX = tx;
  CC1101_MODULATION = modulation;
  CC1101_DRATE = dRate.toInt();
  CC1101_RX_BW = rxBw.toFloat();
  CC1101_PKT_FORMAT = pktFormat;
  CC1101_LAST_AVG_LQI = lqi.toFloat();
  CC1101_LAST_AVG_RSSI = rssi.toFloat();

  // RELOAD ADAPTER
  initCC1101();
}

void replaySignalFromIncomingBluetoothCommand(){
  Serial.println("Replaying Signal from incoming Bluetooth Command");

  int fileIndex = 0;
  int parsedSamples = 0;
  String currentSample = "";

  String CC1101_ConfigurationString = "";

  File file = SPIFFS.open("/incomingBluetoothCommand.txt", FILE_READ);
  while (file.available()) {
    char c = file.read();

    if(fileIndex >= INCOMING_BLUETOOTH_COMMAND_HEADER_LENGTH && fileIndex < INCOMING_BLUETOOTH_COMMAND_HEADER_LENGTH + CC1101_ADAPTER_CONGIRURATION_LENGTH){
      CC1101_ConfigurationString += c;      
    }
    
    if(fileIndex >= INCOMING_BLUETOOTH_COMMAND_HEADER_LENGTH + CC1101_ADAPTER_CONGIRURATION_LENGTH){
      // START PARSING THE SIGNAL FROM THE DATA PORTION OF THE COMMAND
      if(c == '\n' || c == ','){
        int sample = currentSample.toInt();
        //Serial.println("Adding Sample from File: " + String(sample));
        if(parsedSamples < INCOMING_BLUETOOTH_SIGNAL_BUFFER_SIZE){
          incomingBluetoothSignal[parsedSamples] = sample;
          currentSample = "";
          parsedSamples++;
        } else {
          Serial.println("Too many Samples");
        }
        
        if(c == '\n'){
          break;
        }
      } else {
        currentSample += c;
      }
    }

    fileIndex++;
  }
  file.close();

  if(parsedSamples > 0){
    // SET CC1101 CONFIG
    setCC1101Configuration(CC1101_ConfigurationString);
    // REPLAY THE SIGNAL
    sendSamples(incomingBluetoothSignal, parsedSamples);
  }
}

void setOperationModeFromIncomingBluetoothCommand(){
  Serial.println("Setting OperationMode from incoming Bluetooth Command");
  
  int fileIndex = 0;
  String parsedOperationMode = "";

  File file = SPIFFS.open("/incomingBluetoothCommand.txt", FILE_READ);
  while (file.available()) {
    char c = file.read();
    
    if(fileIndex >= INCOMING_BLUETOOTH_COMMAND_HEADER_LENGTH){
      // START PARSING THE SIGNAL FROM THE DATA PORTION OF THE COMMAND
      if(c == '\n'){
        operationMode = parsedOperationMode;
        Serial.println("Parsed Operation Mode: " + parsedOperationMode);
        break;
      } else {
        parsedOperationMode += c;
      }
    }

    fileIndex++;
  }
  file.close();
}


void setAdapterConfigurationFromIncomingBluetoothCommand(){
  Serial.println("Setting Adapter Configuration from incoming Bluetooth Command");
  
  int fileIndex = 0;
  String parsedConfigurationString = "";

  File file = SPIFFS.open("/incomingBluetoothCommand.txt", FILE_READ);
  while (file.available()) {
    char c = file.read();
    
    if(fileIndex >= INCOMING_BLUETOOTH_COMMAND_HEADER_LENGTH){
      // START PARSING THE SIGNAL FROM THE DATA PORTION OF THE COMMAND
      if(c == '\n'){
        setCC1101Configuration(parsedConfigurationString);
        Serial.println("Parsed Configuration String from Command: " + parsedConfigurationString);
        break;
      } else {
        parsedConfigurationString += c;
      }
    }

    fileIndex++;
  }
  file.close();

  digitalWrite(PIN_LED_ONBOARD, HIGH);
  delay(250);
  digitalWrite(PIN_LED_ONBOARD, LOW);
  delay(250);
  digitalWrite(PIN_LED_ONBOARD, HIGH);
  delay(250);
  digitalWrite(PIN_LED_ONBOARD, LOW);
  delay(250);
}

void sendSignalToBluetooth(int samples[], int samplesLength){
  if(SerialBT.hasClient()){
      Serial.println("STARTING TO SEND DATA VIA BLUETOOTH");
      int before = millis();  
      for (int i = 0; i < samplesLength; i++) {
        String currentValue = String(samples[i]);
        for (int c=0;c<strlen(currentValue.c_str());c++) {
          SerialBT.write(currentValue[c]);
        }
        if(i == (samplesLength - 1)){
          SerialBT.write('\n');
          break;
        } else {
          SerialBT.write(',');
        }
      }
      SerialBT.flush();

      int after = millis();

      Serial.print("Transmission ended after: ");
      Serial.print(after - before);
      Serial.println(" Milliseconds");
  }
}

void sendCommand(String command, String commandId, String dataString){
    String commandString = command + commandId + dataString;
    Serial.println("Sending Command: " + command + " with ID: " + commandId + ", Data: " + dataString);

    // SEND IT WITH BT SERIAL
    if(SerialBT.hasClient()){
        int before = millis();  
        for(int i = 0; i < commandString.length(); i++){
          SerialBT.write(commandString[i]);      
        }
        SerialBT.write('\n');
        SerialBT.flush();
        int after = millis();

        Serial.print("Command sent after: ");
        Serial.print(after - before);
        Serial.println(" Milliseconds");
    }

}

void sendSignalToBluetoothFromFile(String fileName){
  if(SerialBT.hasClient()){
    Serial.println("Sending Recorded File to Bluetooth");

    int before = millis();  

    // COMMAND
    for(int i = 0; i <= 3; i++){
        SerialBT.write(COMMAND_TRANSFER_SIGNAL_OVER_BLUETOOTH[i]);      
    }

    // COMMAND ID
    for(int i = 0; i <= 3; i++){
        SerialBT.write(COMMAND_TRANSFER_SIGNAL_OVER_BLUETOOTH[i]);
    }

    // SIGNAL DATA
    File file = SPIFFS.open(fileName, FILE_READ);   
    if(file){
        while(file.available()){
          char c = file.read();
          SerialBT.write(c);          
        }  
    }

    SerialBT.write('\n');
    SerialBT.flush();

    int after = millis();
    Serial.print("Transmission ended after: ");
    Serial.print(after - before);
    Serial.println(" Milliseconds");
  }
}

void loop() {

  // READ BTN STATES
  /*
  int state_btn_1 = analogRead(PIN_BTN_1);
  int state_btn_2 = analogRead(PIN_BTN_2);

  // BUTTON 1  
  if(state_btn_1 >= THRESHOLD_BTN_CLICK){
    while(analogRead(PIN_BTN_1) >= THRESHOLD_BTN_CLICK) {
      delay(10);
    }
    //BTN 1 CLICKED
    recordSignal();
  }

  // BUTTON 2
  if(state_btn_2 >= THRESHOLD_BTN_CLICK){
    while(analogRead(PIN_BTN_2) >= THRESHOLD_BTN_CLICK) {
      delay(10);
    }
    //BTN 2 CLICKED
    replaySignal();
  }*/

  // HANDLE OPERATION MODE
  if(operationMode == OPERATIONMODE_IDLE){
    delay(100);
  } else if(operationMode == OPERATIONMODE_PERISCOPE){
    periscope();
  } else if(operationMode == OPERATIONMODE_RECORD_SIGNAL){
    operationModeRecordSignal();
  } else if(operationMode == OPERATIONMODE_DETECT_SIGNAL){
    operationModeDetectSignal();
  } else if(operationMode == OPERATIONMODE_HANDLE_INCOMING_BLUETOOTH_COMMAND){
    // HANDLE INCOMING COMMAND
    handleIncomingCommand();
  }

  // KEEP TRACK
  lastExecutedOperationMode = operationMode;  
}

void handleIncomingCommand(){
  if(incomingBluetoothCommandParsed == COMMAND_REPLAY_SIGNAL_FROM_BLUETOOTH_COMMAND){
    // REPLAY
    replaySignalFromIncomingBluetoothCommand();
    incomingBluetoothCommandParsed = "";
  }

  if(incomingBluetoothCommandParsed == COMMAND_SET_OPERATION_MODE){
    setOperationModeFromIncomingBluetoothCommand();
    incomingBluetoothCommandParsed = "";
  }

  if(incomingBluetoothCommandParsed == COMMAND_SET_ADAPTER_CONFIGURATION){
    setAdapterConfigurationFromIncomingBluetoothCommand();
    incomingBluetoothCommandParsed = "";
  }

  if(incomingBluetoothCommandParsed == COMMAND_GET_ADAPTER_CONFIGURATION){
    returnAdapterConfigurationToBluetooth();
    incomingBluetoothCommandParsed = "";
  }
  
}


void sendCommandToBluetoothSerial(String command, String commandId, String dataString){
  
   if(SerialBT.hasClient()){
    Serial.println("Sending Command to Bluetooth Serial: " + command);

    int before = millis();  

    // COMMAND
    for(int i = 0; i <= 3; i++){
        SerialBT.write(command[i]);      
    }

    // COMMAND ID
    for(int i = 0; i <= 3; i++){
        SerialBT.write(commandId[i]);
    }

    // DATA
    for(char c : dataString){
      SerialBT.write(c);
    }  

    SerialBT.write('\n');
    SerialBT.flush();

    int after = millis();
    Serial.print("Transmission ended after: ");
    Serial.print(after - before);
    Serial.println(" Milliseconds");
  }

}

void returnAdapterConfigurationToBluetooth(){
  sendCommandToBluetoothSerial(COMMAND_SET_ADAPTER_CONFIGURATION, "0000", getCC1101Configuration());
}

#define MINIMUM_TRANSITIONS 32
#define MINIMUM_COPYTIME_US 16000
#define RESET443 32000 //32m
long lastCopyTime = 0;

void recordSignal(){
  int i, transitions = 0;
  lastCopyTime = 0;
 
  
  // RECORD TO FILE DIRECTLY:
  /*
  if(recordToFileDirectly){
    // OPEN FILE
    File file = SPIFFS.open(SPIFFS_FILENAME_RECORDED_SIGNAL, FILE_WRITE);
    // ADD CONFIGURATION TO FILE
    String cc1101Config = getCC1101Configuration();
    for(auto c : cc1101Config){file.write(c);}  

    transitions = tryRecordSignalToFile(file);          

    file.close();
  }
  */

  // RECORD TO BUFFER AND THEN WRITE TO SPIFFS
  while(transitions < MINIMUM_TRANSITIONS && lastCopyTime < MINIMUM_COPYTIME_US && (operationMode == OPERATIONMODE_PERISCOPE  || operationMode == OPERATIONMODE_RECORD_SIGNAL)){
    transitions = tryRecordSignalToBuffer();          
  }

  bool isSuccess = false;
  if(transitions >= MINIMUM_TRANSITIONS){
    if(lastCopyTime >= MINIMUM_COPYTIME_US){
      isSuccess = true;
    } else {
      Serial.println("Copytime too small: " + String(lastCopyTime));
    }
  } else {
    Serial.println("Not enough Transitions: " + String(transitions));
  }

  if(isSuccess){
    Serial.println("Signal Recorded successfully");
    Serial.println(String(transitions) + " Transitions Recorded");

    Serial.println("AVG RSSI: " + String(CC1101_LAST_AVG_RSSI));
    Serial.println("AVG LQI: " + String(CC1101_LAST_AVG_LQI));

    

    // WRITE RECORDED SIGNAL BUFFER TO SPIFFS FILE
    // REMOVE PREVIUOS FILE
    SPIFFS.remove(SPIFFS_FILENAME_RECORDED_SIGNAL);
    // OPEN FILE
    File file = SPIFFS.open(SPIFFS_FILENAME_RECORDED_SIGNAL, FILE_WRITE);
    // ADD CONFIGURATION TO FILE
    String cc1101Config = getCC1101Configuration();
    for(auto c : cc1101Config){file.write(c);}    

    writeSignalBufferToFile(file,recordedSignal, transitions);   

    file.close();
  
    dumpSpiffsFileToSerial(SPIFFS_FILENAME_RECORDED_SIGNAL);
    sendSignalToBluetoothFromFile(SPIFFS_FILENAME_RECORDED_SIGNAL);    
  } else {
    return;
  }
}

void detectSignal(int minRssi){
  bool signalDetected = false;
  detectedRssi = -100;
  detectedFrequency = 0.0;
  while(signalDetected == false){
      // ITERATE FREQUENCIES      
      for(float fMhz : signalDetectionFrequencies)
      {
          CC1101_MHZ = fMhz;
          initCC1101();

          int rssi = ELECHOUSE_cc1101.getRssi();

          if(rssi >= detectedRssi){
            detectedRssi = rssi;
            detectedFrequency = fMhz;
          }            
      }

      if(detectedRssi >= minRssi){

        signalDetected = true;

        // DEBUG
        Serial.print("MHZ: ");  
        Serial.print(detectedFrequency);   
        Serial.print(" RSSI: ");
        Serial.println(detectedRssi);
        delay(300);

        // SEND BACK
        String frequencyDetected = String(detectedFrequency); // MAX 6 DIGITS E.G. 123.00
        while(frequencyDetected.length() < 6){
          frequencyDetected = "0" + frequencyDetected;
        }

        String rssiDetected = String(detectedRssi * -1); // MAX 4 DIGITS E.G. -100
        while(rssiDetected.length() < 3){
          rssiDetected = "0" + rssiDetected;
        }
        rssiDetected = "-" + rssiDetected;

        String dataString = frequencyDetected + rssiDetected;
        sendCommand(COMMAND_SEND_DETECTED_FREQUENCY,COMMAND_ID_DUMMY,dataString);
        
      } 
  }
}

void writeSignalBufferToFile(File file, int signalBuffer[], int signalLength){
    String prepend = "";
    for(int i = 0; i <= signalLength; i++){
        String valueToAdd = prepend + String(signalBuffer[i]);
        writeStringToFile(file, valueToAdd); 
        prepend = ",";
    }
}


void periscope(){
  Serial.println("Looking around...");
  if(lastExecutedOperationMode != OPERATIONMODE_PERISCOPE || CC1101_TX == true){
    CC1101_TX = false;
    initCC1101();
  }
  digitalWrite(PIN_LED_ONBOARD, HIGH);  
  while(operationMode == OPERATIONMODE_PERISCOPE && CC1101_TX == false){
    recordSignal();    
    Serial.println("Looking for more Signals");
  }
  
  Serial.println("Periscope closed.");
  digitalWrite(PIN_LED_ONBOARD, LOW);  
}

void operationModeRecordSignal(){
  Serial.println("Recording a Signal");
  if(lastExecutedOperationMode != OPERATIONMODE_RECORD_SIGNAL || CC1101_TX == true){
    CC1101_TX = false;
    initCC1101();
  }
  digitalWrite(PIN_LED_ONBOARD, HIGH);  

  recordSignal();    

  Serial.println("Recording done.");
  digitalWrite(PIN_LED_ONBOARD, LOW);  
  setOperationMode(OPERATIONMODE_IDLE);
}

void operationModeDetectSignal(){

  if(incomingBluetoothCommandParsed == OPERATIONMODE_RECORD_SIGNAL && incomingBluetoothCommandDataStringParsed != "" && incomingBluetoothCommandDataStringParsed.length() == 4){
      int parsedMinRssi = incomingBluetoothCommandDataStringParsed.toInt();
      if(parsedMinRssi < 0){
        signalDetectionMinRssi = parsedMinRssi;          
      }
  }

  Serial.println("Detecting a Signal with min. RSSI: " + String(signalDetectionMinRssi));
  if(lastExecutedOperationMode != OPERATIONMODE_RECORD_SIGNAL || CC1101_TX == true){
    CC1101_TX = false;
    initCC1101();
  }
  digitalWrite(PIN_LED_ONBOARD, HIGH);  

  while(operationMode == OPERATIONMODE_DETECT_SIGNAL && CC1101_TX == false){
    detectSignal(signalDetectionMinRssi);   
  }
   
  Serial.println("Detection done.");
  digitalWrite(PIN_LED_ONBOARD, LOW);  
  setOperationMode(OPERATIONMODE_IDLE);
}

void replaySignal(){
  sendSamples(recordedSignal, MAX_LENGHT_RECORDED_SIGNAL);
}

void sendSamples(int samples[], int samplesLenght) {
  if(CC1101_TX == false){
    CC1101_TX = true;
    initCC1101();
  }
  
  Serial.println("Transmitting " + String(samplesLenght) + " Samples");

  int delay = 0;
  unsigned long time;
  byte n = 0;

  for (int i=0; i < samplesLenght; i++) {
    // TRANSMIT
    n = 1;
    delay = samples[i];
    if(delay < 0){
      // DONT TRANSMIT
      delay = delay * -1;
      n = 0;
    }

    digitalWrite(PIN_GDO0,n);
    delayMicroseconds(delay);
  }

  // STOP TRANSMITTING
  digitalWrite(PIN_GDO0,0);

  Serial.println("Transmission completed.");
}

// ------------------------- COPY REPLAY STUFF --> REFACTOR THIS !!!
#define LOOPDELAY 20
#define HIBERNATEMS 30*1000
//#define BUFSIZE MAX_LENGHT_RECORDED_SIGNAL
#define REPLAYDELAY 0
// THESE VALUES WERE FOUND PRAGMATICALLY

#define WAITFORSIGNAL 32 // 32 RESET CYCLES


#define DUMP_RAW_MBPS 0.1 // as percentage of 1Mbps, us precision. (100kbps) This is mainly to dump and analyse in, ex, PulseView
#define BOUND_SAMPLES true
int delayus = REPLAYDELAY;


/*
void copy() {
  int i, transitions = 0;
  lastCopyTime = 0;
  //FILTER OUT NOISE SIGNALS (too few transistions or too fast)
  while (transitions < MINIMUM_TRANSITIONS && lastCopyTime < MINIMUM_COPYTIME_US) {
    transitions = trycopy();
  }
  //CLEAN LAST ELEMENTS
  for (i=transitions-1;i>0;i--) {
    if (recordedSignal[i] == RESET443) recordedSignal[i] = 0;
    else break;
  }
  if (BOUND_SAMPLES) {
    recordedSignal[0] = 200;
    if (i < MAX_LENGHT_RECORDED_SIGNAL) recordedSignal[i+1] = 200;
  }
}*/


/*
void copySignal() {
  int i, transitions = 0;
  lastCopyTime = 0;
  //FILTER OUT NOISE SIGNALS (too few transistions or too fast)

  // REMOVE PREVIUOS FILE
  SPIFFS.remove(SPIFFS_FILENAME_RECORDED_SIGNAL);
  // OPEN FILE
  File file = SPIFFS.open(SPIFFS_FILENAME_RECORDED_SIGNAL, FILE_WRITE);
  // ADD CONFIGURATION TO FILE
  String cc1101Config = getCC1101Configuration();
  for(auto c : cc1101Config)
  {
    file.write(c);    
  }

  /*
  while (transitions < MINIMUM_TRANSITIONS && lastCopyTime < MINIMUM_COPYTIME_US && operationMode == OPERATIONMODE_PERISCOPE) {
    transitions = tryCopySignalToFile(file);
  }#/
  transitions = tryCopySignalToFile(file);
  file.close();
   
  if( !((transitions < MINIMUM_TRANSITIONS && lastCopyTime < MINIMUM_COPYTIME_US && operationMode == OPERATIONMODE_PERISCOPE))){
    Serial.println("Signal Recorded successfully");
    dumpSpiffsFileToSerial(SPIFFS_FILENAME_RECORDED_SIGNAL);
    sendSignalToBluetoothFromFile(SPIFFS_FILENAME_RECORDED_SIGNAL);
  }

  if(transitions < MINIMUM_TRANSITIONS && lastCopyTime < MINIMUM_COPYTIME_US){
    return;    
  }
  
  //CLEAN LAST ELEMENTS
  /*
  for (i=transitions-1;i>0;i--) {
    if (recordedSignal[i] == RESET443) recordedSignal[i] = 0;
    else break;
  }
  if (BOUND_SAMPLES) {
    recordedSignal[0] = 200;
    if (i < MAX_LENGHT_RECORDED_SIGNAL) recordedSignal[i+1] = 200;
  }#//
}*/


void dumpSpiffsFileToSerial(String fileName){
  Serial.println("Content of File: " + fileName);
  
  File file = SPIFFS.open(fileName, FILE_READ);
  while (file.available()) {
    char c = file.read();
    Serial.print(c);
  }

  Serial.println("<-- End of Content");
  file.close(); 
}


/*
int trycopy() {
  int i;
  Serial.println("Copying...");

  // CLEAR ANY PREVIOUSLY RECORDED SIGNAL
  memset(recordedSignal,0,MAX_LENGHT_RECORDED_SIGNAL*sizeof(int));
  byte n = 0;
  int sign = -1;

  int64_t startus = esp_timer_get_time();
  int64_t startread;
  int64_t dif = 0;
  int64_t ttime = 0;
  for (i = 0; i < MAX_LENGHT_RECORDED_SIGNAL; i++) {
    startread = esp_timer_get_time();
    dif = 0;
    //WAIT FOR INIT
    while (dif < RESET443) {
      dif = esp_timer_get_time() - startread;
      if (CCAvgRead() != n) {
        break;
      }
    }
    if (dif >= RESET443) {
      recordedSignal[i] = RESET443 * sign;
      //if not started wait...
      if (i == 0) {
        i = -1;
        ttime++;
        if (ttime > WAITFORSIGNAL) {
          Serial.println("No signal detected!");
          return -1;
        }
      }
      else {
        ttime++;
        if (ttime > WAITFORSIGNAL) {
          Serial.println("End of signal detected!");
          break;
        }
      }
    }
    else {
      recordedSignal[i] = dif * sign;
      n = !n;
      if(n) {
        sign = 1;
      } else {
        sign = -1;
      }
    }
  }
  
  int64_t stopus = esp_timer_get_time();
  Serial.print("Copy took (us): ");
  lastCopyTime = (long)(stopus - startus);
  Serial.println(lastCopyTime , DEC);
  Serial.print("Transitions: ");
  Serial.println(i);
  //memcpy(signal433_current,newsignal433,BUFSIZE*sizeof(uint16_t));
  recordedSamples = i;

  //SEND TO APP
  sendSignalToBluetooth(recordedSignal, recordedSamples);

  return i;
}
*/


int lqiCounter = 1;
int lqiRecorded = 1;
int rssiCounter = 1;
int rssiRecorded = 1;

int tryRecordSignalToBuffer(){
  int i;
  lastCopyTime = 0;
  //Serial.println("Copying...");
  // CLEAR ANY PREVIOUSLY RECORDED SIGNAL
  memset(recordedSignal,0,MAX_LENGHT_RECORDED_SIGNAL*sizeof(int));
  /*
  lqiCounter = 0;
  lqiRecorded = 0;
  rssiCounter = 0;
  rssiRecorded = 0;*/

  byte n = 0;
  int sign = -1;

  int64_t startus = esp_timer_get_time();
  int64_t startread;
  int64_t dif = 0;
  int64_t ttime = 0;
  for (i = 0; i < MAX_LENGHT_RECORDED_SIGNAL; i++) {
    startread = esp_timer_get_time();
    dif = 0;
    //WAIT FOR INIT
    while (dif < RESET443) {
      dif = esp_timer_get_time() - startread;
      if (CCAvgRead() != n) {
        break;
      }
    }
    if (dif >= RESET443) {
      recordedSignal[i] = RESET443 * sign;
      //if not started wait...
      if (i == 0) {
        i = -1;
        ttime++;
        if (ttime > WAITFORSIGNAL) {
          //Serial.println("No signal detected!");
          return -1;
        }
      }
      else {
        ttime++;
        if (ttime > WAITFORSIGNAL) {
          //Serial.println("End of signal detected!");
          break;
        }
      }
    }
    else {
      recordedSignal[i] = dif * sign;

      if(sign > 0){
        // UPDATE RSSI / LQI
        /*
        lqiCounter++;
        lqiRecorded += ELECHOUSE_cc1101.getLqi();
        rssiCounter++;
        rssiRecorded += ELECHOUSE_cc1101.getRssi();
        */
      }
      n = !n;
      if(n) {
        sign = 1;
      } else {
        sign = -1;
      }
    }
  }

  recordedSamples = i;

  int64_t stopus = esp_timer_get_time();
  lastCopyTime = (long)(stopus - startus);

  CC1101_LAST_AVG_LQI = lqiRecorded / lqiCounter;
  CC1101_LAST_AVG_RSSI = rssiRecorded / rssiCounter;

  return i;  
}

/*
int tryRecordSignalToFile(File file) {
  int i;
  Serial.println("Recording...");

  byte n = 0;
  int sign = -1;
  int currentVal = 0;
  int64_t startus = esp_timer_get_time();
  int64_t startread;
  int64_t dif = 0;
  int64_t ttime = 0;
  String prepend = "";

  for (i = 0; i < MAX_LENGHT_RECORDED_SIGNAL; i++) {
    startread = esp_timer_get_time();
    dif = 0;
    //WAIT FOR INIT
    while (dif < RESET443 && CC1101_TX == false) {
      dif = esp_timer_get_time() - startread;
      if (CCAvgRead() != n) {
        break;
      }
    }
    if(CC1101_TX == true){
      Serial.println("Adapter Config was changed");
      return i;      
    }
    if (dif >= RESET443) {
      if(i > 0){
        String valueToAdd = prepend + String(RESET443 * sign);
        prepend = ",";
        writeStringToFile(file, valueToAdd);
      }
      //if not started wait...
      if (i == 0) {
        i = -1;
        ttime++;
        if (ttime > WAITFORSIGNAL) {
          Serial.println("No signal detected!");
          return -1;
        }
      }
      else {
        ttime++;
        if (ttime > WAITFORSIGNAL) {
          Serial.println("End of signal detected!");
          break;
        }
      }
    }
    else {
      //recordedSignal[i] = dif * sign;
      currentVal = dif * sign;
      String valueToAdd = prepend + String(currentVal);
      prepend = ",";
      writeStringToFile(file, valueToAdd);
      n = !n;
      if(n) {
        sign = 1;
      } else {
        sign = -1;
      }
    }
  }

  int64_t stopus = esp_timer_get_time();
  lastCopyTime = (long)(stopus - startus);

  return i;
}*/

void writeStringToFile(File file, String data){
  //Serial.println("Adding String to File: " + data);
  for(char c : data){
    file.write(c);    
  }
}


#define BAVGSIZE 11
byte bavg[BAVGSIZE];
byte pb = 0;
byte cres = 0;

byte CCAvgRead() {
  cres -= bavg[pb];
  bavg[pb] = digitalRead(PIN_GDO0);
  cres += bavg[pb];
  pb++;
  if (pb >= BAVGSIZE) pb = 0;
  if (cres > BAVGSIZE/2) return 1;
  return 0;
}

/*
void dump(){
  int i;
  int n = 0;
  Serial.println("Dump transition times: ");
  for (i = 0; i < recordedSamples; i++) {
    if (i > 0) Serial.print(",");
    Serial.print(recordedSignal[i]);
  }  
  Serial.println("");
}*/