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

#include "atari2600Export.h"

#include <fmt/printf.h>
#include <queue>
#include <set>
#include "../../ta-log.h"

const int AUDC0 = 0x15;
const int AUDC1 = 0x16;
const int AUDF0 = 0x17;
const int AUDF1 = 0x18;
const int AUDV0 = 0x19;
const int AUDV1 = 0x1A;

std::map<unsigned int, unsigned int> channel0AddressMap = {
  {AUDC0, 0},
  {AUDF0, 1},
  {AUDV0, 2},
};

std::map<unsigned int, unsigned int> channel1AddressMap = {
  {AUDC1, 0},
  {AUDF1, 1},
  {AUDV1, 2},
};

const char* TiaRegisterNames[] = {
  "AUDC0",
  "AUDC1",
  "AUDF0",
  "AUDF1",
  "AUDV0",
  "AUDV1"
};

std::vector<DivROMExportOutput> DivExportAtari2600::go(DivEngine* e) {
  std::vector<DivROMExportOutput> ret;

  // get register dump
  std::vector<RegisterWrite> registerWrites;
  registerDump(e, 0, registerWrites);

  // write track data
  switch (exportType) {
    case DIV_EXPORT_TIA_DUMP:
      writeTrackDataDump(e, registerWrites, ret);
      break;
    case DIV_EXPORT_TIA_SIMPLE:
      writeTrackDataSimple(e, registerWrites, ret);
      break;
    case DIV_EXPORT_TIA_COMPACT:
      writeTrackDataCompact(e, registerWrites, ret);
      break;
  }

  // create meta data (optional)
  logD("writing track title graphics");
  SafeWriter* titleData=new SafeWriter;
  titleData->init();
  titleData->writeText(fmt::sprintf("; Name: %s\n", e->song.name));
  titleData->writeText(fmt::sprintf("; Author: %s\n", e->song.author));
  titleData->writeText(fmt::sprintf("; Album: %s\n", e->song.category));
  titleData->writeText(fmt::sprintf("; System: %s\n", e->song.systemName));
  titleData->writeText(fmt::sprintf("; Tuning: %g\n", e->song.tuning));
  titleData->writeText(fmt::sprintf("; Instruments: %d\n", e->song.insLen));
  titleData->writeText(fmt::sprintf("; Wavetables: %d\n", e->song.waveLen));
  titleData->writeText(fmt::sprintf("; Samples: %d\n\n", e->song.sampleLen));
  auto title = (e->song.name.length() > 0) ?
     (e->song.name + " by " + e->song.author) :
     "furnace tracker";
  if (title.length() > 26) {
    title = title.substr(23) + "...";
  }
  writeTextGraphics(titleData, title.c_str());
  ret.push_back(DivROMExportOutput("Track_meta.asm", titleData));

  return ret;

}

void DivExportAtari2600::writeTrackDataDump(
  DivEngine* e, 
  std::vector<RegisterWrite> &registerWrites,
  std::vector<DivROMExportOutput> &ret
) {
  // debugging: dump all register writes
  logD("writing register dump");
  SafeWriter* dump = new SafeWriter;
  dump->init();
  for (auto &write : registerWrites) {
    dump->writeText(fmt::sprintf("%d %d %d:SS%d ORD%d ROW%d SYS%d> %d = %d\n",
      write.writeIndex,
      write.seconds,
      write.ticks,
      write.rowIndex.subsong,
      write.rowIndex.ord,
      write.rowIndex.row,
      write.systemIndex,
      write.addr,
      write.val
    ));
  }
  ret.push_back(DivROMExportOutput("Track_dump.txt", dump));
}

// simple register dump
void DivExportAtari2600::writeTrackDataSimple(
  DivEngine* e, 
  std::vector<RegisterWrite> &registerWrites,
  std::vector<DivROMExportOutput> &ret
) {

  SafeWriter* trackData=new SafeWriter;
  trackData->init();
  trackData->writeText(fmt::sprintf("; Song: %s\n", e->song.name));
  trackData->writeText(fmt::sprintf("; Author: %s\n", e->song.author));

  for (int channel = 0; channel < 2; channel++) {
    ChannelStateSequence dumpSequence;

    writeChannelStateSequence(
      registerWrites,
      0,
      channel,
      0,
      channel == 0 ? channel0AddressMap : channel1AddressMap,
      dumpSequence);

    size_t waveformDataSize = 0;
    size_t totalFrames = 0;
    trackData->writeC('\n');
    trackData->writeText(fmt::sprintf("CHANNEL_%d\n", channel));
    for (auto& n: dumpSequence.intervals) {
      trackData->writeText(fmt::sprintf("    byte %d, %d, %d, %d\n",
        n.state.registers[0],
        n.state.registers[1],
        n.state.registers[2],
        n.duration
      ));
      waveformDataSize += 4;
      totalFrames += n.duration;
    }
    trackData->writeText("    byte 0\n");
    waveformDataSize++;
    trackData->writeText(fmt::sprintf("    ; %d bytes %d frames", waveformDataSize, totalFrames));
  }

  ret.push_back(DivROMExportOutput("Track_simple.asm", trackData));

}

// compacted encoding
void DivExportAtari2600::writeTrackDataCompact(
  DivEngine* e, 
  std::vector<RegisterWrite> &registerWrites,
  std::vector<DivROMExportOutput> &ret
) {

  // convert to state sequences
  logD("performing sequence capture");
  std::vector<String> channelSequences[2];
  std::map<String, ChannelStateSequence> registerDumps;
  for (int channel = 0; channel < 2; channel++) {
    writeChannelStateSequenceByRow(
      registerWrites,
      0,
      channel,
      0,
      channel == 0 ? channel0AddressMap : channel1AddressMap,
      channelSequences[channel],
      registerDumps);
  }

  // scrunch the register dumps with 0 volume
  for (auto& x: registerDumps) {
      for (auto& y: x.second.intervals) {
        logD("checking 0 volume interval %s %d %d %d %d", x.first, y.state.registers[0], y.state.registers[1], y.state.registers[2], y.duration);
        if (0 == y.state.registers[2]) {
          logD("found 0 volume interval");
          y.state.registers[0] = 0;
          y.state.registers[1] = 0;
        }
      }
  }

  // compress the patterns into common subsequences
  logD("performing sequence compression");
  std::map<uint64_t, String> commonDumpSequences;
  std::map<uint64_t, unsigned int> frequencyMap;
  std::map<String, String> representativeMap;
  findCommonSequences(
    registerDumps,
    commonDumpSequences,
    frequencyMap,
    representativeMap);

  // create track data
  logD("writing track audio data");
  SafeWriter* trackData=new SafeWriter;
  trackData->init();
  trackData->writeText(fmt::sprintf("; Song: %s\n", e->song.name));
  trackData->writeText(fmt::sprintf("; Author: %s\n", e->song.author));

  // emit song table
  logD("writing song table");
  size_t songTableSize = 0;
  trackData->writeText("\n; Song Lookup Table\n");
  trackData->writeText(fmt::sprintf("NUM_SONGS = %d\n", e->song.subsong.size()));
  trackData->writeText("SONG_TABLE_START_LO\n");
  for (size_t i = 0; i < e->song.subsong.size(); i++) {
    trackData->writeText(fmt::sprintf("SONG_%d = . - SONG_TABLE_START_LO\n", i));
    trackData->writeText(fmt::sprintf("    byte <SONG_%d_ADDR\n", i));
    songTableSize++;
  }
  trackData->writeText("SONG_TABLE_START_HI\n");
  for (size_t i = 0; i < e->song.subsong.size(); i++) {
    trackData->writeText(fmt::sprintf("    byte >SONG_%d_ADDR\n", i));
    songTableSize++;
  }

  // collect and emit song data
  // borrowed from fileops
  size_t songDataSize = 0;
  trackData->writeText("; songs\n");
  std::vector<PatternIndex> patterns;
  const int channelCount = 2;
  bool alreadyAdded[channelCount][256];
  for (size_t i = 0; i < e->song.subsong.size(); i++) {
    trackData->writeText(fmt::sprintf("SONG_%d_ADDR\n", i));
    DivSubSong* subs = e->song.subsong[i];
    memset(alreadyAdded, 0, 2*256*sizeof(bool));
    for (int j = 0; j < subs->ordersLen; j++) {
      trackData->writeText("    byte ");
      for (int k = 0; k < channelCount; k++) {
        if (k > 0) {
          trackData->writeText(", ");
        }
        unsigned short p = subs->orders.ord[k][j];
        logD("ss: %d ord: %d chan: %d pat: %d", i, j, k, p);
        String key = getPatternKey(i, k, p);
        trackData->writeText(key);
        songDataSize++;

        if (alreadyAdded[k][p]) continue;
        patterns.push_back(PatternIndex(key, i, j, k, p));
        alreadyAdded[k][p] = true;
      }
      trackData->writeText("\n");
    }
    trackData->writeText("    byte 255\n");
    songDataSize++;
  }
  
  // pattern lookup
  size_t patternTableSize = 0;
  trackData->writeC('\n');
  trackData->writeText("; Pattern Lookup Table\n");
  trackData->writeText(fmt::sprintf("NUM_PATTERNS = %d\n", patterns.size()));
  trackData->writeText("PAT_TABLE_START_LO\n");
  for (PatternIndex& patternIndex: patterns) {
    trackData->writeText(fmt::sprintf("%s = . - PAT_TABLE_START_LO\n", patternIndex.key.c_str()));
    trackData->writeText(fmt::sprintf("   byte <%s_ADDR\n", patternIndex.key.c_str()));
    patternTableSize++;
  }
  trackData->writeText("PAT_TABLE_START_HI\n");
  for (PatternIndex& patternIndex: patterns) {
    trackData->writeText(fmt::sprintf("   byte >%s_ADDR\n", patternIndex.key.c_str()));
    patternTableSize++;
  }

  // emit sequences
  // we emit the "note" being played as an assembly variable 
  // later we will figure out what we need to emit as far as TIA register settings
  // this assumes the song has a limited number of unique "notes"
  size_t patternDataSize = 0;
  for (PatternIndex& patternIndex: patterns) {
    DivPattern* pat = e->song.subsong[patternIndex.subsong]->pat[patternIndex.chan].getPattern(patternIndex.pat, false);
    trackData->writeText(fmt::sprintf("; Subsong: %d Channel: %d Pattern: %d / %s\n", patternIndex.subsong, patternIndex.chan, patternIndex.pat, pat->name));
    trackData->writeText(fmt::sprintf("%s_ADDR", patternIndex.key.c_str()));
    for (int j = 0; j<e->song.subsong[patternIndex.subsong]->patLen; j++) {
      String key = getSequenceKey(patternIndex.subsong, patternIndex.ord, j, patternIndex.chan);
      auto rr = representativeMap.find(key);
      if (rr == representativeMap.end()) {
        // BUGBUG: pattern had no writes
        continue;
      }
      if (j % 8 == 0) {
        trackData->writeText("\n    byte ");
      } else {
        trackData->writeText(",");
      }
      trackData->writeText(rr->second); // the representative
      patternDataSize++;
    }
    trackData->writeText("\n    byte 255\n");
    patternDataSize++;
  }

  // emit waveform table
  // this is where we can lookup specific instrument/note/octave combinations
  // can be quite expensive to store this table (2 bytes per waveform)
  size_t waveformTableSize = 0;
  trackData->writeC('\n');
  trackData->writeText("; Waveform Lookup Table\n");
  trackData->writeText(fmt::sprintf("NUM_WAVEFORMS = %d\n", commonDumpSequences.size()));
  trackData->writeText("WF_TABLE_START_LO\n");
  for (auto& x: commonDumpSequences) {
    trackData->writeText(fmt::sprintf("%s = . - WF_TABLE_START_LO\n", x.second.c_str()));
    trackData->writeText(fmt::sprintf("   byte <%s_ADDR\n", x.second.c_str()));
    waveformTableSize++;
  }
  trackData->writeText("WF_TABLE_START_HI\n");
  for (auto& x: commonDumpSequences) {
    trackData->writeText(fmt::sprintf("   byte >%s_ADDR\n", x.second.c_str()));
    waveformTableSize++;
  }
    
  // emit waveforms
  size_t waveformDataSize = 0;
  trackData->writeC('\n');
  trackData->writeText("; Waveforms\n");
  for (auto& x: commonDumpSequences) {
    auto freq = frequencyMap[x.first];
    writeWaveformHeader(trackData, x.second.c_str());
    trackData->writeText(fmt::sprintf("; Hash %d, Freq %d\n", x.first, freq));
    auto& dump = registerDumps[x.second];
    ChannelState last(dump.initialState);
    for (auto& n: dump.intervals) {
      waveformDataSize += writeNoteF0(trackData, n.state, n.duration, last);
      last = n.state;
    }
    trackData->writeText("    byte 0\n");
    waveformDataSize++;
  }

  // audio metadata
  trackData->writeC('\n');
  trackData->writeText(fmt::sprintf("; Song Table Size %d\n", songTableSize));
  trackData->writeText(fmt::sprintf("; Song Data Size %d\n", songDataSize));
  trackData->writeText(fmt::sprintf("; Pattern Lookup Table Size %d\n", patternTableSize));
  trackData->writeText(fmt::sprintf("; Pattern Data Size %d\n", patternDataSize));
  trackData->writeText(fmt::sprintf("; Waveform Lookup Table Size %d\n", waveformTableSize));
  trackData->writeText(fmt::sprintf("; Waveform Data Size %d\n", waveformDataSize));
  size_t totalDataSize = 
    songTableSize + songDataSize + patternTableSize + 
    patternDataSize + waveformTableSize + waveformDataSize;
  trackData->writeText(fmt::sprintf("; Total Data Size %d\n", totalDataSize));

  ret.push_back(DivROMExportOutput("Track_data.asm", trackData));

}

/**
 *  Write note data. Format 0:
 * 
 *   fffff010 wwwwvvvv           frequency + waveform + volume, duration 1
 *   fffff110 wwwwvvvv           " " ", duration 2
 *   fffff100 dddddddd wwwwvvvv  " " ", duration d
 *   xxxx0001                    volume = x >> 4, duration 1 
 *   xxxx1001                    volume = x >> 4, duration 2
 *   xxxx0101                    wave = x >> 4, duration 1
 *   xxxx1101                    wave = x >> 4, duration 2
 *   xxxxx011                    frequency = x >> 3, duration 1
 *   xxxxx111                    frequency = x >> 3, duration 2
 *   00000000                    stop
 */
size_t DivExportAtari2600::writeNoteF0(SafeWriter* w, const ChannelState& next, const char duration, const ChannelState& last) {
  size_t bytesWritten = 0;
  unsigned char dmod = 0; // if duration is small, store in top bits of frequency

  if (duration == 0) {
      logD("0 duration note");
  }
  const char framecount = duration > 0 ? duration - 1 : 0; // BUGBUG: when duration is zero... we force to 1...

  unsigned char audfx, audcx, audvx;
  int cc, fc, vc;
  audcx = next.registers[0];
  cc = audcx != last.registers[0];
  audfx = next.registers[1];
  fc = audfx != last.registers[1];
  audvx = next.registers[2];
  vc = audvx != last.registers[2];
  
  w->writeText(fmt::sprintf("    ;F%d C%d V%d D%d\n", audfx, audcx, audvx, duration));

  if ( ((cc + fc + vc) == 1) && framecount < 2) {
    // write a delta row - only change one register
    dmod = framecount; 
    unsigned char rx;
    if (fc > 0) {
      // frequency
      rx = audfx << 3 | dmod << 2 | 0x03; //  d11
    } else if (cc > 0 ) {
      // waveform
      rx = audcx << 4 | dmod << 3 | 0x05; // d101
    } else {
      // volume 
      rx = audvx << 4 | dmod << 3 | 0x01; // d001
    }
    w->writeText(fmt::sprintf("    byte %d\n", rx));
    bytesWritten += 1;

  } else {
    // write all registers
    if (framecount < 2) {
      // short duration
      dmod = framecount << 1 | 0x01; // BUGBUG: complicated format
    } else {
      dmod = 0x02;
    }
    // frequency
    unsigned char x = audfx << 3 | dmod << 1;
    w->writeText(fmt::sprintf("    byte %d", x));
    if (dmod == 0x02) {
      w->writeText(fmt::sprintf(",%d", framecount));
      bytesWritten += 1;
    }
    // waveform and volume
    unsigned char y = (audcx << 4) + audvx;
    w->writeText(fmt::sprintf(",%d\n", y));
    bytesWritten += 2;

  }

  return bytesWritten;

}

/** 
 * 
 *  Write note data. Format 1:
 * 
 * 00000000 stop/return
 * 0000dddd pause,   15 >= d >= 1
 * 0001dddd sustain, 15 >= d >= 1
 * 0ddfffff wwwwvvvv, 3 >= d >= 1
 * 100dffff f +- 8  , 2 >= d >= 1
 * 101dwwww w       , 2 >= d >= 1
 * 110dvvvv v       , 2 >= d >= 1
 * 111xxxxx xxxxxxxx, dictionary lookup
 * 
 */
size_t DivExportAtari2600::writeNoteF1(SafeWriter* w, const ChannelState& next, const char duration, const ChannelState& last) {
  size_t bytesWritten = 0;

    if (duration == 0) {
      logD("0 duration note");
  }

  unsigned char dmod = 0; // if duration is small, store in top bits of frequency
  unsigned char framecount = duration > 0 ? duration : 1; 

  unsigned char audfx, audcx, audvx;
  int cc, fc, vc;
  audcx = next.registers[0];
  cc = audcx != last.registers[0];
  audfx = next.registers[1];
  const char fmod = audfx - last.registers[1];
  fc = audfx != last.registers[1];
  audvx = next.registers[2];
  vc = audvx != last.registers[2];
  
  w->writeText(fmt::sprintf("    ;%d %d %d\nc", last.registers[0], last.registers[1], last.registers[2]));
  w->writeText(fmt::sprintf("    ;F%d C%d V%d D%d %d %d %d %d\n", audfx, audcx, audvx, duration, cc, fc, vc, fmod));

  if ( audvx == 0 ) {
    // pause 
    dmod = (framecount > 15) ? 15 : framecount;
    w->writeText(fmt::sprintf("    byte %d; PAUSE\n", dmod));
    bytesWritten += 1;

  } else if ( ((cc + fc + vc) == 1) ) { // BUGBUG: && abs(fmod) <  8) {
    // write a delta row - only change one register
    dmod = (framecount > 2) ? framecount - 1 : 1;
    unsigned char rx;
    if (fc > 0) {
      // frequency
      // BUGBUG: incorrect
      rx = dmod << 4 | fmod | 0x80; // 100dffff 
    } else if (cc > 0) {
      // waveform
      rx = dmod << 4 | audcx | 0x90; // 101dwwww
    } else {
      // volume 
      rx = dmod << 4 | audvx | 0xc0; //110dccc
    }
    w->writeText(fmt::sprintf("    byte %d\n", rx));
    bytesWritten += 1;

  } else {
    // write all registers
    dmod = framecount > 3 ? 3 : framecount;
    // frequency
    unsigned char x = dmod << 5 | audfx << 3;
    w->writeText(fmt::sprintf("    byte %d", x));
    bytesWritten += 1;
    // waveform and volume
    unsigned char y = (audcx << 4) + audvx;
    w->writeText(fmt::sprintf(",%d\n", y));
    bytesWritten += 1;
  }

  framecount = framecount - dmod;
  while (framecount > 0) {
    dmod = (framecount > 15) ? 15 : framecount;
    framecount = framecount - dmod;
    unsigned char sx = 0x10 | dmod;
    w->writeText(fmt::sprintf("    byte %d; SUSTAIN\n", sx));
    bytesWritten += 1;
  }

  return bytesWritten;
}

void DivExportAtari2600::writeWaveformHeader(SafeWriter* w, const char * key) {
  w->writeText(fmt::sprintf("%s_ADDR\n", key));
}


int getFontIndex(const char c) {
  if ('0' <= c && c <= '9') return c - '0';
  if (c == ' ' || c == 0) return 10;
  if (c == '.') return 12;
  if (c == '<') return 13;
  if (c == '>') return 14;
  if ('a' <= c && c <= 'z') return 15 + c - 'a';
  if ('A' <= c && c <= 'Z') return 15 + c - 'A';
  return 11;
}

// 4x6 font data used to encode title
unsigned char FONT_DATA[41][6] = {
  {0x00, 0x04, 0x0a, 0x0a, 0x0a, 0x04}, // SYMBOL_ZERO
  {0x00, 0x0e, 0x04, 0x04, 0x04, 0x0c}, // SYMBOL_ONE
  {0x00, 0x0e, 0x08, 0x06, 0x02, 0x0c}, // SYMBOL_TWO
  {0x00, 0x0c, 0x02, 0x06, 0x02, 0x0c}, // SYMBOL_THREE
  {0x00, 0x02, 0x02, 0x0e, 0x0a, 0x0a}, // SYMBOL_FOUR
  {0x00, 0x0c, 0x02, 0x0c, 0x08, 0x06}, // SYMBOL_FIVE
  {0x00, 0x06, 0x0a, 0x0c, 0x08, 0x06}, // SYMBOL_SIX
  {0x00, 0x08, 0x08, 0x04, 0x02, 0x0e}, // SYMBOL_SEVEN
  {0x00, 0x06, 0x0a, 0x0e, 0x0a, 0x0c}, // SYMBOL_EIGHT
  {0x00, 0x02, 0x02, 0x0e, 0x0a, 0x0c}, // SYMBOL_NINE
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // SYMBOL_SPACE
  {0x00, 0x0e, 0x00, 0x00, 0x00, 0x00}, // SYMBOL_UNDERSCORE
  {0x00, 0x04, 0x00, 0x00, 0x00, 0x00}, // SYMBOL_DOT
  {0x00, 0x02, 0x04, 0x08, 0x04, 0x02}, // SYMBOL_LT
  {0x00, 0x08, 0x04, 0x02, 0x04, 0x08}, // SYMBOL_GT
  {0x00, 0x0a, 0x0a, 0x0e, 0x0a, 0x0e}, // SYMBOL_A
  {0x00, 0x0e, 0x0a, 0x0c, 0x0a, 0x0e}, // SYMBOL_B
  {0x00, 0x0e, 0x08, 0x08, 0x08, 0x0e}, // SYMBOL_C
  {0x00, 0x0c, 0x0a, 0x0a, 0x0a, 0x0c}, // SYMBOL_D
  {0x00, 0x0e, 0x08, 0x0c, 0x08, 0x0e}, // SYMBOL_E
  {0x00, 0x08, 0x08, 0x0c, 0x08, 0x0e}, // SYMBOL_F
  {0x00, 0x0e, 0x0a, 0x08, 0x08, 0x0e}, // SYMBOL_G
  {0x00, 0x0a, 0x0a, 0x0e, 0x0a, 0x0a}, // SYMBOL_H
  {0x00, 0x04, 0x04, 0x04, 0x04, 0x04}, // SYMBOL_I
  {0x00, 0x0e, 0x0a, 0x02, 0x02, 0x02}, // SYMBOL_J
  {0x00, 0x0a, 0x0a, 0x0c, 0x0a, 0x0a}, // SYMBOL_K
  {0x00, 0x0e, 0x08, 0x08, 0x08, 0x08}, // SYMBOL_L
  {0x00, 0x0a, 0x0a, 0x0e, 0x0e, 0x0e}, // SYMBOL_M
  {0x00, 0x0a, 0x0a, 0x0a, 0x0a, 0x0e}, // SYMBOL_N
  {0x00, 0x0e, 0x0a, 0x0a, 0x0a, 0x0e}, // SYMBOL_O
  {0x00, 0x08, 0x08, 0x0e, 0x0a, 0x0e}, // SYMBOL_P
  {0x00, 0x06, 0x08, 0x0a, 0x0a, 0x0e}, // SYMBOL_Q
  {0x00, 0x0a, 0x0a, 0x0c, 0x0a, 0x0e}, // SYMBOL_R
  {0x00, 0x0e, 0x02, 0x0e, 0x08, 0x0e}, // SYMBOL_S
  {0x00, 0x04, 0x04, 0x04, 0x04, 0x0e}, // SYMBOL_T
  {0x00, 0x0e, 0x0a, 0x0a, 0x0a, 0x0a}, // SYMBOL_U
  {0x00, 0x04, 0x04, 0x0e, 0x0a, 0x0a}, // SYMBOL_V
  {0x00, 0x0e, 0x0e, 0x0e, 0x0a, 0x0a}, // SYMBOL_W
  {0x00, 0x0a, 0x0e, 0x04, 0x0e, 0x0a}, // SYMBOL_X
  {0x00, 0x04, 0x04, 0x0e, 0x0a, 0x0a}, // SYMBOL_Y
  {0x00, 0x0e, 0x08, 0x04, 0x02, 0x0e}  // SYMBOL_Z
};

size_t DivExportAtari2600::writeTextGraphics(SafeWriter* w, const char* value) {
  size_t bytesWritten = 0;

  bool end = false;
  size_t len = 0; 
  while (len < 6 || !end) {
    w->writeText(fmt::sprintf("TITLE_GRAPHICS_%d\n    byte ", len));
    len++;
    char ax = 0;
    if (!end) {
      ax = *value++;
      if (0 == ax) {
        end = true;
      }
    } 
    char bx = 0;
    if (!end) {
      bx = *value++;
      if (0 == bx) end = true;
    }
    auto ai = getFontIndex(ax);
    auto bi = getFontIndex(bx);
      for (int i = 0; i < 6; i++) {
      if (i > 0) {
        w->writeText(",");
      }
      const unsigned char c = (FONT_DATA[ai][i] << 4) + FONT_DATA[bi][i];
      w->writeText(fmt::sprintf("%d", c));
      bytesWritten += 1;
    }
    w->writeText("\n");
  }
  w->writeText(fmt::sprintf("TITLE_LENGTH = %d", len));
  return bytesWritten;
}
