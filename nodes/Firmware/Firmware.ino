// Tested with Arduino Pro Mini

// Basic algorithm idea: Parent nodes announce themselves. When a node
// sees the announcement of a parent, it creates a pair with the
// announcing node and itself. Pairs are propagated up to the root
// node.

#include <Adafruit_NeoPixel.h>
#include "colors.h"
#include "settings.h"
#include "id.h" // ID of this node, e.g.: `#define ID A`
#include "TransceiverOnPort.h"
#include "Port.h"
#include "Pair.h"
#include "message.h"
#include "pairMessageQueue.h"
#include "nodeColorsList.h"

#define IDENTITY(x) x
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

constexpr uint16_t angleTableSize = 1024;
#define ANGLE_TABLE_FILENAME TOSTRING(../IDENTITY(ID).h)
#if __has_include(ANGLE_TABLE_FILENAME)
#include ANGLE_TABLE_FILENAME
#else
#include "defaultAngleTable.h"
#endif

Adafruit_NeoPixel neoPixel;

constexpr char idOfThisNode = TOSTRING(ID)[0];
constexpr uint8_t pinNumber1 = 2;
constexpr uint8_t pinNumber2 = 3;
constexpr uint8_t pinNumber3 = 8;
constexpr uint8_t pinNumber4 = 9;
static TransceiverOnPort<pinNumber1, 1> transceiverOnPort1;
static TransceiverOnPort<pinNumber2, 2> transceiverOnPort2;
static TransceiverOnPort<pinNumber3, 3> transceiverOnPort3;
static TransceiverOnPort<pinNumber4, 4> transceiverOnPort4;
static uint8_t numberOfPortWithParent = 0; // 0 = no parent

static uint32_t parentExpiryTime = 0; // ms

ISR(TIMER2_COMPA_vect) {
  transceiverOnPort1.transceiver.handleTimer2Interrupt();
  transceiverOnPort2.transceiver.handleTimer2Interrupt();
  transceiverOnPort3.transceiver.handleTimer2Interrupt();
  transceiverOnPort4.transceiver.handleTimer2Interrupt();
}

ISR(PCINT2_vect) { // D0-D7
  transceiverOnPort1.transceiver.handlePinChangeInterrupt();
  transceiverOnPort2.transceiver.handlePinChangeInterrupt();
}

ISR(PCINT0_vect) { // D8-D13
  transceiverOnPort3.transceiver.handlePinChangeInterrupt();
  transceiverOnPort4.transceiver.handlePinChangeInterrupt();
}

uint8_t adjustForBrightness(uint8_t subColor) {
  return uint32_t(subColor) * ledBrightness / 0xff;
}

uint32_t neoPixelColor(const byte * const color) {
  return neoPixel.Color(
    adjustForBrightness(color[0]),
    adjustForBrightness(color[1]),
    adjustForBrightness(color[2])
  );
}

void setupColors() { // TODO: -> colorSetup?
  const uint8_t numberOfLeds = 4;
  const uint8_t dataPin = 4;
  const uint8_t listIndex = iAmRoot()
    ? 0
    : (idOfThisNode - 0x40);
  const byte * const * const nodeColors = nodeColorsList[listIndex];
  const uint32_t color1 = neoPixelColor(nodeColors[0]);
  const uint32_t color2 = neoPixelColor(nodeColors[1]);

  neoPixel = Adafruit_NeoPixel(numberOfLeds, dataPin,
                               NEO_RGB + NEO_KHZ800);
  neoPixel.begin();
  neoPixel.setPixelColor(0, color1);
  neoPixel.setPixelColor(1, color1);
  neoPixel.setPixelColor(2, color2);
  neoPixel.setPixelColor(3, color2);
  neoPixel.show();
}

void setupAccelerometer() {
  analogReference(EXTERNAL);
}

void setup() {
  setupColors();
  setupAccelerometer(); 

  pinMode(ledPin, OUTPUT);
  flashLed();

  if (iAmRoot()) {
    Serial.begin(115200);
  }

  setupMultiTransceiver();
}

bool iHaveAParent() {
  return numberOfPortWithParent != 0;
}

void loop() {
  if (iAmRoot()) {
    rootLoop();
  } else {
    nonRootLoop();
  }
}

void printPair(const Pair &pair) {
  char buffer[] = {
                   pair.parentPort.nodeId,
                   charFromDigit(pair.parentPort.portNumber),
                   pair.childPort.nodeId,
                   charFromDigit(pair.childPort.portNumber),
                   '\0'
  };

  Serial.println(buffer);
}

void rootLoop() {
  parseMessage(transceiverOnPort1, transceiverOnPort1.getMessage());

  if (numberOfQueuedPairMessages() > 0) {
    const byte *pairMessage = dequeuePairMessage();
    Pair pair = pairFromPairMessage(pairMessage);
    printPair(pair);
  }

  periodicallyAnnounceMe();
}

void parseMessages() {
  bool messageHasBeenReceived;

  do {
    messageHasBeenReceived = false;
    messageHasBeenReceived |= 
      parseMessage(transceiverOnPort1, transceiverOnPort1.getMessage());
    messageHasBeenReceived |=
      parseMessage(transceiverOnPort2, transceiverOnPort2.getMessage());
    messageHasBeenReceived |=
      parseMessage(transceiverOnPort3, transceiverOnPort3.getMessage());
    messageHasBeenReceived |=
      parseMessage(transceiverOnPort4, transceiverOnPort4.getMessage());
  } while (messageHasBeenReceived);
}

void nonRootLoop() {
  parseMessages();

  if (!iHaveAParent()) {
    clearPairMessageQueue();
    return;
  }

  if (!transmissionToParentIsInProgress()) {
    if (numberOfQueuedPairMessages() > 0) {
      sendPairMessageToParent(dequeuePairMessage());
    }
  }
  periodicallyAnnounceMe();
  removeParentIfExpired();
}

bool transmissionToParentIsInProgress() {
  if (numberOfPortWithParent == transceiverOnPort1.portNumber) {
    return transceiverOnPort1.transceiver.transmissionIsInProgress();
  }
  if (numberOfPortWithParent == transceiverOnPort2.portNumber) {
    return transceiverOnPort2.transceiver.transmissionIsInProgress();
  }
  if (numberOfPortWithParent == transceiverOnPort3.portNumber) {
    return transceiverOnPort3.transceiver.transmissionIsInProgress();
  }
  if (numberOfPortWithParent == transceiverOnPort4.portNumber) {
    return transceiverOnPort4.transceiver.transmissionIsInProgress();
  }
  return false;
}

void removeParent() {
  numberOfPortWithParent = 0;
}

void removeParentIfExpired() {
  if (iHaveAParent() && millis() > parentExpiryTime) {
    removeParent();
  }
}

void periodicallyAnnounceMe() {
  static uint32_t scheduledAnnouncementTime =
    millis() + announcementPeriod; // ms
  uint32_t now = millis(); // ms

  if (now >= scheduledAnnouncementTime) {
    announceMeToChildren();
    scheduledAnnouncementTime = now + announcementPeriod;
  }
}

static inline uint8_t digitFromChar(char c) {
  return c - 48;
}

static inline char charFromDigit(uint8_t digit) {
  return digit + 48;
}

static void flashLed() {
  if (flashLedIsEnabled) {
    digitalWrite(ledPin, HIGH);
    delay(50);
    digitalWrite(ledPin, LOW);
    delay(50);
  }
}

static inline bool iAmRoot() {
  return idOfThisNode == '*';
}

template <typename T>
void sendPairMessageToParent(T &transceiverOnPort, const byte *pairMessage) {
  transceiverOnPort.transceiver.startTransmissionOfBytes(pairMessage,
                                                         pairMessageSize);
}

void enablePinChangeInterrupts() {
  PCICR |= // Pin Change Interrupt Control Register
    bit(PCIE2) | // D0 to D7
    bit(PCIE0); // D8 to D15
}

void setupMultiTransceiver() {
  multiTransceiver.startTimer1();
  multiTransceiver.startTimer2();
  enablePinChangeInterrupts();
  transceiverOnPort1.transceiver.begin();
  transceiverOnPort2.transceiver.begin();
  transceiverOnPort3.transceiver.begin();
  transceiverOnPort4.transceiver.begin();
}

template <typename T>
void transmitAnnouncement(T &transceiverOnPort) {
  transceiverOnPort.transceiver.startTransmissionOfBytes(
    buildAnnouncementMessage({idOfThisNode, transceiverOnPort.portNumber}),
    announcementMessageSize
  );
}

template <typename T>
void announceMeToChild(T &transceiverOnPort) {
  if (transceiverOnPort.portNumber == numberOfPortWithParent) {
    return;
  }
  transmitAnnouncement(transceiverOnPort);
  flashLed();
}

void resetParentExpiryTime() {
  parentExpiryTime = millis() + parentExpiryDuration; // ms
}

template <typename T>
void parseAnnouncementMessage(T &transceiverOnPort, const byte *message) {
  if (iAmRoot()) {
    return; // root cannot have a parent => ignore announcement
  }

  if (!iHaveAParent()) {
    numberOfPortWithParent = transceiverOnPort.portNumber;
  }

  if (transceiverOnPort.portNumber == numberOfPortWithParent) {
    resetParentExpiryTime(); // call regularly to keep parent fresh
  }

  // Create pair, either describing parent-child relationship, or
  // loop:
  Port port = decodePort(message[1]);
  Pair pair;
  pair.parentPort = port;
  pair.childPort = {idOfThisNode, transceiverOnPort.portNumber};
  enqueuePairMessage(buildPairMessage(pair));
}

static inline Pair pairFromPairMessage(const byte *message) {
  Pair pair;
  pair.parentPort = decodePort(message[1]);
  pair.childPort = decodePort(message[2]);
  return pair;
}

template <typename T>
bool parseMessage(T &transceiverOnPort, byte *message) {
  if (message == 0) {
    return false;
  }

  if (message[0] & B01000000) {
    // pair message
    enqueuePairMessage(message);
    return true;
  } else {
    // announcement message
    parseAnnouncementMessage(transceiverOnPort, message);
    return true;
  }
  return false;
}

void sendPairMessageToParent(const byte *pairMessage) {
  if (numberOfPortWithParent == transceiverOnPort1.portNumber) {
    sendPairMessageToParent(transceiverOnPort1, pairMessage);
  } else if (numberOfPortWithParent == transceiverOnPort2.portNumber) {
    sendPairMessageToParent(transceiverOnPort2, pairMessage);
  } else if (numberOfPortWithParent == transceiverOnPort3.portNumber) {
    sendPairMessageToParent(transceiverOnPort3, pairMessage);
  } else if (numberOfPortWithParent == transceiverOnPort4.portNumber) {
    sendPairMessageToParent(transceiverOnPort4, pairMessage);
  }
}

void announceMeToChildren() {
  announceMeToChild(transceiverOnPort1);
  if (!iAmRoot()) {
    announceMeToChild(transceiverOnPort2);
    announceMeToChild(transceiverOnPort3);
    announceMeToChild(transceiverOnPort4);
  }
}
