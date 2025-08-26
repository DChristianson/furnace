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

#include "tiaZip.h"
#include "suffixTree.h"
#include "huffman.h"

#include <fmt/printf.h>
#include <queue>
#include <set>
#include "../../ta-log.h"

//
//
//  - compression goals
//    - Coconut_Mall in 4k
//       - JUMP+SKIP encoded  4350 data / 1213 jump = 5563
//       - JUMP+SKIP+POP+HUFF 3295 data / 536 jump = 3831   
//       - w/no multi cmd 3193 data / 629 jump = 3822
//       - w encoding bytes 3532 data / 629 jump = 4161
//       - w encoding bytes + less bits for volume 3296 data / 675 jump = 3971
//       - w more complex instruction set + RETURN  = 2936 data / 661 jump = 3596
//       - w validated output = 2998 data / 771 jump = 3769
//
// DONE
//  - alpha
//  - output schemes
//    - delta player
//    - simple player
//    - simple player with duration
//  - testability
//    - assemble test songs
//      - karri songs
//      - short 
//    - automate process and build
//      - all songs X all compressions
//    - automate headless stella run + capture reg writes
//    - automate headless stella run log comparison
//    - fix multi-song test
//  - glitches
//    - TIA_Spanish_Fly is slow again
//  - debugging
//    - debug output for byte codes
//  - suffix encoding tools
//    - multi-byte alpha encode scheme
//  - compression testing
//    - encode then decode at AlphaCode
//  - the good compression
//    - figure out compression scheme name
//    - produce actual bytecode output (can be messy)
//    - successful decoder in assembly
//    - encoding schemes
//        - JUMP (on 0, check jump stream)
//        - GOTO (on 0, check jump stream, on 0xf8 go straight to next)
//        - FORK (on 0x??, read bit off jump stream)
//        - POP back to last/front scheme (on 0x??, read from stack )
//        - try separating sustain/pause commands in data stream (hardcoded option)
//        - trial huffman code data stream
//          - construct huffman tree
//          - perform encoding of tree and stream
//          - try compressing jumps as well
//          - try encoding the bytes 
//          - try lossy compression :(
//          - try changing instruction set
//              - POP       0
//              - GOTO      1000-1FFF
//              - VOLUME    0-15
//              - FREQUENCY 0-31
//              - CONTROL   0-15
//              - DURATION  0-15
//              - variable length for sustain bits
//              - "next" volume +/- (bigrams?)
//          - create test decoder
//  - compression goals
//    - Coconut... small in 4k
//    - breakbeat in 4k
//    - test compression in 4k
//  - final output schemes
//    - compact no bank switching
//        - encoder
//        - decoder
//        - assembly
//        - warning if too big
//        - tested
//    - dynamic with zip
//        - trial span encoder - 3882 estimated bytes
//        - encoder
//        - decoder
//        - validated
//    - 2600 batari basic
//        - decoder
//    - compact with bank switching
//        - encoder
//        - decoder
//        - validated
//        - assembly
//        - tested
//    - zip with huffman and bank switching
//        - assembly
//        - tested: simple repeat
// BETA 
//  - final output schemes
//    - 7800 basic
//        - decoder
//    - 7800 zip
//        - decoder
//    - zip with huffman and bank switching
//        - tested: complex example
//    - zip with 4 channel isolation, huffman and bank switching
//        - tested: complex example
//  - compression goals
// >>> 18 + 18 + 6 + 6 + 8 + 8 + 12 + 12 + 17 + 17 + 23 + 23 + 20 + 20 + 22 + 22 + 7 + 7 + 18 + 18 + 2 + 2 + 16 + 16 + 14 + 14
// 366
// >>> 3577 + 366 current
// 3943
// >>> 18 + 12 + 16 + 31 + 31 + 16 + 16 + 14 + 1
// 155
// >>> 3741 + 155 base
// 3896
//    - Coconut Mall 4k
//        - try separate channel code compression
//        - try separate effects tracks
//        - try separate instrument encodings
//        - instrumate ADSR to handle volume?
//        - properly analytic span compression analysis?
//  - debugging
//    - proper analytic debug output for TIAZIP spans
//  - glitch
//    - two track jumps in one frame goes over
//    - tia_entertainer has inconsistent timing, missing patterns
//      - missing pattern 0 causes glitching with FSEQ codex
//  - testability
//    - all targets test
//    - clean up test output
//  - dev help
//    - docs on how to use multiple schemes
//    - makefile aware of song size / compression
//  - code
//    - cleanup pass
//    - create binary builder
//  - usability
//    - documentation
//      - BASIC scheme
//      - TIAZIP scheme
//    - select target formats (asm, basic, rom)
//    - EZ mode - either select appropriate player (mini, etc) / or do bank switching by default
// STRETCH
//  - standalone tiazip tool
//  - moar output schemes
//    - Atari 8-bit exports
//    - DPC+ export
//  - zip compression
//    - use estimate of compression savings to guide span choice
//        - trial instrument / waveform scheme
//          - use slocum tuning?
//        - just use actual zip or 7z with augments?
//          - need low memory 6502 decoder
// 

bool DivExportTIAZip::go(DivEngine* eng) {
  progress[0].name = "Export";
  progress[0].amount = 0.0f;

  e = eng;
  running = true;
  failed = false;
  mustAbort = false;
  exportThread = new std::thread(&DivExportTIAZip::run, this);
  return true;
}

DivExportTIAZip::~DivExportTIAZip() {
  for (auto registerDump : registerDumps) {
    delete registerDump;
  }
}

void DivExportTIAZip::wait() {
  if (exportThread!=NULL) {
    exportThread->join();
    delete exportThread;
  }
}

void DivExportTIAZip::abort() {
  mustAbort=true;
  wait();
}

bool DivExportTIAZip::isRunning() {
  return running;
}

bool DivExportTIAZip::hasFailed() {
  return failed;
}

DivROMExportProgress DivExportTIAZip::getProgress(int index) {
  return progress[0];
}

void DivExportTIAZip::run() {

  bool debugRegisterDump = conf.getBool("debug", false);
  int compressionLevel = conf.getInt("compressionLevel", 1);
  int minSpanLength = conf.getInt("minSpanLength", 3);
  int maxSustain = conf.getInt("maxSustain", 16);
  int jumpMapBits = conf.getInt("jumpMapBits", 5);

  logD("maxSustain %d jumpMapBits %d", maxSustain, jumpMapBits);

  assert(compressionLevel <= 4 && compressionLevel >= 0);

  // create register dumps
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    registerDumps.push_back(new RegisterDump(e, subsong));
  }

  if (debugRegisterDump) {
    // dump all register writes
    SafeWriter* dump = new SafeWriter;
    dump->init();
    dump->writeText(fmt::sprintf("; Song: %s\n", e->song.name));
    dump->writeText(fmt::sprintf("; Author: %s\n", e->song.author));
    writeRegisterDumps(dump, registerDumps);
    output.push_back(DivROMExportOutput("RegisterDump.txt", dump));
  }

  // write track data
  writeTrackDataTIAZip(compressionLevel, minSpanLength, maxSustain, jumpMapBits);

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
  String title = (e->song.name.length() > 0) ?
     (e->song.name + " by " + e->song.author) :
     "furnace tracker";
  if (title.length() > 21) {
    title = title.substr(0, 18) + "...";
    logD("shortening title to %s (%d)", title, title.length());
  }
  writeTextGraphics(titleData, title.c_str());
  output.push_back(DivROMExportOutput("Track_meta.asm", titleData));
  running = false;
}

/**
 * Code scheme
 */
enum CODE_TYPE {
  STOP,            // 0 end of stream
  WRITE_REGISTERS, // 1 write registers
  VOL_INC,         // 2 increment volume
  VOL_DEC,         // 3 decrement volume
  PAUSE,           // 4 wait for duration
  SUSTAIN,         // 5 sustain for duration
  JUMP,            // 6 jump address
  BRANCH_POINT,    // 7 branch point
  SKIP,            // 8 skip forward to next block
  TAKE_DATA_JUMP,  // 9 jump to next address in data stream
  TAKE_TRACK_JUMP, // 10 jump to next address in track stream
  RETURN_LAST,     // 11 return to last jump point
  RETURN_FF,       // 12 advance to end of stream
  RETURN_NOOP,     // 13
  VELOCITY         // 14 set velocity
};

static const std::map<CODE_TYPE, size_t> CODE_TYPE_WEIGHTS = {
  {STOP, 5},
  {WRITE_REGISTERS, 11},
  {VOL_INC, 5},
  {VOL_DEC, 3},
  {PAUSE, 0},
  {SUSTAIN, 5},
  {JUMP, 13},
  {BRANCH_POINT, 4},
  {SKIP, 1},
  {TAKE_DATA_JUMP, 2},
  {TAKE_TRACK_JUMP, 12},
  {RETURN_LAST, 4},
  {RETURN_FF, 4},
  {RETURN_NOOP, 0},
  {VELOCITY, 5}
};

enum CHANGE_STATE {
  NOOP,
  CHANGE
};

AlphaCode CODE_WRITE_REGISTERS(
  CHANGE_STATE cc,
  unsigned char cx,
  CHANGE_STATE fc,
  unsigned char fx,
  CHANGE_STATE vc,
  unsigned char vx, 
  unsigned char duration
) {
  return (AlphaCode) (
    (AlphaCode) WRITE_REGISTERS << 56 | 
    (AlphaCode) cc << 48 |
    (AlphaCode) cx << 40 |
    (AlphaCode) fc << 32 |
    (AlphaCode) fx << 24 |
    (AlphaCode) vc << 16 |
    (AlphaCode) vx << 8 |
    (AlphaCode) duration
  );
}

AlphaCode CODE_PAUSE(unsigned char duration) {
  return (AlphaCode) ((AlphaCode) PAUSE << 56 | duration);
}

AlphaCode CODE_SUSTAIN(unsigned char duration) {
  return (AlphaCode) ((AlphaCode) SUSTAIN << 56 | duration);
}

AlphaCode CODE_VELOCITY(int velocity) {
  return (AlphaCode) ((AlphaCode) VELOCITY << 56 | (velocity & 0xff));
}

// Fixed codes
const AlphaCode CODE_STOP = ((AlphaCode) STOP) << 56;
const AlphaCode CODE_WRITE_REGISTERS_MASK = 0xffff00ff00ff0000;
const AlphaCode CODE_WRITE_REGISTERS_000  = 0x0100000000000000; // BUGBUG: HACKY
const AlphaCode CODE_WRITE_REGISTERS_001  = 0x0100000000010000; // BUGBUG: HACKY
const AlphaCode CODE_WRITE_REGISTERS_010  = 0x0100000100000000; // BUGBUG: HACKY
const AlphaCode CODE_WRITE_REGISTERS_011  = 0x0100000100010000; // BUGBUG: HACKY
const AlphaCode CODE_WRITE_REGISTERS_100  = 0x0101000000000000; // BUGBUG: HACKY
const AlphaCode CODE_WRITE_REGISTERS_101  = 0x0101000000010000; // BUGBUG: HACKY
const AlphaCode CODE_WRITE_REGISTERS_110  = 0x0101000100000000; // BUGBUG: HACKY
const AlphaCode CODE_WRITE_REGISTERS_111  = 0x0101000100010000; // BUGBUG: HACKY
const AlphaCode CODE_VOL_INC = ((AlphaCode) VOL_INC) << 56;
const AlphaCode CODE_VOL_DEC = ((AlphaCode) VOL_DEC) << 56;
const AlphaCode CODE_PAUSE_0 = ((AlphaCode) PAUSE) << 56;
const AlphaCode CODE_SUSTAIN_0 = ((AlphaCode) SUSTAIN) << 56;
const AlphaCode CODE_VELOCITY_0 = ((AlphaCode) VELOCITY) << 56;
const AlphaCode CODE_BRANCH_POINT = ((AlphaCode) BRANCH_POINT) << 56 | 0;
const AlphaCode CODE_SKIP = ((AlphaCode) SKIP) << 56 | 0;
const AlphaCode CODE_TAKE_DATA_JUMP = ((AlphaCode) TAKE_DATA_JUMP) << 56;
const AlphaCode CODE_TAKE_TRACK_JUMP = ((AlphaCode) TAKE_TRACK_JUMP) << 56;
const AlphaCode CODE_RETURN_LAST = ((AlphaCode) RETURN_LAST) << 56;
const AlphaCode CODE_RETURN_FF = ((AlphaCode) RETURN_FF) << 56;
const AlphaCode CODE_RETURN_NOOP = ((AlphaCode) RETURN_NOOP) << 56;

AlphaCode CODE_JUMP(size_t subsong, int channel, size_t address) {
  return ((AlphaCode)JUMP << 56) | 
         ((AlphaCode)subsong << 48) |
         ((AlphaCode)channel << 40) |
         address;
}

size_t GET_CODE_JUMP_ADDRESS(const AlphaCode c) {
  return c & 0x1fff;
}

CODE_TYPE GET_CODE_TYPE(const AlphaCode code) {
  return (CODE_TYPE)(code >> 56);
}

AlphaCode GET_CODE_WRITE_REGISTERS_MASKED(const AlphaCode c) {
  return c & CODE_WRITE_REGISTERS_MASK;
}

CHANGE_STATE GET_CODE_WRITE_CC(AlphaCode c) {
  return (CHANGE_STATE) ((c >> 48) & 0xff);
}

unsigned char GET_CODE_WRITE_CX(AlphaCode c) {
  return (c >> 40) & 0x0f;
}

CHANGE_STATE GET_CODE_WRITE_FC(AlphaCode c) {
  return (CHANGE_STATE) ((c >> 32) & 0xff);
}

unsigned char GET_CODE_WRITE_FX(AlphaCode c) {
  return (c >> 24) & 0x3f;
}

CHANGE_STATE GET_CODE_WRITE_VC(AlphaCode c) {
  return (CHANGE_STATE) ((c >> 16) & 0xff);
}

unsigned char GET_CODE_WRITE_VX(AlphaCode c) {
  return (c >> 8) & 0xff;
}

unsigned char GET_CODE_WRITE_DURATION(AlphaCode c) {
  return (c & 0xff);
}

size_t GET_CODE_SUBSONG(const AlphaCode c) {
  return (c >> 48) & 0xff;
}

int GET_CODE_CHANNEL(const AlphaCode c) {
  return (c >> 40) & 0xff;
}

unsigned char GET_CODE_VELOCITY(const AlphaCode c) {
  return (c & 0xff);
}

size_t BITSTREAM_ADDR(size_t addr) {
  return ((addr << 1) & 0x3ff0) | (addr & 0x7);
}

// BUGBUG: STATS
size_t CALC_ENTROPY(const std::map<AlphaCode, size_t> &frequencyMap) {
  double entropy = 0;
  size_t totalCount = 0;
  for (auto &x : frequencyMap) {
    totalCount += x.second;
  }
  const double symbolCount = totalCount;
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
  return ceil(expectedBits);
}

// BUGBUG: STATS
void SHOW_FREQUENCIES(const std::map<AlphaCode, size_t> &frequencyMap) {
  std::vector<std::pair<AlphaCode, size_t>> frequencies(
    frequencyMap.begin(),
    frequencyMap.end()
  );
  std::sort(
    frequencies.begin(),
    frequencies.end(),
    compareCodeFrequency
  );
  for (auto &x: frequencies) {
    logD("  %08x -> %d", x.first, x.second);
  }
}

// BUGBUG: STATS
void SHOW_TREE(
  const std::map<AlphaCode, size_t> &frequencyMap,
  const std::map<AlphaCode, std::vector<bool>> &codeIndex,
  const int unencodedBits,
  AlphaCode defaultCode
) {
  logD("compressed dictionary size: %d", frequencyMap.size());
  std::vector<std::pair<AlphaCode, size_t>> frequencies(
    frequencyMap.begin(),
    frequencyMap.end()
  );
  std::sort(
    frequencies.begin(),
    frequencies.end(),
    compareCodeFrequency
  );
  size_t totalBits = 0;
  size_t totalUnencodedBits = 0;
  for (auto &x: frequencies) {
    auto it = codeIndex.find(x.first);
    if (it == codeIndex.end()) {
      it = codeIndex.find(defaultCode);
    }
    auto &bitvec = (*it).second;
    String huffmanCode = ".";
    if (bitvec.size() > 0) {
      for (int i = bitvec.size(); --i >= 0; ) {
        huffmanCode += bitvec.at(i) ? "1" : "0";
      }
    }
    huffmanCode += ".";
    totalBits += x.second * bitvec.size();
    totalUnencodedBits += x.second * unencodedBits;
    logD("  %08x -> %d (%s) %d", x.first, x.second, huffmanCode, bitvec.size());
  }
  logD("  totalBits: %d, unencodedBits: %d", totalBits, totalUnencodedBits);
}

// compacted encoding
void DivExportTIAZip::writeTrackDataTIAZip(int compressionLevel, int minSpanLength, int maxSustain, int jumpMapBits) {

  // encode command streams
  size_t totalUncompressedSequenceSize = 0;
  std::map<AlphaCode, size_t> frequencyMap;
  std::vector<AlphaCode> codeSequences[e->song.subsong.size()][NUM_ZIP_CHANNELS];
  for (size_t subsong = 0; subsong < registerDumps.size(); subsong++) {
    auto registerDump = registerDumps[subsong];

    for (int channel = 0; channel < NUM_ZIP_CHANNELS; channel++) {
      auto &codeSequence = codeSequences[subsong][channel];

      // get channel states
      ChannelStateSequence dumpSequence(ChannelState(0), maxSustain);
      registerDump->writeChannelStateSequence(
        0,
        -1, // channel == 0 ? AUDV0 : AUDV1,
        channel == 0 ? tiaChannel0AddressMap : tiaChannel1AddressMap,
        dumpSequence
      );

      // // BUGBUG testing
      // if (compressionLevel == 2) {
      //   ChannelStateSequence adsrStateSequence(ChannelState(0), maxSustain);
      //   ChannelState currentState(0);
      //   for (auto& n: dumpSequence.intervals) {
      //     // BUGBUG
      //     // // normalize lead voice
      //     // if (n.state.registers[0] == 12) {
      //     //   n.state.registers[0] = 0x04;
      //     //   n.state.registers[1] += 0x20;
      //     // }
      //     currentState.registers[2] = n.state.registers[2];
      //     n.state.registers[2] = 15;          
      //     logD("ADSR %d %d %d", subsong, channel, currentState.registers[2]);
      //     adsrStateSequence.updateState(currentState, n.row);
      //     adsrStateSequence.intervals.back().duration = n.duration;
      //   }
      //
      //   // convert to AlphaCode
      //   auto &adsrSequence = adsrSequences[subsong][channel];
      //   ChannelState last(adsrStateSequence.initialState);
      //   for (auto& n: adsrStateSequence.intervals) {
      //     encodeChannelStateCodes(n.state, n.duration, last, maxSustain, adsrSequence);
      //     last = n.state;
      //   }
      //   adsrSequence.emplace_back(CODE_STOP);
      //
      // //   // create frequency map
      // //   for (auto c: effectSequence) {
      // //     frequencyMap[c]++;
      // //   }
      // //   totalUncompressedSequenceSize += effectSequence.size();
      //
      // }
      
      // try to create a volume level prediction
      std::vector<VelocityInterval> velocityIntervals;
      velocityIntervals.push_back(VelocityInterval(0, 0, 0));
      ChannelState lastChannelState(dumpSequence.initialState);
      for (auto& n: dumpSequence.intervals) { 
        VelocityInterval &currentVelocity = velocityIntervals.back();
        int velocity = compressionLevel < 2 ? 0 : (int) n.state.registers[2] - (int) lastChannelState.registers[2];
        int step = n.duration;
        if (velocity == currentVelocity.velocity && velocity == 0) {
          currentVelocity.duration += n.duration;
          if (currentVelocity.step == 0 || step < currentVelocity.step) {
            currentVelocity.step = step;
          }
        } else if (velocity == currentVelocity.velocity && step == currentVelocity.step) {
          currentVelocity.duration += n.duration;
        } else {
          velocityIntervals.push_back(VelocityInterval(n.duration, velocity, step));
        }
        lastChannelState = n.state;
      }

      // convert to AlphaCode
      auto it = velocityIntervals.begin();
      logD("%d %d STARTING INTERVAL DURATION %d VELOCITY %d STEP %d", subsong, channel, (*it).duration, (*it).velocity, (*it).step);
      int effectiveVelocity = 0;
      int intervalDuration = 0;
      ChannelState lastState(dumpSequence.initialState);
      ChannelState currentState(dumpSequence.initialState);
      for (auto& n: dumpSequence.intervals) {
        intervalDuration += n.duration;
        if (intervalDuration > (*it).duration) {
          intervalDuration -= (*it).duration;
          it++;
          logD("%d %d NEW INTERVAL DURATION %d VELOCITY %d STEP %d NSTEPS %d", subsong, channel, (*it).duration, (*it).velocity, (*it).step, (*it).numSteps());
          effectiveVelocity = (*it).velocity;
          // if ((*it).velocity != effectiveVelocity && (*it).numSteps() > 3) { // BUGBUG: magic number
          //   effectiveVelocity = (*it).velocity;
          //   codeSequence.push_back(CODE_VELOCITY(effectiveVelocity));
          // }
        }
        currentState = n.state;
        if (effectiveVelocity == -1 || effectiveVelocity == 1) {
          logD("%d %d ACTIVATING EFFECT BIT", subsong, channel);
          currentState.registers[2] = 0x80 | (effectiveVelocity & 0x0f);
        }
        encodeChannelStateCodes(currentState, n.duration, lastState, maxSustain, effectiveVelocity, codeSequence);
        lastState = currentState;
      }
      codeSequence.emplace_back(CODE_STOP);

      // create frequency map
      for (auto c: codeSequence) {
        frequencyMap[c]++;
      }
      totalUncompressedSequenceSize += codeSequence.size();
    }
  }

  // using the initial frequency map, index all distinct codes into an "alphabet"
  std::vector<AlphaCode> alphabet;
  std::map<AlphaCode, AlphaChar> index;
  createAlphabet(
    frequencyMap,
    alphabet,
    index
  );

  std::map<AlphaChar, size_t>  alphaCharWeights;
  for (auto code : alphabet) {
    CODE_TYPE type = GET_CODE_TYPE(code);
    assert(CODE_TYPE_WEIGHTS.find(type) != CODE_TYPE_WEIGHTS.end());
    alphaCharWeights[index[code]] = CODE_TYPE_WEIGHTS.at(type);
  }
  const size_t branchWeight = 13;

  // debugging: compute basic stats
  // statistics
  logD("total codes : %d ", frequencyMap.size());
  CALC_ENTROPY(frequencyMap);

  // create compressed code sequence
  std::vector<AlphaCode> compressedCodeSequences[e->song.subsong.size()][NUM_ZIP_CHANNELS];
  std::vector<AlphaCode> spanSequences[e->song.subsong.size()][NUM_ZIP_CHANNELS];
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < NUM_ZIP_CHANNELS; channel += 1) {
      auto &codeSequence = codeSequences[subsong][channel];
      auto &compressedCodeSequence = compressedCodeSequences[subsong][channel];
      auto &spanSequence = spanSequences[subsong][channel];

      compressCodeSequence(
        subsong, 
        channel,
        alphabet,
        index,
        alphaCharWeights,
        branchWeight,
        codeSequence,
        compressionLevel,
        minSpanLength,
        compressedCodeSequence,
        spanSequence
      );

      validateCodeSequence(
        subsong, 
        channel,
        codeSequence,
        compressedCodeSequence,
        spanSequence
      );

      // if (compressionLevel == 2) {
      //   auto &effectSequence = codeSequences[subsong][channel + 2];
      //   auto &compressedEffectSequence = compressedCodeSequences[subsong][channel + 2];
      //   auto &spanEffectSequence = spanSequences[subsong][channel + 2];
      //   compressCodeSequence(
      //     subsong, 
      //     channel,
      //     alphabet,
      //     index,
      //     effectSequence,
      //     compressionLevel,
      //     minSpanLength * 2,
      //     compressedEffectSequence,
      //     spanEffectSequence
      //   );
      //   logD(
      //     "effect sequence size %d, compression %d, span %d", 
      //     effectSequence.size(),
      //     compressedEffectSequence.size(),
      //     spanEffectSequence.size());
      //   for (auto &n : compressedEffectSequence) {
      //     compressedCodeSequence.emplace_back(n);
      //   }
      //   for (auto &n : spanEffectSequence) {
      //     spanSequence.emplace_back(n);
      //   }
      // }

    }
  }

  // // compute frequencies
  // std::map<AlphaCode, size_t> codeFrequencies;
  // std::map<AlphaCode, size_t> codeTypeFrequencies;
  // std::map<AlphaCode, size_t> spanTypeFrequencies;
  // std::map<AlphaCode, size_t> jumpFrequencies;
  // std::map<AlphaCode, size_t> spanFrequencies;
  // std::map<AlphaCode, size_t> trackFrequencies;
  // size_t totalCodes = 0;
  // size_t totalData = 0;
  // size_t totalSpans = 0;
  // size_t totalJumps = 0;
  // size_t totalTracks = 0;
  // for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
  //   for (int channel = 0; channel < 2; channel += 1) {
  //     for (auto c : compressedCodeSequences[subsong][channel]) {
  //       codeFrequencies[c]++;
  //       totalCodes++;
  //       CODE_TYPE type = GET_CODE_TYPE(c);
  //       codeTypeFrequencies[type]++;
  //       if (type == CODE_TYPE::WRITE_REGISTERS) {
  //         CHANGE_STATE cc = GET_CODE_WRITE_CC(c);
  //         CHANGE_STATE vc = GET_CODE_WRITE_VC(c);
  //         CHANGE_STATE fc = GET_CODE_WRITE_FC(c);
  //         if (cc + vc + fc > 1) {
  //           totalData += 2;
  //         } else {
  //           totalData += 1;
  //         }
  //       } else {
  //         totalData += 1;
  //       }
  //     } 
  //     for (auto c : spanSequences[subsong][channel]) {
  //       trackFrequencies[c]++;
  //       totalTracks++;
  //       CODE_TYPE type = GET_CODE_TYPE(c);
  //       spanTypeFrequencies[type]++;
  //       if (type == CODE_TYPE::JUMP) {
  //         jumpFrequencies[c]++;
  //         totalJumps++;
  //       } else {
  //         spanFrequencies[c]++;
  //         totalSpans++;
  //       }
  //     }
  //   }
  // }

  // logD("total data: %d", totalData);
  // logD("unique jumps: %d/%d", jumpFrequencies.size(), totalJumps);
  // CALC_ENTROPY(jumpFrequencies);
  // logD("unique spans: %d/%d", spanFrequencies.size(), totalSpans);
  // CALC_ENTROPY(spanFrequencies);
  // logD("unique codes: %d/%d", codeFrequencies.size(), totalCodes);
  // CALC_ENTROPY(codeFrequencies);
  // logD("unique tracks: %d/%d", trackFrequencies.size(), totalTracks);
  // CALC_ENTROPY(trackFrequencies);
  // logD("data stream types");
  // SHOW_FREQUENCIES(codeTypeFrequencies);
  // logD("span types");
  // SHOW_FREQUENCIES(spanTypeFrequencies);

  encodeBitstreamDynamic(
    codeSequences,
    compressedCodeSequences,
    spanSequences,
    jumpMapBits,
    compressionLevel,
    0x0000,
    4096 * 8
  );

}

void DivExportTIAZip::compressCodeSequence(
  size_t subsong,
  int channel,
  const std::vector<AlphaCode> &alphabet,
  const std::map<AlphaCode, AlphaChar> &index,
  const std::map<AlphaChar, size_t> &alphaCharWeights,
  size_t branchWeight,
  const std::vector<AlphaCode>&codeSequence,
  int compressionLevel,
  int minSpanLength,
  std::vector<AlphaCode> &compressedCodeSequence,
  std::vector<AlphaCode> &spanSequence
) {

  spanSequence.reserve(codeSequence.size());
  compressedCodeSequence.reserve(codeSequence.size());

  std::vector<AlphaChar> alphaSequence;
  alphaSequence.reserve(codeSequence.size());         

  // copy string into alphabet
  for (auto code : codeSequence) {
    AlphaChar c = index.at(code);
    alphaSequence.emplace_back(c);
  }

  // create suffix tree 
  SuffixTree *root = createSuffixTree(
    alphabet,
    alphaSequence
  );

  // copyMap[i] -> index of leftmost copy of alphaSequence[i]
  std::vector<size_t> copyMap; 
  copyMap.resize(alphaSequence.size());

  // branch frequency
  std::vector<std::map<size_t, size_t>> branchFrequencyMap;
  branchFrequencyMap.resize(alphaSequence.size());

  // estimate total sequence weight (in bits)
  std::vector<size_t> alphaSequenceWeight;
  alphaSequenceWeight.resize(alphaSequence.size());
  for (size_t i = 0; i < alphaSequence.size(); i++) {
    alphaSequenceWeight[i] = alphaCharWeights.at(alphaSequence[i]);
  }

  // greedily find spans to compress with 
  std::vector<Span> spans;
  Span currentSpan((int)subsong, channel, 0, 0);
  Span nextSpan((int)subsong, channel, 0, 0);
  for (size_t i = 0; i < alphaSequence.size(); ) {
    root->find_prior(i, alphaSequence, nextSpan);
    size_t spanWeight = 0;
    size_t nextSpanEnd = nextSpan.start + nextSpan.length;
    for (size_t j = nextSpan.start; j < nextSpanEnd; j++) {
      spanWeight += alphaSequenceWeight[j];
    }
    if (compressionLevel > 0 && spanWeight > branchWeight && nextSpan.length > minSpanLength) { 
      // use prior span
      if (currentSpan.length > 0) {
        spans.emplace_back(currentSpan);
      }
      spans.emplace_back(nextSpan);
      for (size_t j = nextSpan.start; j < nextSpanEnd; j++, i++) {
        // traversing the prior span, duplicate the copy map
        size_t nextCodeAddr = copyMap[j];
        copyMap[i] = nextCodeAddr;
        if (i > 0) {
          size_t lastCodeAddr = copyMap[i - 1];
          branchFrequencyMap[lastCodeAddr][nextCodeAddr]++;
        }
      }
      currentSpan.start = i;
      currentSpan.length = 0;

    } else {
      // continue current span
      if (i > 0) {
        size_t lastCodeAddr = copyMap[i - 1];
        branchFrequencyMap[lastCodeAddr][i]++;
      }
      copyMap[i] = i;
      currentSpan.length++;
      i++;

    }
  }
  if (currentSpan.length > 0) {
    logD("last span %d, %d - copy end %d", currentSpan.start, currentSpan.length, copyMap[copyMap.size() - 1]);
    spans.emplace_back(currentSpan);
  }

  // prune all the trivial branch frequencies
  std::vector<size_t> skipMap;
  skipMap.resize(branchFrequencyMap.size(), 0);
  for (size_t i = 0; i < branchFrequencyMap.size(); i++) {
    auto &branchFrequencies = branchFrequencyMap[i];
    size_t maxFreq = 0;
    size_t skipIndex = copyMap[i + 1];
    size_t nextIndex = i + 1;
    for (auto &x: branchFrequencies) {
      if (x.first != nextIndex && x.second > maxFreq) {
        maxFreq = x.second;
        skipIndex = x.first;
      } 
    }
    skipMap[i] = skipIndex;
  }

  // no longer need suffix tree
  delete root;

  std::vector<size_t> labels;
  labels.resize(alphaSequence.size());
  size_t end = 0;
  size_t totalGoto = 0;
  for (auto &span: spans) {
    size_t spanEnd = span.start + span.length;
    bool repeatSpan = end > span.start;
    // traverse span
    for (size_t i = span.start; i < spanEnd; i++) {
      size_t leftmostCodeAddr = copyMap[end];
      if (!repeatSpan) {
        AlphaCode c = codeSequence[i];
        labels[i] = compressedCodeSequence.size();
        if (c == CODE_STOP) {
          logD("writing stop @%d %d", i, end);
          // write stop
          compressedCodeSequence.emplace_back(CODE_BRANCH_POINT);
          spanSequence.emplace_back(CODE_STOP);
          break;

        } else {
          // write regular
          logD("%d|%d write %016x at %d", end, leftmostCodeAddr, c, compressedCodeSequence.size());
          compressedCodeSequence.emplace_back(c);
        }
      } else {
        logD("%d|%d ...", end, leftmostCodeAddr);
      }
      end++;
      assert (end < copyMap.size());
      size_t nextCodeAddress = copyMap[end];
      auto& branchTable = branchFrequencyMap[leftmostCodeAddr];
      if (nextCodeAddress == leftmostCodeAddr + 1 && branchTable.size() < 2) {
        continue;
      }
      size_t skipCodeAddress = skipMap[leftmostCodeAddr];
      if (branchTable.size() < 2) {
        logD("force goto");
        totalGoto++;
      }
      if (!repeatSpan) {
        compressedCodeSequence.emplace_back(branchTable.size() < 2 ? CODE_TAKE_DATA_JUMP : CODE_BRANCH_POINT);
        compressedCodeSequence.emplace_back(CODE_JUMP(subsong, channel, skipCodeAddress));
        for (auto &x: branchTable) {
          String mods = "";
          if (x.first == skipCodeAddress) {
            mods += "*";
          }
          if (x.first == nextCodeAddress) {
            mods += "<";
          }
          if (x.first == leftmostCodeAddr + 1) {
            mods += "+";
          }
          logD("%d: -> %d (freq %d) %s", leftmostCodeAddr, x.first, x.second, mods);
        }
      }
      if (branchTable.size() > 1) {
        if (nextCodeAddress == skipCodeAddress) {
          spanSequence.emplace_back(CODE_TAKE_DATA_JUMP);
          logD("%d|%d use goto %d from %d", end-1, leftmostCodeAddr, nextCodeAddress, labels[leftmostCodeAddr] + 1);
        } else if (nextCodeAddress == leftmostCodeAddr + 1) {
          spanSequence.emplace_back(CODE_SKIP);
          logD("%d|%d use skip", end-1, leftmostCodeAddr);
        } else {
          spanSequence.emplace_back(CODE_TAKE_TRACK_JUMP);
          spanSequence.emplace_back(CODE_JUMP(subsong, channel, nextCodeAddress));
          logD("%d|%d use jump %d ", end-1, leftmostCodeAddr, nextCodeAddress);
        }
      }
    }
  }

  logD("total force gotos %d", totalGoto);
  // rewrite jump addresses
  for (size_t i = 0; i < compressedCodeSequence.size(); i++) {
    AlphaCode c = compressedCodeSequence[i];
    if (GET_CODE_TYPE(c) == CODE_TYPE::JUMP) {
      size_t address = labels[GET_CODE_JUMP_ADDRESS(c)];
      c = CODE_JUMP(subsong, channel, address);
      compressedCodeSequence[i] = c;
    }
  }
  for (size_t i = 0; i < spanSequence.size(); i++) {
    AlphaCode c = spanSequence[i];
    if (GET_CODE_TYPE(c) == CODE_TYPE::JUMP) {
      size_t address = labels[GET_CODE_JUMP_ADDRESS(c)];
      c = CODE_JUMP(subsong, channel, address);
      spanSequence[i] = c;
    } else if (GET_CODE_TYPE(c) == CODE_TYPE::WRITE_REGISTERS) {
      logD("bad code @%d", i);
      assert(false);
    }
  }

  // rewrite jumps as returns where possible
  size_t maxOffset = 0;
  size_t returnAddress = 0;
  size_t nextReadAddress = 0;
  size_t nextSpanAddress = 0;
  while (true) {
    assert(nextReadAddress < compressedCodeSequence.size());
    AlphaCode c = compressedCodeSequence[nextReadAddress++];
    if (c == CODE_TAKE_DATA_JUMP) {
      // inline jump
      c = compressedCodeSequence[nextReadAddress++];
      size_t jumpAddress = GET_CODE_JUMP_ADDRESS(c);
      returnAddress = nextReadAddress;
      if (returnAddress >= maxOffset) {
        maxOffset = returnAddress;
      }
      nextReadAddress = jumpAddress;
      continue;

    } else if (c != CODE_BRANCH_POINT) {
      continue;
    }

    assert(nextSpanAddress < spanSequence.size());
    AlphaCode s = spanSequence[nextSpanAddress++];
    if (s == CODE_STOP) {
      break;

    } else if (s == CODE_SKIP) {
      nextReadAddress++;

    } else if (s == CODE_TAKE_DATA_JUMP) {
      // decisioned inline jump
      c = compressedCodeSequence[nextReadAddress++];
      size_t jumpAddress = GET_CODE_JUMP_ADDRESS(c);
      returnAddress = nextReadAddress;
      if (returnAddress >= maxOffset) {
        maxOffset = returnAddress;
      }
      nextReadAddress = jumpAddress;

    } else  if (s == CODE_RETURN_FF) {
        nextReadAddress = maxOffset;
        nextSpanAddress++;

    } else if (s == CODE_RETURN_LAST) {
        nextReadAddress = returnAddress;
        nextSpanAddress++;

    } else if (s == CODE_TAKE_TRACK_JUMP) {
      s = spanSequence[nextSpanAddress];
      assert(GET_CODE_TYPE(s) == CODE_TYPE::JUMP);
      size_t jumpAddress = GET_CODE_JUMP_ADDRESS(s);
      if (jumpAddress == returnAddress) {
        spanSequence[nextSpanAddress-1] = CODE_RETURN_LAST;
        spanSequence[nextSpanAddress] = CODE_RETURN_NOOP;
        logD("rewriting to return last from %d to %d", nextReadAddress-1, jumpAddress);

      } else if (jumpAddress == maxOffset) {
        spanSequence[nextSpanAddress-1] = CODE_RETURN_FF;
        spanSequence[nextSpanAddress] = CODE_RETURN_NOOP;
        logD("rewriting to return front from %d to %d", nextReadAddress-1, jumpAddress);

      } else {
        returnAddress = nextReadAddress + 1;
        if (returnAddress >= maxOffset) {
          maxOffset = returnAddress;
        }
      }
      nextReadAddress = jumpAddress;
      nextSpanAddress++;

    } else {
      logD("bad code %08x", s);
      assert(false);
    }
  }
}

void DivExportTIAZip::encodeBitstreamDynamic(
  const std::vector<AlphaCode> (*codeSequences)[NUM_ZIP_CHANNELS],
  const std::vector<AlphaCode> (*compressedCodeSequences)[NUM_ZIP_CHANNELS],
  const std::vector<AlphaCode> (*spanSequences)[NUM_ZIP_CHANNELS],
  int jumpMapBits,
  int compressionLevel,
  size_t baseDataOffset,
  size_t blockSize
)
{
  size_t numSongs = e->song.subsong.size();

  // write track audio data
  SafeWriter* trackData = new SafeWriter;
  trackData->init();
  trackData->writeText("; Furnace Tracker audio data file\n");
  trackData->writeText("; TIAZip data format\n");
  trackData->writeText(fmt::sprintf("; Song: %s\n", e->song.name));
  trackData->writeText(fmt::sprintf("; Author: %s\n", e->song.author));

  trackData->writeText(fmt::sprintf("\nAUDIO_NUM_TRACKS = %d\n", numSongs));

  trackData->writeText("\n#include \"cores/tiazip_2_player_core.asm\"\n");

  // create a lookup table for use in player apps
  size_t songDataSize = 0;
  // one track table for all channels
  trackData->writeText("    MAC AUDIO_CONTROL_TABLE\n");
  trackData->writeText("AUDIO_TRACKS:\n");
  for (size_t subsong = 0; subsong < numSongs; subsong++) {
    // note reverse order for copy routine
    trackData->writeText(fmt::sprintf("    byte >(AUDIO_TRACK_S%d_C1_START - 1), <(AUDIO_TRACK_S%d_C1_START - 1)\n", subsong, subsong));
    trackData->writeText(fmt::sprintf("    byte >(AUDIO_TRACK_S%d_C0_START - 1), <(AUDIO_TRACK_S%d_C0_START - 1)\n", subsong, subsong));
    trackData->writeText(fmt::sprintf("    byte >(AUDIO_DATA_S%d_C1_START - 1), <(AUDIO_DATA_S%d_C1_START - 1)\n", subsong, subsong));
    trackData->writeText(fmt::sprintf("    byte >(AUDIO_DATA_S%d_C0_START - 1), <(AUDIO_DATA_S%d_C0_START - 1)\n", subsong, subsong));
    songDataSize += 8;
  }
  trackData->writeText("    ENDM\n");

  // frequency maps for coding
  std::map<AlphaCode, size_t> spanFrequencyMap;
  std::map<AlphaCode, size_t> abstractFrequencyMap;
  std::map<AlphaCode, size_t> controlFrequencyMap;
  std::map<AlphaCode, std::map<AlphaCode, size_t>> initialFrequencyMap;
  std::map<AlphaCode, size_t> volumeFrequencyMap;
  std::map<AlphaCode, size_t> durationFrequencyMap;
  std::map<AlphaCode, size_t> velocityFrequencyMap;
  std::map<AlphaCode, size_t> jumpFrequencyMap;
  std::map<AlphaCode, size_t> gotoFrequencyMap; 

  size_t totalCompressedCodeSequenceSize = 0;
  size_t totalSpanSequenceSize = 0;
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < NUM_ZIP_CHANNELS; channel += 1) {
      auto &compressedCodeSequence = compressedCodeSequences[subsong][channel];
      auto &spanSequence = spanSequences[subsong][channel];

      // update code frequencies
      for (size_t i = 0; i < compressedCodeSequence.size(); i++) {
        AlphaCode c = compressedCodeSequence[i];
        CODE_TYPE type = GET_CODE_TYPE(c);
        if (c == CODE_BRANCH_POINT) {
          abstractFrequencyMap[CODE_BRANCH_POINT]++;

        } else if (c == CODE_TAKE_DATA_JUMP) {
          abstractFrequencyMap[CODE_TAKE_DATA_JUMP]++;

        } else if (type == CODE_TYPE::VOL_DEC) {
          abstractFrequencyMap[CODE_VOL_DEC]++;

        } else if (type == CODE_TYPE::VOL_INC) {
          abstractFrequencyMap[CODE_VOL_INC]++;

        } else if (type == CODE_TYPE::PAUSE) {
          abstractFrequencyMap[CODE_PAUSE_0]++;
          unsigned char duration = GET_CODE_WRITE_DURATION(c);
          durationFrequencyMap[(AlphaCode)duration]++;

        } else if (type == CODE_TYPE::SUSTAIN) {
          abstractFrequencyMap[CODE_SUSTAIN_0]++;
          unsigned char duration = GET_CODE_WRITE_DURATION(c);
          durationFrequencyMap[(AlphaCode)duration]++;

        } else if (type == CODE_TYPE::VELOCITY) {
          abstractFrequencyMap[CODE_VELOCITY_0]++;
          unsigned char velocity = GET_CODE_VELOCITY(c);
          velocityFrequencyMap[(AlphaCode)velocity]++;          

        } else if (type == CODE_TYPE::WRITE_REGISTERS) {
          AlphaCode ac = GET_CODE_WRITE_REGISTERS_MASKED(c); 
          abstractFrequencyMap[ac]++;
          CHANGE_STATE cc = GET_CODE_WRITE_CC(c);
          if (cc == CHANGE_STATE::CHANGE) {
            unsigned char cx = GET_CODE_WRITE_CX(c);
            controlFrequencyMap[cx]++;
          }
          CHANGE_STATE fc = GET_CODE_WRITE_FC(c);
          if (fc == CHANGE_STATE::CHANGE) {
            unsigned char fx = GET_CODE_WRITE_FX(c);
            unsigned char cx = GET_CODE_WRITE_CX(c);
            initialFrequencyMap[cx][fx]++;
          }
          CHANGE_STATE vc = GET_CODE_WRITE_VC(c);
          if (vc == CHANGE_STATE::CHANGE) {
            unsigned char vx = GET_CODE_WRITE_VX(c);
            volumeFrequencyMap[vx]++;
          }
          unsigned char duration = GET_CODE_WRITE_DURATION(c);
          assert(duration == 1);
        } else if (type == CODE_TYPE::JUMP) {
          gotoFrequencyMap[c]++;
          jumpFrequencyMap[c]++;
        } else {
          logD("bad code %08x", c);
          assert(false);
        }
      }
      totalCompressedCodeSequenceSize += compressedCodeSequence.size();

      // update jump frequencies
      for (size_t j = 0; j < spanSequence.size(); j++) {
        AlphaCode spanCode = spanSequence[j];
        CODE_TYPE type = GET_CODE_TYPE(spanCode);
        if (type == CODE_TYPE::JUMP) {
          jumpFrequencyMap[spanCode]++;
        } else if (type != CODE_TYPE::RETURN_NOOP) {
          spanFrequencyMap[spanCode]++;
        }
      }
      totalSpanSequenceSize += spanSequence.size();
    }
  }
  logD("goto dictionary size: %d", gotoFrequencyMap.size());
  SHOW_FREQUENCIES(gotoFrequencyMap);

  // logD("byte dictionary size: %d", byteFrequencyMap.size());
  // SHOW_FREQUENCIES(byteFrequencyMap);
  logD("jump dictionary size: %d", jumpFrequencyMap.size());
  SHOW_FREQUENCIES(jumpFrequencyMap);
  std::priority_queue<std::pair<AlphaCode, size_t>, std::vector<std::pair<AlphaCode, size_t>>, CompareFrequencies> jumpHeap;
  for (auto &x:jumpFrequencyMap) {
    if (x.second == 1) {
      continue;
    }
    jumpHeap.emplace(x);
  }
  std::map<AlphaCode, size_t> jumpMap;
  while (!jumpHeap.empty()) {
    auto node = jumpHeap.top();
    jumpHeap.pop();
    logD("jump map consider: addr:%d weight:%d %d", node.first, node.second, jumpHeap.size());
    if (node.second <= 1) {
      logD("skip trivial");
      continue;
    }
    size_t index = jumpHeap.size();
    if (index >= (1 << jumpMapBits)) {
      logD("skip full");
      continue;
    }
    logD("emplace");
    jumpMap[node.first] = index;
  }
  logD("jump map size: %d", jumpMap.size());
  SHOW_FREQUENCIES(jumpMap);
  logD("duration dictionary size: %d", durationFrequencyMap.size());
  SHOW_FREQUENCIES(durationFrequencyMap);

  // encode bitstreams
  size_t maxHuffmanCodes = 128;
  size_t minWeight = 0;
  size_t maxBits = 7;


  logD("code tree");
  std::vector<CodebookEntry> abstractCodebook;
  HuffmanTree *abstractCodeTree;
  std::map<AlphaCode, std::vector<bool>> abstractCodeIndex;
  logD("abstract dictionary size: %d", abstractFrequencyMap.size());
  SHOW_FREQUENCIES(abstractFrequencyMap);
  abstractCodeTree = buildHuffmanTree(abstractFrequencyMap, maxHuffmanCodes, minWeight, maxBits, CODE_WRITE_REGISTERS_000, abstractCodebook);
  abstractCodeTree->buildIndex(abstractCodeIndex);
  SHOW_TREE(abstractFrequencyMap, abstractCodeIndex, 3, CODE_WRITE_REGISTERS_000);

  logD("span tree");
  std::vector<CodebookEntry> spanCodebook;
  HuffmanTree *spanTree;
  std::map<AlphaCode, std::vector<bool>> spanCodeIndex;
  logD("span dictionary size: %d", spanFrequencyMap.size());
  SHOW_FREQUENCIES(spanFrequencyMap);
  spanTree = buildHuffmanTree(spanFrequencyMap, maxHuffmanCodes, minWeight, maxBits, 0, spanCodebook);
  spanTree->buildIndex(spanCodeIndex);
  SHOW_TREE(spanFrequencyMap, spanCodeIndex, 3, 0);

  logD("control tree");
  std::vector<CodebookEntry> controlCodebook;
  HuffmanTree *controlTree = buildHuffmanTree(controlFrequencyMap, maxHuffmanCodes, minWeight, maxBits, 0, controlCodebook);
  std::map<AlphaCode, std::vector<bool>> controlCodeIndex;
  controlTree->buildIndex(controlCodeIndex);
  SHOW_TREE(controlFrequencyMap, controlCodeIndex, 4, 0);

  // merge frequencies  
  logD("merging frequency trees");
  std::map<AlphaCode, AlphaCode> controlCodeMergeMap = {
    {0, 0},
    {1, 0},
    {2, 0},
    {3, 0},
    {4, 1},
    {5, 0},
    {6, 0},
    {7, 0},
    {8, 1},
    {9, 0},
    {10, 0},
    {11, 0},
    {12, 1},
    {13, 0},
    {14, 0},
    {15, 0},
  };
  std::map<AlphaCode, std::map<AlphaCode, size_t>> mergedFrequencyMap;
  for (auto &x : initialFrequencyMap) {
    AlphaCode mergeFrequencyCode = controlCodeMergeMap[x.first];
    for (auto &f: initialFrequencyMap[x.first]) {
      mergedFrequencyMap[mergeFrequencyCode][f.first] += f.second;
    }
    logD("merged frequency tree %d audcx %d", mergeFrequencyCode, x.first);
  }
  // std::priority_queue<std::pair<AlphaCode, size_t>, std::vector<std::pair<AlphaCode, size_t>>, CompareFrequencies> frequencyHeap;
  // // find out most important cx value
  // for (auto &x: initialFrequencyMap) {
  //   size_t weight = 0;
  //   for (auto &f: x.second) {
  //     weight += f.second;
  //   }
  //   logD("pushing frequency audcx %d weight %d", x.first, weight);
  //   frequencyHeap.emplace(std::pair<AlphaCode, size_t>(x.first, weight));
  // }
  // std::map<AlphaCode, std::map<AlphaCode, size_t>> mergedFrequencyMap;
  // AlphaCode mergedFrequencyCode = 0;
  // size_t totalWeight = 0;
  // while (!frequencyHeap.empty()) {
  //   auto x = frequencyHeap.top();
  //   frequencyHeap.pop();
  //   const size_t nextWeight = x.second * 5;
  //   if (compressionLevel > 1 && totalWeight > 0 && (totalWeight + nextWeight) > 3000) {
  //     mergedFrequencyCode++;
  //     totalWeight = 0;
  //   }
  //   totalWeight += nextWeight;
  //   controlCodeMergeMap[x.first] = mergedFrequencyCode;
  //   for (auto &f: initialFrequencyMap[x.first]) {
  //     mergedFrequencyMap[mergedFrequencyCode][f.first] += f.second;
  //   }
  //   logD("merged frequency tree %d audcx %d weight %d", mergedFrequencyCode, x.first, totalWeight);
  // }

  std::map<AlphaCode, std::vector<CodebookEntry>> mergedFrequencyCodebooks;
  std::map<AlphaCode, HuffmanTree *> mergedFrequencyTrees;
  std::map<AlphaCode, std::map<AlphaCode, std::vector<bool>>> mergedFrequencyCodeIndexes;
  for (auto &x: mergedFrequencyMap) {
    logD("merged frequency tree %d", x.first);
    mergedFrequencyTrees[x.first] = buildHuffmanTree(x.second, maxHuffmanCodes, minWeight, maxBits, 0, mergedFrequencyCodebooks[x.first]);
    mergedFrequencyTrees[x.first]->buildIndex(mergedFrequencyCodeIndexes[x.first]);
    SHOW_TREE(x.second, mergedFrequencyCodeIndexes[x.first], 9, 0);
  }

  logD("volume tree");
  std::vector<CodebookEntry> volumeCodebook;
  HuffmanTree *volumeTree = buildHuffmanTree(volumeFrequencyMap, maxHuffmanCodes, minWeight, maxBits, 0, volumeCodebook);
  std::map<AlphaCode, std::vector<bool>> volumeCodeIndex;
  volumeTree->buildIndex(volumeCodeIndex);
  SHOW_TREE(volumeFrequencyMap, volumeCodeIndex, 4, 0);

  logD("duration tree");
  std::vector<CodebookEntry> durationCodebook;
  HuffmanTree *durationTree = buildHuffmanTree(durationFrequencyMap, maxHuffmanCodes, minWeight, maxBits, 0, durationCodebook);
  std::map<AlphaCode, std::vector<bool>> durationCodeIndex;
  durationTree->buildIndex(durationCodeIndex);
  SHOW_TREE(durationFrequencyMap, durationCodeIndex, 4, 0);

  logD("velocity tree");
  std::vector<CodebookEntry> velocityCodebook;
  HuffmanTree *velocityTree = buildHuffmanTree(velocityFrequencyMap, maxHuffmanCodes, minWeight, maxBits, 0, velocityCodebook);
  std::map<AlphaCode, std::vector<bool>> velocityCodeIndex;
  if (NULL != velocityTree) {
    velocityTree->buildIndex(velocityCodeIndex);
  }
  SHOW_TREE(velocityFrequencyMap, velocityCodeIndex, 4, 0);

  const int addressBits = 15;
  const int addressIndexBits = jumpMapBits;

  // produce bitstreams
  size_t streamDataOffset = (baseDataOffset << 3);
  Bitstream *dataStreams[e->song.subsong.size()][NUM_ZIP_CHANNELS];
  Bitstream *trackStreams[e->song.subsong.size()][NUM_ZIP_CHANNELS];
  std::vector<size_t> jumpAddresses;
  jumpAddresses.resize(jumpMap.size());
  std::map<CODE_TYPE, size_t> codeTypeBitsMap;
  std::map<CODE_TYPE, size_t> codeTypeCountMap;
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < NUM_ZIP_CHANNELS; channel += 1) {
      // produce data stream
      logD("encoding data stream for %d %d", subsong, channel);
      auto &compressedCodeSequence = compressedCodeSequences[subsong][channel];

      std::vector<size_t> positionMap;
      positionMap.resize(compressedCodeSequence.size());
      std::map<size_t, size_t> dataStreamPointerMap;

      Bitstream *dataStream = new Bitstream(blockSize);
      dataStreams[subsong][channel] = dataStream;

      for (size_t i = 0; i < compressedCodeSequence.size(); i++) {
        AlphaCode c = compressedCodeSequence[i];
        size_t startPosition = dataStream->position();
        size_t streamPosition = startPosition + streamDataOffset;
        positionMap[i] = streamPosition;
        CODE_TYPE type = GET_CODE_TYPE(c);
        switch (type) {
          case CODE_TYPE::BRANCH_POINT: {
            logD("DATA %d %d %08x - BRANCH_POINT", subsong, channel, BITSTREAM_ADDR(dataStream->position()));
            dataStream->writeBits(abstractCodeIndex.at(CODE_BRANCH_POINT));
            break;
          }

          case CODE_TYPE::TAKE_DATA_JUMP: {
            logD("DATA %d %d %08x - TAKE_DATA_JUMP", subsong, channel, BITSTREAM_ADDR(dataStream->position()));
            dataStream->writeBits(abstractCodeIndex.at(CODE_TAKE_DATA_JUMP));
            break;
          }

          case CODE_TYPE::WRITE_REGISTERS: {
            AlphaCode ac = GET_CODE_WRITE_REGISTERS_MASKED(c); 
            logD("DATA %d %d %08x - WRITE_REGISTERS", subsong, channel, BITSTREAM_ADDR(dataStream->position()));
            dataStream->writeBits(abstractCodeIndex.at(ac));
            CHANGE_STATE cc = GET_CODE_WRITE_CC(c);
            if (cc == CHANGE_STATE::CHANGE) {
              unsigned char cx = GET_CODE_WRITE_CX(c);
              logD("DATA %d %d %08x - CX %d", subsong, channel, BITSTREAM_ADDR(dataStream->position()), cx);
              dataStream->writeBits(controlCodeIndex.at(cx));
            }
            CHANGE_STATE fc = GET_CODE_WRITE_FC(c);
            if (fc == CHANGE_STATE::CHANGE) {
              unsigned char fx = GET_CODE_WRITE_FX(c);
              unsigned char cx = GET_CODE_WRITE_CX(c);
              logD("DATA %d %d %08x - FX %d", subsong, channel, BITSTREAM_ADDR(dataStream->position()), fx);
              // dataStream->writeBits(frequencyCodeIndex.at(fx));
              AlphaCode mfc = controlCodeMergeMap.at(cx);
              dataStream->writeBits(mergedFrequencyCodeIndexes[mfc].at(fx));
            }
            CHANGE_STATE vc = GET_CODE_WRITE_VC(c);
            if (vc == CHANGE_STATE::CHANGE) {
              unsigned char vx = GET_CODE_WRITE_VX(c);
              logD("DATA %d %d %08x - VX %d", subsong, channel, BITSTREAM_ADDR(dataStream->position()), vx);
              dataStream->writeBits(volumeCodeIndex.at(vx));
            }
            // duration always 1
            // unsigned char duration = GET_CODE_WRITE_DURATION(c);
            // dataStream->writeBits(durationCodeIndex.at(duration));
            break;
          }

          case CODE_TYPE::VOL_INC: {
            logD("DATA %d %d %08x - VOL_INC", subsong, channel, BITSTREAM_ADDR(dataStream->position()));
            dataStream->writeBits(abstractCodeIndex.at(CODE_VOL_INC));
            break;
          }

          case CODE_TYPE::VOL_DEC: {
            logD("DATA %d %d %08x - VOL_DEC", subsong, channel, BITSTREAM_ADDR(dataStream->position()));
            dataStream->writeBits(abstractCodeIndex.at(CODE_VOL_DEC));
            break;
          }

          case CODE_TYPE::PAUSE: {
            unsigned char duration = GET_CODE_WRITE_DURATION(c);
            logD("DATA %d %d %08x - PAUSE %d", subsong, channel, BITSTREAM_ADDR(dataStream->position()), duration);
            dataStream->writeBits(abstractCodeIndex.at(CODE_PAUSE_0));
            dataStream->writeBits(durationCodeIndex.at(duration));
            break;
          }

          case CODE_TYPE::SUSTAIN: {
            unsigned char duration = GET_CODE_WRITE_DURATION(c);
            logD("DATA %d %d %08x - SUSTAIN %d", subsong, channel, BITSTREAM_ADDR(dataStream->position()), duration);
            dataStream->writeBits(abstractCodeIndex.at(CODE_SUSTAIN_0));
            dataStream->writeBits(durationCodeIndex.at(duration));
            break;
          }

          case CODE_TYPE::VELOCITY: {
            unsigned char velocity = GET_CODE_VELOCITY(c);
            logD("DATA %d %d %08x - VELOCITY %d", subsong, channel, BITSTREAM_ADDR(dataStream->position()), velocity);
            assert(abstractCodeIndex.find(CODE_VELOCITY_0) != abstractCodeIndex.end());
            assert(velocityCodeIndex.find(velocity) != velocityCodeIndex.end());
            dataStream->writeBits(abstractCodeIndex.at(CODE_VELOCITY_0));
            dataStream->writeBits(velocityCodeIndex.at(velocity));
            break;
          }

          case CODE_TYPE::JUMP: {
            size_t address = GET_CODE_JUMP_ADDRESS(c);
            auto ij = jumpMap.find(c);
            if (ij != jumpMap.end()) {
              size_t index = (*ij).second;
              logD("DATA %d %d %08x - JUMP TABLE %d", subsong, channel, BITSTREAM_ADDR(dataStream->position()), index);
              dataStream->writeBit(false); // is lookup
              dataStream->writeBits(index, addressIndexBits);
            } else {
              logD("DATA %d %d %08x - JUMP ADDRESS %08x", subsong, channel, BITSTREAM_ADDR(dataStream->position()), address);
              dataStream->writeBit(true); // no lookup
              dataStreamPointerMap[dataStream->position()] = address;
              dataStream->writeBits(address, addressBits);
            }
            break;
          }

          default:
            logD("bad code %08x", c);
            assert(false);
        }
        codeTypeCountMap[type]++;
        codeTypeBitsMap[type] += dataStream->position() - startPosition;

      }

      for (auto& x : dataStreamPointerMap) {
        dataStream->seek(x.first);
        size_t address = positionMap[x.second];
        dataStream->writeBits(address, addressBits);
        unsigned long bits = msb(address);
        logD("DATA %d %d - REMAP JUMP ADDRESS@%08x: %08x -> %08x (%08x is %d bits)", subsong, channel, BITSTREAM_ADDR(x.first), x.second, BITSTREAM_ADDR(address), address, bits);
      }

      // produce track stream
      logD("encoding track stream for %d %d", subsong, channel);
      auto &spanSequence = spanSequences[subsong][channel];
      std::map<size_t, size_t> trackStreamPointerMap;
      Bitstream *trackStream = new Bitstream(blockSize);
      trackStreams[subsong][channel] = trackStream;
      for (size_t i = 0; i < spanSequence.size(); i++) {
        AlphaCode s = spanSequence[i];
        CODE_TYPE type = GET_CODE_TYPE(s);
        size_t startPosition = trackStream->position();
        if (s == CODE_STOP) {
          logD("SPAN %d %d %08x - STOP", subsong, channel, BITSTREAM_ADDR(trackStream->position()));
          trackStream->writeBits(spanCodeIndex.at(CODE_STOP));

        } else if (s == CODE_RETURN_LAST) {
          logD("SPAN %d %d %08x - RETURN_LAST", subsong, channel, BITSTREAM_ADDR(trackStream->position()));
          trackStream->writeBits(spanCodeIndex.at(CODE_RETURN_LAST));          

        } else if (s == CODE_RETURN_FF) {
          logD("SPAN %d %d %08x - RETURN_FF", subsong, channel, BITSTREAM_ADDR(trackStream->position()));
          trackStream->writeBits(spanCodeIndex.at(CODE_RETURN_FF));
        
        } else if (s == CODE_RETURN_NOOP) {
          // pass

        } else if (s == CODE_SKIP) {
          logD("SPAN %d %d %08x - SKIP", subsong, channel, BITSTREAM_ADDR(trackStream->position()));
          trackStream->writeBits(spanCodeIndex.at(CODE_SKIP));

        } else if (s == CODE_TAKE_DATA_JUMP) {
          logD("SPAN %d %d %08x - DATA_JUMP", subsong, channel, BITSTREAM_ADDR(trackStream->position()));
          trackStream->writeBits(spanCodeIndex.at(CODE_TAKE_DATA_JUMP));

        } else if (s == CODE_TAKE_TRACK_JUMP) {
          logD("SPAN %d %d %08x - TRACK_JUMP", subsong, channel, BITSTREAM_ADDR(trackStream->position()));
          trackStream->writeBits(spanCodeIndex.at(CODE_TAKE_TRACK_JUMP));
          i++;
          s = spanSequence[i];
          auto ij = jumpMap.find(s);
          if (ij != jumpMap.end()) {
            size_t index = (*ij).second;
            logD("SPAN %d %d %08x - JUMP TABLE %08x", subsong, channel, BITSTREAM_ADDR(trackStream->position()), index);
            trackStream->writeBit(false); // is lookup
            trackStream->writeBits(index, addressIndexBits);

          } else {
            size_t address = GET_CODE_JUMP_ADDRESS(s);
            logD("SPAN %d %d %08x - JUMP ADDRESS %08x", subsong, channel, BITSTREAM_ADDR(trackStream->position()), address);
            trackStream->writeBit(true); // no lookup
            trackStreamPointerMap[trackStream->position()] = address;
            trackStream->writeBits(address, addressBits);

          }
        } else {
          logD("bad code %08x", s);
          assert(false);

        }
        codeTypeCountMap[type]++;
        codeTypeBitsMap[type] += trackStream->position() - startPosition;

      }

      for (auto& x : trackStreamPointerMap) {
        trackStream->seek(x.first);
        size_t address = positionMap[x.second];
        trackStream->writeBits(address, addressBits);
        long bits = log2l(address);
        logD("TRACK %d %d - REMAP JUMP ADDRESS@%08x: %08x -> %08x (%d bits)", subsong, channel, BITSTREAM_ADDR(x.first), x.second, BITSTREAM_ADDR(address), bits);
      }

      for (auto& x : jumpMap) {
        if (subsong != GET_CODE_SUBSONG(x.first)) {
          continue;
        }
        if (channel != GET_CODE_CHANNEL(x.first)) {
          continue;
        }
        size_t address = GET_CODE_JUMP_ADDRESS(x.first);
        jumpAddresses[x.second] = positionMap[address];
      }

      streamDataOffset += (dataStream->bytesUsed() << 3);

    }
  }


  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < NUM_ZIP_CHANNELS; channel += 1) {
      logD("DATA %d %d size: %d", subsong, channel, dataStreams[subsong][channel]->size());
      logD("TRACK %d %d size: %d", subsong, channel, trackStreams[subsong][channel]->size());
    }
  }

  // validate bitstream
  streamDataOffset = (baseDataOffset << 3);
  std::map<AlphaCode, size_t> jumpDistanceMap;
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < NUM_ZIP_CHANNELS; channel += 1) {
      auto &codeSequence = codeSequences[subsong][channel];
      auto &dataStream = dataStreams[subsong][channel];
      dataStream->seek(0);
      auto &trackStream = trackStreams[subsong][channel];
      trackStream->seek(0);
      size_t i = 0;
      size_t returnAddress = 0;
      size_t maxOffset = 0;
      unsigned char lastCx = 0;
      AlphaCode lastCommand = 0;
      while (dataStream->hasBits()) {
        size_t streamPosition = dataStream->position();
        AlphaCode code;
        AlphaCode nextCommand = abstractCodeTree->decode(dataStream);
        if (lastCommand == CODE_BRANCH_POINT && nextCommand == CODE_BRANCH_POINT) {
          logD("SPAN: double branch point at %08x", streamPosition);
        }
        switch (nextCommand) {
          case CODE_WRITE_REGISTERS_111: {
            CHANGE_STATE cc = CHANGE_STATE::CHANGE;
            unsigned char cx = controlTree->decode(dataStream);

            CHANGE_STATE fc = CHANGE_STATE::CHANGE;
            // unsigned char fx = frequencyTree->decode(dataStream);
            AlphaCode mfc = controlCodeMergeMap.at(cx);
            unsigned char fx = mergedFrequencyTrees[mfc]->decode(dataStream);

            CHANGE_STATE vc = CHANGE_STATE::CHANGE;
            unsigned char vx = volumeTree->decode(dataStream);

            code = CODE_WRITE_REGISTERS(cc, cx, fc, fx, vc, vx, 1);
            lastCx = cx;
            break;
          }

          case CODE_WRITE_REGISTERS_110: {
            CHANGE_STATE cc = CHANGE_STATE::CHANGE;
            unsigned char cx = controlTree->decode(dataStream);

            CHANGE_STATE fc = CHANGE_STATE::CHANGE;
            // unsigned char fx = frequencyTree->decode(dataStream);
            AlphaCode mfc = controlCodeMergeMap.at(cx);
            unsigned char fx = mergedFrequencyTrees[mfc]->decode(dataStream);

            code = CODE_WRITE_REGISTERS(cc, cx, fc, fx, CHANGE_STATE::NOOP, 0, 1);
            lastCx = cx;
            break;
          }

          case CODE_WRITE_REGISTERS_101: {
            CHANGE_STATE cc = CHANGE_STATE::CHANGE;
            unsigned char cx = controlTree->decode(dataStream);

            CHANGE_STATE vc = CHANGE_STATE::CHANGE;
            unsigned char vx = volumeTree->decode(dataStream);

            code = CODE_WRITE_REGISTERS(cc, cx, CHANGE_STATE::NOOP, 0, vc, vx, 1);
            lastCx = cx;
            break;
          }


          case CODE_WRITE_REGISTERS_100: {
            CHANGE_STATE cc = CHANGE_STATE::CHANGE;
            unsigned char cx = controlTree->decode(dataStream);

            code = CODE_WRITE_REGISTERS(cc, cx, CHANGE_STATE::NOOP, 0, CHANGE_STATE::NOOP, 0, 1);
            lastCx = cx;
            break;
          }

          case CODE_WRITE_REGISTERS_011: {

            CHANGE_STATE fc = CHANGE_STATE::CHANGE;
            // unsigned char fx = frequencyTree->decode(dataStream);
            AlphaCode mfc = controlCodeMergeMap.at(lastCx);
            unsigned char fx = mergedFrequencyTrees[mfc]->decode(dataStream);
            
            CHANGE_STATE vc = CHANGE_STATE::CHANGE;
            unsigned char vx = volumeTree->decode(dataStream);
  
            code = CODE_WRITE_REGISTERS(CHANGE_STATE::NOOP, lastCx, fc, fx, vc, vx, 1);
            break;
          }

          case CODE_WRITE_REGISTERS_001: {

            CHANGE_STATE vc = CHANGE_STATE::CHANGE;
            unsigned char vx = volumeTree->decode(dataStream);

            code = CODE_WRITE_REGISTERS(CHANGE_STATE::NOOP, lastCx, CHANGE_STATE::NOOP, 0, vc, vx, 1);
            break;
          }

          case CODE_WRITE_REGISTERS_010: {

            CHANGE_STATE fc = CHANGE_STATE::CHANGE;
            // unsigned char fx = frequencyTree->decode(dataStream);
            AlphaCode mfc = controlCodeMergeMap.at(lastCx);
            unsigned char fx = mergedFrequencyTrees[mfc]->decode(dataStream);

            code = CODE_WRITE_REGISTERS(CHANGE_STATE::NOOP, lastCx, fc, fx, CHANGE_STATE::NOOP, 0, 1);
            break;
          }

          case CODE_TAKE_DATA_JUMP: {
            // get address
            size_t nextAddress;
            bool isAddress = dataStream->readBit();
            if (isAddress) {
              nextAddress = dataStream->readBits(addressBits);
            } else {
              size_t index = dataStream->readBits(addressIndexBits);
              nextAddress = jumpAddresses[index];
            }
            nextAddress -= streamDataOffset;
            long distance = ((long) nextAddress) - streamPosition;
            jumpDistanceMap[distance > 0 ? distance : (((uint64_t)1) << 56) | -distance]++;
            returnAddress = dataStream->position();
            if (maxOffset < returnAddress) {
              maxOffset = returnAddress;
            }
            dataStream->seek(nextAddress);
            continue;
          }

          case CODE_VOL_INC: {
            code = CODE_VOL_INC;
            break;
          }


          case CODE_VOL_DEC: {
            code = CODE_VOL_DEC;
            break;
          }

          case CODE_PAUSE_0: {
            AlphaCode dx = durationTree->decode(dataStream);
            code = CODE_PAUSE((unsigned char) dx & 0x0f);
            break;
          }

          case CODE_SUSTAIN_0: {
            AlphaCode dx = durationTree->decode(dataStream);
            code = CODE_SUSTAIN((unsigned char) dx & 0x0f);
            break;
          }

          case CODE_VELOCITY_0: {
            AlphaCode dvx = velocityTree->decode(dataStream);
            code = CODE_VELOCITY((char) dvx);
            break;
          }

          case CODE_BRANCH_POINT: {

            // jump and seek
            size_t trackPosition = trackStream->position();
            AlphaCode sx = spanTree->decode(trackStream);
            size_t nextAddress;

            if (sx == CODE_STOP) {
              code = CODE_STOP;
              break;

            } else if (sx == CODE_RETURN_LAST) {
              dataStream->seek(returnAddress);
              continue;

            } else if (sx == CODE_RETURN_FF) {
              dataStream->seek(maxOffset);
              continue;
            
            } else if (sx == CODE_RETURN_NOOP) {
              assert(false);
              continue;
              
            } else if (sx == CODE_SKIP) {
              // skip next datastream address
              bool isAddress = dataStream->readBit();
              dataStream->readBits(isAddress ? addressBits : addressIndexBits);
              continue;

            } else if (sx == CODE_TAKE_DATA_JUMP ) {
              bool isAddress = dataStream->readBit();
              if (isAddress) {
                nextAddress = dataStream->readBits(addressBits);
              } else {
                size_t index = dataStream->readBits(addressIndexBits);
                nextAddress = jumpAddresses[index];
              }

            } else if (sx == CODE_TAKE_TRACK_JUMP) {
              bool isAddress = trackStream->readBit();
              if (isAddress) {
                nextAddress = trackStream->readBits(addressBits);
              } else {
                size_t index = trackStream->readBits(addressIndexBits);
                nextAddress = jumpAddresses[index];
              }
              // skip datastream pointer
              isAddress = dataStream->readBit();
              dataStream->readBits(isAddress ? addressBits : addressIndexBits);

            } else {
              // should not happen
              assert(false);
            }

            nextAddress -= streamDataOffset;
            long distance = ((long) nextAddress) - streamPosition;
            jumpDistanceMap[distance > 0 ? distance : (((uint64_t)1) << 56) | -distance]++;
            returnAddress = dataStream->position();
            if (maxOffset < returnAddress) {
              maxOffset = returnAddress;
            }
            dataStream->seek(nextAddress);
            continue;
          }
          
          default:
            assert(false);
        }
        AlphaCode codeToCompare = codeSequence[i++];
        if (code != codeToCompare) {
          logD("%d/%d: (%d/%d) [%d, %d] %016x ?= %016x", i, codeSequence.size(), streamPosition, dataStream->size(), returnAddress, maxOffset, code, codeToCompare);
        }
        assert(code == codeToCompare);
      }
      logD("ss %d ch %d is valid at %d/%d %d", subsong, channel, i, codeSequence.size(), dataStream->position());
      assert(i == codeSequence.size());
      assert(!dataStream->hasBits());
      assert(!trackStream->hasBits());

      streamDataOffset += (dataStream->bytesUsed() << 3);
    }
  }

  logD("code type bits");
  for (auto &x: codeTypeBitsMap) {
    const double bits = x.second;
    const double count = codeTypeCountMap.at(x.first);
    const double avg = bits / count;
    logD("CODE TYPE %d = %lf (%lf / %lf)", (int)x.first, avg, bits, count);
  }

  logD("jump distance map");
  SHOW_FREQUENCIES(jumpDistanceMap);

  size_t totalCompressedBytes = 0;

  // write the data streams
  trackData->writeText("\nAUDIO_DATA_OFFSET");
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < NUM_ZIP_CHANNELS; channel += 1) {
      trackData->writeText(fmt::sprintf("\nAUDIO_DATA_S%d_C%d_START", subsong, channel));
      Bitstream *dataStream = dataStreams[subsong][channel];
      dataStream->seek(0);
      size_t mod = 0;
      size_t bytesWritten = 0;
      while (dataStream->hasBits()) {
        unsigned char uc = dataStream->readByte();
        if (mod == 0) {
          trackData->writeText(fmt::sprintf("\n    byte $%02x", uc));
        } else {
          trackData->writeText(fmt::sprintf(", $%02x", uc));
        }
        mod = (mod + 1) % 16;
        bytesWritten++;
      }
      trackData->writeText(fmt::sprintf("\n; AUDIO_DATA_S%d_C%d bytes: %d\n", subsong, channel, bytesWritten));
      totalCompressedBytes += bytesWritten;
      delete dataStream;
    }
  }

  // write the track streams
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < NUM_ZIP_CHANNELS; channel += 1) {
      trackData->writeText(fmt::sprintf("\nAUDIO_TRACK_S%d_C%d_START", subsong, channel));
      Bitstream *trackStream = trackStreams[subsong][channel];
      trackStream->seek(0);
      size_t mod = 0;
      size_t bytesWritten = 0;
      while (trackStream->hasBits()) {
        unsigned char uc = trackStream->readByte();
        if (mod == 0) {
          trackData->writeText(fmt::sprintf("\n    byte $%02x", uc));
        } else {
          trackData->writeText(fmt::sprintf(", $%02x", uc));
        }
        mod = (mod + 1) % 16;
        bytesWritten++;
      }
      trackData->writeText(fmt::sprintf("\n; AUDIO_TRACK_S%d_C%d bytes: %d\n", subsong, channel, bytesWritten));
      totalCompressedBytes += bytesWritten;
      delete trackStream;
    }
  }

  // write the jump table
  // 0hhhhlll lllllsss stored as 0ssshhhh llllllll
  trackData->writeText(fmt::sprintf("\nAUDIO_JUMP_TABLE_LO_START"));
  for (auto addr : jumpAddresses) {
      trackData->writeText(fmt::sprintf("\n    byte $%02x ; <%02x", (addr >> 3) & 0xff, addr));
      totalCompressedBytes += 1;
  }
  trackData->writeText(fmt::sprintf("\nAUDIO_JUMP_TABLE_HI_START"));
  for (auto addr : jumpAddresses) {
      trackData->writeText(fmt::sprintf("\n    byte $%02x ; >%02x", ((addr & 0x07) << 4) | ((addr >> 11) & 0x0f), addr));
      totalCompressedBytes += 1;
  }

  // write control and decoder tables
  trackData->writeText(fmt::sprintf("\nCODEBOOK_LADDER"));
  totalCompressedBytes += writeCodebookLadder(
    trackData, 
    "audio_decode_command",
    abstractCodebook,
    abstractCodeIndex
  );
  totalCompressedBytes += writeCodebookLadder(
    trackData,
    "audio_decode_span",
    spanCodebook,
    spanCodeIndex
  );
  totalCompressedBytes += writeCodebookLadder(trackData, "audio_decode_control", controlCodebook, controlCodeIndex);
  // totalCompressedBytes += writeCodebookLadder(trackData, "audio_decode_frequency", frequencyCodebook, frequencyCodeIndex);
  for (auto &x : mergedFrequencyCodebooks) {
    totalCompressedBytes += writeCodebookLadder(
      trackData,
      fmt::sprintf("audio_decode_control_%d_frequency", x.first).c_str(),
      x.second,
      mergedFrequencyCodeIndexes[x.first]
    );
  }
  totalCompressedBytes += writeCodebookLadder(trackData, "audio_decode_volume", volumeCodebook, volumeCodeIndex);
  totalCompressedBytes += writeCodebookLadder(trackData, "audio_decode_duration", durationCodebook, durationCodeIndex);
  totalCompressedBytes += writeCodebookLadder(trackData, "audio_decode_duration", velocityCodebook, velocityCodeIndex);

  // codes
  trackData->writeText(fmt::sprintf("\nCODEBOOK_CODES"));
  totalCompressedBytes += writeCommandCodes(
    trackData,
    "audio_decode_command",
    abstractCodebook, 
    abstractCodeIndex
  );
  totalCompressedBytes += writeCommandCodes(
    trackData,
    "audio_decode_span",
    spanCodebook,
    spanCodeIndex
  );
  totalCompressedBytes += writeDataCodes(trackData, "audio_decode_control", controlCodebook, controlCodeIndex);
  // totalCompressedBytes += writeDataCodes(trackData, "audio_decode_frequency", frequencyCodebook, frequencyCodeIndex);
  for (auto &x : mergedFrequencyCodebooks) {
    totalCompressedBytes += writeDataCodes(
      trackData,
      fmt::sprintf("audio_decode_control_%d_frequency", x.first).c_str(),
      x.second,
      mergedFrequencyCodeIndexes[x.first]
    );
  }
  totalCompressedBytes += writeDataCodes(trackData, "audio_decode_volume", volumeCodebook, volumeCodeIndex);
  totalCompressedBytes += writeDataCodes(trackData, "audio_decode_duration", durationCodebook, durationCodeIndex);
  totalCompressedBytes += writeDataCodes(trackData, "audio_decode_velocity", velocityCodebook, velocityCodeIndex);

  // macros
  writeCodebookMacro(
    trackData, 
    "audio_decode_command",
    "audio_data_stream_idx", 
    abstractCodebook
  );
  if (spanCodebook.size() == 1) {
    // BUGBUG: massive kludge
    trackData->writeText("\n    ; audio_decode_span\n");
    trackData->writeText("    MAC audio_decode_span_%d_MACRO\n");
    trackData->writeText("    lda #<CODE_STOP\n");
    trackData->writeText("    ENDM\n\n");
  } else {
    writeCodebookMacro(
      trackData,
      "audio_decode_span",
      "audio_span_stream_idx",
      spanCodebook);
  }
  writeCodebookMacro(trackData, "audio_decode_control", "audio_data_stream_idx", controlCodebook);
  // writeCodebookMacro(trackData, "audio_decode_frequency", "audio_data_stream_idx", frequencyCodebook);
  for (auto &x : mergedFrequencyCodebooks) {
    writeCodebookMacro(
      trackData,
      fmt::sprintf("audio_decode_control_%d_frequency", x.first).c_str(),
      "audio_data_stream_idx", 
      x.second
    );
  };
  writeCodebookMacro(trackData, "audio_decode_volume", "audio_data_stream_idx", volumeCodebook);
  writeCodebookMacro(trackData, "audio_decode_duration", "audio_data_stream_idx", durationCodebook);
  writeCodebookMacro(trackData, "audio_decode_velocity", "audio_data_stream_idx", velocityCodebook);

  // cleanup
  delete abstractCodeTree;
  delete spanTree;
  delete controlTree;
  for (auto &x: mergedFrequencyTrees) {
    delete x.second;
  }
  delete volumeTree;
  delete durationTree;
  if (NULL != velocityTree) {
    velocityTree;
  }

  trackData->writeText(fmt::sprintf("\n\n; Song data size: %d\n", songDataSize));
  trackData->writeText(fmt::sprintf("; Compressed Code Sequence Length: %d\n", totalCompressedCodeSequenceSize));
  trackData->writeText(fmt::sprintf("; Span Sequence Length: %d\n", totalSpanSequenceSize));
  trackData->writeText(fmt::sprintf("; Compressed Bytes %d\n", totalCompressedBytes));

  output.push_back(DivROMExportOutput("Track_data.asm", trackData));

}

size_t DivExportTIAZip::writeCodebookLengths(
  SafeWriter *w,
  const char *label,
  const std::vector<CodebookEntry> &codebook
) {
  size_t bytesWritten = 0;
  w->writeText(fmt::sprintf("\n%s_LENGTHS = . - CODEBOOK_LENGTHS - 1", label));
  size_t currentLength = 1;
  size_t total = 0;
  for (auto &entry : codebook) {
    if (entry.height == 0) {
      continue;
    }
    while (entry.height > currentLength) {
      w->writeText(fmt::sprintf("\n    byte %d", total));
      bytesWritten += 1;
      currentLength += 1;
      total = 0;
    }
    total += 1;
  }
  if (total > 0) {
    w->writeText(fmt::sprintf("\n    byte %d", total));
    bytesWritten +=1;
  }
  return bytesWritten;
}

size_t DivExportTIAZip::writeCodebookLadder(
  SafeWriter *w,
  const char *label,
  const std::vector<CodebookEntry> &codebook,
  const std::map<AlphaCode, std::vector<bool>> &codeIndex
) {
  size_t bytesWritten = 0;
  w->writeText(fmt::sprintf("\n%s_LADDER = . - CODEBOOK_LADDER", label));
  for (auto &entry : codebook) {
    if (entry.height == 0) {
      continue;
    }
    AlphaCode code = entry.code;
    String bitcode = "1";
    auto it = codeIndex.find(code);
    if (it != codeIndex.end()) {
      auto &bitvec = (*it).second;  
      for (int i = bitvec.size(); --i >= 0; ) {
        bitcode += bitvec.at(i) ? "1" : "0";
      }
    }
    w->writeText(fmt::sprintf("\n    byte %%%s", bitcode));
    bytesWritten += 1;
    if (entry.height == 7) {
      // only take the first entry at 7 bits
      break;
    }
  }
  return bytesWritten;
}

size_t DivExportTIAZip::writeCommandCodes(
  SafeWriter *w,
  const char *label,
  const std::vector<CodebookEntry> &codebook,
  const std::map<AlphaCode, std::vector<bool>> &codeIndex
) {
  size_t bytesWritten = 0;  
  w->writeText(fmt::sprintf("\n%s_CODES = . - CODEBOOK_CODES", label));
  for (auto &entry : codebook) {
    if (entry.height == 0) {
      continue;
    }
    AlphaCode code = entry.code;
    CODE_TYPE type = GET_CODE_TYPE(code);
    String bitcode = "";
    auto it = codeIndex.find(code);
    if (it != codeIndex.end()) {
      auto &bitvec = (*it).second;  
      for (int i = bitvec.size(); --i >= 0; ) {
        bitcode += bitvec.at(i) ? "1" : "0";
      }
    }
    if (code == CODE_BRANCH_POINT) {
      w->writeText(fmt::sprintf("\n    byte <CODE_BRANCH_POINT; %s", bitcode));
    } else if (code == CODE_TAKE_DATA_JUMP) {
      w->writeText(fmt::sprintf("\n    byte <CODE_TAKE_DATA_JUMP; %s", bitcode));
    } else if (code == CODE_TAKE_TRACK_JUMP) {
      w->writeText(fmt::sprintf("\n    byte <CODE_TAKE_TRACK_JUMP; %s", bitcode));
    } else if (code == CODE_STOP) {
      w->writeText(fmt::sprintf("\n    byte <CODE_STOP; %s", bitcode));
    } else if (code == CODE_RETURN_LAST) {
      w->writeText(fmt::sprintf("\n    byte <CODE_RETURN_LAST; %s", bitcode));
    } else if (code == CODE_RETURN_FF) {
      w->writeText(fmt::sprintf("\n    byte <CODE_RETURN_FF; %s", bitcode));        
    } else if (code == CODE_RETURN_NOOP) {
      w->writeText(fmt::sprintf("\n    byte <CODE_RETURN_NOOP; %s", bitcode));
    } else if (code == CODE_SKIP) {
      w->writeText(fmt::sprintf("\n    byte <CODE_SKIP; %s", bitcode));
    } else if (type == CODE_TYPE::JUMP) {
      w->writeText(fmt::sprintf("\n    byte <CODE_JUMP; %s", bitcode));
    } else if (type == CODE_TYPE::VOL_DEC) {
      w->writeText(fmt::sprintf("\n    byte <CODE_VOL_DEC; %s", bitcode));
    } else if (type == CODE_TYPE::VOL_INC) {
      w->writeText(fmt::sprintf("\n    byte <CODE_VOL_INC; %s", bitcode));
    } else if (type == CODE_TYPE::PAUSE) {
      w->writeText(fmt::sprintf("\n    byte <CODE_PAUSE; %s", bitcode));
    } else if (type == CODE_TYPE::SUSTAIN) {
      w->writeText(fmt::sprintf("\n    byte <CODE_SUSTAIN; %s", bitcode));
    } else if (type == CODE_TYPE::VELOCITY) {
      w->writeText(fmt::sprintf("\n    byte <CODE_VELOCITY; %s", bitcode));
    } else if (type == CODE_TYPE::WRITE_REGISTERS) {
      if (code == CODE_WRITE_REGISTERS_001) {
        w->writeText(fmt::sprintf("\n    byte <CODE_WRITE_REGISTERS_001; %s", bitcode));
      } else if (code == CODE_WRITE_REGISTERS_010) {
        w->writeText(fmt::sprintf("\n    byte <CODE_WRITE_REGISTERS_010; %s", bitcode));
      } else if (code == CODE_WRITE_REGISTERS_011) {
        w->writeText(fmt::sprintf("\n    byte <CODE_WRITE_REGISTERS_011; %s", bitcode));
      } else if (code == CODE_WRITE_REGISTERS_100) {
        w->writeText(fmt::sprintf("\n    byte <CODE_WRITE_REGISTERS_100; %s", bitcode));
      } else if (code == CODE_WRITE_REGISTERS_101) {
        w->writeText(fmt::sprintf("\n    byte <CODE_WRITE_REGISTERS_101; %s", bitcode));
      } else if (code == CODE_WRITE_REGISTERS_110) {
        w->writeText(fmt::sprintf("\n    byte <CODE_WRITE_REGISTERS_110; %s", bitcode));
      } else if (code == CODE_WRITE_REGISTERS_111) {
        w->writeText(fmt::sprintf("\n    byte <CODE_WRITE_REGISTERS_111; %s", bitcode));
      } else {
        assert(false);
      }
    } else {
      assert(false);
    }
    bytesWritten +=1;
  }
  return bytesWritten;
}

size_t DivExportTIAZip::writeDataCodes(
  SafeWriter *w,
  const char *label,
  const std::vector<CodebookEntry> &codebook,
  const std::map<AlphaCode, std::vector<bool>> &codeIndex
) {
  size_t bytesWritten = 0;  
  w->writeText(fmt::sprintf("\n%s_CODES = . - CODEBOOK_CODES", label));
  for (auto &entry : codebook) {
    if (entry.height == 0) {
      continue;
    }
    AlphaCode code = entry.code;
    String bitcode = "";
    auto it = codeIndex.find(code);
    if (it != codeIndex.end()) {
      auto &bitvec = (*it).second;  
      for (int i = bitvec.size(); --i >= 0; ) {
        bitcode += bitvec.at(i) ? "1" : "0";
      }
    }
    assert(code < 256);
    w->writeText(fmt::sprintf("\n    byte %d ; %s", code, bitcode));
    bytesWritten +=1;
  }
  return bytesWritten;
}


void DivExportTIAZip::writeCodebookMacro(
  SafeWriter *w,
  const char *label,
  const char *track,
  const std::vector<CodebookEntry> &codebook
) {
  w->writeText(fmt::sprintf("\n    ; %s\n", label));
  w->writeText(fmt::sprintf("\n    MAC %s_MACRO\n", label));
  if (codebook.size() == 1) {
    AlphaCode code = codebook.at(0).code;
    w->writeText(fmt::sprintf("    lda #%d\n", code));

  } else {
    w->writeText(fmt::sprintf("    ldx %s\n", track));
    w->writeText(fmt::sprintf("    ldy #%s_CODES\n", label));
    w->writeText("    jsr audio_stream_read_symbol\n");
  }
  w->writeText("\n    ENDM\n\n");
}

void DivExportTIAZip::validateCodeSequence(
  size_t subsong,
  int channel,
  const std::vector<AlphaCode> &codeSequence,
  const std::vector<AlphaCode> &compressedCodeSequence,
  const std::vector<AlphaCode> &spanSequence
) {
  // Test compression correctness 
  auto it = spanSequence.begin();
  size_t nextReadAddress = 0, compareAddress = 0;
  size_t maxOffset = 0;
  size_t returnAddress = 0;
  while (true) {
    AlphaCode c = compressedCodeSequence[nextReadAddress];
    CODE_TYPE codeType = GET_CODE_TYPE(c);
    if (c == CODE_TAKE_DATA_JUMP) {
        nextReadAddress++;
        AlphaCode c = compressedCodeSequence[nextReadAddress];
        size_t jumpAddress = GET_CODE_JUMP_ADDRESS(c);
        assert(CODE_TYPE::JUMP == GET_CODE_TYPE(c));
        if (jumpAddress >= maxOffset) {
          logD("missed force goto back to front");
        }
        if (jumpAddress == returnAddress) {
          logD("missed force goto back to last");
        }
        returnAddress = nextReadAddress + 1;
        if (returnAddress >= maxOffset) {
          maxOffset = returnAddress;
        }
        nextReadAddress = jumpAddress;
        continue;

    } else if (codeType == CODE_TYPE::BRANCH_POINT) {
      nextReadAddress++;
      assert(it != spanSequence.end());
      AlphaCode s = *it++;
      if (s == CODE_STOP) {
        AlphaCode x = codeSequence[compareAddress];
        if (x != CODE_STOP) {
          logD("%d %d | %d: no stop found at %d: %016x", subsong, channel, nextReadAddress, compareAddress, x);
          assert(false);
        }
        assert(it == spanSequence.end());
        compareAddress++;
        break;
        
      } else if (s == CODE_SKIP) {
        // skip 1 in data stream 
        nextReadAddress++;
        
      } else if (s == CODE_TAKE_DATA_JUMP) {
        AlphaCode c = compressedCodeSequence[nextReadAddress];
        size_t jumpAddress = GET_CODE_JUMP_ADDRESS(c);
        assert(CODE_TYPE::JUMP == GET_CODE_TYPE(c));
        if (jumpAddress >= maxOffset) {
          logD("missed goto back to front");
        }
        if (jumpAddress == returnAddress) {
          logD("missed goto back to last");
        }
        returnAddress = nextReadAddress + 1;
        if (returnAddress >= maxOffset) {
          maxOffset = returnAddress;
        }
        logD("goto %d", jumpAddress);
        nextReadAddress = jumpAddress;

      } else if (s == CODE_RETURN_FF) {
        logD("return to front %d", maxOffset);
        nextReadAddress = maxOffset;
        it++;

      } else if (s == CODE_RETURN_LAST) {
        logD("return to last %d", returnAddress);
        nextReadAddress = returnAddress;
        it++;

      } else if (s == CODE_TAKE_TRACK_JUMP) {
        s = *it++;
        size_t jumpAddress = GET_CODE_JUMP_ADDRESS(s);
        if (jumpAddress >= maxOffset) {
          logD("missed jump back to front");
        }
        if (jumpAddress == returnAddress) {
          logD("missed jump back to last");
        }
        returnAddress = nextReadAddress + 1;
        if (returnAddress >= maxOffset) {
          maxOffset = returnAddress;
        }
        logD("jump to %d", jumpAddress);
        nextReadAddress = jumpAddress;

      } else {
        assert(false);
      }
    } else {
      AlphaCode x = codeSequence[compareAddress];
      if (c != x) {
        logD("%d %d | %d: %08x    %08x",subsong, channel, nextReadAddress, compressedCodeSequence[nextReadAddress-1], codeSequence[compareAddress-1]);
        logD("%d %d | %d: %08x <> %08x (%d)",subsong, channel, nextReadAddress, c, x, compareAddress);
        logD("%d %d | %d: %08x    %08x",subsong, channel, nextReadAddress+1, compressedCodeSequence[nextReadAddress+1], codeSequence[compareAddress+1]);
        assert(false);
      }
      nextReadAddress++;
      compareAddress++;
    }
  }
    
  logD("valid at %d/%d", compareAddress, codeSequence.size());
  assert(compareAddress == codeSequence.size());
}

size_t DivExportTIAZip::encodeChannelStateCodes(
  const ChannelState& next,
  const char duration,
  const ChannelState& last,
  const int maxSustain,
  const int velocity,
  std::vector<AlphaCode> &out)
{
  // when duration is zero... some kind of rounding issue has happened upstream... we force to 1...
  if (duration == 0) {
      logD("0 duration note");
  }
  int framecount = duration > 0 ? duration : 1;

  unsigned char audcx = next.registers[0];
  unsigned char audfx = next.registers[1];
  unsigned char audvx = next.registers[2];
  //unsigned char predVx = CLAMP(last.registers[2] + velocity, 0, 15);
  CHANGE_STATE cc = audcx != last.registers[0] ? CHANGE_STATE::CHANGE : CHANGE_STATE::NOOP;
  CHANGE_STATE fc = audfx != last.registers[1] ? CHANGE_STATE::CHANGE : CHANGE_STATE::NOOP;
  CHANGE_STATE vc = audvx != last.registers[2] ? CHANGE_STATE::CHANGE : CHANGE_STATE::NOOP;
  //predVx != audvx ? CHANGE_STATE::CHANGE : CHANGE_STATE::NOOP;
  //char vxd = audvx - last.registers[2];
  // if (audvx == last.registers[2] + 1) {
  //   audvx = 0x10;
  // } else if (last.registers[2] == audvx + 1) {
  //   audvx = 0xf0;
  // }

  // BUGBUG: this is important, a sustain is likely to come after a node
  // maybe not a pause
  unsigned char dx = 1; // framecount > 2 ? 2 : framecount;

  // BUGBUG: this is also important, seldom make control changes by themselves
  if (cc > 0) {
    // fc = CHANGE_STATE::CHANGE;
    fc = vc = CHANGE_STATE::CHANGE;
  };
  // if (fc > 0) {
  //   vc = CHANGE_STATE::CHANGE;
  // }

  size_t codesWritten = 0;
  // BUGBUG: PAUSE problematic 
  // if (audvx == 0) {
  //   assert(dx > 0);
  //   // BUGBUG: PAUSE CAN BE LONGER?
  //   out.emplace_back(CODE_PAUSE(dx - 1));
  //   codesWritten++;
  // } else 
  if (cc + fc + vc> 0) {
    out.emplace_back(CODE_WRITE_REGISTERS(
      cc,
      audcx,
      fc,
      fc == CHANGE_STATE::NOOP ? 0 : audfx,
      vc,
      vc == CHANGE_STATE::NOOP ? 0 : audvx,
      dx
    ));
    codesWritten++;
  }
  //  else if (vc > 0) {
  //   if (vxd == -1) {
  //     out.emplace_back(CODE_VOL_DEC);
  //   } else if (vxd == 1) {
  //     out.emplace_back(CODE_VOL_INC);
  //   } else {
  //     out.emplace_back(CODE_WRITE_REGISTERS(cc, audcx, fc, 0, vc, audvx, dx));
  //   }
  //   codesWritten++;
  // }
  if (codesWritten > 0) {
    framecount = framecount - dx;
  }

  while (framecount > 0) {
    unsigned char dx = framecount > maxSustain ? maxSustain : framecount;
    framecount = framecount - dx;
    out.emplace_back(CODE_SUSTAIN(dx - 1));
    codesWritten++;
  } 

  return codesWritten;
}

void DivExportTIAZip::writeWaveformHeader(SafeWriter* w, const char * key) {
  w->writeText(fmt::sprintf("%s_ADDR\n", key));
}

