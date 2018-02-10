// Tested with Digispark as "Digispark (16 Mhz - No USB)"

#include <SoftSerial.h>
#include <TinyPinChange.h>
#include <San.h>

const uint8_t ledPin = 1;
const char nodeId = 'a';
const int portsCount = 2;

struct neighbor_t {
  char nodeId;
  byte sourcePort; // port on the neighbor that leads to here (0 - 3)
  boolean isParent;
};

neighbor_t neighbors[2]; // sorted by port

San san;

void setup() {
  for (int i = 0; i < portsCount; i ++) {
    san.ports[i]->begin(4800);
  }
  pinMode(ledPin, OUTPUT);
}

void sendRequest() {
  san.ports[1]->listen();
  san.ports[1]->txMode();
  char buffer[] = {'?', nodeId, '1', '\n', '\0'}; // line break for easy debugging
  san.ports[1]->write(buffer);
  san.ports[1]->rxMode();
}

boolean startsReply(char c) {
  return c == '!';
}

void readReply() {
  char payload[3];
  boolean payloadIsComplete = san.readPayload(payload, 3);

  if (!payloadIsComplete) {
    return;
  }

  neighbor_t &neighbor = neighbors[1];
  neighbor.nodeId = payload[0];
  neighbor.sourcePort = san.digitFromChar(payload[1]);
}

void waitForReply() {
  while (!san.timeSlotHasEnded()) {
    if (san.ports[1]->available()) {
      char c = san.ports[1]->read();
      if (startsReply(c)) {
        readReply();
        return;
      }
    }
  }
}

void loop() {
  san.openNextTimeSlot();
  san.giveOtherSideTimeToGetReady();
  sendRequest();
  san.waitForEndOfTimeSlot();

  san.openNextTimeSlot();
  san.flashLed();
  san.flashLed();
  waitForReply();
  san.waitForEndOfTimeSlot();
}