#include <Arduino.h>

#include "Tomato.h"


TOMATO::TOMATO(uint8_t SSpin, uint8_t BUSYpin, uint8_t RSTpin)
              : PN5180ISO15693(SSpin, BUSYpin, RSTpin) {
}

ISO15693ErrorCode TOMATO::issueISO15693Command(uint8_t *cmd, uint8_t cmdLen, uint8_t **resultPtr) {
#ifdef DEBUG
  Serial.print(F("Issue Command 0x"));
  Serial.print(formatHex(cmd[1]));
  Serial.print("...\n");
#endif

  sendData(cmd, cmdLen);
  delay(10);

  if (0 == (getIRQStatus() & RX_SOF_DET_IRQ_STAT)) {
    return EC_NO_CARD;
  }

  uint32_t rxStatus;
  readRegister(RX_STATUS, &rxStatus);

  Serial.print(F("RX-Status="));
  Serial.print(rxStatus, HEX);

  uint16_t len = (uint16_t)(rxStatus & 0x000001ff);

  Serial.print(", len=");
  Serial.print(len);
  Serial.print("\n");

 *resultPtr = readData(len);
  if (0L == *resultPtr) {
    Serial.print(F("*** ERROR in readData!\n"));
    return ISO15693_EC_UNKNOWN_ERROR;
  }

#ifdef DEBUG
  Serial.print("Read=");
  for (int i=0; i<len; i++) {
    Serial.print(formatHex((*resultPtr)[i]));
    if (i<len-1) Serial.print(":");
  }
  Serial.println();
#endif

  uint32_t irqStatus = getIRQStatus();
  if (0 == (RX_SOF_DET_IRQ_STAT & irqStatus)) { // no card detected
     clearIRQStatus(TX_IRQ_STAT | IDLE_IRQ_STAT);
     return EC_NO_CARD;
  }

  uint8_t responseFlags = (*resultPtr)[0];
  if (responseFlags & (1<<0)) { // error flag
    uint8_t errorCode = (*resultPtr)[1];

    Serial.print("ERROR code=");
    Serial.print(errorCode, HEX);
    Serial.print(" - ");
    Serial.print(strerror(ISO15693ErrorCode(errorCode)));
    Serial.print("\n");

    if (errorCode >= 0xA0) { // custom command error codes
      return ISO15693_EC_CUSTOM_CMD_ERROR;
    }
    else return (ISO15693ErrorCode)errorCode;
  }

#ifdef DEBUG
  if (responseFlags & (1<<3)) { // extendsion flag
    Serial.print("Extension flag is set!\n");
  }
#endif

  clearIRQStatus(RX_SOF_DET_IRQ_STAT | IDLE_IRQ_STAT | TX_IRQ_STAT | RX_IRQ_STAT);
  return ISO15693_EC_OK;
}

ISO15693ErrorCode TOMATO::getPatchInfo(uint8_t *patchInfo) {
  uint8_t readPatchInfo[] = { 0x02, 0xa1, 0x07 }; // LSB first!

#ifdef DEBUG
  Serial.print("Read Patch Info");
  Serial.print(": ");
  for (int i=0; i<sizeof(readPatchInfo); i++) {
    Serial.print(" ");
    Serial.print(readPatchInfo[i], HEX);
  }
  Serial.print("\n");
#endif

  uint8_t *resultPtr;
  ISO15693ErrorCode rc = issueISO15693Command(readPatchInfo, sizeof(readPatchInfo), &resultPtr);
  if (ISO15693_EC_OK != rc) {
    return rc;
  }
#ifdef DEBUG
  Serial.print("Value=");
#endif
  for (int i=0; i<7; i++) {
    patchInfo[i] = resultPtr[i];
#ifdef DEBUG
    Serial.print(formatHex(patchInfo[i]));
    Serial.print(" ");
#endif
  }

#ifdef DEBUG
  Serial.print(" ");
  for (int i=0; i<7; i++) {
    char c = patchInfo[i];
    if (isPrintable(c)) {
      Serial.print(c);
    }
    else Serial.print(".");
  }
  Serial.print("\n");
#endif

  return ISO15693_EC_OK;
}


ISO15693ErrorCode TOMATO::getReadings(uint8_t *readings, uint8_t blockNo) {
  uint8_t readReadings[] = { 0x02, 0x23, blockNo, 0x02 }; // LSB first!

#ifdef DEBUG
  Serial.print("Read Readings Block #");
  Serial.print(blockNo);
  Serial.print(": ");
  for (int i=0; i<sizeof(readReadings); i++) {
    Serial.print(" ");
    Serial.print(formatHex(readReadings[i]));
  }
  Serial.print("\n");
#endif

  uint8_t *resultPtr;
  ISO15693ErrorCode rc = issueISO15693Command(readReadings, sizeof(readReadings), &resultPtr);
  if (ISO15693_EC_OK != rc) {
    return rc;
  }
#ifdef DEBUG    
  Serial.print("Value=");
#endif
  for (int i=0; i<25; i++) {
    readings[i] = resultPtr[i];
#ifdef DEBUG    
    Serial.print(formatHex(readings[i]));
    Serial.print(" ");
#endif
  }

#ifdef DEBUG
  Serial.print(" ");
  for (int i=0; i<7; i++) {
    char c = readings[i];
    if (isPrintable(c)) {
      Serial.print(c);
    }
    else Serial.print(".");
  }
  Serial.print("\n");
#endif

  return ISO15693_EC_OK;
}