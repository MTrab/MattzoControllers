#include <Arduino.h>
#include <NimBLEDevice.h>

// MattzoControllerType
#define MATTZO_CONTROLLER_TYPE "MTC4SB"

// MattzoController network configuration (the following file needs to be moved into the Arduino library folder):
//#include <MattzoController_Network_Configuration.h>

#include "MattzoController_Library.h"
#include "MattzoWifiClient.h"
#include "MattzoMQTTPublisher.h"
#include "MattzoMQTTSubscriber.h"
#include "SBrickConst.h"
#include "SBrickHubClient.h"

// Globals
NimBLEScan *scanner;
SBrickHubClient *mySBricks[2] = {
    new SBrickHubClient("YC66405", "00:07:80:d0:47:43"),
    new SBrickHubClient("HE10233", "00:07:80:d0:3a:f2"),
    // new SBrickHubClient("BC60052", "88:6b:0f:23:78:10")
};

/// <summary>
/// Boolean value indicating whether the MQTT Publisher and Subscriber are enabled.
/// </summary>
bool ENABLE_MQTT = false;

/// <summary>
/// Number of message to send the MQTT queue can hold before we start dropping them.
/// </summary>
int MQTT_QUEUE_LENGTH = 1000;

/// <summary>
/// BLE scan duration in seconds. If the device isn't found within this timeframe the scan is aborted.
/// </summary>
uint32_t BLE_SCAN_DURATION_IN_SECONDS = 1;

/// <summary>
/// Duration between BLE discovery and connect attempts in seconds.
/// </summary>
uint32_t BLE_CONNECT_DELAY_IN_SECONDS = 5;

/// <summary>
/// Sets the watchdog timeout (0D &lt; timeout in 0.1 secs, 1 byte &gt;)
/// The purpose of the watchdog is to stop driving in case of an application failure.
/// Watchdog starts when the first DRIVE command is issued during a connection.
/// Watchdog is stopped when all channels are either set to zero drive, or are braking.
/// The value is saved to the persistent store.
/// The recommended watchdog frequency is 0.2-0.5 seconds, but a smaller and many larger settings are also available.
/// Writing a zero disables the watchdog.
/// By default watchdog is set to 5, which means a 0.5 second timeout.
/// </summary>
const int8_t WATCHDOG_TIMEOUT_IN_TENS_OF_SECONDS = 20;

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  char msg[length + 1];
  for (int i = 0; i < length; i++)
  {
    msg[i] = (char)payload[i];
  }
  msg[length] = '\0';

  Serial.println("[" + String(xPortGetCoreID()) + "] Ctrl: Received MQTT message [" + String(topic) + "]: " + String(msg));
}

void setup()
{
  // Configure Serial.
  Serial.begin(115200);

  // Wait a moment to start (so we don't miss Serial output).
  delay(1000 / portTICK_PERIOD_MS);
  Serial.println();
  Serial.println("[" + String(xPortGetCoreID()) + "] Ctrl: Starting MattzoTrainController for SBrick...");

  // Setup Mattzo controller.
  setupMattzoController(MATTZO_CONTROLLER_TYPE);

  if(ENABLE_MQTT) {
    // Setup MQTT publisher (with a queue that can hold 1000 messages).
    MattzoMQTTPublisher::Setup(MQTT_QUEUE_LENGTH);

    // Setup MQTT subscriber.
    MattzoMQTTSubscriber::Setup("rocrail/service/command", mqttCallback);
  }

  Serial.println("[" + String(xPortGetCoreID()) + "] Setup: Initializing BLE...");

  // Initialize BLE client.
  NimBLEDevice::init("");

  // Configure a BLE scanner.
  scanner = NimBLEDevice::getScan();
  scanner->setInterval(1349);
  scanner->setWindow(449);
  scanner->setActiveScan(true);

  Serial.print("[" + String(xPortGetCoreID()) + "] Setup: Number of SBrick(s) to discover: ");
  Serial.println(sizeof(mySBricks) / sizeof(mySBricks[0]));
}

void loop()
{
  for (int i = 0; i < sizeof(mySBricks) / sizeof(mySBricks[0]); i++)
  {
    SBrickHubClient *sbrick = mySBricks[i];

    if (sbrick->IsConnected())
    {
      // Drive at avarage speed (supported range: 0-255) on all channels either forwards or backwards.
      sbrick->Drive(-75, -75, 75, 75);
    }
    else
    {
      if (!sbrick->IsDiscovered())
      {
        // SBrick not discovered yet, first discover it.
        sbrick->StartDiscovery(scanner, BLE_SCAN_DURATION_IN_SECONDS);
      }

      if (sbrick->IsDiscovered())
      {
        // SBrick discovered, try to connect now.
        if (!sbrick->Connect(WATCHDOG_TIMEOUT_IN_TENS_OF_SECONDS))
        {
          // Connect attempt failed. Will retry in next loop.
          Serial.println("[" + String(xPortGetCoreID()) + "] Loop: Connect failed");
        }
      }
    }
  }

  if(ENABLE_MQTT) {
    // Construct message.
    String message = String("Hello world @ ");
    message.concat(millis());

    // Print message we are about to queue.
    Serial.println("[" + String(xPortGetCoreID()) + "] Loop: Queing message (" + message + ").");

    // Try to add message to queue (fails if queue is full).
    if (!MattzoMQTTPublisher::QueueMessage(message.c_str()))
    {
      Serial.println("[" + String(xPortGetCoreID()) + "] Loop: Queue full");
    }
  }

  // Print available heap space.
  // Serial.print("[" + String(xPortGetCoreID()) + "] Loop: Available heap: ");
  // Serial.println(xPortGetFreeHeapSize());

  // Delay next scan/connect attempt for a while, allowing the background drive tasks of already connected SBricks to send their periodic commands.
  delay(BLE_CONNECT_DELAY_IN_SECONDS * 1000 / portTICK_PERIOD_MS);
}