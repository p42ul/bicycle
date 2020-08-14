#ifndef _INCLUDE_LOOPER_H_
#define _INCLUDE_LOOPER_H_

#include <cstdint>


typedef uint16_t DeltaTime;
typedef uint8_t MidiEvent[3];

const DeltaTime maxEventInterval = 20000;
  // maximum amount of time spent waiting for a new event



typedef void (*EventFunc)(const MidiEvent&);
  // TODO: Needs time somehow? delta? absolute?

class Loop {
public:
  Loop(EventFunc);

  void advance(DeltaTime);
  void addEvent(const MidiEvent&);
  void keep();
  void arm();       // clear when next event added
  void clear();

  static void begin();

private:
  const EventFunc player;

  bool      armed;
  uint8_t   epoch;

  class Cell;
  Cell* firstCell;
  Cell* recentCell;
  DeltaTime timeSinceRecent;
};


#endif // _INCLUDE_LOOPER_H_
