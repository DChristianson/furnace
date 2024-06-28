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
#include "suffixTree.h"
#include "huffman.h"

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
//    - encoding schemes
//        - JUMP (on 0, check jump stream)
//        - GOTO (on 0, check jump stream, on 0xf8 go straight to next)
//        - FORK (on 0x??, read bit off jump stream)
//        - try separating sustain/pause commands in data stream (hardcoded option)
//        - trial huffman code data stream
//          - construct huffman tree
//          - perform encoding of tree and stream
//    - successful decoder in assembly
//  - compression goals
//    - Coconut... small in 4k
//    - breakbeat in 4k
//    - test compression in 4k
// BETA 
//  - the good compression
//    - encoding schemes
//        - POP back to front scheme (on 0x??, read from stack )
//        - trial huffman code data stream
//          - try restricting to limited vocab
//          - try compressing jumps as well
//          - try encoding raw bytes
//          - create test decoder
//          - write 6502 decoder
//        - trial instrument / waveform scheme
//        - just use actual zip or 7z with augments?
//          - need low memory 6502 decoder
//    - use estimate of compression savings to guide span choice
//  - debugging
//    - proper analytic debug output for TIAZIP spans
//  - compression goals
//    - Coconut_Mall in 4k  
//  - other output schemes
//    - batari basic player
//  - testability
//    - all targets test
//    - fix multi-song test
//    - clean up test output
//  - dev help
//    - docs on how to use multiple schemes
//    - makefile aware of song size / compression
//  - usability
//    - documentation
//      - BASIC scheme
//      - TIACOMP scheme
//      - TIAZIP scheme
//    - select target formats (asm, basic, rom)
//    - EZ mode - either select appropriate player (mini, etc) / or do bank switching by default
//  - standalone tiazip tool
// STRETCH
//  - moar output schemes
//    - bank switching
//    - 7800 support
//    - Atari 8-bit exports
//    - DPC+ export
// 

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

DivExportAtari2600::DivExportAtari2600(DivEngine *e) {
  String exportTypeString = e->getConfString("romout.tiaExportType", "FSEQ");
  logD("retrieving config exportType [%s]", exportTypeString);
  // BUGBUG: cleanse and normalize
  if (exportTypeString == "RAW") {
    exportType = DIV_EXPORT_TIA_RAW;
  } else if (exportTypeString == "BASIC") {
    exportType = DIV_EXPORT_TIA_BASIC;
  } else if (exportTypeString == "BASIC_RLE") {
    exportType = DIV_EXPORT_TIA_BASIC_RLE;
  } else if (exportTypeString == "TIACOMP") {
    exportType = DIV_EXPORT_TIA_TIACOMP;
  } else if (exportTypeString == "FSEQ") {
    exportType = DIV_EXPORT_TIA_FSEQ;
  } else if (exportTypeString == "TIAZIP") {
    exportType = DIV_EXPORT_TIA_TIAZIP;
  }
  debugRegisterDump = e->getConfBool("romout.debugOutput", false);
}

std::vector<DivROMExportOutput> DivExportAtari2600::go(DivEngine* e) {
  std::vector<DivROMExportOutput> ret;

  // get register dump
  const size_t numSongs = e->song.subsong.size();
  std::vector<RegisterWrite> registerWrites[numSongs];
  for (size_t subsong = 0; subsong < numSongs; subsong++) {
    registerDump(e, (int) subsong, registerWrites[subsong]);  
  }
  if (debugRegisterDump) {
      writeRegisterDump(e, registerWrites, ret);
  }

  // write track data
  switch (exportType) {
    case DIV_EXPORT_TIA_RAW:
      writeTrackDataRaw(e, true, registerWrites, ret);
      break;
    case DIV_EXPORT_TIA_BASIC:
      writeTrackDataBasic(e, false, true, registerWrites, ret);
      break;
    case DIV_EXPORT_TIA_BASIC_RLE:
      writeTrackDataBasic(e, true, true, registerWrites, ret);
      break;
    case DIV_EXPORT_TIA_TIACOMP:
      writeTrackDataTIAComp(e, registerWrites, ret);
      break;
    case DIV_EXPORT_TIA_FSEQ:
      writeTrackDataFSeq(e, registerWrites, ret);
      break;
    case DIV_EXPORT_TIA_TIAZIP:
      writeTrackDataTIAZip(e, registerWrites, ret);
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

void DivExportAtari2600::writeRegisterDump(
  DivEngine* e, 
  std::vector<RegisterWrite> *registerWrites,
  std::vector<DivROMExportOutput> &ret
) {
  // dump all register writes
  SafeWriter* dump = new SafeWriter;
  dump->init();
  dump->writeText(fmt::sprintf("; Song: %s\n", e->song.name));
  dump->writeText(fmt::sprintf("; Author: %s\n", e->song.author));

  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    int maxFrames = 0;

    dump->writeText(fmt::sprintf("\n; Song %d\n", subsong));

    for (auto &write : registerWrites[subsong]) {

      int currentTicks = write.ticks;
      int currentSeconds = write.seconds;
      int freq = ((float)TICKS_PER_SECOND) / write.hz;

      int totalTicks = currentTicks  + 
        (TICKS_PER_SECOND * currentSeconds);
      int totalFrames = totalTicks / freq;
      int totalFramesR = totalTicks - (totalFrames * freq);
      if (totalFrames > maxFrames) {
        maxFrames = totalFrames;
      }

      dump->writeText(fmt::sprintf("; %d T%d.%d F%d.%d: SS%d ORD%d ROW%d SYS%d> %d = %d\n",
        write.writeIndex,
        write.seconds,
        write.ticks,
        totalFrames,
        totalFramesR,
        write.rowIndex.subsong,
        write.rowIndex.ord,
        write.rowIndex.row,
        write.systemIndex,
        write.addr,
        write.val
      ));
    }

    dump->writeText("\n");
    dump->writeText(fmt::sprintf("; Writes: %d\n", registerWrites[subsong].size()));
    dump->writeText(fmt::sprintf("; Frames: %d\n", maxFrames));
    dump->writeText("\n");

  }

  ret.push_back(DivROMExportOutput("RegisterDump.txt", dump));

}

// simple register dump
void DivExportAtari2600::writeTrackDataRaw(
  DivEngine* e, 
  bool encodeDuration,
  std::vector<RegisterWrite> *registerWrites,
  std::vector<DivROMExportOutput> &ret
) {

  SafeWriter* trackData=new SafeWriter;
  trackData->init();
  trackData->writeText(fmt::sprintf("; Song: %s\n", e->song.name));
  trackData->writeText(fmt::sprintf("; Author: %s\n", e->song.author));

  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < 2; channel++) {
      ChannelStateSequence dumpSequence;

      writeChannelStateSequence(
        registerWrites[subsong],
        (int) subsong,
        channel,
        0,
        -1,
        channel == 0 ? channel0AddressMap : channel1AddressMap,
        dumpSequence
      );

      size_t waveformDataSize = 0;
      size_t totalFrames = 0;
      trackData->writeC('\n');
      trackData->writeText(fmt::sprintf("TRACK_%d_CHANNEL_%d\n", subsong, channel));
      if (encodeDuration) {
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
      } else {
        for (auto& n: dumpSequence.intervals) {
          for (size_t i = n.duration; i > 0; i++) {
            trackData->writeText(fmt::sprintf("    byte %d, %d, %d\n",
              n.state.registers[0],
              n.state.registers[1],
              n.state.registers[2]
            ));
            waveformDataSize += 4;
            totalFrames += 1;
          }
        }
      }
      trackData->writeText("    byte 0\n");
      waveformDataSize++;
      trackData->writeText(fmt::sprintf("    ; %d bytes %d frames", waveformDataSize, totalFrames));
    }
  }

  ret.push_back(DivROMExportOutput("Track_data.asm", trackData));

}

// simple register dump with separate tables for frequency and control / volume
void DivExportAtari2600::writeTrackDataBasic(
  DivEngine* e,
  bool encodeDuration,
  bool independentChannelPlayback,
  std::vector<RegisterWrite> *registerWrites,
  std::vector<DivROMExportOutput> &ret
) {
  size_t numSongs = e->song.subsong.size();

  // write track audio data
  SafeWriter* trackData = new SafeWriter;
  trackData->init();
  trackData->writeText("; Furnace Tracker audio data file\n");
  trackData->writeText("; Basic data format\n");
  trackData->writeText(fmt::sprintf("; Song: %s\n", e->song.name));
  trackData->writeText(fmt::sprintf("; Author: %s\n", e->song.author));

  trackData->writeText(fmt::sprintf("\nAUDIO_NUM_TRACKS = %d\n", numSongs));

  if (encodeDuration) {
    trackData->writeText("\n#include \"cores/basicx_player_core.asm\"\n");
  } else {
    trackData->writeText("\n#include \"cores/basic_player_core.asm\"\n");
  }

  // create a lookup table (for use in player apps)
  size_t songDataSize = 0;
  if (independentChannelPlayback) {
    // one track table per channel
    for (int channel = 0; channel < 2; channel++) {
      trackData->writeText(fmt::sprintf("AUDIO_TRACKS_%d:\n", channel));
      for (size_t subsong = 0; subsong < numSongs; subsong++) {
        trackData->writeText(fmt::sprintf("    byte AUDIO_TRACK_%d_%d\n", subsong, channel));
        songDataSize += 1;
      }
    }

  } else {
    // one track table for both channels
    trackData->writeText("AUDIO_TRACKS\n");
    for (size_t i = 0; i < e->song.subsong.size(); i++) {
      trackData->writeText(fmt::sprintf("    byte AUDIO_TRACK_%d\n", i));
      songDataSize += 1;
    }

  }

  // dump sequences
  size_t sizeOfAllSequences = 0;
  size_t sizeOfAllSequencesPerChannel[2] = {0, 0};
  ChannelStateSequence dumpSequences[numSongs][2];
  for (size_t subsong = 0; subsong < numSongs; subsong++) {
    for (int channel = 0; channel < 2; channel++) {
      // limit to 1 frame per note
      dumpSequences[subsong][channel].maxIntervalDuration = encodeDuration ? 8 : 1;
      writeChannelStateSequence(
        registerWrites[subsong],
        (int) subsong,
        channel,
        0,
        -1,
        channel == 0 ? channel0AddressMap : channel1AddressMap,
        dumpSequences[subsong][channel]
      );
      size_t totalDataPointsThisSequence = dumpSequences[subsong][channel].size() + 1;
      sizeOfAllSequences += totalDataPointsThisSequence;
      sizeOfAllSequencesPerChannel[channel] += totalDataPointsThisSequence;
    }
  }

  if (independentChannelPlayback) {
    // channels do not have to be synchronized, can be played back independently
    if (sizeOfAllSequences > 256) {
      String msg = fmt::sprintf(
        "cannot export data in this format: data sequence has %d > 256 data points",
        sizeOfAllSequences
      );
      logE(msg.c_str());
      throw new std::runtime_error(msg);
    }
  } else {
    // data for each channel locked to same index
    if (sizeOfAllSequencesPerChannel[0] != sizeOfAllSequencesPerChannel[1]) {
      String msg = fmt::sprintf(
        "cannot export data in this format: channel data sequence lengths [%d, %d] do not match",
        sizeOfAllSequencesPerChannel[0],
        sizeOfAllSequencesPerChannel[1]
      );
      logE(msg.c_str());
      throw new std::runtime_error(msg);
    }
    if (sizeOfAllSequencesPerChannel[0] > 256) {
      String msg = fmt::sprintf(
        "cannot export data in this format: data sequence has %d > 256 data points",
        sizeOfAllSequencesPerChannel[0]
      );
      logE(msg.c_str());
      throw new std::runtime_error(msg);
    }
  }

  // Frequencies table
  size_t freqTableSize = 0;
  trackData->writeText("\n    ; FREQUENCY TABLE\n");
  if (independentChannelPlayback) {
    trackData->writeText("AUDIO_F:\n");
  }
  for (int channel = 0; channel < 2; channel++) {
    if (!independentChannelPlayback) {
      trackData->writeText(fmt::sprintf("AUDIO_F_%d:\n", channel));
    }
    for (size_t subsong = 0; subsong < numSongs; subsong++) {
      trackData->writeText(fmt::sprintf("    ; TRACK %d, CHANNEL %d\n", subsong, channel));
      if (independentChannelPlayback) {
        trackData->writeText(fmt::sprintf("AUDIO_TRACK_%d_%d = . - AUDIO_F + 1", subsong, channel));
      } else if (channel == 0) {
        trackData->writeText(fmt::sprintf("AUDIO_TRACK_%d = . - AUDIO_F%d + 1", subsong, channel));
      }
      size_t i = 0;
      for (auto& n: dumpSequences[subsong][channel].intervals) {
        if (i % 16 == 0) {
          trackData->writeText("\n    byte ");
        } else {
          trackData->writeText(",");
        }
        i++;
        unsigned char fx = n.state.registers[1];
        unsigned char dx = n.duration > 0 ? n.duration - 1 : 0;
        unsigned char rx = dx << 5 | fx;
        trackData->writeText(fmt::sprintf("%d", rx));
        freqTableSize += 1;
      }
      trackData->writeText(fmt::sprintf("\n    byte 0;\n"));
      freqTableSize += 1;
    }
  }

  // Control-volume table
  size_t cvTableSize = 0;
  trackData->writeText("\n    ; CONTROL/VOLUME TABLE\n");
  if (independentChannelPlayback) {
    trackData->writeText("AUDIO_CV:\n");
  }
  for (int channel = 0; channel < 2; channel++) {
    if (!independentChannelPlayback) {
      trackData->writeText(fmt::sprintf("AUDIO_CV_%d:\n", channel));
    }
    for (size_t subsong = 0; subsong < numSongs; subsong++) {
      trackData->writeText(fmt::sprintf("    ; TRACK %d, CHANNEL %d", subsong, channel));
      size_t i = 0;
      for (auto& n: dumpSequences[subsong][channel].intervals) {
        if (i % 16 == 0) {
          trackData->writeText("\n    byte ");
        } else {
          trackData->writeText(",");
        }
        i++;
        unsigned char cx = n.state.registers[0];
        unsigned char vx = n.state.registers[2];
        // if volume is zero, make cx nonzero
        unsigned char rx = (vx == 0 ? 0xf0 : cx << 4) | vx; 
        trackData->writeText(fmt::sprintf("%d", rx));
        cvTableSize += 1;
      }
      trackData->writeText(fmt::sprintf("\n    byte 0;\n"));
      cvTableSize += 1;
    }
  }

  trackData->writeC('\n');
  trackData->writeText(fmt::sprintf("; Num Tracks %d\n", numSongs));
  trackData->writeText(fmt::sprintf("; All Tracks Sequence Length %d\n", sizeOfAllSequences));
  trackData->writeText(fmt::sprintf("; Track Table Size %d\n", songDataSize));
  trackData->writeText(fmt::sprintf("; Freq Table Size %d\n", freqTableSize));
  trackData->writeText(fmt::sprintf("; CV Table Size %d\n", cvTableSize));
  size_t totalDataSize = songDataSize + freqTableSize + cvTableSize;
  trackData->writeText(fmt::sprintf("; Total Data Size %d\n", totalDataSize));

  ret.push_back(DivROMExportOutput("Track_data.asm", trackData));

}

// Compact delta encoding
void DivExportAtari2600::writeTrackDataTIAComp(
  DivEngine* e,
  std::vector<RegisterWrite> *registerWrites,
  std::vector<DivROMExportOutput> &ret
) {
  size_t numSongs = e->song.subsong.size();

  // write track audio data
  SafeWriter* trackData = new SafeWriter;
  trackData->init();
  trackData->writeText("; Furnace Tracker audio data file\n");
  trackData->writeText("; TIAComp delta encoding\n");
  trackData->writeText(fmt::sprintf("; Song: %s\n", e->song.name));
  trackData->writeText(fmt::sprintf("; Author: %s\n", e->song.author));

  trackData->writeText(fmt::sprintf("\nAUDIO_NUM_TRACKS = %d\n", numSongs));
  
  trackData->writeText("\n#include \"cores/tiacomp_player_core.asm\"\n");

  // create a lookup table for use in player apps
  size_t songDataSize = 0;
  // one track table per channel
  for (int channel = 0; channel < 2; channel++) {
    trackData->writeText(fmt::sprintf("AUDIO_TRACKS_%d:\n", channel));
    for (size_t subsong = 0; subsong < numSongs; subsong++) {
      trackData->writeText(fmt::sprintf("    byte AUDIO_TRACK_%d_%d\n", subsong, channel));
      songDataSize += 1;
    }
  }

  // dump sequences
  size_t trackDataSize = 0;
  trackData->writeText("AUDIO_DATA:\n");
  for (size_t subsong = 0; subsong < numSongs; subsong++) {
    for (int channel = 0; channel < 2; channel++) {
      ChannelStateSequence dumpSequence;
      writeChannelStateSequence(
        registerWrites[subsong],
        (int) subsong,
        channel,
        0,
        -1,
        channel == 0 ? channel0AddressMap : channel1AddressMap,
        dumpSequence
      );
      trackData->writeText(fmt::sprintf("AUDIO_TRACK_%d_%d = . - AUDIO_DATA + 1\n", subsong, channel));
      ChannelState last(dumpSequence.initialState);
      std::vector<unsigned char> codeSeq;
      for (auto& n: dumpSequence.intervals) {
        codeSeq.clear();
        trackData->writeText(
          fmt::sprintf(
            "    ;F%d C%d V%d D%d\n",
            n.state.registers[1],
            n.state.registers[0],
            n.state.registers[2],
            n.duration
          )
        );
        encodeChannelState(n.state, n.duration, last, true, codeSeq);
        trackDataSize += codeSeq.size();
        trackData->writeText("    byte ");
        for (size_t i = 0; i < codeSeq.size(); i++) {
          if (i > 0) {
            trackData->writeC(',');
          }
          trackData->writeText(fmt::sprintf("%d", codeSeq[i]));
        }
        trackData->writeC('\n');
        last = n.state;
      }
      trackData->writeText("    byte 0\n");
      trackDataSize++;
    }
  }

  trackData->writeC('\n');
  trackData->writeText(fmt::sprintf("; Num Tracks %d\n", numSongs));
  trackData->writeText(fmt::sprintf("; Track Table Size %d\n", songDataSize));
  trackData->writeText(fmt::sprintf("; Data Table Size %d\n", trackDataSize));
  size_t totalDataSize = songDataSize + trackDataSize;
  trackData->writeText(fmt::sprintf("; Total Data Size %d\n", totalDataSize));

  ret.push_back(DivROMExportOutput("Track_data.asm", trackData));

}

// furnace sequence encoding
void DivExportAtari2600::writeTrackDataFSeq(
  DivEngine* e, 
  std::vector<RegisterWrite> *registerWrites,
  std::vector<DivROMExportOutput> &ret
) {

  // convert to state sequences
  logD("performing sequence capture");
  std::vector<String> channelSequences[2];
  std::map<String, ChannelStateSequence> registerDumps;
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < 2; channel++) {
      writeChannelStateSequenceByRow(
        registerWrites[subsong],
        (int) subsong,
        channel,
        0,
        2,
        channel == 0 ? channel0AddressMap : channel1AddressMap,
        channelSequences[channel],
        registerDumps);
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

  trackData->writeText("\n#include \"cores/fseq_player_core.asm\"\n");

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
    std::vector<unsigned char> codeSeq;
    int totalDuration = 0;
    for (auto& n: dump.intervals) {
      codeSeq.clear();
      trackData->writeText(
        fmt::sprintf(
          "    ;F%d C%d V%d D%d\n",
          n.state.registers[1],
          n.state.registers[0],
          n.state.registers[2],
          n.duration
        )
      );
      encodeChannelState(n.state, n.duration, last, true, codeSeq);
      waveformDataSize += codeSeq.size();
      trackData->writeText("    byte ");
      for (size_t i = 0; i < codeSeq.size(); i++) {
        if (i > 0) {
          trackData->writeC(',');
        }
        trackData->writeText(fmt::sprintf("%d", codeSeq[i]));
      }
      trackData->writeC('\n');
      totalDuration += n.duration;
      last = n.state;
    }
    trackData->writeText("    byte 0\n");
    trackData->writeText(fmt::sprintf("    ;Total Duration = %d\n", totalDuration));
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

enum CODE_TYPE {
  POP,
  LITERAL,
  JUMP,
  GOTO,
  SKIP,
  RETURN
};

// BUGBUG: make macro/inline
AlphaCode CODE_STATE_LITERAL(const ChannelStateInterval &c) {
  return (AlphaCode) (
    (uint64_t) c.state.registers[0] << 24 |
    (uint64_t) c.state.registers[1] << 16 |
    (uint64_t) c.state.registers[2] << 8  |
    (uint64_t) c.duration
  );
}

AlphaCode CODE_EMPTY_LITERAL = ((AlphaCode) LITERAL << 56);

AlphaCode CODE_RETURN = ((AlphaCode) RETURN << 56);

AlphaCode CODE_DELTA_LITERAL(const std::vector<unsigned char> &codeSeq) {
  AlphaCode c = 0;
  for (size_t i = codeSeq.size(); i-- != 0; ) {
    c = (c << 8) | codeSeq.at(i);
  }
  return ((AlphaCode) LITERAL << 56) | (codeSeq.size() << 48) | c;
}

// BUGBUG: make macro/inline
AlphaCode CODE_JUMP(size_t index) {
  return ((AlphaCode)JUMP << 56) | index;
}

AlphaCode CODE_GOTO(size_t index) {
  return ((AlphaCode)GOTO << 56) | index;
}

AlphaCode CODE_SKIP(bool skip) {
  return ((AlphaCode)SKIP << 56) | (skip ? 1 : 0);
}

CODE_TYPE GET_CODE_TYPE(const AlphaCode code) {
  return (CODE_TYPE)(code >> 56);
}

bool GET_CODE_SKIP(AlphaCode code) {
  return (code & 0xff) > 0;
}

size_t GET_CODE_SIZE(const AlphaCode c) {
  return (c >> 48) & 0xff;
}

unsigned char GET_CODE_BYTE(AlphaCode c, size_t index) {
  return (c >> (8 * index)) & 0xff;
}

size_t GET_CODE_ADDRESS(const AlphaCode c) {
  return c & 0xffff;
}

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

// compacted encoding
void DivExportAtari2600::writeTrackDataTIAZip(
  DivEngine* e, 
  std::vector<RegisterWrite> *registerWrites,
  std::vector<DivROMExportOutput> &ret
) {
  size_t numSongs = e->song.subsong.size();

  // write track audio data
  SafeWriter* trackData = new SafeWriter;
  trackData->init();
  trackData->writeText("; Furnace Tracker audio data file\n");
  trackData->writeText("; TIAZip data format\n");
  trackData->writeText(fmt::sprintf("; Song: %s\n", e->song.name));
  trackData->writeText(fmt::sprintf("; Author: %s\n", e->song.author));

  trackData->writeText(fmt::sprintf("\nAUDIO_NUM_TRACKS = %d\n", numSongs));

  trackData->writeText("\n#include \"cores/tiazip_player_core.asm\"\n");

  // create a lookup table for use in player apps
  size_t songDataSize = 0;
  // one track table for all channels
  trackData->writeText("AUDIO_TRACKS:\n");
  for (size_t subsong = 0; subsong < numSongs; subsong++) {
    // note reverse order for copy routine
    trackData->writeText(fmt::sprintf("    byte >JUMPS_S%d_C1_START, <JUMPS_S%d_C1_START\n", subsong, subsong));
    trackData->writeText(fmt::sprintf("    byte >JUMPS_S%d_C0_START, <JUMPS_S%d_C0_START\n", subsong, subsong));
    trackData->writeText(fmt::sprintf("    byte >SPANS_S%d_C1_START, <SPANS_S%d_C1_START\n", subsong, subsong));
    trackData->writeText(fmt::sprintf("    byte >SPANS_S%d_C0_START, <SPANS_S%d_C0_START\n", subsong, subsong));
    songDataSize += 8;
  }

  // encode command streams
  size_t totalUncompressedCodes = 0;
  size_t totalUncompressedBytes = 0;
  std::map<AlphaCode, size_t> frequencyMap;
  std::vector<AlphaCode> codeSequences[e->song.subsong.size()][2];
  for (size_t subsong = 0; subsong < numSongs; subsong++) {
    for (int channel = 0; channel < 2; channel++) {
      // get channel states
      ChannelStateSequence dumpSequence(ChannelState(0), 16);
      writeChannelStateSequence(
        registerWrites[subsong],
        (int) subsong,
        channel,
        0,
        -1,
        channel == 0 ? channel0AddressMap : channel1AddressMap,
        dumpSequence
      );

      SafeWriter* binaryData = new SafeWriter;
      binaryData->init();

      // convert to AlphaCode
      bool multipleCommandEncoding = true; // BUGBUG: make configurable
      ChannelState last(dumpSequence.initialState);
      std::vector<unsigned char> codeSeq;
      for (auto& n: dumpSequence.intervals) {
        int duration = n.duration;
        do {
          codeSeq.clear();
          duration = encodeChannelState(n.state, duration, last, multipleCommandEncoding, codeSeq);
          assert(codeSeq.size() > 0);
          for (auto x : codeSeq) {
            totalUncompressedBytes++;
            binaryData->writeC(x);
          }
          AlphaCode c = CODE_DELTA_LITERAL(codeSeq);
          totalUncompressedCodes += codeSeq.size();
          frequencyMap[c]++;
          codeSequences[subsong][channel].emplace_back(c);
          last = n.state;
        } while (duration > 0);
      }
      totalUncompressedCodes++;
      frequencyMap[0]++;
      codeSequences[subsong][channel].emplace_back(0);

      ret.push_back(DivROMExportOutput(fmt::sprintf("Track_binary.%d.%d.o", subsong, channel), binaryData));
    }
  }

  // using the initial frequency map, index all distinct codes into an "alphabet" and build a suffix tree
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

  // zero out frequency map to recalculate
  for (auto &x: frequencyMap) {
    x.second = 0;
  }

  // create compressed code sequence
  size_t totalCompressedCodes = 0;
  size_t totalCompressedJumps = 0;
  std::vector<AlphaCode> compressedCodeSequences[e->song.subsong.size()][2];
  std::vector<AlphaCode> jumpSequences[e->song.subsong.size()][2];
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < 2; channel += 1) {
      auto &codeSequence = codeSequences[subsong][channel];
      std::vector<AlphaChar> alphaSequence;
      alphaSequence.reserve(codeSequence.size());         

      // copy string into alphabet
      for (auto code : codeSequences[subsong][channel]) {
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
      
      // branchFrequencyMap[i] -> frequency of branches from alphaSequence[i]
      std::vector<std::map<size_t, size_t>> branchFrequencyMap; 
      branchFrequencyMap.resize(alphaSequence.size());

      // greedily find spans to compress with 
      std::vector<Span> spans;
      Span currentSpan((int)subsong, channel, 0, 0);
      Span nextSpan((int)subsong, channel, 0, 0);
      for (size_t i = 0; i < alphaSequence.size(); ) {
        root->find_prior(i, alphaSequence, nextSpan);
        if (nextSpan.length > 5) { // BUGBUG: do trial compression
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
        spans.emplace_back(currentSpan);
      }

      // prune all the trivial branch frequencies
      std::vector<size_t> skipMap;
      skipMap.resize(branchFrequencyMap.size(), 0);
      for (size_t i = 0; i < branchFrequencyMap.size(); i++) {
        auto &branchFrequencies = branchFrequencyMap[i];
        if (branchFrequencies.size() < 2) {
          branchFrequencies.clear();
        }
        size_t maxFreq = 0;
        size_t skipIndex = 0;
        for (auto &x: branchFrequencies) {
          if (x.second > maxFreq) {
            maxFreq = x.second;
            skipIndex = x.first;
          }
        }
        skipMap[i] = skipIndex;
      }
 
      // no longer need suffix tree
      delete root;

      // create compressed sequence
      auto &compressedCodeSequence = compressedCodeSequences[subsong][channel];
      compressedCodeSequence.reserve(alphaSequence.size());
      auto &jumpSequence = jumpSequences[subsong][channel];
      jumpSequence.reserve(alphaSequence.size());
      std::vector<size_t> labels;
      labels.resize(alphaSequence.size());
      size_t lastSpanEnd = 0;
      for (auto &span: spans) {
        if (lastSpanEnd > span.start) {
          // traverse prior span
          logD("%d: traversing prior %d - %d", lastSpanEnd, span.start, span.length);
          for (size_t i = 0; i < span.length; i++) {
            size_t leftmostCodeAddr = copyMap[lastSpanEnd];
            logD("%d: ... ", lastSpanEnd);
            lastSpanEnd++;
            size_t nextCodeAddr = copyMap[lastSpanEnd];
            if (branchFrequencyMap[leftmostCodeAddr].size() > 0) {
              // decide if this is a skip or a take
              size_t skipAddr = skipMap[leftmostCodeAddr];
              bool skip = nextCodeAddr == skipAddr;
              jumpSequence.emplace_back(CODE_SKIP(skip));
              if (skip) {
                logD("%d: skip jump @ %d to %d", lastSpanEnd, leftmostCodeAddr, skipAddr);
              } else {
                logD("%d: taking jump @ %d to %d", lastSpanEnd, leftmostCodeAddr, nextCodeAddr);
                jumpSequence.emplace_back(CODE_JUMP(nextCodeAddr));  
              }
            }
          }
          
        } else {
          // encode literal span
          logD("%d: encoding current %d - %d", lastSpanEnd, span.start, span.length);
          for (size_t i = span.start; i < span.start + span.length; i++) {
            AlphaCode c = codeSequence[i];
            labels[i] = compressedCodeSequence.size();
            compressedCodeSequence.emplace_back(c);
            logD("%d: adding code %08x", i, c);
            if (i + 1 < alphaSequence.size()) {
              size_t nextCodeAddr = copyMap[i+1];
              if (branchFrequencyMap[i].size() > 0) {
                // jump table
                compressedCodeSequence.emplace_back(0); // BUGBUG: CODE_POP
                auto &jumps = branchFrequencyMap.at(i);
                for (auto &x: jumps) {
                  if (x.first == nextCodeAddr) {
                    logD("%d: -> %d (freq %d)*", i, x.first, x.second);
                  } else {
                    logD("%d: -> %d (freq %d)", i, x.first, x.second);
                  }
                }
                size_t skipAddr = skipMap[i];
                // decide if the immediate jump is a skip or take
                bool skip = nextCodeAddr == skipAddr;
                jumpSequence.emplace_back(CODE_SKIP(skip));
                if (skip) {
                  logD("%d: skip jump @ %d to %d", i, lastSpanEnd, nextCodeAddr);
                } else {
                  logD("%d: take jump @ %d to %d", i, lastSpanEnd, nextCodeAddr);
                  jumpSequence.emplace_back(CODE_JUMP(nextCodeAddr));  
                }
                if (skipAddr != (i + 1)) {
                  logD("%d: goto %d", i, skipAddr);
                  compressedCodeSequence.emplace_back(CODE_GOTO(skipAddr));
                }

              } else if (nextCodeAddr != (i + 1)) {
                // straight GOTO
                // BUGBUG: could be inline return?
                logD("%d: goto %d", i, nextCodeAddr);
                compressedCodeSequence.emplace_back(CODE_GOTO(nextCodeAddr));

              }
            } else {
              jumpSequence.emplace_back(0);
            }
            lastSpanEnd++;
          }
        }

      }

      // rewrite gotos, update frequency map
      for (size_t i = 0; i < compressedCodeSequence.size(); i++) {
        AlphaCode c = compressedCodeSequence[i];
        CODE_TYPE type = GET_CODE_TYPE(c);
        if (type == CODE_TYPE::GOTO) {
          size_t address = labels[GET_CODE_ADDRESS(c)];
          c = CODE_GOTO(address);
          compressedCodeSequence[i] = c;
          frequencyMap[c] += 2;
        } else if (type == CODE_TYPE::LITERAL && GET_CODE_SIZE(c) >= 2) {
          frequencyMap[c] += 2;
        } else {
          frequencyMap[c]++;
        }
      }
      totalCompressedCodes += compressedCodeSequence.size();

      // rewrite jumps 
      for (size_t i = 0; i < jumpSequence.size(); i++) {
        AlphaCode c = jumpSequence[i];
        if (GET_CODE_TYPE(c) == CODE_TYPE::JUMP) {
          size_t address = labels[GET_CODE_ADDRESS(c)];
          c = CODE_JUMP(address);
          jumpSequence[i] = c;
        }
      }
      totalCompressedJumps += jumpSequence.size();

      // rewrite returns
      size_t returnAddress;
      for (size_t i = 0, j = 0; i < compressedCodeSequence.size(); ) {
        AlphaCode c = compressedCodeSequence[i];
        CODE_TYPE type = GET_CODE_TYPE(c);
        if (type == CODE_TYPE::POP) {
          AlphaCode jump = jumpSequence[j++];
          CODE_TYPE jumpType = GET_CODE_TYPE(jump);
          if (jumpType == CODE_TYPE::POP) {
            break;

          } else if (jumpType == CODE_TYPE::SKIP) {
            bool skip = GET_CODE_SKIP(jump);
            if (skip) {
              i++;
            } else {
              jump = jumpSequence[j];
              jumpType = GET_CODE_TYPE(jumpType);
              size_t jumpAddress = GET_CODE_ADDRESS(jump);
              if (jumpAddress == returnAddress) {
                logD("swap jump code %08x at %d/%d", jump, i, j-1);
                jumpSequence[j] = CODE_RETURN;
              } else {
                returnAddress = i + 1;
              }
              i = jumpAddress;
              j++;
            }

          } else {
            logD("unexpected code %08x at %d/%d", jump, i, j-1);
            assert(false);
          }
        } else if (type == CODE_TYPE::GOTO) {
          // GOTO
          size_t jumpAddress = GET_CODE_ADDRESS(c);
          returnAddress = i + 1;
          i = jumpAddress;
        } else {
          i++;
        }
      } 
    }
  }

  // Test compression correctness 
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < 2; channel += 1) {
      auto &compressedCodeSequence = compressedCodeSequences[subsong][channel];
      auto &jumpSequence = jumpSequences[subsong][channel];
      // std::vector<AlphaCode> uncompressedSequence;
      // uncompressedSequence.reserve(alphaSequence.size());
      auto it = jumpSequence.begin();
      size_t j = 0;
      int depth = 0;
      int maxDepth = 0;
      int popBackToFront = 0;
      int popBackToLast= 0;
      int returnCodeAddress = 0;
      int maxOffset = 0;
      for (size_t i = 0; i < compressedCodeSequence.size(); ) {
        AlphaCode c = compressedCodeSequence[i];
        CODE_TYPE type = GET_CODE_TYPE(c);
        if (type == CODE_TYPE::POP) {
          AlphaCode codeJump = *it++;
          CODE_TYPE jumpType = GET_CODE_TYPE(codeJump);

          if (jumpType == CODE_TYPE::POP) {
            AlphaCode x = codeSequences[subsong][channel][j];
            if (c != x) {
              logD("%d %d | %d: %08x <> %08x (%d)", subsong, channel, i, c, x, j);
              logD("fail at end %d", i);
              assert(false);
            }
            break;
          } else if (jumpType == CODE_TYPE::SKIP) {
            bool skip = GET_CODE_SKIP(codeJump);
            if (skip) {
              i++;
            } else {
              codeJump = *it++;
              jumpType = GET_CODE_TYPE(codeJump);
              if  (jumpType == CODE_TYPE::RETURN) {
                i = returnCodeAddress;

              } else {
                assert(GET_CODE_TYPE(codeJump) == CODE_TYPE::JUMP);
                size_t jumpAddress = GET_CODE_ADDRESS(codeJump);
                if (jumpAddress < i) {
                  depth += 1;
                  if (depth > maxDepth) {
                    maxDepth = depth;
                  }
                } else if (jumpAddress > i) {
                  depth -= 1;
                }
                if (jumpAddress >= maxOffset) {
                  popBackToFront++;
                }
                if (jumpAddress == returnCodeAddress) {
                  popBackToLast++;
                }
                i = jumpAddress;
              }
            }
          } else if (jumpType == CODE_TYPE::RETURN) {
            i = returnCodeAddress;

          } else {
            size_t jumpAddress = GET_CODE_ADDRESS(codeJump);
            if (jumpAddress < i) {
              depth += 1;
              if (depth > maxDepth) {
                maxDepth = depth;
              }
            } else {
              depth -= 1;
            }
            if (jumpAddress >= maxOffset) {
              popBackToFront++;
            }
            if (jumpAddress == returnCodeAddress) {
              popBackToLast++;
            }
            i = jumpAddress;
          }
        } else if (type == CODE_TYPE::GOTO) {
          // GOTO
          size_t jumpAddress = GET_CODE_ADDRESS(c);
          if (jumpAddress < i) {
            depth += 1;
            if (depth > maxDepth) {
              maxDepth = depth;
            }
          } else {
            depth -= 1;
          }
          if (jumpAddress >= maxOffset) {
            popBackToFront++;
          }
          if (jumpAddress == returnCodeAddress) {
            popBackToLast++;
          }
          returnCodeAddress = i + 1;
          i = jumpAddress;
        } else {
          AlphaCode x = codeSequences[subsong][channel][j];
          if (c != x) {
            logD("%d %d | %d: %08x <> %08x (%d)",subsong, channel, i, c, x, j);
            logD("fail at %d", i);
            assert(false);
          }
          // uncompressedSequence.emplace_back(c);
          i++;
          j++;
        }
        if (i > maxOffset) {
          maxOffset = i;
        }
      } 
      logD("max depth %d", maxDepth);
      logD("pop back to front %d", popBackToFront);
      logD("pop back to last %d", popBackToLast);
    }
  }

// ; Song data size: 8
// ; Uncompressed Sequence Length: 12511
// ; Uncompressed Bytes: 12509
// ; Compressed Data Sequence Length: 2518
// ; Compressed Jump Sequence Length: 1215
// ; Compressed Bytes 3831
// ; AUDIO_DATA_S0_C0 bytes: 2381
// ; AUDIO_DATA_S0_C1 bytes: 914
// ; AUDIO_JUMP_0_C0 bytes: 125
// ; AUDIO_JUMP_0_C1 bytes: 411
//
// ; SPANS_S0_C0_START Bytes = 3098
// ; JUMPS_S0_C0_START Bytes = 302
// ; SPANS_S0_C1_START Bytes = 1252
// ; JUMPS_S0_C1_START Bytes = 911
// ; Uncompressed Bytes: 12509
// ; Compressed Bytes 4350




  // encode bitstreams
  bool enableHuffmanCodes = true;
  size_t maxHuffmanCodes = 256;
  size_t dataOffset = 0xf300; // BUGBUG: magic number
  HuffmanTree *codeTree = enableHuffmanCodes ?
    buildHuffmanTree(frequencyMap, maxHuffmanCodes, CODE_EMPTY_LITERAL) : new HuffmanTree(CODE_EMPTY_LITERAL, 1);
  std::map<AlphaCode, std::vector<bool>> codeIndex;
  codeTree->buildIndex(codeIndex);
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
      it = codeIndex.find(CODE_EMPTY_LITERAL);
    }
    auto &bitvec = (*it).second;
    String huffmanCode = "";
    for (int i = bitvec.size(); --i >= 0; ) {
      huffmanCode += bitvec.at(i) ? "1" : "0";
    }
    logD("  %08x -> %d (%s)", x.first, x.second, huffmanCode);
  }
  CALC_ENTROPY(frequencyMap);
  logD("tree depth: %d", codeTree->depth);

  Bitstream *dataStreams[e->song.subsong.size()][2];
  Bitstream *jumpStreams[e->song.subsong.size()][2];
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < 2; channel += 1) {
      // produce data stream
      logD("encoding data stream for %d %d", subsong, channel);
      auto &compressedCodeSequence = compressedCodeSequences[subsong][channel];
      Bitstream *dataStream = new Bitstream(compressedCodeSequence.size() * 24);
      dataStreams[subsong][channel] = dataStream;
      for (auto c: compressedCodeSequence) {
        auto it = codeIndex.find(c);
        if (it == codeIndex.end()) {
          it = codeIndex.find(CODE_EMPTY_LITERAL);
          auto &bitvec = codeIndex.at(CODE_EMPTY_LITERAL);
          for (int i = bitvec.size(); --i >= 0; ) {
            dataStream->writeBit(bitvec[i]);
          }
          CODE_TYPE type = GET_CODE_TYPE(c);
          // BUGBUG: fake
          switch (type) {
            case CODE_TYPE::POP: {
              dataStream->writeByte(0);
              break;
            }
            case CODE_TYPE::GOTO: {
              dataStream->writeByte(0);
              dataStream->writeByte(0);
              break;
            }
            case CODE_TYPE::LITERAL: {
              for (size_t i = 0; i < GET_CODE_SIZE(c); i++) {
                dataStream->writeByte(GET_CODE_BYTE(c, i));
              }
              break;
            }
            default:
              assert(false);
          }
        } else {
          auto &bitvec = (*it).second;
          for (int i = bitvec.size(); --i >= 0; ) {
            dataStream->writeBit(bitvec[i]);
          }
        }
      }

      size_t nextDataOffset = dataStream->size() / 8;
      size_t pad = dataStream->size() % 8;
      if (pad > 0) {
        nextDataOffset += 1;
      }

      // produce jump stream
      logD("encoding jump stream for %d %d", subsong, channel);
      auto &jumpSequence = jumpSequences[subsong][channel];
      Bitstream *jumpStream = new Bitstream(jumpSequence.size() * 16);
      jumpStreams[subsong][channel] = jumpStream;
      for (auto j: jumpSequence) {
        CODE_TYPE type = GET_CODE_TYPE(j);
        switch (type) {
          case CODE_TYPE::POP:
            jumpStream->writeByte(0);

          case CODE_TYPE::SKIP: {
            bool skip = GET_CODE_SKIP(j);
            jumpStream->writeBit(skip);
            break;
          }

          case CODE_TYPE::RETURN: {
            jumpStream->writeBit(false);
            break;
          }

          case CODE_TYPE::JUMP: {
            size_t addr = dataOffset + GET_CODE_ADDRESS(j);
            unsigned char bx = addr & 0x7;
            unsigned char lx = (addr >> 3) & 0xff;
            unsigned char hx = (bx << 5) | ((addr >> 12) & 0x1f);
            // BUGBUG: fakee
            jumpStream->writeBit(true);
            jumpStream->writeBit(true);
            jumpStream->writeBit(true);
            jumpStream->writeBit(true);
            jumpStream->writeBit(true);
            jumpStream->writeBit(true);
            jumpStream->writeBit(true);
            jumpStream->writeByte(lx);
            break;
          }

          case CODE_TYPE::LITERAL:
          case CODE_TYPE::GOTO: 
            // should not be present in jump stream
            assert(false);          
        }
      }

      dataOffset += nextDataOffset;

    }
  }

  // write the audio track data
  size_t totalCompressedBytes = 0;
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < 2; channel += 1) {
      logD("assembling track data for %d %d", subsong, channel);
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

  // write the audio jump data
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < 2; channel += 1) {
      logD("assembling jump data for %d %d", subsong, channel);
      trackData->writeText(fmt::sprintf("\nAUDIO_JUMP_%d_C%d_START", subsong, channel));
      Bitstream *jumpStream = jumpStreams[subsong][channel];
      jumpStream->seek(0);
      size_t mod = 0;
      size_t bytesWritten = 0;
      while (jumpStream->hasBits()) {
        unsigned char uc = jumpStream->readByte();
        if (mod == 0) {
          trackData->writeText(fmt::sprintf("\n    byte $%02x", uc));
        } else {
          trackData->writeText(fmt::sprintf(", $%02x", uc));
        }
        mod = (mod + 1) % 16;
        bytesWritten++;
      }
      trackData->writeText(fmt::sprintf("\n; AUDIO_JUMP_%d_C%d bytes: %d\n", subsong, channel, bytesWritten));
      totalCompressedBytes += bytesWritten;
      delete jumpStream;
    }
  }

  // write the code tree
  // BUGBUG: TODO
  //totalCompressedBytes += frequencyMap.size() * 3;
  delete codeTree;

  trackData->writeText(fmt::sprintf("\n\n; Song data size: %d\n", songDataSize));
  trackData->writeText(fmt::sprintf("; Uncompressed Sequence Length: %d\n", totalUncompressedCodes));
  trackData->writeText(fmt::sprintf("; Uncompressed Bytes: %d\n", totalUncompressedBytes));
  trackData->writeText(fmt::sprintf("; Compressed Data Sequence Length: %d\n", totalCompressedCodes));
  trackData->writeText(fmt::sprintf("; Compressed Jump Sequence Length: %d\n", totalCompressedJumps));
  trackData->writeText(fmt::sprintf("; Compressed Bytes %d\n", totalCompressedBytes));

  ret.push_back(DivROMExportOutput("Track_data.asm", trackData));
  
}

/**
 *  Write note data. Format 0:
 * 
 *   fffff010 ccccvvvv           frequency + control + volume, duration 1
 *   fffff110 ccccvvvv           " " ", duration 2
 *   dddd1100                    sustain d+1 frames
 *   dddd0100                    pause d+1 frames
 *   xxxx0001                    volume = x >> 4, duration 1 
 *   xxxx1001                    volume = x >> 4, duration 2
 *   xxxx0101                    control = x >> 4, duration 1
 *   xxxx1101                    control = x >> 4, duration 2
 *   xxxxx011                    frequency = x >> 3, duration 1
 *   xxxxx111                    frequency = x >> 3, duration 2
 *   xxxxx000                    reserved
 *   00000000                    stop
 */
int DivExportAtari2600::encodeChannelState(
  const ChannelState& next,
  const char duration,
  const ChannelState& last,
  bool encodeRemainder,
  std::vector<unsigned char> &out)
{
  // when duration is zero... some kind of rounding issue has happened upstream... we force to 1...
  if (duration == 0) {
      logD("0 duration note");
  }
  int framecount = duration > 0 ? duration : 1; 

  unsigned char audfx, audcx, audvx;
  int cc, fc, vc;
  audcx = next.registers[0];
  cc = audcx != last.registers[0];
  audfx = next.registers[1];
  fc = audfx != last.registers[1];
  audvx = next.registers[2];
  vc = audvx != last.registers[2];
  int delta = (cc + fc + vc);
  
  if (audvx == 0 && delta != 0) {
    // volume is zero, pause
    unsigned char dmod;
    if (framecount > 16) {
      dmod = 15;
      framecount -= 16;
    } else {
      dmod = framecount - 1;
      framecount = 0;
    }
    unsigned char rx = dmod << 4 | 0x04;
    //w->writeText(fmt::sprintf("    byte %d; PAUSE %d\n", rx, dmod));
    out.emplace_back(rx);
    
  } else if ( delta == 1 ) {
    // write a delta row - only change one register
    unsigned char dmod;
    if (framecount > 2) {
      dmod = 1;
      framecount -= 2;
    } else {
      dmod = framecount - 1;
      framecount = 0;
    }

    unsigned char rx;
    if (fc > 0) {
      // frequency
      rx = audfx << 3 | dmod << 2 | 0x03; //  d11
    } else if (cc > 0 ) {
      // control
      rx = audcx << 4 | dmod << 3 | 0x05; // d101
    } else {
      // volume 
      rx = audvx << 4 | dmod << 3 | 0x01; // d001
    }
    //w->writeText(fmt::sprintf("    byte %d\n", rx));
    out.emplace_back(rx);

  } else if ( delta > 1 ) {
    // write all registers
    unsigned char dmod;
    if (framecount > 2) {
      dmod = 1;
      framecount -= 2;
    } else {
      dmod = framecount - 1;
      framecount = 0;
    }

    // frequency
    unsigned char fdx = audfx << 3 | dmod << 2 | 0x02;
    //w->writeText(fmt::sprintf("    byte %d", x));
    out.emplace_back(fdx);

    // waveform and volume
    unsigned char cvx = (audcx << 4) + audvx;
    //w->writeText(fmt::sprintf(",%d\n", y));
    out.emplace_back(cvx);

  }

  if (delta > 0 && !encodeRemainder) {
    return framecount;
  }

  // when delta is zero / we have leftover frames, sustain
  while (framecount > 0) {
    unsigned char dmod;
    if (framecount > 16) {
      dmod = 15;
      framecount -= 16;
    } else {
      dmod = framecount - 1;
      framecount = 0;
    }
    unsigned char sx =  dmod << 4 | 0x0c;
    //w->writeText(fmt::sprintf("    byte %d; SUSTAIN %d\n", sx, dmod + 1));
    out.emplace_back(sx);
  }

  return 0;

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
