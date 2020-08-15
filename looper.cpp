#include "looper.h"

#include <cassert>
#include <cstring>

#include "cell.h"


namespace {
  template<typename T>
  inline T clamp(T v, T lo, T hi) {
    return v < lo ? lo : v <= hi ? v : hi;
  }

  inline uint8_t scaleVelocity(uint8_t vel, uint8_t vol) {
    return static_cast<uint8_t>(clamp(
      static_cast<uint32_t>(vel) * static_cast<uint32_t>(vol) / 100,
      1ul, 127ul));
  }
}

class Loop::Util {
public:
  static void startAwaitingOff(Loop& loop, Cell* cell) {
    finishAwaitingOff(loop, cell->event);
    auto& ao = loop.awaitingOff[cell->event.data1];
    ao.cell = cell;
    ao.start = loop.walltime;
  }

  static void cancelAwatingOff(Loop& loop, const Cell* cell) {
    auto& ao = loop.awaitingOff[cell->event.data1];
    if (ao.cell == cell) {
      ao.cell = nullptr;
    }
  }

  static void finishAwaitingOff(Loop& loop, const MidiEvent& ev) {
    auto& ao = loop.awaitingOff[ev.data1];
    if (ao.cell) {
      ao.cell->duration = loop.walltime - ao.start;
      ao.cell = nullptr;
    }
  }

  static void clearAwatingOff(Loop& loop) {
    for (auto& ao : loop.awaitingOff)
      ao = {nullptr, 0};
  }
};


Loop::Loop(EventFunc func)
  : player(func),
    walltime(0),
    armed(true), activeLayer(0), layerArmed(false),
    firstCell(nullptr), recentCell(nullptr),
    timeSinceRecent(0),
    pendingOff(nullptr)
  {
    for (auto& m : layerMutes) m = false;
    for (auto& v : layerVolumes) v = 100;

    Util::clearAwatingOff(*this);
  }


void Loop::advance(AbsTime now) {
  // In theory the offs should be interleaved as we go through the next
  // set of cells to play. BUT, since dt has already elapsed, it is roughly
  // okay to just spit out the NoteOff events first. And anyway, dt is rarely
  // more than 1.

  AbsTime dt = now - walltime;
  walltime = now;
    // FIXME: Handle rollover of walltime?

  for (Cell *p = pendingOff, *q = nullptr; p;) {
    if (dt < p->duration) {
      p->duration -= dt;
      q = p;
      p = p->next();
    } else {
      player(p->event);

      Cell* n = p->next();
      p->free();

      if (q)  q->link(n);
      else    pendingOff = n;
      p = n;
    }
  }

  if (!recentCell) return;

  if (recentCell->atEnd()) {
    walltime = now;

    if (dt > maxEventInterval - timeSinceRecent) {
      clear();
      return;
    }

    timeSinceRecent += dt;
    return;
  }

  while (recentCell->nextTime <= timeSinceRecent + dt) {
    // time to move to the next event, and play it

    Cell* nextCell = recentCell->next();
    auto layer = nextCell->layer;

    if (layer == activeLayer && !layerArmed) {
      // prior data from this layer currently recording into, delete it
      // note: if the layer is armed, then awaiting first event to start
      // recording
      if (nextCell->event.isNoteOn())
        Util::cancelAwatingOff(*this, nextCell);

      recentCell->link(nextCell->next());
      recentCell->nextTime += nextCell->nextTime;
      nextCell->free();
    } else {
      dt -= recentCell->nextTime - timeSinceRecent;
      timeSinceRecent = 0;

      recentCell = nextCell;

      if (!(layer < layerMutes.size() && layerMutes[layer])) {

        if (recentCell->event.isNoteOn() && recentCell->duration > 0) {
          MidiEvent note = recentCell->event;
          if (layer < layerVolumes.size())
            note.data2 = scaleVelocity(note.data2, layerVolumes[layer]);
          player(note);

          Cell* offCell = Cell::alloc();
          offCell->event = note;
          offCell->event.data2 = 0; // volume 0 makes it a NoteOff
          offCell->duration = recentCell->duration;
          offCell->link(pendingOff);
          pendingOff = offCell;
        } else {
          player(recentCell->event);
        }
      }
    }
  }

  timeSinceRecent += dt;
}


void Loop::addEvent(const MidiEvent& ev) {
  if (ev.isNoteOff()) {
    // note off processing
    Util::finishAwaitingOff(*this, ev);
    return;
  }

  if (armed) {
    clear();
    armed = false;
  }
  layerArmed = false;
  if (activeLayer < layerMutes.size())
    layerMutes[activeLayer] = false;
    // TODO: Should we be doing this? how to communicate back to controller?

  if (ev.isNoteOn()) {
    MidiEvent note = ev;
    if (activeLayer < layerVolumes.size())
      note.data2 = scaleVelocity(note.data2, layerVolumes[activeLayer]);
    player(note);
  } else {
    player(ev);
  }

  Cell* newCell = Cell::alloc();
  if (!newCell) return; // ran out of cells!
  newCell->event = ev;
  newCell->layer = activeLayer;
  newCell->duration = 0;

  if (ev.isNoteOn())
    Util::startAwaitingOff(*this, newCell);

  if (recentCell) {
    Cell* nextCell = recentCell->next();
    if (nextCell) {
      newCell->link(nextCell);
      newCell->nextTime = recentCell->nextTime - timeSinceRecent;
    }

    recentCell->link(newCell);
    recentCell->nextTime = timeSinceRecent;
  } else {
    firstCell = newCell;
    // FIXME: note "the one" here?
  }

  recentCell = newCell;
  timeSinceRecent = 0;
}


void Loop::keep() {
  if (firstCell) {
    // closing the loop
    recentCell->link(firstCell);
    recentCell->nextTime = timeSinceRecent;
    firstCell = nullptr;
  }

  activeLayer += activeLayer < (layerMutes.size() - 1) ? 1 : 0;
  layerArmed = true;

  // advance into the start of the loop
  advance(walltime);
}

void Loop::arm() {
  armed = true;
}

void Loop::clear() {
  Cell* start = recentCell;

  while (recentCell) {
    Cell* doomed = recentCell;
    recentCell = doomed->next();
    doomed->free();
    if (recentCell == start)
      break;
  }

  Util::clearAwatingOff(*this);

  firstCell = nullptr;
  recentCell = nullptr;
  timeSinceRecent = 0;
  armed = true;
  activeLayer = 0;
  layerArmed = false;
  for (auto& m : layerMutes) m = false;
    // TODO: Should we be doing this? how to communicate back to controller?
}


void Loop::layerMute(uint8_t layer, bool muted) {
  if (layer < layerMutes.size()) layerMutes[layer] = muted;
}

void Loop::layerVolume(uint8_t layer, uint8_t volume) {
  if (layer < layerVolumes.size()) layerVolumes[layer] = volume;
}

void Loop::layerArm(uint8_t layer) {
  // FIXME: what to do if still recording initial layer?
  activeLayer = layer;
  layerArmed = true;
}

void Loop::begin() {
  Cell::begin();
}

