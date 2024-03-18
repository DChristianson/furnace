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
#include <set>
#include "../../ta-log.h"

const unsigned int AUDC0 = 0x15;
const unsigned int AUDC1 = 0x16;
const unsigned int AUDF0 = 0x17;
const unsigned int AUDF1 = 0x18;
const unsigned int AUDV0 = 0x19;
const unsigned int AUDV1 = 0x1A;

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
  logD("writing register dump");
  SafeWriter* dump =new SafeWriter;
  dump->init();
  for (auto &write : registerWrites) {
    dump->writeText(fmt::sprintf("%d %d %d:SS%d ORD%d ROW%d SYS%d> %d = %d\n",
      write.nextTickCount,
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

  // capture all sequences
  logD("performing sequence capture");
  std::vector<String> channelSequences[2];
  std::map<String, DumpSequence> registerDumps;
  captureSequence(e, 0, 0, DIV_SYSTEM_TIA, channel0AddressMap, channelSequences[0], registerDumps);
  captureSequence(e, 0, 1, DIV_SYSTEM_TIA, channel1AddressMap, channelSequences[1], registerDumps);

  int uniqueValues[3];
  int valueFreq[3][256];
  memset(uniqueValues, 0, 3 * sizeof(int));
  memset(valueFreq, 0, 3 * 256 * sizeof(int));
  for (auto& x: registerDumps) {
    for (auto& y: x.second.intervals) {
      for (int i = 0; i < 3; i++) {
        if (0 == valueFreq[i][y.state.registers[i]]) {
          uniqueValues[i]++;
        }
        valueFreq[i][y.state.registers[i]] += 1;
      }
    }
  }
  logD("regFreq %d %d %d", uniqueValues[0], uniqueValues[1], uniqueValues[2]);

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
  findCommonDumpSequences(
    registerDumps,
    commonDumpSequences,
    frequencyMap,
    representativeMap);

  writeTrackDataV1(
    e,
    commonDumpSequences,
    frequencyMap,
    representativeMap,
    registerDumps,
    ret
  );
  writeTrackData(
    e,
    registerWrites,
    ret
  );
  // writeTrackDataV3(
  //   e,
  //   registerWrites,
  //   ret
  // );

  // create meta data (optional)
  logD("writing track title graphics");
  SafeWriter* titleData=new SafeWriter;
  titleData->init();
  titleData->writeText(fmt::sprintf("; Song: %s\n", e->song.name));
  titleData->writeText(fmt::sprintf("; Author: %s\n", e->song.author));
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

void DivExportAtari2600::writeTrackDataV1(
  DivEngine* e, 
  std::map<uint64_t, String> &commonDumpSequences,
  std::map<uint64_t, unsigned int> &frequencyMap,
  std::map<String, String> &representativeMap,
  std::map<String, DumpSequence> &registerDumps,
  std::vector<DivROMExportOutput> &ret
) {

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
      if (j % 8 == 0) {
        trackData->writeText("\n    byte ");
      } else {
        trackData->writeText(",");
      }
      String key = getSequenceKey(patternIndex.subsong, patternIndex.ord, j, patternIndex.chan);
      trackData->writeText(representativeMap[key]); // the representative
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
    trackData->writeText(fmt::sprintf("    ;F%d C%d V%d\n", last.registers[1], last.registers[0], last.registers[2]));
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
 * Write track data 
 * 
 * Stream data format:
 * 
 * 00000000 stop
 * 000ddddd sustain for d, 31 >= d >= 1
 * rrrxxxxx r <- x, 6 >= r >= 1, 31 >= x >= 0
 * 
 * 0000cccc 0000dddd 000fffff 000ggggg 0000vvvv 0000wwww
 * 
 */
void DivExportAtari2600::writeTrackData(
  DivEngine* e, 
  std::vector<RegisterWrite> &registerWrites,
  std::vector<DivROMExportOutput> &ret
) {


  // encode command streams
  std::vector<AlphaCode> codeSequences[e->song.subsong.size()][2];
  for (size_t song = 0; song < e->song.subsong.size(); song++) {
    for (int channel = 0; channel < 2; channel += 1) {
      int pendingRegisters[NUM_TIA_REGISTERS];
      int currentRegisters[NUM_TIA_REGISTERS];
      memset(currentRegisters, 0, NUM_TIA_REGISTERS * sizeof(int)); 
      memset(pendingRegisters, 0, NUM_TIA_REGISTERS * sizeof(int));
      int lastCommandTicks = 0;
      int lastCommandSeconds = 0;
      int ticksSinceLastWrite = 0;
      int commandCount = 0;
      for (auto &write : registerWrites) {
        // get address
        const int r = write.addr - AUDC0;
        if ((r < 0) || (r > AUDV1) || ((r % 2) != channel)) {
          continue;
        }

        // get delta since last iteration
        const int deltaTicks = 0 == commandCount ? 0 : (
          write.ticks - lastCommandTicks + 
          (TICKS_PER_SECOND * (write.seconds - lastCommandSeconds))
        );
        lastCommandSeconds = write.seconds;
        lastCommandTicks = write.ticks;
        commandCount++;
        
        ticksSinceLastWrite += deltaTicks;
        const int framesToWrite = ticksSinceLastWrite / TICKS_AT_60HZ;
        
        if (framesToWrite > 0) {
          TiaRegisterMask registerMask;
          for (size_t i = channel; i < NUM_TIA_REGISTERS; i += 2) {
            if (pendingRegisters[i] != currentRegisters[i]) {
              registerMask[i] = 1;
            }
          }
          if (registerMask.any()) {
            // changed frame time
            writeAlphaCodesToChannel(
              channel,
              registerMask,
              pendingRegisters,
              framesToWrite,
              codeSequences[song][channel]);
            memcpy(currentRegisters, pendingRegisters, NUM_TIA_REGISTERS * sizeof(int));
            ticksSinceLastWrite = ticksSinceLastWrite - (framesToWrite * TICKS_AT_60HZ);
          }
        }
        // update register value
        pendingRegisters[r] = write.val;
      }
      // BUGBUG: end of song delay?
      const int framesToWrite = ticksSinceLastWrite / TICKS_AT_60HZ;
      if (framesToWrite > 0) {
        TiaRegisterMask registerMask;
        for (size_t i = channel; i < NUM_TIA_REGISTERS; i += 2) {
          if (pendingRegisters[i] != currentRegisters[i]) {
            registerMask[i] = 1;
          }
        }
        if (registerMask.any()) {
          writeAlphaCodesToChannel(
            channel,
            registerMask,
            pendingRegisters,
            framesToWrite,
            codeSequences[song][channel]);
        }
      }
      codeSequences[song][channel].emplace_back(0);
    }
  }

  // create a frequency map of all codes
  std::map<AlphaCode, unsigned int> frequencyMap;
  size_t codeSequenceTotalSize = 0;
  for (size_t song = 0; song < e->song.subsong.size(); song++) {
    for (int channel = 0; channel < 2; channel += 1) {
      codeSequenceTotalSize += codeSequences[song][channel].size();
      for (auto cx : codeSequences[song][channel]) {
        frequencyMap[cx]++;
      }
    }
  }

  // index all codes into an alphabet
  std::vector<AlphaCode> alphabet;
  std::map<AlphaCode, AlphaChar> index;
  createAlphabet(
    frequencyMap,
    alphabet,
    index
  );
    
  // compute some basic stats
  logD("alphabet size %d", alphabet.size());
  for (auto a : alphabet) {
    logD("  %08x -> %d", a, frequencyMap[a]);
  }
  double entropy = 0;
  const double symbolCount = codeSequenceTotalSize;
  for (auto &x : frequencyMap) {
    if (0 == x.first) {
      continue;
    }
    const double p = ((double) x.second) / symbolCount;
    const double logp = log2(p);
    entropy = entropy - (p * logp);
  }
  const double expectedBits = entropy * symbolCount;
  const double expectedBytes = expectedBits / 8;
  logD("entropy: %lf (%lf bits / %lf bytes)", entropy, expectedBits, expectedBytes);

  // compress sequences
  for (size_t song = 0; song < e->song.subsong.size(); song++) {
    for (int channel = 0; channel < 2; channel += 1) {
      std::vector<AlphaChar> alphaSequence;
      alphaSequence.reserve(codeSequences[song][channel].size());         

      // copy string into alphabet
      for (auto code : codeSequences[song][channel]) {
        AlphaChar c = index.at(code);
        alphaSequence.emplace_back(c);
      }
      // create suffix tree 
      SuffixTree *root = createSuffixTree(
        alphabet,
        alphaSequence
      );

      // find maximal repeats
      std::vector<Span> repeats;
      std::vector<Span> copySequence;
      root->gather_repeated_subsequences(alphaSequence, repeats, copySequence);

      delete root;

      const size_t encodingSize = encodeSpan(
        codeSequences[song][channel], 
        Span(0, alphaSequence.size(), 1),
        copySequence, true);

      logD("sequence estimated size: %d", encodingSize);

    }
  }

  // write literal dictionary
  if (this->literalDictionarySize > 0) { 
    SafeWriter* literals =new SafeWriter;
    literals->init();

    std::vector<std::pair<AlphaCode, size_t>> literalFreq;
    std::map<AlphaCode, size_t> literalDictionary;
    for (auto &x : frequencyMap) {
      // BUGBUG: only write 
      if ((x.first >> 24) != 7) continue; // BUGBUG magic detection of 2-byte codes
      if (x.second < 2) continue; // BUGBUG: 
      literalFreq.emplace_back(x);
    }
    // sort by freq
    std::sort(literalFreq.begin(), literalFreq.end(), compareSecond<AlphaCode, size_t>);
    for (size_t i = 0; i < this->literalDictionarySize && i < literalFreq.size(); i++) {
      literalDictionary[literalFreq[i].first] = i;
    }

    // BUGBUG: output
    ret.push_back(DivROMExportOutput("Track_literals.asm", literals));
  }
      

  // get suffix tree

  // start to write
  // if repeat, put in address of start
  // 
  // if  literal sequence
  // end literal
  // inject repeats
  //

  // BUGBUG: Not production 
  // just testing 
//  testCommonSubsequences("banana");
  testCommonSubsequences("abcdeabcdefghijfghijabcdexyxyxyx");
 // testCommonSubsequences("xabcyiiizabcqabcyr");
//  testCommonSubsequencesBrute("banana");
//  testCommonSubsequencesBrute("xabcyiiizabcqabcyr");
  // findCommonSubSequences(
  //   channelSequences[0],
  //   commonDumpSequences,
  //   representativeMap
    // );


  // create track data
  logD("writing song audio data");
  SafeWriter* songData=new SafeWriter;
  songData->init();
  songData->writeText(fmt::sprintf("; Song: %s\n", e->song.name));
  songData->writeText(fmt::sprintf("; Author: %s\n", e->song.author));

  // emit song table
  logD("writing song table");
  size_t songTableSize = 0;
  songData->writeText("\n; Song Lookup Table\n");
  songData->writeText(fmt::sprintf("NUM_SONGS = %d\n", e->song.subsong.size()));
  songData->writeText("SONG_TABLE_START_LO\n");
  for (size_t i = 0; i < e->song.subsong.size(); i++) {
    songData->writeText(fmt::sprintf("    byte <SONG_%d_ADDR\n", i));
    songTableSize++;
  }
  songData->writeText("SONG_TABLE_START_HI\n");
  for (size_t i = 0; i < e->song.subsong.size(); i++) {
    songData->writeText(fmt::sprintf("    byte >SONG_%d_ADDR\n", i));
    songTableSize++;
  }

  // audio metadata
  songData->writeC('\n');
  songData->writeText(fmt::sprintf("; Song Table Size %d\n", songTableSize));
  ret.push_back(DivROMExportOutput("Track_song_table.asm", songData));

}

/**
 * @brief write a set of registers
 * 
 * @param channel 
 * @param registerMask 
 * @param values 
 * @param framesToWrite 
 * @param codeSequence 
 */
void DivExportAtari2600::writeAlphaCodesToChannel(
  int channel,
  const TiaRegisterMask &registerMask,
  int *values,
  int framesToWrite,
  std::vector<AlphaCode> &codeSequence
) {

  // encode a register update as a 32 bit integer containing all the register updates for a single channel
  // 0000cfv 0000cccc 000fffff 0000vvvv mask bits + cx + fx + vx
  AlphaCode rx = 0;
  const int setBits = registerMask.count();
  for (size_t r = channel; r < NUM_TIA_REGISTERS; r += 2) {
    rx = (rx << 1) | (setBits > 1 ? 1 : registerMask[r]);
  }
  for (size_t r = channel; r < NUM_TIA_REGISTERS; r += 2) {
    rx = (rx << 8) | ((setBits > 1 || registerMask[r]) ? values[r] : 0);
  }
  codeSequence.emplace_back(rx);
  framesToWrite -= 1;

  // encode any additional frames as 5 bits indicating the number of frames to skip
  while (framesToWrite > 0) {
    const unsigned char sx = framesToWrite > 63 ? 63 : framesToWrite;
    codeSequence.emplace_back(sx);
    framesToWrite = framesToWrite - sx;
  }

}

size_t DivExportAtari2600::encodeSpan(
  const std::vector<AlphaCode> sequence, 
  const Span &bounds,
  const std::vector<Span> &copySequence,
  const bool recurse)
{
  size_t totalSize = 0;
  size_t currentIndex = bounds.start;
  size_t endIndex = bounds.start + bounds.length;
  while (currentIndex < endIndex) {
    if ((!recurse) || (copySequence[currentIndex].length == 1)) {
      totalSize += writeAlphaCode(sequence[currentIndex]);
      currentIndex++;
      continue;
    }

    if (copySequence[currentIndex].start == currentIndex) {
      // push copy block
      totalSize += this->writeSpanReference(copySequence[currentIndex].start, copySequence[currentIndex].length);
      totalSize += this->writeSpanLabel(copySequence[currentIndex]);
      this->encodeSpan(
        sequence, 
        copySequence[currentIndex],
        copySequence,
        false);
      totalSize += writePop();
    } else {
      // emit block ref
      totalSize += this->writeSpanReference(copySequence[currentIndex].start, 1);
    }

    currentIndex += copySequence[currentIndex].length;
  }

  return totalSize;
}

size_t DivExportAtari2600::writeAlphaCode(AlphaCode code) { return ((code >> 24) == 7) ? 2 : 1; }
size_t DivExportAtari2600::writeSpanReference(const size_t start, const size_t length) { return 2; }
size_t DivExportAtari2600::writeSpanLabel(const Span &span) { return 0; }
size_t DivExportAtari2600::writePop() { return 1; }

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
      rx = audcx << 3 | dmod << 3 | 0x05; // d101
    } else {
      // volume 
      rx = audvx << 3 | dmod << 3 | 0x01; // d001
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
