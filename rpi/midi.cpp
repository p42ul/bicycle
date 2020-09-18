#include "midi.h"


#include <alsa/asoundlib.h>
#include <iostream>
#include <poll.h>

namespace {

  class AlsaMidi {
    public:
      AlsaMidi() { };
      ~AlsaMidi() { end(); }

      void begin();
      void end();

      bool send(const MidiEvent&);
      bool receive(MidiEvent&, AbsTime timeout);

    private:
      snd_seq_t *seq = nullptr;
      snd_midi_event_t *mev = nullptr;
      int npfds = 0;
      struct pollfd *pfds = nullptr;

      int inPort;
      int outPort;

      bool errCheck(int, const char* = nullptr);
      bool errFatal(int, const char* = nullptr);
  };



  void AlsaMidi::begin() {
    int serr;
    serr = snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK);
    if (errFatal(serr, "open sequencer")) return;

    serr = snd_seq_set_client_name(seq, "bicycle");
    if (errFatal(serr, "name sequencer")) return;

    inPort = snd_seq_create_simple_port(seq, "controllers",
      SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,
      SND_SEQ_PORT_TYPE_APPLICATION);
    if (errFatal(inPort, "create in port")) return;

    outPort = snd_seq_create_simple_port(seq, "synths",
      SND_SEQ_PORT_CAP_READ|SND_SEQ_PORT_CAP_SUBS_READ,
      SND_SEQ_PORT_TYPE_APPLICATION);
    if (errFatal(outPort, "create out port")) return;

    npfds = snd_seq_poll_descriptors_count(seq, POLLIN);
    pfds = new struct pollfd[npfds];
    npfds = snd_seq_poll_descriptors(seq, pfds, npfds, POLLIN);

    serr = snd_midi_event_new(3, &mev);
    if (errFatal(serr, "create event decoder")) return;

    snd_midi_event_no_status(mev, 1); // 1 = turn off running status
  }

  void AlsaMidi::end() {
    if (pfds) {
      delete pfds;
    }
    if (mev) {
      snd_midi_event_free(mev);
      mev = nullptr;
    }
    if (seq) {
      snd_seq_close(seq);
      seq = nullptr;
    }
  }


  bool AlsaMidi::send(const MidiEvent& m) {
    snd_seq_event_t ev;

    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_source(&ev, outPort);
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev);

    snd_midi_event_reset_encode(mev);
    auto n = snd_midi_event_encode(mev, m.bytes, sizeof(m.bytes), &ev);
    if (errCheck(n, "midi encode")) return false;

    int serr;
    serr = snd_seq_event_output_direct(seq, &ev);
    if (errCheck(serr, "event output")) return false;

    // serr = snd_seq_drain_output(seq);
    // errCheck(serr, "drain");

    return true;
  }

  bool AlsaMidi::receive(MidiEvent& m, AbsTime timeout) {
    snd_seq_event_t *ev;

    auto q = snd_seq_event_input(seq, &ev);
    if (q == -EAGAIN) {
      // nothing there, try once more...
      if (timeout != forever)
        std::cout << "tNext = " << std::dec << timeout << std::endl;
      // int t = (timeout == forever) ? 500 : timeout;
      // std::cout << "polling " << std::dec << npfds << " fds, waiting " << t << std::endl;
      // poll(pfds, npfds, t);
      // q = snd_seq_event_input(seq, &ev);
      if (q == -EAGAIN)
        return false;   // still nothing there
    }
    if (errCheck(q, "event input")) return false;

    snd_midi_event_reset_decode(mev);
    auto n = snd_midi_event_decode(mev, m.bytes, sizeof(m.bytes), ev);
    // snd_seq_seq_free_event(ev);    // no longer needed in modern ALSA

    if (errCheck(n, "midi decode")) return false;

    return true;
  }

  bool AlsaMidi::errCheck(int serr, const char* op) {
    if (serr >= 0) return false;
    std::cerr << "ALSA Seq error " << std::dec << serr;
    if (op) std::cerr << " in " << op;
    std::cerr << std::endl;
    return true;
  }

  bool AlsaMidi::errFatal(int serr, const char* op) {
    bool r = errCheck(serr, op);
    if (r) end();
    return r;
  }

}




class Midi::Impl : public AlsaMidi { };

void Midi::begin() {
  end();
  impl = new Impl;
  if (impl) impl->begin();
}

void Midi::end() {
  if (impl) {
    delete impl;
    impl = nullptr;
  }
}

bool Midi::send(const MidiEvent& ev) {
  return impl ? impl->send(ev) : false;
}

bool Midi::receive(MidiEvent& ev, AbsTime timeout) {
  return impl ? impl->receive(ev, timeout) : false;
}
