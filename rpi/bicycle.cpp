#include <Arduino.h>

#include "display.h"
#include "looper.h"
#include "types.h"


// USB MIDI object
class FauxMidi {
  public:
    bool send(const uint8_t[4]);
    bool receive(uint8_t [4]);
};
FauxMidi usb_midi;


/*
                            boppad assignments
  Sequence: pin D23 (MOSI)  ch 2 note 50
  Measure:  pin D1  (TX)
  Beat:     pin D24 (SCK)   ch 1 note 38
  Tuplet:   pin D22 (MISO)  ch 1 note 36

  ch 1 cc 70 & 71 are radius on boppad lower two pads
*/


enum Boppad {
  noteLowerLeft = 36,
  noteLowerRight = 38,
  noteUpperRight = 42,
  noteUpperLeft = 48,

  ccRadiusLowerLeft = 70,
  ccRadiusLowerRight = 71,
};


void playEvent(const MidiEvent& ev) {
  uint8_t packet[4];

  packet[0] = 0 | (ev.status >> 4);
    // TODO: This works because of the limited range of things the
    // noteEvent() accepts into the looper... but this should be
    // fixed support all messages just to be safe.
  packet[1] = ev.status;
  packet[2] = ev.data1;
  packet[3] = ev.data2;

  usb_midi.send(packet);
}

Loop theLoop(playEvent);


void controlEvent(const MidiEvent& ev) {
  // Currently set up for the nanoKontrol default

  switch (ev.status & 0xf0) {
    case 0xb0: // CC
      switch (ev.data1) {
        case   2: theLoop.layerVolume(0, ev.data2); break;
        case   3: theLoop.layerVolume(1, ev.data2); break;
        case   4: theLoop.layerVolume(2, ev.data2); break;
        case   5: theLoop.layerVolume(3, ev.data2); break;
        case   6: theLoop.layerVolume(4, ev.data2); break;
        case   8: theLoop.layerVolume(5, ev.data2); break;
        case   9: theLoop.layerVolume(6, ev.data2); break;
        case  11: theLoop.layerVolume(7, ev.data2); break;
        case  12: theLoop.layerVolume(8, ev.data2); break;
          // yes, CCs 7, 10, & 11 are skipped

        case  23: theLoop.layerMute(0, ev.data2 != 0); break;
        case  24: theLoop.layerMute(1, ev.data2 != 0); break;
        case  25: theLoop.layerMute(2, ev.data2 != 0); break;
        case  26: theLoop.layerMute(3, ev.data2 != 0); break;
        case  27: theLoop.layerMute(4, ev.data2 != 0); break;
        case  28: theLoop.layerMute(5, ev.data2 != 0); break;
        case  29: theLoop.layerMute(6, ev.data2 != 0); break;
        case  30: theLoop.layerMute(7, ev.data2 != 0); break;
        case  31: theLoop.layerMute(8, ev.data2 != 0); break;

        case  33: if (ev.data2) theLoop.layerArm(0); break;
        case  34: if (ev.data2) theLoop.layerArm(1); break;
        case  35: if (ev.data2) theLoop.layerArm(2); break;
        case  36: if (ev.data2) theLoop.layerArm(3); break;
        case  37: if (ev.data2) theLoop.layerArm(4); break;
        case  38: if (ev.data2) theLoop.layerArm(5); break;
        case  39: if (ev.data2) theLoop.layerArm(6); break;
        case  40: if (ev.data2) theLoop.layerArm(7); break;
        case  41: if (ev.data2) theLoop.layerArm(8); break;

        case  44:  if (ev.data2) theLoop.arm();    break;
        case  46:  if (ev.data2) theLoop.clear();  break;
        case  49:  if (ev.data2) theLoop.keep();   break;

      }
      break;
  }
}

void noteEvent(const MidiEvent& ev) {
  auto ch = ev.status & 0x0f;
  if (ch == 0x0f) {
    controlEvent(ev);
    return;
  }

  if (ch == 0x01) {
    if ((ev.status & 0xf0) == 0x90) {
      switch (ev.data1) {
        case noteUpperLeft:   theLoop.keep(); break;
        case noteUpperRight:  theLoop.arm();  break;
      }
    }
    return;
  }

  switch (ev.status & 0xf0) {
    case 0x80: // Note Off
    case 0x90: // Note On
    case 0xa0: // Poly Aftertouch
      break;

    case 0xb0: // CC
      switch (ev.data1) {
        case 64:  if (ev.data2) theLoop.keep();   return;
          // treat the sustain pedal as the keep function
      }
      break;

    case 0xc0: // Program change
      return;     // TODO: echo these?

    case 0xd0: // Channel Aftertouch
    case 0xe0: // Pitch Bend
      break;

    case 0xf0: // System Messages
      return;

    default:
      return;
  }

  theLoop.addEvent(ev);
}

void notePacket(const uint8_t packet[4]) {
  MidiEvent ev;
  ev.status = packet[1];
  ev.data1 = packet[2];
  ev.data2 = packet[3];
  noteEvent(ev);
}



void setup() {
  displaySetup();

  Serial.begin(115200);
  // while (!Serial);

  theLoop.begin();

  Serial.println("Ready!");
}

void loop() {
  static uint32_t then = millis();
  uint32_t now = millis();

  if (now > then) {
    theLoop.advance(now);
    then = now;
  }

  uint8_t packet[4];
  while (usb_midi.receive(packet)) {
    notePacket(packet);
  }

  // analogUpdate(now);

  Loop::Status s = theLoop.status();
  displayUpdate(now, s);
}
