#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

#include "Tomato.h"
#include "Utils.h"


#if defined(ARDUINO_ARCH_ESP32)

#define PN5180_NSS  16
#define PN5180_BUSY 5
#define PN5180_RST  17
#define BATTERY_PIN  32

#else
#error define your pin here
#endif

// get UUID for tomato in xdrip+ project, see
// https://github.com/NightscoutFoundation/xDrip

#define BLE_DEVICE_NAME "miaomiao"
#define NRF_UART_SERVICE "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NRF_UART_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
#define NRF_UART_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"


#define uS_TO_S_FACTOR 1000000ULL   /* Conversion factor for micro seconds to seconds */
#define SEC_IN_MIN 60

#define TOMATO_FIRMWARE 0x0100
#define TOMATO_HARDWARE 0x0100

RTC_DATA_ATTR uint minuteToSleep = 1; // minute to deep sleep, can be alter by command 0xD1

uint8_t sensorNotStart = 0x32;
uint8_t sensorNotFound = 0x34;

BLEServer *pServer = NULL;
BLECharacteristic * pCharacteristicTx;
bool deviceConnected = false;
bool msgSent = false;


TOMATO nfc(PN5180_NSS, PN5180_BUSY, PN5180_RST);

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    deviceConnected = true;
  };
  void onDisconnect(BLEServer *pServer) {
    deviceConnected = false;
  }
};

class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string rxValue = pCharacteristic->getValue();
    if (rxValue.length() > 0) {
      Serial.print("Received data: ");
      for (int i = 0; i < rxValue.length(); i++)
      {
        Serial.printf("%X ",rxValue[i]);
      }
      Serial.println();
    }
    if ((char)rxValue[0] == 0xD1 ) {
      minuteToSleep = (uint)rxValue[1];
    }
    if ((char)rxValue[0] == 0xF0 ) {
      Serial.println("Initialising NFC ...");
      nfc.begin();
      nfc.setupRF();
      Serial.println("NFC initialised");

      /* Get UID */
      uint8_t uid[8];
      ISO15693ErrorCode rc = nfc.getInventory(uid);
      if (ISO15693_EC_OK != rc) {
        Serial.print("Error in getInventory: ");
        Serial.println(nfc.strerror(rc));
        pCharacteristicTx->setValue(&sensorNotFound, 1);
        pCharacteristicTx->notify();
        msgSent = true;
        return;
      }
#ifdef DEBUG
      Serial.print("Inventory successful, UID=");
      for (int i=0; i<8; i++) {
        Serial.print(uid[i], HEX);
        Serial.print(" ");
      }
      Serial.println();
#endif
      /* Get UID End */

      /* Get PatchInfo */
      uint8_t patchInfo[7];
      rc = nfc.getPatchInfo(patchInfo);
      if (ISO15693_EC_OK != rc) {
        Serial.print("Error in getPatchInfo: ");
        Serial.println(nfc.strerror(rc));
        pCharacteristicTx->setValue(&sensorNotFound, 1);
        pCharacteristicTx->notify();
        msgSent = true;
        return;
      }
#ifdef DEBUG
      Serial.print("getPatchInfo successful, PatchInfo=");
      for (int i=0; i<sizeof(patchInfo); i++) {
        Serial.print(patchInfo[i], HEX);
        Serial.print(" ");
      }
      Serial.println();
#endif
      /* Get PatchInfo End */

      /* Get Data */
      uint8_t data[360];
      uint8_t replyData[25];
      int pcount = 0;
      for (int i = 0; i <= 43; i = i + 3) {
        rc = nfc.getReadings(replyData, (uint8_t)i);
        if (ISO15693_EC_OK != rc) {
          Serial.print("Error in getReadings: ");
          Serial.println(nfc.strerror(rc));
          pCharacteristicTx->setValue(&sensorNotFound, 1);
          pCharacteristicTx->notify();
          msgSent = true;
          return;
        }
#ifdef DEBUG
        Serial.print("getReadings successful, data=");
        for (int j=0; j<sizeof(replyData) - 1; j++) {
          data[(pcount * 24) + j] = replyData[j+1];
          Serial.print(replyData[j+1], HEX);
          Serial.print(" ");
        }
        Serial.println();
#endif
        pcount++;
      }
      /* Get Data End */

      nfc.setRF_off();
      nfc.end();

      /* Assemble the message package */
      const uint8_t sizeOfData = sizeof(data);
      uint8_t reply[369];
      reply[0] = 0x28;                                    // tomato protocal header
      reply[1] = sizeof(reply) / 256;                     // Higher byte for message lenght
      reply[2] = sizeof(reply) % 256;                     // Lower byte for message lenght
                                                          // 4th & 5th not found (help wanted)
      for (int j = 0; j < sizeof(uid); j++) {             // 6th to 14th UID by get inventory
        reply[5+j] = uid[j];
      }
      reply[13] = toPercentage(analogRead(BATTERY_PIN));  // device battery

      reply[14] = TOMATO_FIRMWARE >> 8;                   // Firmware Version
      reply[15] = TOMATO_FIRMWARE & 0xff;
      reply[16] = TOMATO_HARDWARE >> 8;                   // Hardware Version
      reply[17] = TOMATO_HARDWARE & 0xff;

      for (int j = 0; j < 344; j++) {                     // Libre readings
        reply[18+j] = data[j];
      }

      for (int j = 0; j < 6; j++) {                       // patchinfo
        reply[18+ 344 + 1+j] = patchInfo[j+1];
      }
      /* Assemble the message package end */

      /* Send bluetooth message */
      // divide into 20 bytes long chunks, 
      // for default MTU is 23 bytes(3 for header)
      const int chunk = sizeof(reply) / 20;
      const int remaining = sizeof(reply) % 20;
      for (int i=0;i<chunk;i++) {
        pCharacteristicTx->setValue(&reply[i*20], 20);
        pCharacteristicTx->notify();
        delay(10);
      }
      if (remaining) {
        pCharacteristicTx->setValue(&reply[chunk*20], remaining);
        pCharacteristicTx->notify();
      }
      /* Send bluetooth message end */
      msgSent = true;
    }
  }
};


void setup() {
  Serial.begin(115200);

  Serial.println("Initialising bluetooth...");
  msgSent = false;
  BLEDevice::init(BLE_DEVICE_NAME);

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(NRF_UART_SERVICE);

  pCharacteristicTx = pService->createCharacteristic(
                      NRF_UART_TX, 
                      BLECharacteristic::PROPERTY_NOTIFY
                      ); 
  pCharacteristicTx->addDescriptor(new BLE2902());

  BLECharacteristic *pCharacteristicRx = pService->createCharacteristic(
                                        NRF_UART_RX, 
                                        BLECharacteristic::PROPERTY_WRITE
                                        );
  pCharacteristicRx->setCallbacks(new MyCallbacks());

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(NRF_UART_SERVICE);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("Bluetooth initialised!");

}

void loop() {
  const int expireThreadhold = 300;
  int expire = 0;
  // wait for msg sent or too long
  while(!msgSent & expire < expireThreadhold) {
    expire++;
    delay(100); //check every 100ms
  }

  if (!msgSent & expire >= expireThreadhold) {
    Serial.println("Waited for " + String(expireThreadhold/10) + " Seconds");
  } else {
    Serial.println("Message sent!");
  }

  // going to Deep Sleep mode
  esp_sleep_enable_timer_wakeup(minuteToSleep * SEC_IN_MIN * uS_TO_S_FACTOR);
  Serial.println("Going to sleep for " + String(minuteToSleep) +
  " minutes");
  Serial.flush();
  esp_deep_sleep_start();
}

// Help function for PN5180 library
void showIRQStatus(uint32_t irqStatus) {
  Serial.print(F("IRQ-Status 0x"));
  Serial.print(irqStatus, HEX);
  Serial.print(": [ ");
  if (irqStatus & (1<< 0)) Serial.print(F("RX_END "));
  if (irqStatus & (1<< 1)) Serial.print(F("TX_END "));
  if (irqStatus & (1<< 2)) Serial.print(F("IDLE "));
  if (irqStatus & (1<< 3)) Serial.print(F("MODE_DETECTED "));
  if (irqStatus & (1<< 4)) Serial.print(F("CARD_ACTIVATED "));
  if (irqStatus & (1<< 5)) Serial.print(F("STATE_CHANGE "));
  if (irqStatus & (1<< 6)) Serial.print(F("RFOFF_DET "));
  if (irqStatus & (1<< 7)) Serial.print(F("RFON_DET "));
  if (irqStatus & (1<< 8)) Serial.print(F("TX_RFOFF "));
  if (irqStatus & (1<< 9)) Serial.print(F("TX_RFON "));
  if (irqStatus & (1<<10)) Serial.print(F("RF_ACTIVE_ERROR "));
  if (irqStatus & (1<<11)) Serial.print(F("TIMER0 "));
  if (irqStatus & (1<<12)) Serial.print(F("TIMER1 "));
  if (irqStatus & (1<<13)) Serial.print(F("TIMER2 "));
  if (irqStatus & (1<<14)) Serial.print(F("RX_SOF_DET "));
  if (irqStatus & (1<<15)) Serial.print(F("RX_SC_DET "));
  if (irqStatus & (1<<16)) Serial.print(F("TEMPSENS_ERROR "));
  if (irqStatus & (1<<17)) Serial.print(F("GENERAL_ERROR "));
  if (irqStatus & (1<<18)) Serial.print(F("HV_ERROR "));
  if (irqStatus & (1<<19)) Serial.print(F("LPCD "));
  Serial.println("]");
}
