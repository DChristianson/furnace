/**
 * Furnace Tracker - multi-system chiptune tracker
 * Copyright (C) 2021-2022 tildearrow and contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef _REGISTERDUMP_H
#define _REGISTERDUMP_H

#include "../engine.h"

const int TICKS_PER_SECOND = 1000000;

typedef uint64_t AlphaCode;
typedef uint64_t SpanCode;
typedef int AlphaChar;

struct PatternIndex {
  String key;
  unsigned short subsong, ord, chan, pat;
  PatternIndex(
    const String& k,
    unsigned short s,
    unsigned short o,
    unsigned short c,
    unsigned short p):
    key(k),
    subsong(s),
    ord(o),
    chan(c),
    pat(p) {}
};

struct RowIndex {
  unsigned short subsong, ord, row;
  RowIndex(unsigned short s, unsigned short o, unsigned short r):
    subsong(s),
    ord(o),
    row(r) {}
  bool advance(unsigned short s, unsigned short o, unsigned short r)
  {
    bool changed = false;
    if (subsong != s) {
      subsong = s;
      changed = true;
    }
    if (ord != o) {
      ord = o;
      changed = true;
    }
    if (row != r) {
      row = r;
      changed = true;
    }
    return changed;
  }  
};

inline auto getSequenceKey(unsigned short subsong, unsigned short ord, unsigned short row, unsigned short channel) {
  return fmt::sprintf(
        "SEQ_S%02x_O%02x_R%02x_C%02x",
         subsong, 
         ord,
         row,
         channel);
}

inline auto getPatternKey(unsigned short subsong, unsigned short channel, unsigned short pattern) {
  return fmt::sprintf(
    "PAT_S%02x_C%02x_P%02x",
    subsong,
    channel,
    pattern
  );
}

const size_t CHANNEL_REGISTERS = 4;

struct ChannelState {

  unsigned char registers[CHANNEL_REGISTERS];

  ChannelState() {}

  ChannelState(unsigned char c) {
    memset(registers, c, CHANNEL_REGISTERS);
  }

  ChannelState(const ChannelState &c) {
    memcpy(registers, c.registers, CHANNEL_REGISTERS);
  }

  ChannelState& operator=(ChannelState& c) {
    memcpy(registers, c.registers, CHANNEL_REGISTERS);
    return *this;
  }

  bool write(unsigned int address, unsigned int value) {
    unsigned char v = (unsigned char)value;
    if (registers[address] == v) return false;
    registers[address] = v;
    return true;
  }

  bool equals(const ChannelState &c) {
    for (size_t i = 0; i < CHANNEL_REGISTERS; i++) {
      if (registers[i] != c.registers[i]) return false;
    }
    return true;
  }

  uint64_t hash() const {
    uint64_t h = 0;
    for (size_t i = 0; i < CHANNEL_REGISTERS; i++) {
      h = ((uint64_t)registers[i]) + (h << 8);
    }
    return h;
  }

};


/**
 * ChannelState + time interval
 */
struct ChannelStateInterval {

  ChannelState state;
  int duration;

  ChannelStateInterval(const ChannelStateInterval &n) : state(n.state), duration(n.duration) {}

  ChannelStateInterval(const ChannelState &state, int duration) : state(state), duration(duration) {}

  uint64_t hash() const {
    uint64_t h = state.hash();
    h += ((uint64_t)duration << ((CHANNEL_REGISTERS + 1) * 8));
    return h;
  }

}; 

/**
 * Sequence of channel states
 */
struct ChannelStateSequence {

  ChannelState initialState;
  std::vector<ChannelStateInterval> intervals;


  ChannelStateSequence() : initialState(255) {}
  ChannelStateSequence(const ChannelState &initialState) : initialState(initialState) {}

  void updateState(const ChannelState &state) {
    if (intervals.size() > 0 && intervals.back().state.equals(state)) {
      // ignore state update if it represents no change
      return;
    }
    intervals.emplace_back(ChannelStateInterval(state, 0));
  }

  int addDuration(const int ticks, const int remainder, const int freq) {
    if (intervals.size() == 0) {
      intervals.emplace_back(ChannelStateInterval(ChannelState(0), 0));
    }
    int total = ticks + remainder;
    int cycles = total / freq;
    intervals.back().duration += cycles;
    return total - (cycles * freq);
  }

  size_t size() const {
    return intervals.size();
  }

  uint64_t hash() const {
    // rolling polyhash: see https://cp-algorithms.com/string/string-hashing.html CC 4.0 license
    const int p = 31;
    const int m = 1e9 + 9;
    uint64_t pp = 1;
    uint64_t value = 0;
    value = value + (initialState.hash() * pp) % m;
    pp = (pp * p) % m;
    for (auto& x : intervals) {
      value = value + (x.hash() * pp) % m;
      pp = (pp * p) % m;
    }
    return value;
  }

};

/**
 * Capture of register write
 */
struct RegisterWrite {

  long writeIndex;
  RowIndex rowIndex;
  int systemIndex;
  DivSystem system;
  int seconds;
  int ticks;
  float hz;
  int addr;
  int val;

  RegisterWrite(
    long writeIndex,
    unsigned short subsong,
    unsigned short ord,
    unsigned short row,
    int systemIndex,
    DivSystem currentSystem,
    int totalSeconds,
    int totalTicks,
    float hz,
    int addr,
    int val
  ):
    writeIndex(writeIndex),
    rowIndex(subsong, ord, row),
    systemIndex(systemIndex),
    system(currentSystem),
    seconds(totalSeconds),
    ticks(totalTicks),
    hz(hz),
    addr(addr),
    val(val) {}

};

/**
 * Extract all register writes in a song.
 */
void registerDump(
  DivEngine* e, 
  int subsong,
  std::vector<RegisterWrite> &writes
);

/**
 * Extract channel states from register writes.
 */
void writeChannelStateSequence(
  const std::vector<RegisterWrite> &writes,
  int subsong,
  int channel,
  int systemIndex,
  const std::map<unsigned int, unsigned int> &addressMap,
  ChannelStateSequence &dumpSequence 
);

/**
 * Extract channel states in a song, keyed on subsong, ord, row and channel.
 */
void writeChannelStateSequenceByRow(
  const std::vector<RegisterWrite> &writes,
  int subsong,
  int channel,
  int systemIndex,
  const std::map<unsigned int, unsigned int> &addressMap,
  std::vector<String> &sequence,
  std::map<String, ChannelStateSequence> &registerDumps 
);

/**
 * Deduplicate channel state sequences by hash code
 */
void findCommonSequences(
  const std::map<String, ChannelStateSequence> &registerDumps,
  std::map<uint64_t, String> &commonSequences,
  std::map<uint64_t, unsigned int> &frequencyMap,
  std::map<String, String> &representativeMap
);


#endif // _REGISTERDUMP_H