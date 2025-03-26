#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRac.h>
#include <IRtext.h>
#include <IRutils.h>

const uint16_t kRecvPin = 1;
const uint16_t kCaptureBufferSize = 1024;
const uint8_t kTimeout = 15;
IRrecv irrecv(kRecvPin, kCaptureBufferSize, kTimeout, true);
decode_results results; // Somewhere to store the results
const uint16_t kIrLed = 2;
IRsend irsend(kIrLed);

BLEServer *pServer = NULL;
BLECharacteristic *dataCharacteristics[4];
BLECharacteristic *metaCharacteristics[4];
bool deviceConnected = false;
bool oldDeviceConnected = false;
uint32_t value = 0;

#define SERVICE_UUID "19b10000-e8f2-537e-4f6c-d104768a1214"
const char *Data_Characteristic_UUIDs[4] = {"19b10001-e8f2-537e-4f6c-d104768a1214", "19b10002-e8f2-537e-4f6c-d104768a1214", "19b10003-e8f2-537e-4f6c-d104768a1214", "19b10004-e8f2-537e-4f6c-d104768a1214"};
const char *Meta_Characteristic_UUIDs[4] = {"19b10005-e8f2-537e-4f6c-d104768a1214", "19b10006-e8f2-537e-4f6c-d104768a1214", "19b10007-e8f2-537e-4f6c-d104768a1214", "19b10008-e8f2-537e-4f6c-d104768a1214"};

class DataCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pCharacteristic)
  {
    Serial.println("Started reading");
    std::string length_str = pCharacteristic->getValue();
    int length = atoi(length_str.c_str());
    uint16_t buf[length];
    for (int i = 0; i < 4; i++)
    {
      uint8_t *value = dataCharacteristics[i]->getData();
      for (int j = 0; j < 256; j++)
      {
        if (j + i * 256 >= length)
          break;
        uint16_t v = value[j * 2];
        v = v << 8;
        v |= value[j * 2 + 1];
        buf[j + i * 256] = v;
      }
    }
    Serial.println();
    Serial.println("Buf :");
    // print buf content
    for (int i = 0; i < length; i++)
    {
      Serial.print(buf[i]);
      Serial.print(" ");
      delay(1);
    }
    Serial.println();
    Serial.print("Length: ");
    Serial.println(length);
    irsend.sendRaw(buf, length, 38);
    delay(1000);
  }
};

class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    deviceConnected = true;
  };

  void onDisconnect(BLEServer *pServer)
  {
    deviceConnected = false;
  }
};

void setup()
{
  Serial.begin(115200);
  irrecv.enableIRIn();
  irsend.begin();

  // // Create the BLE Device
  BLEDevice::init("ESP32");

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(BLEUUID(SERVICE_UUID), 20);

  for (int i = 0; i < 4; i++)
  {
    dataCharacteristics[i] = pService->createCharacteristic(
        Data_Characteristic_UUIDs[i],
        BLECharacteristic::PROPERTY_READ |
            BLECharacteristic::PROPERTY_WRITE |
            BLECharacteristic::PROPERTY_NOTIFY |
            BLECharacteristic::PROPERTY_INDICATE);
    dataCharacteristics[i]->addDescriptor(new BLE2902());
  }
  for (int i = 0; i < 4; i++)
  {
    metaCharacteristics[i] = pService->createCharacteristic(
        Meta_Characteristic_UUIDs[i],
        BLECharacteristic::PROPERTY_READ |
            BLECharacteristic::PROPERTY_WRITE |
            BLECharacteristic::PROPERTY_NOTIFY |
            BLECharacteristic::PROPERTY_INDICATE);
    metaCharacteristics[i]->addDescriptor(new BLE2902());
  }

  metaCharacteristics[0]->setCallbacks(new DataCallbacks());
  // Start the service
  pService->start();

  // // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0); // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();
  Serial.println("Waiting a client connection to notify...");
}

void loop()
{

  if (deviceConnected)
  {
    if (irrecv.decode(&results) && results.decode_type != LUTRON)
    {
      String description = IRAcUtils::resultAcToString(&results);
      String str = resultToHumanReadableBasic(&results) + description;
      metaCharacteristics[1]->setValue(str.c_str());
      volatile uint16_t *rawData = results.rawbuf;
      int len = results.rawlen - 1;

      uint16_t datas[4][256];

      Serial.println(resultToSourceCode(&results));

      irrecv.resume();
      for (int i = 0; i < 256; i++)
      {
        for (int j = 0; j < 4; j++)
        {
          int index = i + j * 256 + 1;
          // take care of invalid access
          if (index < len)
          {
            datas[j][i] = rawData[index] * 2;
          }
          else
          {
            datas[j][i] = 0;
          }
        }
      }
      for (int i = 0; i < 4; i++)
      {
        dataCharacteristics[i]->setValue((uint8_t *)datas[i], 512);
        dataCharacteristics[i]->notify();
      }
      metaCharacteristics[0]->setValue(len);
      metaCharacteristics[0]->notify();
      Serial.println();
      Serial.println("sent: ");
      Serial.println(results.rawlen);
      yield();
    }
  }

  // disconnecting
  if (!deviceConnected && oldDeviceConnected)
  {
    Serial.println("Device disconnected.");
    delay(500);                  // give the bluetooth stack the chance to get things ready
    pServer->startAdvertising(); // restart advertising
    Serial.println("Start advertising");
    oldDeviceConnected = deviceConnected;
  }
  // connecting
  if (deviceConnected && !oldDeviceConnected)
  {
    // do stuff here on connecting
    oldDeviceConnected = deviceConnected;
    Serial.println("Device Connected");
  }
}