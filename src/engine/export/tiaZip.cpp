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
//        - try separate channel code compression (INCONCLUSIVE)
//        - try separate effects tracks (NO)
//        - try separate instrument encodings  (YES)
//        - instrumate ADSR to handle volume? (YES)
//        - properly analytic span compression analysis? (FAKED IT)
//        - try to optimize frequency tables
//          - consider if no table makes sense for some 
//          - exhaustive clustering combinations?
//        - try to optimize jump encoding
//          - can any gotos be eliminated?
//          - are there branch points where the skip is more / less likely
//        - try to optimize volume
//        - consider more adaptive coding
//        - consider arithmetic encoding somehow?
//    - compression nits
//        - double jumps exist
//        - 0 distance jumps could be skip except for return
//  - debugging
//    - proper analytic debug output for TIAZIP spans
//  - glitch
//    - two track jumps in one frame goes over
//      - try saving returns at start of branch then doing skips on return...?
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
  // BUGBUG: NEED?
  // delete exportThread;

  for (auto registerDump : registerDumps) {
    delete registerDump;
  }

  delete dataCommandCodeTree;
  delete trackCommandTree;
  delete controlTree;
  for (auto &x: mergedFrequencyTrees) {
    delete x.second;
  }
  delete volumeTree;
  delete durationTree;
  if (NULL != velocityTree) {
    velocityTree;
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
  compressionLevel = conf.getInt("compressionLevel", 1);
  minSpanLength = conf.getInt("minSpanLength", 3);
  maxSustain = conf.getInt("maxSustain", 16);
  jumpMapBits = conf.getInt("jumpMapBits", 5);
  returnFF = conf.getBool("returnFF", true);
  changeControlPredict = conf.getBool("changeControlPredict", true);
  changeFrequencyPredict = conf.getBool("changeFrequencyPredict", false);
  branchPointerOptimization = conf.getBool("bpo", false);
  branchWeight = conf.getInt("branchWeight", 13);

  baseDataOffset = 0xF100;
  blockSize = (0x10000 - baseDataOffset) * 8;
  addressBits = 15;
  addressIndexBits = jumpMapBits;

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

AlphaCode CODE_JUMP(size_t subsong, int channel, size_t targetIndex) {
  return ((AlphaCode)JUMP << 56) | 
         ((AlphaCode)subsong << 48) |
         ((AlphaCode)channel << 40) |
         targetIndex;
}

size_t GET_CODE_JUMP_INDEX(const AlphaCode c) {
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

size_t GET_ADDRESS_COMPONENTS(size_t addr) {
  return ((addr << 1) & 0xfff0) | (addr & 0x7);
}

size_t BITSTREAM_2_ADDRESS(size_t address) {
  return (address - 1) & 0x7fff;
}

size_t ADDRESS_2_BITSTREAM(size_t address) {
  return (address + 1) & 0x7fff;
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
void SHOW_CODEBOOK(
  const std::vector<CodebookEntry> &codebook,
  const std::map<AlphaCode, std::vector<bool>> &codeIndex,
  const int unencodedBits,
  AlphaCode defaultCode
) {
  size_t totalBits = 0;
  size_t totalUnencodedBits = 0;
  size_t currentLength = 0;
  size_t codeTableEntryCount = 0;
  size_t firstValueTableEntryCount = 0;
  size_t lengthTableEntryCount = 0;
  size_t totalWeight = 0;
  for (auto &entry : codebook) {
    if (entry.height == 0) {
      continue;
    }
    auto it = codeIndex.find(entry.code);
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
    if (currentLength == 0) {
      currentLength = entry.height;
      lengthTableEntryCount++;
      firstValueTableEntryCount++;
    } else if (currentLength < entry.height) {
      firstValueTableEntryCount++;
      lengthTableEntryCount++;
      currentLength++;
      if (currentLength < entry.height) {
        firstValueTableEntryCount++;
        lengthTableEntryCount++;
        currentLength = entry.height;
      }
    }
    codeTableEntryCount++;
    totalBits += entry.weight * entry.height;
    totalUnencodedBits += entry.weight * unencodedBits;
    totalWeight += entry.weight;
    logD("  %08x -> %d (%s) %d", entry.code, entry.weight, huffmanCode, bitvec.size());
  }
  if (currentLength == 6) {
    lengthTableEntryCount++;
    firstValueTableEntryCount++;
  }
  size_t tableEntryCount = codeTableEntryCount + firstValueTableEntryCount + lengthTableEntryCount;
  size_t totalTotalBits = totalBits + tableEntryCount * 8;
  double deltaBytes = ((double) totalUnencodedBits - (double)totalTotalBits) / 8.0;
  logD(" totalWeight: %d, totalBits: %d, unencodedBits: %d, tableEntries: %d, totalTotalBits: %d, delta: %lf", totalWeight, totalBits, totalUnencodedBits, tableEntryCount, totalTotalBits, deltaBytes);
}

// compacted encoding
void DivExportTIAZip::writeTrackDataTIAZip(int compressionLevel, int minSpanLength, int maxSustain, int jumpMapBits) {

  // prepare
  codeSequences.resize(e->song.subsong.size() * NUM_ZIP_CHANNELS);

  // encode command streams
  size_t totalUncompressedSequenceSize = 0;
  std::map<AlphaCode, size_t> frequencyMap;
  for (size_t subsong = 0; subsong < registerDumps.size(); subsong++) {
    auto registerDump = registerDumps[subsong];

    for (int channel = 0; channel < NUM_ZIP_CHANNELS; channel++) {
      auto &codeSequence = codeSequences[subsong * NUM_ZIP_CHANNELS + channel];

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
      //   auto &adsrSequence = adsrSequences[subsong * NUM_ZIP_CHANNELS + channel];
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
        int velocity = (compressionLevel < 2 || n.duration > 1) ? 0 : (int) n.state.registers[2] - (int) lastChannelState.registers[2];
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
        if (effectiveVelocity != 0 && effectiveVelocity >= -1 && effectiveVelocity <= 1) {
          logD("%d %d ACTIVATING EFFECT BIT", subsong, channel);
          currentState.registers[2] = 0x80 | (effectiveVelocity & 0x0f);
        }
        encodeChannelStateCodes(currentState, n.duration, lastState, effectiveVelocity, codeSequence);
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

  // debugging: compute basic stats
  // statistics
  logD("total codes : %d ", frequencyMap.size());
  CALC_ENTROPY(frequencyMap);

  // create compressed code sequence
  compressedCodeSequences.resize(e->song.subsong.size() * NUM_ZIP_CHANNELS);
  trackSequences.resize(e->song.subsong.size() * NUM_ZIP_CHANNELS);
  trackPositionMaps.resize(e->song.subsong.size() * NUM_ZIP_CHANNELS);
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < NUM_ZIP_CHANNELS; channel += 1) {
      auto &codeSequence = codeSequences[subsong * NUM_ZIP_CHANNELS + channel];
      auto &compressedCodeSequence = compressedCodeSequences[subsong * NUM_ZIP_CHANNELS + channel];
      auto &trackSequence = trackSequences[subsong * NUM_ZIP_CHANNELS + channel];
      auto &trackPositionMap = trackPositionMaps[subsong * NUM_ZIP_CHANNELS + channel];

      compressCodeSequence(
        subsong, 
        channel,
        alphabet,
        index,
        alphaCharWeights,
        branchWeight,
        codeSequence,
        compressedCodeSequence,
        trackSequence,
        trackPositionMap
      );

      validateCodeSequence(
        subsong, 
        channel,
        codeSequence,
        compressedCodeSequence,
        trackSequence
      );

      // if (compressionLevel == 2) {
      //   auto &effectSequence = codeSequences[subsong][channel + 2];
      //   auto &compressedEffectSequence = compressedCodeSequences[subsong][channel + 2];
      //   auto &spanEffectSequence = trackSequences[subsong][channel + 2];
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
      //     trackSequence.emplace_back(n);
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
  //     for (auto c : compressedCodeSequences[subsong * NUM_ZIP_CHANNELS + channel]) {
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
  //     for (auto c : trackSequences[subsong * NUM_ZIP_CHANNELS + channel]) {
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

  assembleBitstreams();
  validateBitstreams();
  writeBitstreams();

}

void DivExportTIAZip::compressCodeSequence(
  size_t subsong,
  int channel,
  const std::vector<AlphaCode> &alphabet,
  const std::map<AlphaCode, AlphaChar> &index,
  const std::map<AlphaChar, size_t> &alphaCharWeights,
  size_t branchWeight,
  const std::vector<AlphaCode>&codeSequence,
  std::vector<AlphaCode> &compressedCodeSequence,
  std::vector<AlphaCode> &trackSequence,
  std::map<size_t, size_t> &trackPositionMap
) {

  trackSequence.reserve(codeSequence.size());
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
      size_t leftmostCodeIndex = copyMap[end];
      if (!repeatSpan) {
        AlphaCode c = codeSequence[i];
        labels[i] = compressedCodeSequence.size();
        if (c == CODE_STOP) {
          logD("writing stop @%d %d", i, end);
          // write stop
          compressedCodeSequence.emplace_back(CODE_BRANCH_POINT);
          trackSequence.emplace_back(CODE_STOP);
          break;

        } else {
          // write regular
          logD("%d|%d write %016x at %d", end, leftmostCodeIndex, c, compressedCodeSequence.size());
          compressedCodeSequence.emplace_back(c);
        }
      } else {
        logD("%d|%d ...", end, leftmostCodeIndex);
      }
      end++;
      assert (end < copyMap.size());
      size_t nextCodeIndex = copyMap[end];
      auto& branchTable = branchFrequencyMap[leftmostCodeIndex];
      if (nextCodeIndex == leftmostCodeIndex + 1 && branchTable.size() < 2) {
        continue;
      }
      size_t skipCodeIndex = skipMap[leftmostCodeIndex];
      if (branchTable.size() < 2) {
        logD("force goto");
        totalGoto++;
      }
      if (!repeatSpan) {
        compressedCodeSequence.emplace_back(branchTable.size() < 2 ? CODE_TAKE_DATA_JUMP : CODE_BRANCH_POINT);
        compressedCodeSequence.emplace_back(CODE_JUMP(subsong, channel, skipCodeIndex));
        for (auto &x: branchTable) {
          String mods = "";
          if (x.first == skipCodeIndex) {
            mods += "*";
          }
          if (x.first == nextCodeIndex) {
            mods += "<";
          }
          if (x.first == leftmostCodeIndex + 1) {
            mods += "+";
          }
          logD("%d: -> %d (freq %d) %s", leftmostCodeIndex, x.first, x.second, mods);
        }
      }
      if (branchTable.size() > 1) {
        if (nextCodeIndex == skipCodeIndex) {
          trackSequence.emplace_back(CODE_TAKE_DATA_JUMP);
          logD("%d|%d use goto %d from %d", end-1, leftmostCodeIndex, nextCodeIndex, labels[leftmostCodeIndex] + 1);
        } else if (nextCodeIndex == leftmostCodeIndex + 1) {
          trackSequence.emplace_back(CODE_SKIP);
          logD("%d|%d use skip", end-1, leftmostCodeIndex);
        } else {
          trackSequence.emplace_back(CODE_TAKE_TRACK_JUMP);
          trackSequence.emplace_back(CODE_JUMP(subsong, channel, nextCodeIndex));
          logD("%d|%d use jump %d ", end-1, leftmostCodeIndex, nextCodeIndex);
        }
      }
    }
  }

  logD("total force gotos %d", totalGoto);
  // rewrite jump addresses
  for (size_t i = 0; i < compressedCodeSequence.size(); i++) {
    AlphaCode c = compressedCodeSequence[i];
    if (GET_CODE_TYPE(c) == CODE_TYPE::JUMP) {
      size_t index = labels[GET_CODE_JUMP_INDEX(c)];
      c = CODE_JUMP(subsong, channel, index);
      compressedCodeSequence[i] = c;
    }
  }
  for (size_t i = 0; i < trackSequence.size(); i++) {
    AlphaCode c = trackSequence[i];
    if (GET_CODE_TYPE(c) == CODE_TYPE::JUMP) {
      size_t index = labels[GET_CODE_JUMP_INDEX(c)];
      c = CODE_JUMP(subsong, channel, index);
      trackSequence[i] = c;
    } else if (GET_CODE_TYPE(c) == CODE_TYPE::WRITE_REGISTERS) {
      logD("bad code @%d", i);
      assert(false);
    }
  }

  // rewrite jumps as returns where possible
  size_t maxIndex= 0;
  size_t returnIndex = 0;
  size_t nextReadIndex = 0;
  size_t nextSpanIndex = 0;
  while (true) {
    assert(nextReadIndex < compressedCodeSequence.size());
    AlphaCode c = compressedCodeSequence[nextReadIndex++];
    if (c == CODE_TAKE_DATA_JUMP) {
      // inline jump
      c = compressedCodeSequence[nextReadIndex++];
      size_t jumpIndex = GET_CODE_JUMP_INDEX(c);
      returnIndex = nextReadIndex;
      if (returnIndex >= maxIndex) {
        maxIndex = returnIndex;
      }
      nextReadIndex = jumpIndex;
      continue;

    } else if (c != CODE_BRANCH_POINT) {
      continue;
    }

    assert(nextSpanIndex < trackSequence.size());
    AlphaCode s = trackSequence[nextSpanIndex++];
    if (s == CODE_STOP) {
      break;

    } else if (s == CODE_SKIP) {
      nextReadIndex++;

    } else if (s == CODE_TAKE_DATA_JUMP) {
      // decisioned inline jump
      c = compressedCodeSequence[nextReadIndex++];
      size_t jumpIndex = GET_CODE_JUMP_INDEX(c);
      returnIndex = nextReadIndex;
      if (returnIndex >= maxIndex) {
        maxIndex = returnIndex;
      }
      nextReadIndex = jumpIndex;

    } else  if (s == CODE_RETURN_FF) {
        nextReadIndex = maxIndex;
        nextSpanIndex++;

    } else if (s == CODE_RETURN_LAST) {
        nextReadIndex = returnIndex;
        nextSpanIndex++;

    } else if (s == CODE_TAKE_TRACK_JUMP) {
      s = trackSequence[nextSpanIndex];
      assert(GET_CODE_TYPE(s) == CODE_TYPE::JUMP);
      size_t jumpIndex = GET_CODE_JUMP_INDEX(s);
      if (jumpIndex == returnIndex) {
        trackSequence[nextSpanIndex-1] = CODE_RETURN_LAST;
        trackSequence[nextSpanIndex] = CODE_RETURN_NOOP;
        logD("rewriting to return last from %d to %d", nextReadIndex-1, returnIndex);

      } else if (returnFF && jumpIndex == maxIndex) {
        trackSequence[nextSpanIndex-1] = CODE_RETURN_FF;
        trackSequence[nextSpanIndex] = CODE_RETURN_NOOP;
        logD("rewriting to return front from %d to %d", nextReadIndex-1, maxIndex);

      } else {
        trackPositionMap[nextSpanIndex] = nextReadIndex; // track jumps do not advance data stream
        returnIndex = nextReadIndex + 1;
        if (returnIndex >= maxIndex) {
          maxIndex = returnIndex;
        }
      }
      nextReadIndex = jumpIndex;
      nextSpanIndex++;

    } else {
      logD("bad code %08x", s);
      assert(false);
    }
  }
}

void DivExportTIAZip::assembleBitstreams()
{

  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < NUM_ZIP_CHANNELS; channel += 1) {
      auto &compressedCodeSequence = compressedCodeSequences[subsong * NUM_ZIP_CHANNELS + channel];
      auto &trackSequence = trackSequences[subsong * NUM_ZIP_CHANNELS + channel];

      // update code frequencies
      for (size_t i = 0; i < compressedCodeSequence.size(); i++) {
        AlphaCode c = compressedCodeSequence[i];
        CODE_TYPE type = GET_CODE_TYPE(c);
        if (c == CODE_BRANCH_POINT) {
          dataCommandFrequencyMap[CODE_BRANCH_POINT]++;

        } else if (c == CODE_TAKE_DATA_JUMP) {
          dataCommandFrequencyMap[CODE_TAKE_DATA_JUMP]++;

        } else if (type == CODE_TYPE::VOL_DEC) {
          dataCommandFrequencyMap[CODE_VOL_DEC]++;

        } else if (type == CODE_TYPE::VOL_INC) {
          dataCommandFrequencyMap[CODE_VOL_INC]++;

        } else if (type == CODE_TYPE::PAUSE) {
          dataCommandFrequencyMap[CODE_PAUSE_0]++;
          unsigned char duration = GET_CODE_WRITE_DURATION(c);
          durationFrequencyMap[(AlphaCode)duration]++;

        } else if (type == CODE_TYPE::SUSTAIN) {
          dataCommandFrequencyMap[CODE_SUSTAIN_0]++;
          unsigned char duration = GET_CODE_WRITE_DURATION(c);
          durationFrequencyMap[(AlphaCode)duration]++;

        } else if (type == CODE_TYPE::VELOCITY) {
          dataCommandFrequencyMap[CODE_VELOCITY_0]++;
          unsigned char velocity = GET_CODE_VELOCITY(c);
          velocityFrequencyMap[(AlphaCode)velocity]++;          

        } else if (type == CODE_TYPE::WRITE_REGISTERS) {
          AlphaCode ac = GET_CODE_WRITE_REGISTERS_MASKED(c); 
          dataCommandFrequencyMap[ac]++;
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

      // update jump frequencies
      for (size_t j = 0; j < trackSequence.size(); j++) {
        AlphaCode trackCommandCode = trackSequence[j];
        CODE_TYPE type = GET_CODE_TYPE(trackCommandCode);
        if (type == CODE_TYPE::JUMP) {
          jumpFrequencyMap[trackCommandCode]++;
        } else if (type != CODE_TYPE::RETURN_NOOP) {
          trackCommandFrequencyMap[trackCommandCode]++;
        }
      }
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
  logD("data stream command dictionary size: %d", dataCommandFrequencyMap.size());
  SHOW_FREQUENCIES(dataCommandFrequencyMap);
  dataCommandCodeTree = buildHuffmanTree(dataCommandFrequencyMap, maxHuffmanCodes, minWeight, maxBits, CODE_WRITE_REGISTERS_000, dataCommandCodebook);
  dataCommandCodeTree->buildIndex(dataCommandCodes);
  SHOW_CODEBOOK(dataCommandCodebook, dataCommandCodes, 3, CODE_WRITE_REGISTERS_000);

  logD("span tree");
  logD("span dictionary size: %d", trackCommandFrequencyMap.size());
  SHOW_FREQUENCIES(trackCommandFrequencyMap);
  trackCommandTree = buildHuffmanTree(trackCommandFrequencyMap, maxHuffmanCodes, minWeight, maxBits, 0, trackCommandCodebook);
  trackCommandTree->buildIndex(trackCommandCodes);
  SHOW_CODEBOOK(trackCommandCodebook, trackCommandCodes, 3, 0);

  logD("control tree");
  controlTree = buildHuffmanTree(controlFrequencyMap, maxHuffmanCodes, minWeight, maxBits, 0, controlCodebook);
  controlTree->buildIndex(controlCodes);
  SHOW_CODEBOOK(controlCodebook, controlCodes, 4, 0);

  logD("jump type tree");
  if (branchPointerOptimization) {
    jumpTypeFrequencyMap =  {
      {JUMP_POINTER_TYPE::SHORT, 200},
      {JUMP_POINTER_TYPE::INDEX, 100},
      {JUMP_POINTER_TYPE::LONG, 50}
    };
  } else {
    jumpTypeFrequencyMap = {
      {JUMP_POINTER_TYPE::INDEX, 100},
      {JUMP_POINTER_TYPE::LONG, 100}
    };
  }
  logD("jump type dictionary size: %d", jumpTypeFrequencyMap.size());
  SHOW_FREQUENCIES(jumpTypeFrequencyMap);
  jumpTypeCodeTree = buildHuffmanTree(jumpTypeFrequencyMap, maxHuffmanCodes, minWeight, maxBits, 0, jumpTypeCodebook);
  jumpTypeCodeTree->buildIndex(jumpTypeCodes);
  SHOW_CODEBOOK(jumpTypeCodebook, jumpTypeCodes, 2, 0);

  logD("merging frequency trees");
  computeMergedFrequenciesDefault();
  //computeMergedFrequenciesDynamic();
  // build Huffman trees
  for (auto &x: mergedFrequencyMap) {
    logD("merged frequency tree %d", x.first);
    mergedFrequencyTrees[x.first] = buildHuffmanTree(x.second, maxHuffmanCodes, minWeight, maxBits, 0, mergedFrequencyCodebooks[x.first]);
    mergedFrequencyTrees[x.first]->buildIndex(mergedFrequencyCodes[x.first]);
    SHOW_CODEBOOK(mergedFrequencyCodebooks[x.first], mergedFrequencyCodes[x.first], 5, 0);
  }

  logD("volume tree");
  volumeTree = buildHuffmanTree(volumeFrequencyMap, maxHuffmanCodes, minWeight, maxBits, 0, volumeCodebook);
  volumeTree->buildIndex(volumeCodes);
  SHOW_CODEBOOK(volumeCodebook, volumeCodes, 4, 0);

  logD("duration tree");
  durationTree = buildHuffmanTree(durationFrequencyMap, maxHuffmanCodes, minWeight, maxBits, 0, durationCodebook);
  durationTree->buildIndex(durationCodes);
  SHOW_CODEBOOK(durationCodebook, durationCodes, 4, 0);

  logD("velocity tree");
  velocityTree = buildHuffmanTree(velocityFrequencyMap, maxHuffmanCodes, minWeight, maxBits, 0, velocityCodebook);
  if (NULL != velocityTree) {
    velocityTree->buildIndex(velocityCodes);
  }
  SHOW_CODEBOOK(velocityCodebook, velocityCodes, 4, 0);

  std::map<JUMP_POINTER_TYPE, size_t> jumpTypeFrequencies;

  // assemble bitstreams
  size_t streamDataOffset = (baseDataOffset << 3);
  dataStreams.resize(e->song.subsong.size() * NUM_ZIP_CHANNELS);
  trackStreams.resize(e->song.subsong.size() * NUM_ZIP_CHANNELS);
  jumpTableAddresses.resize(jumpMap.size());

  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < NUM_ZIP_CHANNELS; channel += 1) {
      // produce data stream
      logD("encoding data stream for %d %d", subsong, channel);
      auto &compressedCodeSequence = compressedCodeSequences[subsong * NUM_ZIP_CHANNELS + channel];
      auto &trackPositionMap = trackPositionMaps[subsong * NUM_ZIP_CHANNELS + channel];
    
      // initial jump assignments
      std::vector<JUMP_POINTER_TYPE> jumpTypeAssignments;
      jumpTypeAssignments.resize(compressedCodeSequence.size());
      for (size_t i = 0; i < compressedCodeSequence.size(); i++) {
        AlphaCode code = compressedCodeSequence.at(i);
        if (GET_CODE_TYPE(code) != CODE_TYPE::JUMP) {
          continue;
        }
        JUMP_POINTER_TYPE jumpType = branchPointerOptimization ?
          JUMP_POINTER_TYPE::SHORT : JUMP_POINTER_TYPE::LONG;
        if (jumpMap.find(code) != jumpMap.end()) {
          jumpType = JUMP_POINTER_TYPE::INDEX;
        }
        logD("JUMP INIT %d = %d", i, (int)jumpType);
        jumpTypeAssignments[i] = jumpType;    
      }

      std::vector<size_t> positionMap;
      Bitstream *dataStream = NULL;
      while (dataStream == NULL) {
        std::vector<size_t> tooBigJumps;
        dataStream = assembleDatastream(
          compressedCodeSequence,
          jumpTypeAssignments,
          jumpMap,
          streamDataOffset,
          positionMap,
          tooBigJumps
        );
        if (tooBigJumps.size() > 0) {
          logD("found %d too big jumps, will try again", tooBigJumps.size());
          delete dataStream;
          dataStream = NULL;
          for (auto x : tooBigJumps) {
            logD("replacing jump at %d", x);
            jumpTypeAssignments[x] = JUMP_POINTER_TYPE::LONG;
          }
        }
      }
        
      dataStreams[subsong * NUM_ZIP_CHANNELS + channel] = dataStream;

      for (size_t i = 0; i < compressedCodeSequence.size(); i++) {
        AlphaCode code = compressedCodeSequence.at(i);
        if (GET_CODE_TYPE(code) != CODE_TYPE::JUMP) {
          continue;
        }
        jumpTypeFrequencies[jumpTypeAssignments[i]]++;
      }

      // produce track stream
      logD("encoding track stream for %d %d", subsong, channel);
      auto &trackSequence = trackSequences[subsong * NUM_ZIP_CHANNELS + channel];
      Bitstream *trackStream = new Bitstream(blockSize);
      trackStreams[subsong * NUM_ZIP_CHANNELS + channel] = trackStream;
      for (size_t i = 0; i < trackSequence.size(); i++) {
        AlphaCode s = trackSequence[i];
        CODE_TYPE type = GET_CODE_TYPE(s);
        size_t startPosition = trackStream->position();

        if (s == CODE_STOP) {
          logD("SPAN %d %d %08x - STOP", subsong, channel, GET_ADDRESS_COMPONENTS(trackStream->position()));
          trackStream->writeBits(trackCommandCodes.at(CODE_STOP));

        } else if (s == CODE_RETURN_LAST) {
          logD("SPAN %d %d %08x - RETURN_LAST", subsong, channel, GET_ADDRESS_COMPONENTS(trackStream->position()));
          trackStream->writeBits(trackCommandCodes.at(CODE_RETURN_LAST));          

        } else if (s == CODE_RETURN_FF) {
          logD("SPAN %d %d %08x - RETURN_FF", subsong, channel, GET_ADDRESS_COMPONENTS(trackStream->position()));
          trackStream->writeBits(trackCommandCodes.at(CODE_RETURN_FF));
        
        } else if (s == CODE_RETURN_NOOP) {
          // pass

        } else if (s == CODE_SKIP) {
          logD("SPAN %d %d %08x - SKIP(%d)", subsong, channel, GET_ADDRESS_COMPONENTS(trackStream->position()), i);
          trackStream->writeBits(trackCommandCodes.at(CODE_SKIP));

        } else if (s == CODE_TAKE_DATA_JUMP) {
          logD("SPAN %d %d %08x - DATA_JUMP(%d)", subsong, channel, GET_ADDRESS_COMPONENTS(trackStream->position()), i);
          trackStream->writeBits(trackCommandCodes.at(CODE_TAKE_DATA_JUMP));

        } else if (s == CODE_TAKE_TRACK_JUMP) {
          logD("SPAN %d %d %08x - TRACK_JUMP(%d)", subsong, channel, GET_ADDRESS_COMPONENTS(trackStream->position()), i);
          trackStream->writeBits(trackCommandCodes.at(CODE_TAKE_TRACK_JUMP));
          i++;
          s = trackSequence[i];

          auto ij = jumpMap.find(s);
          if (ij != jumpMap.end()) {
            jumpTypeFrequencies[JUMP_POINTER_TYPE::INDEX]++;

            size_t index = (*ij).second;
            logD("SPAN %d %d %08x - JUMP INDEX %08x", subsong, channel, GET_ADDRESS_COMPONENTS(trackStream->position()), index);
            trackStream->writeBits(jumpTypeCodes.at(JUMP_POINTER_TYPE::INDEX));
            trackStream->writeBits(index, addressIndexBits);

          } else if (branchPointerOptimization) {
            // figure out short versus long jump bytes
            size_t targetPosition = GET_CODE_JUMP_INDEX(s);
            size_t targetAddress = BITSTREAM_2_ADDRESS(positionMap[targetPosition]);
            size_t sourcePosition = trackPositionMap.at(i);
            size_t sourceAddress = BITSTREAM_2_ADDRESS(positionMap[sourcePosition]);
            size_t targetAddressBytes = targetAddress >> 3;
            size_t sourceAddressBytes = sourceAddress >> 3;
            long distanceBytes = targetAddressBytes - sourceAddressBytes;
            if (distanceBytes >= -128 && distanceBytes <= 127) {
              jumpTypeFrequencies[JUMP_POINTER_TYPE::SHORT]++;
              // short jump
              size_t shiftAddr = targetAddress & 0x07;
              size_t shortJump = ((0xff & distanceBytes) << 3) | shiftAddr;
              logD("SPAN %d %d %08x - JUMP SHORT data stream %d -> %d source %08x -> target %08x (distance %d) code %08x", subsong, channel, GET_ADDRESS_COMPONENTS(trackStream->position()), sourcePosition, targetPosition, sourceAddress, targetAddress, distanceBytes, shortJump);
              trackStream->writeBits(jumpTypeCodes.at(JUMP_POINTER_TYPE::SHORT));
              trackStream->writeBits(shortJump, 11);

            } else {
              jumpTypeFrequencies[JUMP_POINTER_TYPE::LONG]++;
              // long jump
              logD("SPAN %d %d %08x - JUMP LONG %08x", subsong, channel, GET_ADDRESS_COMPONENTS(trackStream->position()), targetAddress);
              trackStream->writeBits(jumpTypeCodes.at(JUMP_POINTER_TYPE::LONG));
              trackStream->writeBits(targetAddress, addressBits);

            }

          } else {
            jumpTypeFrequencies[JUMP_POINTER_TYPE::LONG]++;
            size_t address = GET_CODE_JUMP_INDEX(s);
            size_t targetAddress = BITSTREAM_2_ADDRESS(positionMap[address]);
            logD("SPAN %d %d %08x - JUMP LONG %08x", subsong, channel, GET_ADDRESS_COMPONENTS(trackStream->position()), address);
            trackStream->writeBits(jumpTypeCodes.at(JUMP_POINTER_TYPE::LONG));
            trackStream->writeBits(targetAddress, addressBits);

          }
        } else {
          logD("bad code %08x", s);
          assert(false);

        }

      }

      // build jump addresses
      for (auto& x : jumpMap) {
        if (subsong != GET_CODE_SUBSONG(x.first)) {
          continue;
        }
        if (channel != GET_CODE_CHANNEL(x.first)) {
          continue;
        }
        size_t jumpIndex = GET_CODE_JUMP_INDEX(x.first);
        jumpTableAddresses[x.second] = BITSTREAM_2_ADDRESS(positionMap[jumpIndex]);
      }

      streamDataOffset += (dataStream->bytesUsed() << 3);

    }
  }

  // emit jump type frequencies
  logD("JUMP TYPE FREQUENCIES");
  size_t jumpTypebits = 0;
  size_t shortFreq = jumpTypeFrequencies[JUMP_POINTER_TYPE::SHORT];
  size_t shortBits = shortFreq * (11 + 2);
  logD("JUMP SHORT: %d - %d bits", shortFreq, shortBits);
  size_t indexFreq = jumpTypeFrequencies[JUMP_POINTER_TYPE::INDEX];
  size_t indexBits = indexFreq * (addressIndexBits + 1);
  logD("JUMP INDEX: %d - %d bits", indexFreq, indexBits);
  size_t longFreq = jumpTypeFrequencies[JUMP_POINTER_TYPE::LONG];
  size_t longBits = longFreq * (addressBits + 2);
  logD("JUMP LONG: %d - %d bits", longFreq, longBits);

  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < NUM_ZIP_CHANNELS; channel += 1) {
      logD("DATA %d %d size: %d", subsong, channel, dataStreams[subsong * NUM_ZIP_CHANNELS + channel]->size());
      logD("TRACK %d %d size: %d", subsong, channel, trackStreams[subsong * NUM_ZIP_CHANNELS + channel]->size());
    }
  }

}

Bitstream * DivExportTIAZip::assembleDatastream(
  const std::vector<AlphaCode> &compressedCodeSequence,
  const std::vector<JUMP_POINTER_TYPE> &jumpTypeAssignments,
  const std::map<AlphaCode, size_t> &jumpMap,
  const size_t streamDataOffset,
  std::vector<size_t> &positionMap,
  std::vector<size_t> &tooBigJumps
)
{
  Bitstream *dataStream = new Bitstream(blockSize);

  // map from data stream position to sequence position and target address
  std::map<size_t, std::pair<size_t, size_t>> jumpRevisionMap;
  positionMap.resize(compressedCodeSequence.size());
  for (size_t i = 0; i < compressedCodeSequence.size(); i++) {
    AlphaCode c = compressedCodeSequence[i];
    size_t startPosition = dataStream->position();
    size_t streamPosition = startPosition + streamDataOffset;
    positionMap[i] = streamPosition;
    CODE_TYPE type = GET_CODE_TYPE(c);
    switch (type) {
      case CODE_TYPE::BRANCH_POINT: {
        logD("DATA %d@%08x - BRANCH_POINT", i, GET_ADDRESS_COMPONENTS(dataStream->position()));
        dataStream->writeBits(dataCommandCodes.at(CODE_BRANCH_POINT));
        break;
      }

      case CODE_TYPE::TAKE_DATA_JUMP: {
        logD("DATA %d@%08x - TAKE_DATA_JUMP", i, GET_ADDRESS_COMPONENTS(dataStream->position()));
        dataStream->writeBits(dataCommandCodes.at(CODE_TAKE_DATA_JUMP));
        break;
      }

      case CODE_TYPE::WRITE_REGISTERS: {
        AlphaCode ac = GET_CODE_WRITE_REGISTERS_MASKED(c); 
        logD("DATA %d@%08x - WRITE_REGISTERS",i,  GET_ADDRESS_COMPONENTS(dataStream->position()));
        dataStream->writeBits(dataCommandCodes.at(ac));
        CHANGE_STATE cc = GET_CODE_WRITE_CC(c);
        if (cc == CHANGE_STATE::CHANGE) {
          unsigned char cx = GET_CODE_WRITE_CX(c);
          logD("DATA %d@%08x - CX %d", i, GET_ADDRESS_COMPONENTS(dataStream->position()), cx);
          dataStream->writeBits(controlCodes.at(cx));
        }
        CHANGE_STATE fc = GET_CODE_WRITE_FC(c);
        if (fc == CHANGE_STATE::CHANGE) {
          unsigned char fx = GET_CODE_WRITE_FX(c);
          unsigned char cx = GET_CODE_WRITE_CX(c);
          logD("DATA %d@%08x - FX %d", i, GET_ADDRESS_COMPONENTS(dataStream->position()), fx);
          // dataStream->writeBits(frequencyCodes.at(fx));
          AlphaCode mfc = controlCodeMergeMap.at(cx);
          dataStream->writeBits(mergedFrequencyCodes[mfc].at(fx));
        }
        CHANGE_STATE vc = GET_CODE_WRITE_VC(c);
        if (vc == CHANGE_STATE::CHANGE) {
          unsigned char vx = GET_CODE_WRITE_VX(c);
          logD("DATA %d@%08x - VX %d", i, GET_ADDRESS_COMPONENTS(dataStream->position()), vx);
          dataStream->writeBits(volumeCodes.at(vx));
        }
        // duration always 1
        // unsigned char duration = GET_CODE_WRITE_DURATION(c);
        // dataStream->writeBits(durationCodes.at(duration));
        break;
      }

      case CODE_TYPE::VOL_INC: {
        logD("DATA %d@%08x - VOL_INC", i, GET_ADDRESS_COMPONENTS(dataStream->position()));
        dataStream->writeBits(dataCommandCodes.at(CODE_VOL_INC));
        break;
      }

      case CODE_TYPE::VOL_DEC: {
        logD("DATA %d@%08x - VOL_DEC", i, GET_ADDRESS_COMPONENTS(dataStream->position()));
        dataStream->writeBits(dataCommandCodes.at(CODE_VOL_DEC));
        break;
      }

      case CODE_TYPE::PAUSE: {
        unsigned char duration = GET_CODE_WRITE_DURATION(c);
        logD("DATA %d@%08x - PAUSE %d", i, GET_ADDRESS_COMPONENTS(dataStream->position()), duration);
        dataStream->writeBits(dataCommandCodes.at(CODE_PAUSE_0));
        dataStream->writeBits(durationCodes.at(duration));
        break;
      }

      case CODE_TYPE::SUSTAIN: {
        unsigned char duration = GET_CODE_WRITE_DURATION(c);
        logD("DATA %d@%08x - SUSTAIN %d", i, GET_ADDRESS_COMPONENTS(dataStream->position()), duration);
        dataStream->writeBits(dataCommandCodes.at(CODE_SUSTAIN_0));
        dataStream->writeBits(durationCodes.at(duration));
        break;
      }

      case CODE_TYPE::VELOCITY: {
        unsigned char velocity = GET_CODE_VELOCITY(c);
        logD("DATA %d@%08x - VELOCITY %d", i, GET_ADDRESS_COMPONENTS(dataStream->position()), velocity);
        assert(dataCommandCodes.find(CODE_VELOCITY_0) != dataCommandCodes.end());
        assert(velocityCodes.find(velocity) != velocityCodes.end());
        dataStream->writeBits(dataCommandCodes.at(CODE_VELOCITY_0));
        dataStream->writeBits(velocityCodes.at(velocity));
        break;
      }

      case CODE_TYPE::JUMP: {

        JUMP_POINTER_TYPE jumpType = jumpTypeAssignments[i];
        switch (jumpType) {

          case JUMP_POINTER_TYPE::INDEX: {
            size_t index = jumpMap.at(c);
            logD("DATA %d@%08x - JUMP INDEX %d", i, GET_ADDRESS_COMPONENTS(dataStream->position()), index);
            dataStream->writeBits(jumpTypeCodes.at(JUMP_POINTER_TYPE::INDEX));
            dataStream->writeBits(index, addressIndexBits);
            break;
          }

          case JUMP_POINTER_TYPE::LONG: {
            size_t targetAddress = GET_CODE_JUMP_INDEX(c);
            logD("DATA %d@%08x - JUMP LONG %08x", i, GET_ADDRESS_COMPONENTS(dataStream->position()), targetAddress);
            dataStream->writeBits(jumpTypeCodes.at(JUMP_POINTER_TYPE::LONG));
            jumpRevisionMap[dataStream->position()] = std::pair<size_t, size_t>(i, targetAddress);
            dataStream->writeBits(targetAddress, addressBits);
            break;
          }

          case JUMP_POINTER_TYPE::SHORT: {
            size_t targetIndex = GET_CODE_JUMP_INDEX(c);
            logD("DATA %d@%08x - JUMP SHORT %08x", i, GET_ADDRESS_COMPONENTS(dataStream->position()), targetIndex);
            dataStream->writeBits(jumpTypeCodes.at(JUMP_POINTER_TYPE::SHORT));
            jumpRevisionMap[dataStream->position()] = std::pair<size_t, size_t>(i, targetIndex);
            dataStream->writeBits(targetIndex, 11);
            break;
          }

        } 
        break;
      }

      default:
        logD("bad code %08x", c);
        assert(false);
    }

  }

  for (auto& x : jumpRevisionMap) {
    dataStream->seek(x.first);
    JUMP_POINTER_TYPE jumpType = jumpTypeAssignments[x.second.first];
    size_t targetAddress = BITSTREAM_2_ADDRESS(positionMap[x.second.second]);
    switch (jumpType) {
      case JUMP_POINTER_TYPE::LONG: {
        dataStream->writeBits(targetAddress, addressBits);
        unsigned long bits = msb(targetAddress);
        logD("DATA - REMAP JUMP ADDRESS@%08x: %08x -> %08x (%08x is %d bits)", GET_ADDRESS_COMPONENTS(x.first), x.second.second, GET_ADDRESS_COMPONENTS(targetAddress), targetAddress, bits);
        break;
      }

      case JUMP_POINTER_TYPE::SHORT: {
        size_t targetAddressBytes = targetAddress >> 3;
        size_t sourceAddress = BITSTREAM_2_ADDRESS(x.first + 11 + streamDataOffset);
        size_t sourceAddressBytes = sourceAddress >> 3;
        size_t shiftAddr = targetAddress & 0x07;
        long distanceBytes = targetAddressBytes - sourceAddressBytes;
        if (distanceBytes > 127 || distanceBytes < -128) {
          tooBigJumps.push_back(x.second.first);
        }
        size_t shortJump = ((0xff & distanceBytes) << 3) | shiftAddr;
        dataStream->writeBits(shortJump, 11);
        logD("DATA - REMAP JUMP RELATIVE@%08x: %08x -> %08x -> %08x (%08x is %d bits) (distance %d is %d bits)", GET_ADDRESS_COMPONENTS(x.first), GET_ADDRESS_COMPONENTS(sourceAddress), shortJump, GET_ADDRESS_COMPONENTS(targetAddress), targetAddress, msb(targetAddress), distanceBytes, msb(distanceBytes));

        break;

      }

      default:
        assert(false);
    }

  }

  return dataStream;
}

void DivExportTIAZip::writeBitstreams() {

  size_t totalCompressedBytes = 0;

  size_t numSongs = e->song.subsong.size();

  // write track audio data
  SafeWriter* trackData = new SafeWriter;
  trackData->init();
  trackData->writeText("; Furnace Tracker audio data file\n");
  trackData->writeText("; TIAZip data format\n");
  trackData->writeText(fmt::sprintf("; Song: %s\n", e->song.name));
  trackData->writeText(fmt::sprintf("; Author: %s\n", e->song.author));

  trackData->writeText(fmt::sprintf("\nAUDIO_NUM_TRACKS = %d\n", numSongs));

  if (branchPointerOptimization) {
    trackData->writeText("\n#include \"cores/tiazip_2_player_core.asm\"\n");
  } else {
    trackData->writeText("\n#include \"cores/tiazip_1_player_core.asm\"\n");
  }

  // create a lookup table for use in player apps
  size_t songDataSize = 0;
  // one track table for all channels
  trackData->writeText("    MAC AUDIO_CONTROL_TABLE\n");
  trackData->writeText("AUDIO_TRACKS:\n");
  for (size_t subsong = 0; subsong < numSongs; subsong++) {
    // note reverse order for copy routine
    trackData->writeText(fmt::sprintf("    byte >(AUDIO_TRACK_S%d_C1_START - 1), <(AUDIO_TRACK_S%d_C1_START - 1)\n", subsong, subsong));
    trackData->writeText(fmt::sprintf("    byte >(AUDIO_TRACK_S%d_C0_START - 1), <(AUDIO_TRACK_S%d_C0_START - 1)\n", subsong, subsong));
    trackData->writeText(fmt::sprintf("    byte >(AUDIO_DATA_S%d_C1_START - 1)), <(AUDIO_DATA_S%d_C1_START - 1)\n", subsong, subsong));
    trackData->writeText(fmt::sprintf("    byte >(AUDIO_DATA_S%d_C0_START - 1)), <(AUDIO_DATA_S%d_C0_START - 1)\n", subsong, subsong));
    songDataSize += 8;
  }
  trackData->writeText("    ENDM\n");

  // write the data streams
  trackData->writeText("\nAUDIO_DATA_OFFSET");
  for (size_t subsong = 0; subsong < numSongs; subsong++) {
    for (int channel = 0; channel < NUM_ZIP_CHANNELS; channel += 1) {
      trackData->writeText(fmt::sprintf("\nAUDIO_DATA_S%d_C%d_START", subsong, channel));
      Bitstream *dataStream = dataStreams[subsong * NUM_ZIP_CHANNELS + channel];
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
  for (size_t subsong = 0; subsong < numSongs; subsong++) {
    for (int channel = 0; channel < NUM_ZIP_CHANNELS; channel += 1) {
      trackData->writeText(fmt::sprintf("\nAUDIO_TRACK_S%d_C%d_START", subsong, channel));
      Bitstream *trackStream = trackStreams[subsong * NUM_ZIP_CHANNELS + channel];
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
  for (auto addr : jumpTableAddresses) {
      trackData->writeText(fmt::sprintf("\n    byte $%02x ; <%02x", (addr >> 3) & 0xff, addr));
      totalCompressedBytes += 1;
  }
  trackData->writeText(fmt::sprintf("\nAUDIO_JUMP_TABLE_HI_START"));
  for (auto addr : jumpTableAddresses) {
      trackData->writeText(fmt::sprintf("\n    byte $%02x ; >%02x", ((addr & 0x07) << 4) | ((addr >> 11) & 0x0f), addr));
      totalCompressedBytes += 1;
  }

  // write control and decoder tables
  trackData->writeText(fmt::sprintf("\nCODEBOOK_FIRST_VALUES"));
  totalCompressedBytes += writeCodebookFirstValues(
    trackData, 
    "audio_decode_command",
    dataCommandCodebook,
    dataCommandCodes
  );
  totalCompressedBytes += writeCodebookFirstValues(
    trackData,
    "audio_decode_span",
    trackCommandCodebook,
    trackCommandCodes
  );
  totalCompressedBytes += writeCodebookFirstValues(trackData, "audio_decode_control", controlCodebook, controlCodes);
  // totalCompressedBytes += writeCodebookFirstValues(trackData, "audio_decode_frequency", frequencyCodebook, frequencyCodeIndex);
  for (auto &x : mergedFrequencyCodebooks) {
    logD("writing first values for %d", x.first);
    // KLUDGE: tryna block low weight low value business
    if (compressionLevel > 2 && x.first == 0) {
      logD("short circuit compression level 3+");
      trackData->writeText("\naudio_decode_control_0_frequency_FIRST_VALUES = 0");
      continue;
    }
    totalCompressedBytes += writeCodebookFirstValues(
      trackData,
      fmt::sprintf("audio_decode_control_%d_frequency", x.first).c_str(),
      x.second,
      mergedFrequencyCodes[x.first]
    );
  }
  totalCompressedBytes += writeCodebookFirstValues(trackData, "audio_decode_volume", volumeCodebook, volumeCodes);
  totalCompressedBytes += writeCodebookFirstValues(trackData, "audio_decode_duration", durationCodebook, durationCodes);
  totalCompressedBytes += writeCodebookFirstValues(trackData, "audio_decode_velocity", velocityCodebook, velocityCodes);
  totalCompressedBytes += writeCodebookFirstValues(trackData, "audio_decode_jump", jumpTypeCodebook, jumpTypeCodes);

  // BUGBUG: disable
  // // write control and decoder tables
  // trackData->writeText(fmt::sprintf("\nCODEBOOK_LAST_VALUES"));
  // totalCompressedBytes += writeCodebookLastValues(
  //   trackData, 
  //   "audio_decode_command",
  //   dataCommandCodebook,
  //   dataCommandCodes
  // );
  // totalCompressedBytes += writeCodebookLastValues(
  //   trackData,
  //   "audio_decode_span",
  //   trackCommandCodebook,
  //   trackCommandCodes
  // );
  // totalCompressedBytes += writeCodebookLastValues(trackData, "audio_decode_control", controlCodebook, controlCodes);
  // // totalCompressedBytes += writeCodebookLastValues(trackData, "audio_decode_frequency", frequencyCodebook, frequencyCodeIndex);
  // for (auto &x : mergedFrequencyCodebooks) {
  //   totalCompressedBytes += writeCodebookLastValues(
  //     trackData,
  //     fmt::sprintf("audio_decode_control_%d_frequency", x.first).c_str(),
  //     x.second,
  //     mergedFrequencyCodes[x.first]
  //   );
  // }
  // totalCompressedBytes += writeCodebookLastValues(trackData, "audio_decode_volume", volumeCodebook, volumeCodes);
  // totalCompressedBytes += writeCodebookLastValues(trackData, "audio_decode_duration", durationCodebook, durationCodes);
  // totalCompressedBytes += writeCodebookLastValues(trackData, "audio_decode_velocity", velocityCodebook, velocityCodes);


  // write length tables
  trackData->writeText(fmt::sprintf("\nCODEBOOK_LENGTHS"));
  size_t codebookTotal = 0;
  totalCompressedBytes += writeCodebookLengths(
    trackData, 
    "audio_decode_command",
    dataCommandCodebook,
    codebookTotal
  );
  totalCompressedBytes += writeCodebookLengths(
    trackData,
    "audio_decode_span",
    trackCommandCodebook,
    codebookTotal
  );
  totalCompressedBytes += writeCodebookLengths(trackData, "audio_decode_control", controlCodebook, codebookTotal);
  for (auto &x : mergedFrequencyCodebooks) {
    // KLUDGE: tryna block low weight low value business
    if (compressionLevel > 2 && x.first == 0) {
      continue;
    }
    totalCompressedBytes += writeCodebookLengths(
      trackData,
      fmt::sprintf("audio_decode_control_%d_frequency", x.first).c_str(),
      x.second,
      codebookTotal
    );
  }
  totalCompressedBytes += writeCodebookLengths(trackData, "audio_decode_volume", volumeCodebook, codebookTotal);
  totalCompressedBytes += writeCodebookLengths(trackData, "audio_decode_duration", durationCodebook, codebookTotal);
  totalCompressedBytes += writeCodebookLengths(trackData, "audio_decode_velocity", velocityCodebook, codebookTotal);
  totalCompressedBytes += writeCodebookLengths(trackData, "audio_decode_jump", jumpTypeCodebook, codebookTotal);

  // codes
  trackData->writeText(fmt::sprintf("\nCODEBOOK_CODES"));
  totalCompressedBytes += writeCommandCodes(
    trackData,
    "audio_decode_command",
    dataCommandCodebook, 
    dataCommandCodes
  );
  totalCompressedBytes += writeCommandCodes(
    trackData,
    "audio_decode_span",
    trackCommandCodebook,
    trackCommandCodes
  );
  totalCompressedBytes += writeDataCodes(trackData, "audio_decode_control", controlCodebook, controlCodes);
  // totalCompressedBytes += writeDataCodes(trackData, "audio_decode_frequency", frequencyCodebook, frequencyCodeIndex);
  for (auto &x : mergedFrequencyCodebooks) {
    // KLUDGE: tryna block low weight low value business
    if (compressionLevel > 2 && x.first == 0) {
      continue;
    }
    totalCompressedBytes += writeDataCodes(
      trackData,
      fmt::sprintf("audio_decode_control_%d_frequency", x.first).c_str(),
      x.second,
      mergedFrequencyCodes[x.first]
    );
  }
  totalCompressedBytes += writeDataCodes(trackData, "audio_decode_volume", volumeCodebook, volumeCodes);
  totalCompressedBytes += writeDataCodes(trackData, "audio_decode_duration", durationCodebook, durationCodes);
  totalCompressedBytes += writeDataCodes(trackData, "audio_decode_velocity", velocityCodebook, velocityCodes);
  totalCompressedBytes += writeJumpCodes(trackData, "audio_decode_jump", jumpTypeCodebook, jumpTypeCodes);

  // macros
  writeCodebookMacro(
    trackData, 
    "audio_decode_command",
    "audio_data_stream_idx", 
    dataCommandCodebook
  );
  if (trackCommandCodebook.size() == 1) {
    // BUGBUG: massive kludge
    trackData->writeText("\n    ; audio_decode_span\n");
    trackData->writeText("    MAC audio_decode_span_MACRO\n");
    trackData->writeText("    lda #<CODE_STOP\n");
    trackData->writeText("    ENDM\n\n");
  } else {
    writeCodebookMacro(
      trackData,
      "audio_decode_span",
      "audio_span_stream_idx",
      trackCommandCodebook);
  }
  writeCodebookMacro(trackData, "audio_decode_control", "audio_data_stream_idx", controlCodebook);
  if (mergedFrequencyCodebooks.size() == 1) {
    auto it = mergedFrequencyCodebooks.begin();
    AlphaCode instrumentCode = (*it).first;
    trackData->writeText(fmt::sprintf("\naudio_decode_frequency_FIRST_VALUES = audio_decode_control_%d_frequency_FIRST_VALUES", instrumentCode));
    writeCodebookMacro(
      trackData,
      "audio_decode_frequency",
      "audio_data_stream_idx", 
      (*it).second
    );
  } else {
    trackData->writeText("\nCONTROL_FREQUENCY_TABLE");
    for (AlphaCode i = 0; i < 16; i++) {
      AlphaCode instrumentCode = controlCodeMergeMap.at(i);
      if (mergedFrequencyCodebooks.find(instrumentCode) == mergedFrequencyCodebooks.end()) {
        instrumentCode = 0;
      }
      trackData->writeText(fmt::sprintf("\n    byte audio_decode_control_%d_frequency_FIRST_VALUES", instrumentCode));
    }
    trackData->writeText("\n    MAC audio_decode_frequency_MACRO\n");
    trackData->writeText("    ldy audio_channel_cx,x\n");
    trackData->writeText("    ldx audio_data_stream_idx\n");
    trackData->writeText("    lda CONTROL_FREQUENCY_TABLE,y\n");
    if (compressionLevel > 2) {
      trackData->writeText("    beq ._audio_decode_frequency_literal\n");
    }
    trackData->writeText("    tay\n");
    trackData->writeText("    jsr audio_stream_read_symbol\n");
    if (compressionLevel > 2) {
      trackData->writeText("    bpl ._audio_decode_frequency_save_fx ; always true\n");
      trackData->writeText("._audio_decode_frequency_literal\n");
      trackData->writeText("    lda audio_stream_buf,x\n");
      trackData->writeText("    ldy #%11110000\n");
      trackData->writeText("    jsr read_symbol_y\n");
      trackData->writeText("    sta audio_stream_buf,x\n");
      trackData->writeText("    tya\n");
      trackData->writeText("._audio_decode_frequency_save_fx\n");
    }
    trackData->writeText("    ENDM\n\n");
  }
  writeCodebookMacro(trackData, "audio_decode_volume", "audio_data_stream_idx", volumeCodebook);
  writeCodebookMacro(trackData, "audio_decode_duration", "audio_data_stream_idx", durationCodebook);
  writeCodebookMacro(trackData, "audio_decode_velocity", "audio_data_stream_idx", velocityCodebook);

  trackData->writeText(fmt::sprintf("\n\n; Song data size: %d\n", songDataSize));

  size_t totalCompressedCodeSequenceSize = 0;
  for (auto &x : compressedCodeSequences) {
    totalCompressedCodeSequenceSize += x.size();
  }
  size_t totalTrackSequenceSize = 0;
  for (auto &x : trackSequences) {
    totalTrackSequenceSize += x.size();
  }

  trackData->writeText(fmt::sprintf("; Compressed Code Sequence Length: %d\n", totalCompressedCodeSequenceSize));
  trackData->writeText(fmt::sprintf("; Track Sequence Length: %d\n", totalTrackSequenceSize));
  trackData->writeText(fmt::sprintf("; Compressed Bytes %d\n", totalCompressedBytes));

  output.push_back(DivROMExportOutput("Track_data.asm", trackData));

}

void DivExportTIAZip::validateBitstreams() {

  // validate bitstream
  size_t streamDataOffset = (baseDataOffset << 3);
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < NUM_ZIP_CHANNELS; channel += 1) {
      logD("starting validation %d %d", subsong, channel);
      auto &codeSequence = codeSequences[subsong * NUM_ZIP_CHANNELS + channel];
      auto dataStream = dataStreams[subsong * NUM_ZIP_CHANNELS + channel];
      dataStream->seek(0);
      auto trackStream = trackStreams[subsong * NUM_ZIP_CHANNELS + channel];
      trackStream->seek(0);
      logD("reset streams %d %d", dataStream->position(), trackStream->position());
      size_t i = 0;
      size_t returnAddress = 0;
      size_t maxAddress = 0;
      unsigned char lastCx = 0;
      AlphaCode lastCommand = 0;
      while (dataStream->hasBits()) {
        size_t streamPosition = dataStream->position();
        logD("AT datastream=%08x trackstream=%08x", GET_ADDRESS_COMPONENTS(streamPosition), GET_ADDRESS_COMPONENTS(trackStream->position()));
        AlphaCode code;
        AlphaCode nextCommand = dataCommandCodeTree->decode(dataStream);
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
            assert(mergedFrequencyTrees[mfc] != NULL);
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
            assert(mergedFrequencyTrees[mfc] != NULL);
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
            assert(mergedFrequencyTrees[mfc] != NULL);
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
            assert(mergedFrequencyTrees[mfc] != NULL);
            unsigned char fx = mergedFrequencyTrees[mfc]->decode(dataStream);

            code = CODE_WRITE_REGISTERS(CHANGE_STATE::NOOP, lastCx, fc, fx, CHANGE_STATE::NOOP, 0, 1);
            break;
          }

          case CODE_TAKE_DATA_JUMP: {
            // get address
            size_t nextAddress;
            switch(jumpTypeCodeTree->decode(dataStream)) {
              case JUMP_POINTER_TYPE::LONG:
                logD("DATA JUMP LONG"); {
                nextAddress = dataStream->readBits(addressBits);
                break;
              }

              case JUMP_POINTER_TYPE::SHORT: {
                logD("DATA JUMP SHORT from %08x", BITSTREAM_2_ADDRESS(dataStream->position()));
                // relative
                size_t relativeJump = dataStream->readBits(11);
                size_t shiftAddr = relativeJump & 0x07;
                size_t relativeAddr = relativeJump >> 3;
                if (relativeAddr & 0x80) {
                  relativeAddr |= -256;
                }
                size_t nextAddressBytes = BITSTREAM_2_ADDRESS(dataStream->position()) >> 3;
                nextAddressBytes = nextAddressBytes + relativeAddr; 
                nextAddress = (nextAddressBytes << 3) | shiftAddr;
                logD("DATA JUMP SHORT next %08x nextBytes %08x offset %08x", nextAddress, nextAddressBytes, streamDataOffset);
                nextAddress += streamDataOffset;
                logD("DATA JUMP SHORT %08x =  rel|shift %08x|%08x to next %08x", relativeJump, relativeAddr, shiftAddr, nextAddress);
                break;
              }

              case JUMP_POINTER_TYPE::INDEX: {
                size_t index = dataStream->readBits(addressIndexBits);
                nextAddress = jumpTableAddresses[index];
                logD("DATA JUMP INDEX %d -> %08x", index, nextAddress);
                break;
              }
            }
          
            nextAddress -= streamDataOffset;
            returnAddress = BITSTREAM_2_ADDRESS(dataStream->position());
            if (maxAddress < returnAddress) {
              maxAddress = returnAddress;
            }
            logD("seek X");
            dataStream->seek(ADDRESS_2_BITSTREAM(nextAddress));
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
            logD("BRANCH - %08x", GET_ADDRESS_COMPONENTS(trackStream->position()));

            // jump and seek
            size_t trackPosition = trackStream->position();
            AlphaCode sx = trackCommandTree->decode(trackStream);
            size_t nextAddress;

            if (sx == CODE_STOP) {
              logD("STOP");
              code = CODE_STOP;
              break;

            } else if (sx == CODE_RETURN_LAST) {
              logD("seek A");
              assert(returnAddress < dataStream->size());
              dataStream->seek(ADDRESS_2_BITSTREAM(returnAddress));
              continue;

            } else if (sx == CODE_RETURN_FF) {
              logD("seek B");
              assert(maxAddress < dataStream->size());
              dataStream->seek(ADDRESS_2_BITSTREAM(maxAddress));
              continue;
            
            } else if (sx == CODE_RETURN_NOOP) {
              assert(false);
              continue;
              
            } else if (sx == CODE_SKIP) {
              // skip next datastream address
              switch(jumpTypeCodeTree->decode(dataStream)) {
                case JUMP_POINTER_TYPE::LONG:
                  logD("SKIP LONG");
                  dataStream->readBits(addressBits);
                  break;
                case JUMP_POINTER_TYPE::SHORT:
                  logD("SKIP SHORT");
                  dataStream->readBits(11);
                  break;
                case JUMP_POINTER_TYPE::INDEX:
                  logD("SKIP INDEX");
                  dataStream->readBits(addressIndexBits);
                  break;
              };
              continue;

            } else if (sx == CODE_TAKE_DATA_JUMP ) {

              switch(jumpTypeCodeTree->decode(dataStream)) {

                case JUMP_POINTER_TYPE::LONG: {
                  logD("DATA JUMP LONG");
                  nextAddress = dataStream->readBits(addressBits);
                  break;
                }

                case JUMP_POINTER_TYPE::SHORT: {
                  logD("DATA JUMP SHORT from %08x", BITSTREAM_2_ADDRESS(dataStream->position()));
                  // relative
                  size_t relativeJump = dataStream->readBits(11);
                  size_t shiftAddr = relativeJump & 0x07;
                  size_t relativeAddr = relativeJump >> 3;
                  if (relativeAddr & 0x80) {
                    relativeAddr |= -256;
                  }
                  size_t nextAddressBytes = BITSTREAM_2_ADDRESS(dataStream->position()) >> 3;
                  logD("DATA JUMP SHORT sourceAddr %d aka %08x nextBytes %d", dataStream->position(), BITSTREAM_2_ADDRESS(dataStream->position()), nextAddressBytes);
                  nextAddressBytes = nextAddressBytes + relativeAddr;
                  nextAddress = (nextAddressBytes << 3) | shiftAddr;
                  logD("DATA JUMP SHORT next %08x nextBytes %08x offset %08x", nextAddress, nextAddressBytes, streamDataOffset);
                  nextAddress += streamDataOffset;
                  logD("DATA JUMP SHORT %08x = %08x|%08x to %08x %d", relativeJump, relativeAddr, shiftAddr, nextAddress, nextAddressBytes);
                  break;
                }

                case JUMP_POINTER_TYPE::INDEX: {
                  size_t index = dataStream->readBits(addressIndexBits);
                  nextAddress = jumpTableAddresses[index];
                  logD("DATA JUMP INDEX %d -> %08x", index, nextAddress);
                  break;
                }
              };

            } else if (sx == CODE_TAKE_TRACK_JUMP) {
              // now read track 
              switch(jumpTypeCodeTree->decode(trackStream)) {

                case JUMP_POINTER_TYPE::LONG: {
                  logD("TRACK JUMP LONG");
                  nextAddress = trackStream->readBits(addressBits);
                  break;
                }

                case JUMP_POINTER_TYPE::SHORT: {
                  logD("TRACK JUMP SHORT from %08x", BITSTREAM_2_ADDRESS(dataStream->position()));
                  // relative
                  size_t relativeJump = trackStream->readBits(11);
                  size_t shiftAddr = relativeJump & 0x07;
                  size_t relativeAddr = relativeJump >> 3;
                  if (relativeAddr & 0x80) {
                    relativeAddr |= -256;
                  }
                  size_t nextAddressBytes = BITSTREAM_2_ADDRESS(dataStream->position()) >> 3;
                  logD("TRACK JUMP SHORT sourceAddr %d aka %08x nextBytes %d", dataStream->position(), BITSTREAM_2_ADDRESS(dataStream->position()), nextAddressBytes);
                  nextAddressBytes = nextAddressBytes + relativeAddr;
                  nextAddress = (nextAddressBytes << 3) | shiftAddr;
                  logD("TRACK JUMP SHORT next %08x nextBytes %08x offset %08x", nextAddress, nextAddressBytes, streamDataOffset);
                  nextAddress += streamDataOffset;
                  logD("TRACK JUMP SHORT %08x = %08x|%08x to %08x %d", relativeJump, relativeAddr, shiftAddr, nextAddress, nextAddressBytes);
                  break;
                }

                case JUMP_POINTER_TYPE::INDEX: {
                  size_t index = trackStream->readBits(addressIndexBits);
                  nextAddress = jumpTableAddresses[index];
                  logD("TRACK JUMP INDEX %d -> %08x", index, nextAddress);
                  break;
                }
              };

              // skip datastream pointer to get distance
              switch(jumpTypeCodeTree->decode(dataStream)) {
                case JUMP_POINTER_TYPE::LONG:
                  logD("SKIP LONG");
                  dataStream->readBits(addressBits);
                  break;
                case JUMP_POINTER_TYPE::SHORT:
                  logD("SKIP SHORT");
                  dataStream->readBits(11);
                  break;
                case JUMP_POINTER_TYPE::INDEX:
                  logD("SKIP INDEX");
                  dataStream->readBits(addressIndexBits);
                  break;
              };

            } else {
              // should not happen
              assert(false);
            }

            nextAddress -= streamDataOffset;
            returnAddress = BITSTREAM_2_ADDRESS(dataStream->position());
            if (maxAddress < returnAddress) {
              maxAddress = returnAddress;
            }
            logD("seek C");
            dataStream->seek(ADDRESS_2_BITSTREAM(nextAddress));
            continue;
          }
          
          default:
            assert(false);
        }
        AlphaCode codeToCompare = codeSequence[i++];
        if (code != codeToCompare) {
          logD("%d/%d: (%d/%d) [%d, %d] %016x ?= %016x", i, codeSequence.size(), streamPosition, dataStream->size(), returnAddress, maxAddress, code, codeToCompare);
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
}

void DivExportTIAZip::computeMergedFrequenciesDefault() {
  for (size_t i = 0; i < 16; i++) {
    controlCodeMergeMap[i] = 0;
  }
  if (compressionLevel == 3) {
    controlCodeMergeMap = {
      {0, 0},
      {1, 0},
      {2, 0},
      {3, 2},
      {4, 2},
      {5, 0},
      {6, 0},
      {7, 0},
      {8, 1},
      {9, 0},
      {10, 0},
      {11, 0},
      {12, 3},
      {13, 0},
      {14, 0},
      {15, 0}
    };
  } else if (compressionLevel == 2) {
    controlCodeMergeMap = {
      {0, 0},
      {1, 0},
      {2, 0},
      {3, 1},
      {4, 1},
      {5, 0},
      {6, 1},
      {7, 0},
      {8, 0},
      {9, 0},
      {10, 0},
      {11, 0},
      {12, 1},
      {13, 0},
      {14, 0},
      {15, 0}
    };
  }
  for (auto &x : initialFrequencyMap) {
    AlphaCode mergeFrequencyCode = controlCodeMergeMap[x.first];
    for (auto &f: x.second) {
      mergedFrequencyMap[mergeFrequencyCode][f.first] += f.second;
    }
    String row = "";
    for (size_t i = 0; i < 31; i++) {
      row += fmt::sprintf(" %02d", x.second[i]);
    }
    logD("F%02d %03d %s", x.first, mergeFrequencyCode, row);
  }
  if (compressionLevel > 2) {
    size_t nodeLimit = 32; 
    for (size_t i = 0; i < nodeLimit; i++) {
      mergedFrequencyMap[0][i] = 1;
    }
  }
}

void DivExportTIAZip::computeMergedFrequenciesDynamic() {

  // weights
  std::map<AlphaCode, double> controlCodeWeights;
  std::priority_queue<std::pair<AlphaCode, size_t>, std::vector<std::pair<AlphaCode, size_t>>, CompareFrequencies> frequencyHeap;
  for (auto &x: initialFrequencyMap) {
    // compute total weight
    size_t weight = 0;
    for (auto &f: x.second) {
      weight += f.second;
    }

    logD("pushing frequency audcx %d weight %d", x.first, weight);
    controlCodeWeights[x.first] = weight;
    frequencyHeap.emplace(std::pair<AlphaCode, size_t>(x.first, weight));
  }

  String header = "       ";
  for (auto& c : controlCodeWeights) {
    header += fmt::sprintf("   F%02d", c.first);
  } 
  logD(header.c_str());

  // clusters
  String indent = "     ";
  for (auto ix = initialFrequencyMap.begin(); ix != initialFrequencyMap.end(); ix++) {
    auto& a = ix->second;
    String row = indent;
    double aTotalWeight = controlCodeWeights[ix->first];
    for (auto iy = ix; iy != initialFrequencyMap.end(); iy++) {
      auto& b = iy->second;
      double bTotalWeight = controlCodeWeights[iy->first];
      double chi = 0;
      for (auto& c : controlCodeWeights) {
        double weightA = ((double)a[c.first]) / aTotalWeight;
        double weightB = ((double)b[c.first]) / bTotalWeight;
        double weightC = weightA - weightB;
        chi += weightC * weightC;
      }
      row += fmt::sprintf(" %.3lf", chi);
    }
    logD("F%02d %s", ix->first, row);
    indent += "      ";
  }
  AlphaCode mergedFrequencyCode = 0;
  size_t totalWeight = 0;
  while (!frequencyHeap.empty()) {
    auto x = frequencyHeap.top();
    frequencyHeap.pop();
    const size_t nextWeight = x.second;
    if (compressionLevel > 1 && totalWeight > 0 && (totalWeight + nextWeight) > 100) {
      mergedFrequencyCode++;
      totalWeight = 0;
    }
    totalWeight += nextWeight;
    controlCodeMergeMap[x.first] = mergedFrequencyCode;
    logD("merged frequency tree %d audcx %d weight %d", mergedFrequencyCode, x.first, totalWeight);
  }
  for (auto &x : initialFrequencyMap) {
    AlphaCode mergeFrequencyCode = controlCodeMergeMap[x.first];
    for (auto &f: x.second) {
      mergedFrequencyMap[mergeFrequencyCode][f.first] += f.second;
    }
    String row = "";
    for (size_t i = 0; i < 31; i++) {
      row += fmt::sprintf(" %02d", x.second[i]);
    }
    logD("F%02d %03d %s", x.first, mergeFrequencyCode, row);
  }
}

size_t DivExportTIAZip::writeCodebookLengths(
  SafeWriter *w,
  const char *label,
  const std::vector<CodebookEntry> &codebook,
  size_t &total
) {
  size_t bytesWritten = 0;
  size_t currentLength = 0;
  w->writeText(fmt::sprintf("\n%s_LENGTHS = . - CODEBOOK_LENGTHS", label));
  for (auto &entry : codebook) {
    if (entry.height == 0) {
      continue;
    }
    if (currentLength == 0) {
      currentLength = entry.height - 1;
    }
    if (entry.height > currentLength) {
      currentLength += 1;
      if (entry.height > currentLength) {
        w->writeText("\n    byte $00");
        bytesWritten += 1;
        currentLength = entry.height;
      }
      w->writeText(fmt::sprintf("\n    byte %d", total));
      bytesWritten += 1;
    }
    total += 1;
  }
  if (currentLength > 0 && currentLength < 7) {
    w->writeText(fmt::sprintf("\n    byte %d", total));
    bytesWritten += 1;
  }
  return bytesWritten;
}

size_t DivExportTIAZip::writeCodebookFirstValues(
  SafeWriter *w,
  const char *label,
  const std::vector<CodebookEntry> &codebook,
  const std::map<AlphaCode, std::vector<bool>> &codeIndex
) {
  size_t bytesWritten = 0;
  size_t lastHeight = 0;
  w->writeText(fmt::sprintf("\n%s_FIRST_VALUES = . - CODEBOOK_FIRST_VALUES", label));
  for (auto &entry : codebook) {
    if (entry.height == lastHeight) {
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
    if (lastHeight == 0) {
      lastHeight = entry.height - 1;
    } else if (lastHeight == entry.height) {
      continue;
    }
    lastHeight += 1;
    if (lastHeight < entry.height) {
      // guard entry
      w->writeText(fmt::sprintf("\n    byte %%%s; guard", bitcode.substr(0, lastHeight + 1)));
      bytesWritten += 1;
      lastHeight = entry.height;
    }
    w->writeText(fmt::sprintf("\n    byte %%%s", bitcode));
    bytesWritten += 1;
  }
  if (lastHeight > 0 && lastHeight < 7) {
    w->writeText("\n    byte $ff");
    bytesWritten += 1;
  }
  return bytesWritten;
}

size_t DivExportTIAZip::writeCodebookLastValues(
  SafeWriter *w,
  const char *label,
  const std::vector<CodebookEntry> &codebook,
  const std::map<AlphaCode, std::vector<bool>> &codeIndex
) {
  size_t bytesWritten = 0;
  w->writeText(fmt::sprintf("\n%s_LAST_VALUES = . - CODEBOOK_LAST_VALUES", label));
  for (size_t i = 0; i < codebook.size(); i++) {
    auto &entry = codebook[i];
    if ((i + 1) < codebook.size() && entry.height == codebook[i + 1].height) {
      continue;
    }
    if (entry.height > 0) {
      AlphaCode code = entry.code;
      String bitcode = "1";
      auto it = codeIndex.find(code);
      if (it != codeIndex.end()) {
        auto &bitvec = (*it).second;  
        for (int i = bitvec.size(); --i >= 0; ) {
          bitcode += bitvec.at(i) ? "1" : "0";
        }
      }
      w->writeText(fmt::sprintf("\n    byte <(%%%s + 1)", bitcode));
      bytesWritten += 1;
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

size_t DivExportTIAZip::writeJumpCodes(
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
    if (code == JUMP_POINTER_TYPE::LONG) {
      w->writeText(fmt::sprintf("\n    byte <CODE_JUMP_LONG; %s", bitcode));
    } else if (code == JUMP_POINTER_TYPE::SHORT) {
      w->writeText(fmt::sprintf("\n    byte <CODE_JUMP_SHORT; %s", bitcode));
    } else if (code == JUMP_POINTER_TYPE::INDEX) {
      w->writeText(fmt::sprintf("\n    byte <CODE_JUMP_INDEX; %s", bitcode));
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
    w->writeText(fmt::sprintf("    ldy #%s_FIRST_VALUES\n", label));
    w->writeText("    jsr audio_stream_read_symbol\n");
  }
  w->writeText("\n    ENDM\n\n");
}

void DivExportTIAZip::validateCodeSequence(
  size_t subsong,
  int channel,
  const std::vector<AlphaCode> &codeSequence,
  const std::vector<AlphaCode> &compressedCodeSequence,
  const std::vector<AlphaCode> &trackSequence
) {
  // Test compression correctness 
  auto it = trackSequence.begin();
  size_t nextReadIndex = 0, compareIndex = 0;
  size_t maxIndex = 0;
  size_t returnIndex = 0;
  while (true) {
    AlphaCode c = compressedCodeSequence[nextReadIndex];
    CODE_TYPE codeType = GET_CODE_TYPE(c);
    if (c == CODE_TAKE_DATA_JUMP) {
        nextReadIndex++;
        AlphaCode c = compressedCodeSequence[nextReadIndex];
        size_t jumpIndex = GET_CODE_JUMP_INDEX(c);
        assert(CODE_TYPE::JUMP == GET_CODE_TYPE(c));
        if (jumpIndex >= maxIndex) {
          logD("missed force goto back to front");
        }
        if (jumpIndex == returnIndex) {
          logD("missed force goto back to last");
        }
        returnIndex = nextReadIndex + 1;
        if (returnIndex >= maxIndex) {
          maxIndex = returnIndex;
        }
        nextReadIndex = jumpIndex;
        continue;

    } else if (codeType == CODE_TYPE::BRANCH_POINT) {
      nextReadIndex++;
      assert(it != trackSequence.end());
      AlphaCode s = *it++;
      if (s == CODE_STOP) {
        AlphaCode x = codeSequence[compareIndex];
        if (x != CODE_STOP) {
          logD("%d %d | %d: no stop found at %d: %016x", subsong, channel, nextReadIndex, compareIndex, x);
          assert(false);
        }
        assert(it == trackSequence.end());
        compareIndex++;
        break;
        
      } else if (s == CODE_SKIP) {
        // skip 1 in data stream 
        nextReadIndex++;
        
      } else if (s == CODE_TAKE_DATA_JUMP) {
        AlphaCode c = compressedCodeSequence[nextReadIndex];
        size_t jumpIndex = GET_CODE_JUMP_INDEX(c);
        assert(CODE_TYPE::JUMP == GET_CODE_TYPE(c));
        if (jumpIndex >= maxIndex) {
          logD("missed goto back to front");
        }
        if (jumpIndex == returnIndex) {
          logD("missed goto back to last");
        }
        returnIndex = nextReadIndex + 1;
        if (returnIndex >= maxIndex) {
          maxIndex = returnIndex;
        }
        logD("goto %d", jumpIndex);
        nextReadIndex = jumpIndex;

      } else if (s == CODE_RETURN_FF) {
        logD("return to front %d", maxIndex);
        nextReadIndex = maxIndex;
        it++;

      } else if (s == CODE_RETURN_LAST) {
        logD("return to last %d", returnIndex);
        nextReadIndex = returnIndex;
        it++;

      } else if (s == CODE_TAKE_TRACK_JUMP) {
        s = *it++;
        size_t jumpIndex = GET_CODE_JUMP_INDEX(s);
        if (jumpIndex >= maxIndex) {
          logD("missed jump back to front");
        }
        if (jumpIndex == returnIndex) {
          logD("missed jump back to last");
        }
        returnIndex = nextReadIndex + 1;
        if (returnIndex >= maxIndex) {
          maxIndex = returnIndex;
        }
        logD("jump to %d", jumpIndex);
        nextReadIndex = jumpIndex;

      } else {
        assert(false);
      }
    } else {
      AlphaCode x = codeSequence[compareIndex];
      if (c != x) {
        logD("%d %d | %d: %08x    %08x",subsong, channel, nextReadIndex-1, compressedCodeSequence[nextReadIndex-1], codeSequence[compareIndex-1]);
        logD("%d %d | %d: %08x <> %08x (%d)",subsong, channel, nextReadIndex, c, x, compareIndex);
        logD("%d %d | %d: %08x    %08x",subsong, channel, nextReadIndex+1, compressedCodeSequence[nextReadIndex+1], codeSequence[compareIndex+1]);
        assert(false);
      }
      nextReadIndex++;
      compareIndex++;
    }
  }
    
  logD("valid at %d/%d", compareIndex, codeSequence.size());
  assert(compareIndex == codeSequence.size());
}

size_t DivExportTIAZip::encodeChannelStateCodes(
  const ChannelState& next,
  const char duration,
  const ChannelState& last,
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
  if (changeControlPredict && cc > 0) {
    // fc = CHANGE_STATE::CHANGE;
    fc = vc = CHANGE_STATE::CHANGE;
  };
  if (changeFrequencyPredict && fc > 0) {
    vc = CHANGE_STATE::CHANGE;
  }

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

