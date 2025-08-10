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
// BETA 
//  - final output schemes
//    - 7800 basic
//        - decoder
//    - zip fixed codes
//        - assembly
//        - tested
//    - zip with huffman and bank switching
//        - assembly
//        - tested
//    - compact with zip 
//        - encoder
//        - decoder
//        - validated
//        - assembly
//        - tested
//  - debugging
//    - proper analytic debug output for TIAZIP spans
//  - glitch
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
  int compressionLevel = conf.getInt("compressionLevel", 2);

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
  writeTrackDataTIAZip(compressionLevel);

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
  STOP,            // end of stream
  WRITE_REGISTERS, // write registers
  VOL_INC,         // increment volume
  VOL_DEC,         // decrement volume
  PAUSE,           // wait for duration
  SUSTAIN,         // sustain for duration
  JUMP,            // jump address
  BRANCH_POINT,    // branch point
  SKIP,            // skip forward to next block
  TAKE_DATA_JUMP,  // jump to next address in data stream
  TAKE_TRACK_JUMP, // jump to next address in track stream
  RETURN_LAST,     // return to last jump point
  RETURN_FF,       // advance to end of stream
  RETURN_NOOP      
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

// Fixed codes
const AlphaCode CODE_STOP = ((AlphaCode) STOP) << 56;
const AlphaCode CODE_WRITE_REGISTERS_MASK = 0xffff00ff00ff0000;
const AlphaCode CODE_WRITE_REGISTERS_000  = 0x0100000000000000; // BUGBUG: HACKY
const AlphaCode CODE_WRITE_REGISTERS_001  = 0x0100000000010000; // BUGBUG: HACKY
const AlphaCode CODE_WRITE_REGISTERS_010  = 0x0100000100000000; // BUGBUG: HACKY
const AlphaCode CODE_WRITE_REGISTERS_011  = 0x0100000100010000; // BUGBUG: HACKY
const AlphaCode CODE_WRITE_REGISTERS_111  = 0x0101000100010000; // BUGBUG: HACKY
const AlphaCode CODE_VOL_INC = ((AlphaCode) VOL_INC) << 56;
const AlphaCode CODE_VOL_DEC = ((AlphaCode) VOL_DEC) << 56;
const AlphaCode CODE_PAUSE_0 = ((AlphaCode) PAUSE) << 56;
const AlphaCode CODE_SUSTAIN_0 = ((AlphaCode) SUSTAIN) << 56;
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
  return (c >> 24) & 0x1f;
}

CHANGE_STATE GET_CODE_WRITE_VC(AlphaCode c) {
  return (CHANGE_STATE) ((c >> 16) & 0xff);
}

unsigned char GET_CODE_WRITE_VX(AlphaCode c) {
  return (c >> 8) & 0x0f;
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
    logD("  %08x -> %d (%s) %d", x.first, x.second, huffmanCode, bitvec.size());
  }

}

// compacted encoding
void DivExportTIAZip::writeTrackDataTIAZip(int compressionLevel) {

  // encode command streams
  size_t totalUncompressedSequenceSize = 0;
  std::map<AlphaCode, size_t> frequencyMap;
  std::vector<AlphaCode> codeSequences[e->song.subsong.size()][2];
  for (size_t subsong = 0; subsong < registerDumps.size(); subsong++) {
    auto registerDump = registerDumps[subsong];
    for (int channel = 0; channel < 2; channel++) {
      auto &codeSequence = codeSequences[subsong][channel];

      // get channel states
      ChannelStateSequence dumpSequence(ChannelState(0), 16);
      registerDump->writeChannelStateSequence(
        channel,
        0,
        -1,
        channel == 0 ? tiaChannel0AddressMap : tiaChannel1AddressMap,
        dumpSequence
      );

      // convert to AlphaCode
      ChannelState last(dumpSequence.initialState);
      for (auto& n: dumpSequence.intervals) {
        encodeChannelStateCodes(n.state, n.duration, last, codeSequence);
        last = n.state;
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
  // debugging: compute basic stats
  // statistics
  logD("total codes : %d ", frequencyMap.size());
  CALC_ENTROPY(frequencyMap);

  // create compressed code sequence
  std::vector<AlphaCode> compressedCodeSequences[e->song.subsong.size()][2];
  std::vector<AlphaCode> spanSequences[e->song.subsong.size()][2];
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < 2; channel += 1) {
      auto &codeSequence = codeSequences[subsong][channel];
      auto &compressedCodeSequence = compressedCodeSequences[subsong][channel];
      auto &spanSequence = spanSequences[subsong][channel];

      compressCodeSequence(
        subsong, 
        channel,
        alphabet,
        index,
        codeSequence,
        compressionLevel,
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
    0x0000,
    4096 * 8
  );

}

void DivExportTIAZip::compressCodeSequence(
  size_t subsong,
  int channel,
  const std::vector<AlphaCode> &alphabet,
  const std::map<AlphaCode, AlphaChar> &index,
  const std::vector<AlphaCode>&codeSequence,
  int compressionLevel,
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

  // greedily find spans to compress with 
  std::vector<Span> spans;
  Span currentSpan((int)subsong, channel, 0, 0);
  Span nextSpan((int)subsong, channel, 0, 0);
  for (size_t i = 0; i < alphaSequence.size(); ) {
    root->find_prior(i, alphaSequence, nextSpan);
    if (compressionLevel > 0 && nextSpan.length > 3) { // BUGBUG: magic number
      // use prior span
      if (currentSpan.length > 0) {
        spans.emplace_back(currentSpan);
      }
      spans.emplace_back(nextSpan);
      size_t nextSpanEnd = nextSpan.start + nextSpan.length;
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
  const std::vector<AlphaCode> (*codeSequences)[2],
  const std::vector<AlphaCode> (*compressedCodeSequences)[2],
  const std::vector<AlphaCode> (*spanSequences)[2],
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
  std::map<AlphaCode, size_t> frequencyFrequencyMap;
  std::map<AlphaCode, size_t> volumeFrequencyMap;
  std::map<AlphaCode, size_t> durationFrequencyMap;
  std::map<AlphaCode, size_t> jumpFrequencyMap; 
  std::map<AlphaCode, size_t> gotoFrequencyMap; 

  size_t totalCompressedCodeSequenceSize = 0;
  size_t totalSpanSequenceSize = 0;
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < 2; channel += 1) {
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
            frequencyFrequencyMap[fx]++;
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
        AlphaCode jumpCode = spanSequence[j];
        CODE_TYPE type = GET_CODE_TYPE(jumpCode);
        if (type == CODE_TYPE::JUMP) {
          jumpFrequencyMap[jumpCode]++;
        } else if (type != CODE_TYPE::RETURN_NOOP) {
          spanFrequencyMap[jumpCode]++;
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
    if (node.second <= 1) {
      continue;
    }
    size_t index = jumpHeap.size();
    if (index > 31) {
      continue;
    }
    jumpMap[node.first] = index;
  }
  logD("jump map size: %d", jumpMap.size());
  SHOW_FREQUENCIES(jumpMap);
  logD("abstract dictionary size: %d", abstractFrequencyMap.size());
  SHOW_FREQUENCIES(abstractFrequencyMap);
  logD("duration dictionary size: %d", durationFrequencyMap.size());
  SHOW_FREQUENCIES(durationFrequencyMap);

  // encode bitstreams
  bool enableHuffmanCodes = true;
  size_t maxHuffmanCodes = 128;
  size_t minWeight = 0;


  logD("code tree");
  std::vector<std::pair<AlphaCode, size_t>> abstractCodebook;
  HuffmanTree *abstractCodeTree = enableHuffmanCodes ?
    buildHuffmanTree(abstractFrequencyMap, maxHuffmanCodes, minWeight, CODE_WRITE_REGISTERS_000, abstractCodebook) : 
    new HuffmanTree(CODE_WRITE_REGISTERS_000, 1);
  std::map<AlphaCode, std::vector<bool>> abstractCodeIndex;
  abstractCodeTree->buildIndex(abstractCodeIndex);
  SHOW_TREE(abstractFrequencyMap, abstractCodeIndex, CODE_WRITE_REGISTERS_000);

  logD("span tree");
  std::vector<std::pair<AlphaCode, size_t>> spanCodebook;
  HuffmanTree *spanTree = buildHuffmanTree(spanFrequencyMap, maxHuffmanCodes, minWeight, 0, spanCodebook);
  std::map<AlphaCode, std::vector<bool>> spanCodeIndex;
  spanTree->buildIndex(spanCodeIndex);
  SHOW_TREE(spanFrequencyMap, spanCodeIndex, 0);

  logD("control tree");
  std::vector<std::pair<AlphaCode, size_t>> controlCodebook;
  HuffmanTree *controlTree = buildHuffmanTree(controlFrequencyMap, maxHuffmanCodes, minWeight, 0, controlCodebook);
  std::map<AlphaCode, std::vector<bool>> controlCodeIndex;
  controlTree->buildIndex(controlCodeIndex);
  SHOW_TREE(controlFrequencyMap, controlCodeIndex, 0);

  logD("frequency tree");
  std::vector<std::pair<AlphaCode, size_t>> frequencyCodebook;
  HuffmanTree *frequencyTree = buildHuffmanTree(frequencyFrequencyMap, maxHuffmanCodes, minWeight, 0, frequencyCodebook);
  std::map<AlphaCode, std::vector<bool>> frequencyCodeIndex;
  frequencyTree->buildIndex(frequencyCodeIndex);
  SHOW_TREE(frequencyFrequencyMap, frequencyCodeIndex, 0);

  logD("volume tree");
  std::vector<std::pair<AlphaCode, size_t>> volumeCodebook;
  HuffmanTree *volumeTree = buildHuffmanTree(volumeFrequencyMap, maxHuffmanCodes, minWeight, 0, volumeCodebook);
  std::map<AlphaCode, std::vector<bool>> volumeCodeIndex;
  volumeTree->buildIndex(volumeCodeIndex);
  SHOW_TREE(volumeFrequencyMap, volumeCodeIndex, 0);

  logD("duration tree");
  std::vector<std::pair<AlphaCode, size_t>> durationCodebook;
  HuffmanTree *durationTree = buildHuffmanTree(durationFrequencyMap, maxHuffmanCodes, minWeight, 0, durationCodebook);
  std::map<AlphaCode, std::vector<bool>> durationCodeIndex;
  durationTree->buildIndex(durationCodeIndex);
  SHOW_TREE(durationFrequencyMap, durationCodeIndex, 0);

  const int addressBits = 15;
  const int addressIndexBits = 5;

  // produce bitstreams
  size_t streamDataOffset = (baseDataOffset << 3);
  Bitstream *dataStreams[e->song.subsong.size()][2];
  Bitstream *trackStreams[e->song.subsong.size()][2];
  std::vector<size_t> jumpAddresses;
  jumpAddresses.resize(jumpMap.size());
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < 2; channel += 1) {
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
        size_t streamPosition = dataStream->position() + streamDataOffset;
        positionMap[i] = streamPosition;
        CODE_TYPE type = GET_CODE_TYPE(c);
        switch (type) {
          case CODE_TYPE::BRANCH_POINT: {
            logD("DATA %d %d - BRANCH_POINT", subsong, channel);
            dataStream->writeBits(abstractCodeIndex.at(CODE_BRANCH_POINT));
            break;
          }

          case CODE_TYPE::TAKE_DATA_JUMP: {
            logD("DATA %d %d - TAKE_DATA_JUMP", subsong, channel);
            dataStream->writeBits(abstractCodeIndex.at(CODE_TAKE_DATA_JUMP));
            break;
          }

          case CODE_TYPE::WRITE_REGISTERS: {
            AlphaCode ac = GET_CODE_WRITE_REGISTERS_MASKED(c); 
            dataStream->writeBits(abstractCodeIndex.at(ac));
            logD("DATA %d %d - WRITE_REGISTERS", subsong, channel);
            CHANGE_STATE cc = GET_CODE_WRITE_CC(c);
            if (cc == CHANGE_STATE::CHANGE) {
              unsigned char cx = GET_CODE_WRITE_CX(c);
              logD("DATA %d %d - CX %d", subsong, channel, cx);
              dataStream->writeBits(controlCodeIndex.at(cx));
            }
            CHANGE_STATE fc = GET_CODE_WRITE_FC(c);
            if (fc == CHANGE_STATE::CHANGE) {
              unsigned char fx = GET_CODE_WRITE_FX(c);
              logD("DATA %d %d - FX %d", subsong, channel, fx);
              dataStream->writeBits(frequencyCodeIndex.at(fx));
            }
            CHANGE_STATE vc = GET_CODE_WRITE_VC(c);
            if (vc == CHANGE_STATE::CHANGE) {
              unsigned char vx = GET_CODE_WRITE_VX(c);
              logD("DATA %d %d - VX %d", subsong, channel, vx);
              dataStream->writeBits(volumeCodeIndex.at(vx));
            }
            // duration always 1
            // unsigned char duration = GET_CODE_WRITE_DURATION(c);
            // dataStream->writeBits(durationCodeIndex.at(duration));
            break;
          }

          case CODE_TYPE::VOL_INC: {
            logD("DATA %d %d - VOL_INC", subsong, channel);
            dataStream->writeBits(abstractCodeIndex.at(CODE_VOL_INC));
            break;
          }

          case CODE_TYPE::VOL_DEC: {
            logD("DATA %d %d - VOL_DEC", subsong, channel);
            dataStream->writeBits(abstractCodeIndex.at(CODE_VOL_DEC));
            break;
          }

          case CODE_TYPE::PAUSE: {
            dataStream->writeBits(abstractCodeIndex.at(CODE_PAUSE_0));
            unsigned char duration = GET_CODE_WRITE_DURATION(c);
            dataStream->writeBits(durationCodeIndex.at(duration));
            logD("DATA %d %d - PAUSE %d", subsong, channel, duration);
            break;
          }

          case CODE_TYPE::SUSTAIN: {
            dataStream->writeBits(abstractCodeIndex.at(CODE_SUSTAIN_0));
            unsigned char duration = GET_CODE_WRITE_DURATION(c);
            dataStream->writeBits(durationCodeIndex.at(duration));
            logD("DATA %d %d - SUSTAIN %d", subsong, channel, duration);
            break;
          }

          case CODE_TYPE::JUMP: {
            size_t address = GET_CODE_JUMP_ADDRESS(c);
            auto ij = jumpMap.find(c);
            if (ij != jumpMap.end()) {
              size_t index = (*ij).second;
              dataStream->writeBit(false); // is lookup
              dataStream->writeBits(index, addressIndexBits);
              logD("DATA %d %d - JUMP TABLE %d", subsong, channel, index);
            } else {
              dataStream->writeBit(true); // no lookup
              dataStreamPointerMap[dataStream->position()] = address;
              dataStream->writeBits(address, addressBits);
              logD("DATA %d %d - JUMP ADDRESS %08x", subsong, channel, address);
            }
            break;
          }

          default:
            logD("bad code %08x", c);
            assert(false);
        }
      }

      for (auto& x : dataStreamPointerMap) {
        dataStream->seek(x.first);
        size_t address = positionMap[x.second];
        dataStream->writeBits(address, addressBits);
        logD("DATA %d %d - REMAP JUMP ADDRESS@%08x: %08x -> %08x", subsong, channel, x.first, x.second, address);
      }

      // produce track stream
      logD("encoding track stream for %d %d", subsong, channel);
      auto &spanSequence = spanSequences[subsong][channel];
      std::map<size_t, size_t> trackStreamPointerMap;
      Bitstream *trackStream = new Bitstream(blockSize);
      trackStreams[subsong][channel] = trackStream;
      for (size_t i = 0; i < spanSequence.size(); i++) {
        AlphaCode s = spanSequence[i];
        if (s == CODE_STOP) {
          logD("SPAN %d %d - STOP", subsong, channel);
          trackStream->writeBits(spanCodeIndex.at(CODE_STOP));

        } else if (s == CODE_RETURN_LAST) {
          logD("SPAN %d %d - RETURN_LAST", subsong, channel);
          trackStream->writeBits(spanCodeIndex.at(CODE_RETURN_LAST));          

        } else if (s == CODE_RETURN_FF) {
          logD("SPAN %d %d - RETURN_FF", subsong, channel);
          trackStream->writeBits(spanCodeIndex.at(CODE_RETURN_FF));
        
        } else if (s == CODE_RETURN_NOOP) {
          // pass

        } else if (s == CODE_SKIP) {
          logD("SPAN %d %d - SKIP", subsong, channel);
          trackStream->writeBits(spanCodeIndex.at(CODE_SKIP));

        } else if (s == CODE_TAKE_DATA_JUMP) {
          logD("SPAN %d %d - DATA_JUMP", subsong, channel);
          trackStream->writeBits(spanCodeIndex.at(CODE_TAKE_DATA_JUMP));

        } else if (s == CODE_TAKE_TRACK_JUMP) {
          logD("SPAN %d %d - TRACK_JUMP", subsong, channel);
          trackStream->writeBits(spanCodeIndex.at(CODE_TAKE_TRACK_JUMP));
          i++;
          s = spanSequence[i];
          auto ij = jumpMap.find(s);
          if (ij != jumpMap.end()) {
            size_t index = (*ij).second;
            trackStream->writeBit(false); // is lookup
            trackStream->writeBits(index, addressIndexBits);
            logD("SPAN %d %d - JUMP TABLE %08x", subsong, channel, index);

          } else {
            size_t address = GET_CODE_JUMP_ADDRESS(s);
            trackStream->writeBit(true); // no lookup
            trackStreamPointerMap[trackStream->position()] = address;
            trackStream->writeBits(address, addressBits);
            logD("SPAN %d %d - JUMP ADDRESS %08x", subsong, channel, address);

          }
        } else {
          logD("bad code %08x", s);
          assert(false);

        }
      }

      for (auto& x : trackStreamPointerMap) {
        trackStream->seek(x.first);
        size_t address = positionMap[x.second];
        trackStream->writeBits(address, addressBits);
        logD("TRACK %d %d - REMAP JUMP ADDRESS@%08x: %08x -> %08x", subsong, channel, x.first, x.second, address);
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

  // validate bitstream
  streamDataOffset = (baseDataOffset << 3);
  std::map<AlphaCode, size_t> jumpDistanceMap;
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < 2; channel += 1) {
      auto &codeSequence = codeSequences[subsong][channel];
      auto dataStream = dataStreams[subsong][channel];
      dataStream->seek(0);
      auto trackStream = trackStreams[subsong][channel];
      trackStream->seek(0);
      size_t i = 0;
      size_t returnAddress = 0;
      size_t maxOffset = 0;
      while (dataStream->hasBits()) {
        size_t streamPosition = dataStream->position();
        AlphaCode code;
        AlphaCode nextCommand = abstractCodeTree->decode(dataStream);

        switch (nextCommand) {
          case CODE_WRITE_REGISTERS_111: {
            CHANGE_STATE cc = CHANGE_STATE::CHANGE;
            unsigned char cx = controlTree->decode(dataStream);

            CHANGE_STATE fc = CHANGE_STATE::CHANGE;
            unsigned char fx = frequencyTree->decode(dataStream);

            CHANGE_STATE vc = CHANGE_STATE::CHANGE;
            unsigned char vx = volumeTree->decode(dataStream);

            code = CODE_WRITE_REGISTERS(cc, cx, fc, fx, vc, vx, 1);
            break;
          }

          case CODE_WRITE_REGISTERS_011: {

            CHANGE_STATE fc = CHANGE_STATE::CHANGE;
            unsigned char fx = frequencyTree->decode(dataStream);
            
            CHANGE_STATE vc = CHANGE_STATE::CHANGE;
            unsigned char vx = volumeTree->decode(dataStream);
  
            code = CODE_WRITE_REGISTERS(CHANGE_STATE::NOOP, 0, fc, fx, vc, vx, 1);
            break;
          }

          case CODE_WRITE_REGISTERS_001: {

            CHANGE_STATE vc = CHANGE_STATE::CHANGE;
            unsigned char vx = volumeTree->decode(dataStream);

            code = CODE_WRITE_REGISTERS(CHANGE_STATE::NOOP, 0, CHANGE_STATE::NOOP, 0, vc, vx, 1);
            break;
          }

          case CODE_WRITE_REGISTERS_010: {

            CHANGE_STATE fc = CHANGE_STATE::CHANGE;
            unsigned char fx = frequencyTree->decode(dataStream);

            code = CODE_WRITE_REGISTERS(CHANGE_STATE::NOOP, 0, fc, fx, CHANGE_STATE::NOOP, 0, 1);
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

          case CODE_BRANCH_POINT: {

            // jump and seek
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

  // SHOW_FREQUENCIES(jumpDistanceMap);

  size_t totalCompressedBytes = 0;

  // write the data streams
  trackData->writeText("\nAUDIO_DATA_OFFSET");
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < 2; channel += 1) {
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
    for (int channel = 0; channel < 2; channel += 1) {
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
      trackData->writeText(fmt::sprintf("\n    byte $%02x", (addr >> 3) & 0xff));
      totalCompressedBytes += 1;
  }
  trackData->writeText(fmt::sprintf("\nAUDIO_JUMP_TABLE_HI_START"));
  for (auto addr : jumpAddresses) {
      trackData->writeText(fmt::sprintf("\n    byte $%02x", ((addr << 4) & 0x70) | ((addr >> 11) & 0x0f)));
      totalCompressedBytes += 1;
  }

  // write control and decoder tables
  trackData->writeText(fmt::sprintf("\nCODEBOOK_LENGTHS = . - 1"));
  totalCompressedBytes += writeCodebookLengths(trackData, "audio_decode_command", abstractCodebook);
  totalCompressedBytes += writeCodebookLengths(trackData, "audio_decode_span", spanCodebook);
  totalCompressedBytes += writeCodebookLengths(trackData, "audio_decode_control", controlCodebook);
  totalCompressedBytes += writeCodebookLengths(trackData, "audio_decode_frequency", frequencyCodebook);
  totalCompressedBytes += writeCodebookLengths(trackData, "audio_decode_volume", volumeCodebook);
  totalCompressedBytes += writeCodebookLengths(trackData, "audio_decode_duration", durationCodebook);

  // codes
  trackData->writeText(fmt::sprintf("\nCODEBOOK_CODES"));
  totalCompressedBytes += writeCommandCodes(trackData, "audio_decode_command", abstractCodebook, abstractCodeIndex);
  totalCompressedBytes += writeCommandCodes(trackData, "audio_decode_span", spanCodebook, spanCodeIndex);
  totalCompressedBytes += writeDataCodes(trackData, "audio_decode_control", controlCodebook, controlCodeIndex);
  totalCompressedBytes += writeDataCodes(trackData, "audio_decode_frequency", frequencyCodebook, frequencyCodeIndex);
  totalCompressedBytes += writeDataCodes(trackData, "audio_decode_volume", volumeCodebook, volumeCodeIndex);
  totalCompressedBytes += writeDataCodes(trackData, "audio_decode_duration", durationCodebook, durationCodeIndex);

  // macros
  writeCodebookMacro(trackData, "audio_decode_command", abstractCodebook);
  if (spanCodebook.size() == 1) {
    // BUGBUG: massive kludge
    trackData->writeText("\n    ; audio_decode_span\n");
    trackData->writeText("    MAC audio_decode_span_MACRO\n");
    trackData->writeText("    lda #<CODE_STOP\n");
    trackData->writeText("    ENDM\n\n");
  } else {
    writeCodebookMacro(trackData, "audio_decode_span", spanCodebook);
  }
  writeCodebookMacro(trackData, "audio_decode_control", controlCodebook);
  writeCodebookMacro(trackData, "audio_decode_frequency", frequencyCodebook);
  writeCodebookMacro(trackData, "audio_decode_volume", volumeCodebook);
  writeCodebookMacro(trackData, "audio_decode_duration", durationCodebook);

  // cleanup
  delete abstractCodeTree;
  delete spanTree;
  delete controlTree;
  delete frequencyTree;
  delete volumeTree;
  delete durationTree;

  trackData->writeText(fmt::sprintf("\n\n; Song data size: %d\n", songDataSize));
  trackData->writeText(fmt::sprintf("; Compressed Code Sequence Length: %d\n", totalCompressedCodeSequenceSize));
  trackData->writeText(fmt::sprintf("; Span Sequence Length: %d\n", totalSpanSequenceSize));
  trackData->writeText(fmt::sprintf("; Compressed Bytes %d\n", totalCompressedBytes));

  output.push_back(DivROMExportOutput("Track_data.asm", trackData));

}

size_t DivExportTIAZip::writeCodebookLengths(
  SafeWriter *w,
  const char *label,
  const std::vector<std::pair<AlphaCode, size_t>> &codebook
) {
  size_t bytesWritten = 0;
  w->writeText(fmt::sprintf("\n%s_LENGTHS = . - CODEBOOK_LENGTHS - 1", label));
  size_t currentLength = 1;
  size_t total = 0;
  for (auto &pair : codebook) {
    if (pair.second == 0) {
      continue;
    }
    while (pair.second > currentLength) {
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

size_t DivExportTIAZip::writeCommandCodes(
  SafeWriter *w,
  const char *label,
  const std::vector<std::pair<AlphaCode, size_t>> &codebook,
  const std::map<AlphaCode, std::vector<bool>> &codeIndex
) {
  size_t bytesWritten = 0;  
  w->writeText(fmt::sprintf("\n%s_CODES = . - CODEBOOK_CODES", label));
  for (auto &pair : codebook) {
    if (pair.second == 0) {
      continue;
    }
    AlphaCode c = pair.first;
    CODE_TYPE type = GET_CODE_TYPE(c);
    String bitcode = "";
    auto it = codeIndex.find(c);
    if (it != codeIndex.end()) {
      auto &bitvec = (*it).second;  
      for (int i = bitvec.size(); --i >= 0; ) {
        bitcode += bitvec.at(i) ? "1" : "0";
      }
    }
    if (c == CODE_BRANCH_POINT) {
      w->writeText(fmt::sprintf("\n    byte <CODE_BRANCH_POINT; %s", bitcode));
    } else if (c == CODE_TAKE_DATA_JUMP) {
      w->writeText(fmt::sprintf("\n    byte <CODE_TAKE_DATA_JUMP; %s", bitcode));
    } else if (c == CODE_TAKE_TRACK_JUMP) {
      w->writeText(fmt::sprintf("\n    byte <CODE_TAKE_TRACK_JUMP; %s", bitcode));
    } else if (c == CODE_STOP) {
      w->writeText(fmt::sprintf("\n    byte <CODE_STOP; %s", bitcode));
    } else if (c == CODE_RETURN_LAST) {
      w->writeText(fmt::sprintf("\n    byte <CODE_RETURN_LAST; %s", bitcode));
    } else if (c == CODE_RETURN_FF) {
      w->writeText(fmt::sprintf("\n    byte <CODE_RETURN_FF; %s", bitcode));        
    } else if (c == CODE_RETURN_NOOP) {
      w->writeText(fmt::sprintf("\n    byte <CODE_RETURN_NOOP; %s", bitcode));
    } else if (c == CODE_SKIP) {
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
    } else if (type == CODE_TYPE::WRITE_REGISTERS) {
      if (c == CODE_WRITE_REGISTERS_001) {
        w->writeText(fmt::sprintf("\n    byte <CODE_WRITE_REGISTERS_001; %s", bitcode));
      } else if (c == CODE_WRITE_REGISTERS_010) {
        w->writeText(fmt::sprintf("\n    byte <CODE_WRITE_REGISTERS_010; %s", bitcode));
      } else if (c == CODE_WRITE_REGISTERS_011) {
        w->writeText(fmt::sprintf("\n    byte <CODE_WRITE_REGISTERS_011; %s", bitcode));
      } else if (c == CODE_WRITE_REGISTERS_111) {
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
  const std::vector<std::pair<AlphaCode, size_t>> &codebook,
  const std::map<AlphaCode, std::vector<bool>> &codeIndex
) {
  size_t bytesWritten = 0;  
  w->writeText(fmt::sprintf("\n%s_CODES = . - CODEBOOK_CODES", label));
  for (auto &pair : codebook) {
    if (pair.second == 0) {
      continue;
    }
    AlphaCode c = pair.first;
    String bitcode = "";
    auto it = codeIndex.find(c);
    if (it != codeIndex.end()) {
      auto &bitvec = (*it).second;  
      for (int i = bitvec.size(); --i >= 0; ) {
        bitcode += bitvec.at(i) ? "1" : "0";
      }
    }
    assert(c < 256);
    w->writeText(fmt::sprintf("\n    byte %d ; %s", pair.first, bitcode));
    bytesWritten +=1;
  }
  return bytesWritten;
}


void DivExportTIAZip::writeCodebookMacro(
  SafeWriter *w,
  const char *label,
  const std::vector<std::pair<AlphaCode, size_t>> &codebook
) {
  w->writeText(fmt::sprintf("\n    ; %s\n", label));
  w->writeText(fmt::sprintf("\n    MAC %s_MACRO\n", label));
  if (codebook.size() == 1) {
    AlphaCode code = codebook.at(0).first;
    w->writeText(fmt::sprintf("    lda #%d\n", code));

  } else {
    w->writeText(fmt::sprintf("    lda #%s_LENGTHS\n", label));
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
      CODE_TYPE spanType = GET_CODE_TYPE(s);
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
        spanType = GET_CODE_TYPE(s);
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
  std::vector<AlphaCode> &out)
{
  // when duration is zero... some kind of rounding issue has happened upstream... we force to 1...
  if (duration == 0) {
      logD("0 duration note");
  }
  int framecount = duration > 0 ? duration : 1;

  unsigned char audcx = next.registers[0];
  CHANGE_STATE cc = audcx != last.registers[0] ? CHANGE_STATE::CHANGE : CHANGE_STATE::NOOP;
  unsigned char audfx = next.registers[1];
  CHANGE_STATE fc = audfx != last.registers[1] ? CHANGE_STATE::CHANGE : CHANGE_STATE::NOOP;
  unsigned char audvx = next.registers[2];
  CHANGE_STATE vc = audvx != last.registers[2] ? CHANGE_STATE::CHANGE : CHANGE_STATE::NOOP;
  char vxd = audvx - last.registers[2];
  // if (audvx == last.registers[2] + 1) {
  //   audvx = 0x10;
  // } else if (last.registers[2] == audvx + 1) {
  //   audvx = 0xf0;
  // }

  // BUGBUG: this is important, a sustain is likely to come after a node
  // maybe not a pause
  unsigned char dx = 1; // framecount > 2 ? 2 : framecount;
  framecount = framecount - dx;

  // BUGBUG: this is also important, seldom make control changes by themselves
  if (cc > 0) {
    fc = vc = CHANGE_STATE::CHANGE;
  };

  size_t codesWritten = 0;
  if (audvx == 0) {
    assert(dx > 0);
    // BUGBUG: PAUSE CAN BE LONGER?
    out.emplace_back(CODE_PAUSE(dx - 1));
    codesWritten++;
  } else if (cc + fc > 0) {
    out.emplace_back(CODE_WRITE_REGISTERS(
      cc,
      cc == CHANGE_STATE::NOOP ? 0 : audcx,
      fc,
      fc == CHANGE_STATE::NOOP ? 0 : audfx,
      vc,
      vc == CHANGE_STATE::NOOP ? 0 : audvx,
      dx
    ));
    codesWritten++;
  } else if (vc > 0) {
    if (vxd == -1) {
      out.emplace_back(CODE_VOL_DEC);
    } else if (vxd == 1) {
      out.emplace_back(CODE_VOL_INC);
    } else {
      out.emplace_back(CODE_WRITE_REGISTERS(cc, 0, fc, 0, vc, audvx, dx));
    }
    codesWritten++;
  }

  while (framecount > 0) {
    unsigned char dx = framecount > 16 ? 16 : framecount;
    framecount = framecount - dx;
    out.emplace_back(CODE_SUSTAIN(dx - 1));
    codesWritten++;
  } 

  return codesWritten;
}

void DivExportTIAZip::writeWaveformHeader(SafeWriter* w, const char * key) {
  w->writeText(fmt::sprintf("%s_ADDR\n", key));
}

