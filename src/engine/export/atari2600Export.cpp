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
// BETA 
//  - the good compression
//    - encoding schemes
//        - trial huffman code data stream
//          - write 6502 decoder
//          - more compression of jumps?
//          - try simpler fixed coding?
//        - trial instrument / waveform scheme
//        - just use actual zip or 7z with augments?
//          - need low memory 6502 decoder
//    - use estimate of compression savings to guide span choice
//  - debugging
//    - proper analytic debug output for TIAZIP spans
//  - compression goals
//    - Coconut_Mall in 4k
//        - JUMP+SKIP encoded  4350 data / 1213 jump = 5563
//        - JUMP+SKIP+POP+HUFF 3295 data / 536 jump = 3831   
//        - w/no multi cmd 3193 data / 629 jump = 3822
//        - w encoding bytes 3532 data / 629 jump = 4161
//        - w encoding bytes + less bits for volume 3296 data / 675 jump = 3971
//        - w more complex instruction set + RETURN  = 2936 data / 661 jump = 3596
//        - w validated output = 2998 data / 771 jump = 3769
//  - other output schemes
//    - batari basic player
//  - testability
//    - all targets test
//    - fix multi-song test
//    - clean up test output
//  - dev help
//    - docs on how to use multiple schemes
//    - makefile aware of song size / compression
//  - code
//    - cleanup pass
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
      writeTrackDataTIAZip(e, registerWrites, false, ret);
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
  std::vector<RegisterWrite> (*registerWrites),
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
  std::vector<RegisterWrite> (*registerWrites),
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
  std::vector<RegisterWrite> (*registerWrites),
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
  std::vector<RegisterWrite> (*registerWrites),
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
  std::vector<RegisterWrite> (*registerWrites),
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
  WRITE_DELTA,
  PAUSE,
  SUSTAIN,
  JUMP,
  SKIP,
  RETURN,
  ABSTRACT_JUMP
};

enum CHANGE_STATE {
  NOOP,
  WRITE
};

AlphaCode CODE_WRITE_DELTA(
  CHANGE_STATE cc,
  unsigned char cx,
  CHANGE_STATE fc,
  unsigned char fx,
  CHANGE_STATE vc,
  unsigned char vx, 
  unsigned char duration
) {
  return (AlphaCode) (
    (AlphaCode) WRITE_DELTA << 56 | 
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

const AlphaCode CODE_POP = ((AlphaCode) POP) << 56;
const AlphaCode CODE_RETURN_LAST = ((AlphaCode) RETURN) << 56;
const AlphaCode CODE_RETURN_FRONT = (((AlphaCode) RETURN) << 56) | 1;
const AlphaCode CODE_ABSTRACT_PAUSE = ((AlphaCode) PAUSE) << 56;
const AlphaCode CODE_ABSTRACT_SUSTAIN = ((AlphaCode) SUSTAIN) << 56;
const AlphaCode CODE_ABSTRACT_JUMP = ((AlphaCode) ABSTRACT_JUMP) << 56;
const AlphaCode CODE_ABSTRACT_WRITE  = 0xffff00ff00ff0000; // BUGBUG: HACKY
const AlphaCode CODE_WRITE_DELTA_001 = 0x0100000000010000; // BUGBUG: HACKY
const AlphaCode CODE_WRITE_DELTA_010 = 0x0100000100000000; // BUGBUG: HACKY
const AlphaCode CODE_WRITE_DELTA_011 = 0x0100000100010000; // BUGBUG: HACKY
const AlphaCode CODE_WRITE_DELTA_100 = 0x0101000000000000; // BUGBUG: HACKY
const AlphaCode CODE_WRITE_DELTA_101 = 0x0101000000010000; // BUGBUG: HACKY
const AlphaCode CODE_WRITE_DELTA_110 = 0x0101000100000000; // BUGBUG: HACKY
const AlphaCode CODE_WRITE_DELTA_111 = 0x0101000100010000; // BUGBUG: HACKY

// BUGBUG: make macro/inline
AlphaCode CODE_JUMP(int subsong, int channel, size_t index) {
  return ((AlphaCode)JUMP << 56) | 
         ((AlphaCode)subsong << 48) |
         ((AlphaCode)channel << 40) |
         index;
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

AlphaCode GET_CODE_ABSTRACT_DELTA(AlphaCode c) {
  return CODE_ABSTRACT_WRITE & c; 
}

CHANGE_STATE GET_CODE_WRITE_CC(AlphaCode c) {
  return (CHANGE_STATE) ((c >> 48) & 0xff);
}

unsigned char GET_CODE_WRITE_CX(AlphaCode c) {
  return (c >> 40) & 0xff;
}

CHANGE_STATE GET_CODE_WRITE_FC(AlphaCode c) {
  return (CHANGE_STATE) ((c >> 32) & 0xff);
}

unsigned char GET_CODE_WRITE_FX(AlphaCode c) {
  return (c >> 24) & 0xff;
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

size_t GET_CODE_CHANNEL(const AlphaCode c) {
  return (c >> 40) & 0xff;
}

size_t GET_CODE_ADDRESS(const AlphaCode c) {
  return c & 0x1fff;
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
    String huffmanCode = "";
    for (int i = bitvec.size(); --i >= 0; ) {
      huffmanCode += bitvec.at(i) ? "1" : "0";
    }
    logD("  %08x -> %d (%s)", x.first, x.second, huffmanCode);
  }

}

// compacted encoding
void DivExportAtari2600::writeTrackDataTIAZip(
  DivEngine* e, 
  const std::vector<RegisterWrite> (*registerWrites),
  bool fixedCodes,
  std::vector<DivROMExportOutput> &ret
) {
  size_t numSongs = e->song.subsong.size();

  // encode command streams
  size_t totalUncompressedSequenceSize = 0;
  std::map<AlphaCode, size_t> frequencyMap;
  std::vector<AlphaCode> codeSequences[e->song.subsong.size()][2];
  for (size_t subsong = 0; subsong < numSongs; subsong++) {
    for (int channel = 0; channel < 2; channel++) {
      auto &codeSequence = codeSequences[subsong][channel];

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

      // convert to AlphaCode
      ChannelState last(dumpSequence.initialState);
      for (auto& n: dumpSequence.intervals) {
        encodeChannelStateCodes(n.state, n.duration, last, codeSequence);
        last = n.state;
      }
      codeSequence.emplace_back(0);

      // create frequency map
      for (auto c: codeSequence) {
        frequencyMap[c]++;
      }
      totalUncompressedSequenceSize += codeSequence.size();
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

  // create compressed code sequence
  std::vector<AlphaCode> compressedCodeSequences[e->song.subsong.size()][2];
  std::vector<AlphaCode> jumpSequences[e->song.subsong.size()][2];
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < 2; channel += 1) {
      auto &codeSequence = codeSequences[subsong][channel];
      auto &compressedCodeSequence = compressedCodeSequences[subsong][channel];
      auto &jumpSequence = jumpSequences[subsong][channel];

      compressCodeSequence(
        subsong, 
        channel,
        alphabet,
        index,
        codeSequence,
        compressedCodeSequence,
        jumpSequence
      );

      validateCodeSequence(
        subsong, 
        channel,
        codeSequence,
        compressedCodeSequence,
        jumpSequence
      );

    }
  }

  if (fixedCodes) {
    encodeBitstreamFixed(
      e,
      codeSequences,
      compressedCodeSequences,
      jumpSequences,
      0x0300,
      4096 * 8,
      ret
    );

  } else {
    encodeBitstreamDynamic(
      e,
      codeSequences,
      compressedCodeSequences,
      jumpSequences,
      0x0300,
      4096 * 8,
      ret
    );
  }
  
}

void DivExportAtari2600::compressCodeSequence(
  int subsong,
  int channel,
  const std::vector<AlphaCode> &alphabet,
  const std::map<AlphaCode, AlphaChar> &index,
  const std::vector<AlphaCode>&codeSequence,
  std::vector<AlphaCode> &compressedCodeSequence,
  std::vector<AlphaCode> &jumpSequence
) {

  compressedCodeSequence.reserve(codeSequence.size());
  jumpSequence.reserve(codeSequence.size());

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
  
  // branchFrequencyMap[i] -> frequency of branches from alphaSequence[i]
  std::vector<std::map<size_t, size_t>> branchFrequencyMap; 
  branchFrequencyMap.resize(alphaSequence.size());

  // greedily find spans to compress with 
  std::vector<Span> spans;
  Span currentSpan((int)subsong, channel, 0, 0);
  Span nextSpan((int)subsong, channel, 0, 0);
  for (size_t i = 0; i < alphaSequence.size(); ) {
    root->find_prior(i, alphaSequence, nextSpan);
    if (nextSpan.length > 3) { // BUGBUG: do trial compression
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

  std::vector<size_t> labels;
  labels.resize(alphaSequence.size());
  size_t end = 0;
  for (auto &span: spans) {
    if (end > span.start) {
      // traverse prior span
      logD("%d: traversing prior %d - %d", end, span.start, span.length);
      for (size_t i = 0; i < span.length; i++) {
        size_t leftmostCodeAddr = copyMap[end];
        logD("%d: ... ", end);
        end++;
        size_t nextCodeAddr = copyMap[end];
        if (branchFrequencyMap[leftmostCodeAddr].size() > 0) {
          // decide if this is a skip or a take
          size_t skipAddr = skipMap[leftmostCodeAddr];
          bool skip = nextCodeAddr == skipAddr;
          jumpSequence.emplace_back(CODE_SKIP(skip));
          if (skip) {
            logD("%d: skip jump @ %d to %d", end, leftmostCodeAddr, skipAddr);
          } else {
            logD("%d: taking jump @ %d to %d", end, leftmostCodeAddr, nextCodeAddr);
            jumpSequence.emplace_back(CODE_JUMP(subsong, channel, nextCodeAddr));  
          }
        }
      }
      
    } else {
      // encode literal span
      logD("%d: encoding current %d - %d", end, span.start, span.length);
      for (size_t i = span.start; i < span.start + span.length; i++) {
        AlphaCode c = codeSequence[i];
        labels[i] = compressedCodeSequence.size();
        compressedCodeSequence.emplace_back(c);
        logD("%d: adding code %08x", i, c);
        if (i + 1 < alphaSequence.size()) {
          size_t nextCodeAddr = copyMap[i+1];
          if (branchFrequencyMap[i].size() > 0) {
            // jump table
            compressedCodeSequence.emplace_back(CODE_POP);
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
              logD("%d: skip jump @ %d to %d", i, end, nextCodeAddr);
            } else {
              logD("%d: take jump @ %d to %d", i, end, nextCodeAddr);
              jumpSequence.emplace_back(CODE_JUMP(subsong, channel, nextCodeAddr));  
            }
            if (skipAddr != (i + 1)) {
              logD("%d: goto %d", i, skipAddr);
              compressedCodeSequence.emplace_back(CODE_JUMP(subsong, channel, skipAddr));
            }

          } else if (nextCodeAddr != (i + 1)) {
            // straight GOTO
            // BUGBUG: could be inline return?
            logD("%d: goto %d", i, nextCodeAddr);
            compressedCodeSequence.emplace_back(CODE_JUMP(subsong, channel, nextCodeAddr));

          }
        } else {
          jumpSequence.emplace_back(0);
        }
        end++;
      }
    }
  }

  // rewrite gotos
  for (size_t i = 0; i < compressedCodeSequence.size(); i++) {
    AlphaCode c = compressedCodeSequence[i];
    CODE_TYPE type = GET_CODE_TYPE(c);
    if (type == CODE_TYPE::JUMP) {
      size_t address = labels[GET_CODE_ADDRESS(c)];
      c = CODE_JUMP(subsong, channel, address);
      compressedCodeSequence[i] = c;
    }
  }

  // rewrite jumps
  for (size_t i = 0; i < jumpSequence.size(); i++) {
    AlphaCode c = jumpSequence[i];
    if (GET_CODE_TYPE(c) == CODE_TYPE::JUMP) {
      size_t address = labels[GET_CODE_ADDRESS(c)];
      c = CODE_JUMP(subsong, channel, address);
      jumpSequence[i] = c;
    }
  }

  // rewrite returns
  size_t returnToLastJumpAddress = 0;
  size_t returnToFrontOfStreamAddress = 0;
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
          if (jump == CODE_RETURN_LAST) {
            i = returnToLastJumpAddress;
          } else if (jump == CODE_RETURN_FRONT) {
            i = returnToFrontOfStreamAddress;
          } else {
            size_t jumpAddress = GET_CODE_ADDRESS(jump);
            if (jumpAddress == returnToLastJumpAddress) {
              jumpSequence[j] = CODE_RETURN_LAST;
            } else if (jumpAddress == returnToFrontOfStreamAddress) {
              jumpSequence[j] = CODE_RETURN_FRONT;
            } else {
              returnToLastJumpAddress = i + 1;
              if (returnToLastJumpAddress >= returnToFrontOfStreamAddress) {
                returnToFrontOfStreamAddress = returnToLastJumpAddress;
              }
            }
            i = jumpAddress;
            j++;
          }
        }

      } else {
        logD("unexpected code %08x at %d/%d", jump, i, j-1);
        assert(false);
      }
    } else if (type == CODE_TYPE::JUMP) {
      // GOTO
      size_t jumpAddress = GET_CODE_ADDRESS(c);
      returnToLastJumpAddress = i + 1;
      if (returnToLastJumpAddress >= returnToFrontOfStreamAddress) {
        returnToFrontOfStreamAddress = returnToLastJumpAddress;
      }
      i = jumpAddress;
    } else if (type == CODE_TYPE::RETURN) {
      assert(false);
    } else {
      i++;
    }
  }

}

void DivExportAtari2600::encodeBitstreamFixed(
    DivEngine* e, 
    const std::vector<AlphaCode> (*codeSequences)[2],
    const std::vector<AlphaCode> (*compressedCodeSequences)[2],
    const std::vector<AlphaCode> (*jumpSequences)[2],
    size_t dataOffset,
    size_t blockSize,
    std::vector<DivROMExportOutput> &ret
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

  // frequency maps for coding
  std::map<AlphaCode, size_t> jumpFrequencyMap; 

  size_t totalCompressedCodeSequenceSize = 0;
  size_t totalJumpSequenceSize = 0;
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < 2; channel += 1) {
      auto &compressedCodeSequence = compressedCodeSequences[subsong][channel];
      auto &jumpSequence = jumpSequences[subsong][channel];

      // update code frequencies
      for (size_t i = 0; i < compressedCodeSequence.size(); i++) {
        AlphaCode c = compressedCodeSequence[i];
        CODE_TYPE type = GET_CODE_TYPE(c);
        if (type == CODE_TYPE::JUMP) {
          jumpFrequencyMap[c]++;
        }
      }

      totalCompressedCodeSequenceSize += compressedCodeSequence.size();

      // update jump frequencies
      for (size_t j = 0; j < jumpSequence.size(); j++) {
        AlphaCode jumpCode = jumpSequence[j];
        CODE_TYPE type = GET_CODE_TYPE(jumpCode);
        if (type == CODE_TYPE::JUMP) {
          jumpFrequencyMap[jumpCode]++;
        }
      }
      totalJumpSequenceSize += jumpSequence.size();
    }
  }

  logD("jump dictionary size: %d", jumpFrequencyMap.size());
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
    if (index > 63) {
      continue;
    }
    jumpMap[node.first] = index;
  }
  logD("jump map size: %d", jumpMap.size());
  SHOW_FREQUENCIES(jumpMap);
  
  // produce bitstreams
  Bitstream *dataStreams[e->song.subsong.size()][2];
  Bitstream *jumpStreams[e->song.subsong.size()][2];
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < 2; channel += 1) {
      // produce data stream
      logD("encoding data stream for %d %d", subsong, channel);
      auto &compressedCodeSequence = compressedCodeSequences[subsong][channel];
      Bitstream *dataStream = new Bitstream(blockSize);
      dataStreams[subsong][channel] = dataStream;
      for (auto c: compressedCodeSequence) {
        CODE_TYPE type = GET_CODE_TYPE(c);
        switch (type) {
          case CODE_TYPE::JUMP: {
            dataStream->writeBit(true);
            dataStream->writeBit(false);
            auto ij = jumpMap.find(c);
            if (ij != jumpMap.end()) {
              size_t index = (*ij).second;
              dataStream->writeBit(false);
              dataStream->writeBits(index, 7);

            } else {
              dataStream->writeBit(true);
              size_t address = dataOffset + GET_CODE_ADDRESS(c);
              dataStream->writeBits(address, 12);

            }
            // BUGBUG: FAKE
            break;
          }
          case CODE_TYPE::WRITE_DELTA: {
            dataStream->writeBit(false);
            CHANGE_STATE cc = GET_CODE_WRITE_CC(c);
            switch (cc) {
              case CHANGE_STATE::NOOP:
                dataStream->writeBit(false);
                break;
              case CHANGE_STATE::WRITE: {
                dataStream->writeBit(true);
                dataStream->writeBit(false);
                dataStream->writeBit(true);
                dataStream->writeBit(false);
                break;
              }
            }
            AlphaCode fc = GET_CODE_WRITE_FC(c); 
            switch (fc) {
              case CHANGE_STATE::NOOP:
                dataStream->writeBit(false);
                break;
              case CHANGE_STATE::WRITE: {
                CHANGE_STATE cc = GET_CODE_WRITE_CC(c);
                dataStream->writeBit(true);
                dataStream->writeBit(false);
                dataStream->writeBit(true);
                dataStream->writeBit(false);
                dataStream->writeBit(false);
                break;
              }
            }
            AlphaCode vc = GET_CODE_WRITE_VC(c); 
            switch (vc) {
              case CHANGE_STATE::NOOP:
                dataStream->writeBit(false);
                break;
              case CHANGE_STATE::WRITE: {
                dataStream->writeBit(true);
                dataStream->writeBit(false);
                dataStream->writeBit(true);
                dataStream->writeBit(false);
                break;
              }
            }
          }
          case CODE_TYPE::POP: {
            dataStream->writeBit(true);
            dataStream->writeBit(true);
            dataStream->writeBit(true);
            break;
          }
          case CODE_TYPE::PAUSE: {
            dataStream->writeBit(true);
            dataStream->writeBit(true);
            dataStream->writeBit(false);
            dataStream->writeBit(false);
            unsigned char duration = GET_CODE_WRITE_DURATION(c);
            dataStream->writeBit(true);
            dataStream->writeBit(false);
            dataStream->writeBit(false);
            break;
          }
          case CODE_TYPE::SUSTAIN: {
            dataStream->writeBit(true);
            dataStream->writeBit(true);
            dataStream->writeBit(false);
            dataStream->writeBit(true);
            unsigned char duration = GET_CODE_WRITE_DURATION(c);
            dataStream->writeBit(true);
            dataStream->writeBit(false);
            dataStream->writeBit(false);
            break;
          }
          default:
            assert(false);
        }
      }

      size_t nextDataOffset = dataStream->position() / 8;
      size_t pad = dataStream->position() % 8;
      if (pad > 0) {
        nextDataOffset += 1;
      }

      // produce jump stream
      logD("encoding jump stream for %d %d", subsong, channel);
      auto &jumpSequence = jumpSequences[subsong][channel];
      Bitstream *jumpStream = new Bitstream(blockSize);
      jumpStreams[subsong][channel] = jumpStream;
      for (auto j: jumpSequence) {
        CODE_TYPE type = GET_CODE_TYPE(j);
        switch (type) {
          case CODE_TYPE::POP:
            jumpStream->writeBits(0, 18); // BUGBUG: too many?
            break;

          case CODE_TYPE::SKIP: {
            bool skip = GET_CODE_SKIP(j);
            jumpStream->writeBit(skip);
            break;
          }

          case CODE_TYPE::RETURN: {
            jumpStream->writeBit(false);
            if (j == CODE_RETURN_LAST) {
              jumpStream->writeBit(true);
            }
            break;
          }

          case CODE_TYPE::JUMP: {
            auto ij = jumpMap.find(j);
            if (ij != jumpMap.end()) {
              jumpStream->writeBit(false);
              size_t index = (*ij).second;
              jumpStream->writeBits(index, 6);

            } else {
              jumpStream->writeBit(true);
              size_t address = dataOffset + GET_CODE_ADDRESS(j);
              jumpStream->writeBits(address, 12);

            }

            break;
          }

          default:
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

  trackData->writeText(fmt::sprintf("\n\n; Song data size: %d\n", songDataSize));
  trackData->writeText(fmt::sprintf("; Compressed Code Sequence Length: %d\n", totalCompressedCodeSequenceSize));
  trackData->writeText(fmt::sprintf("; Jump Sequence Length: %d\n", totalJumpSequenceSize));
  trackData->writeText(fmt::sprintf("; Compressed Bytes %d\n", totalCompressedBytes));

  ret.push_back(DivROMExportOutput("Track_data.asm", trackData));

}

void DivExportAtari2600::encodeBitstreamDynamic(
    DivEngine* e, 
    const std::vector<AlphaCode> (*codeSequences)[2],
    const std::vector<AlphaCode> (*compressedCodeSequences)[2],
    const std::vector<AlphaCode> (*jumpSequences)[2],
    size_t dataOffset,
    size_t blockSize,
    std::vector<DivROMExportOutput> &ret
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

  // frequency maps for coding
  std::map<AlphaCode, size_t> abstractFrequencyMap;
  std::map<AlphaCode, size_t> controlFrequencyMap;
  std::map<AlphaCode, size_t> frequencyFrequencyMap;
  std::map<AlphaCode, size_t> volumeFrequencyMap;
  std::map<AlphaCode, size_t> durationFrequencyMap;
  std::map<AlphaCode, size_t> jumpFrequencyMap; 

  size_t totalCompressedCodeSequenceSize = 0;
  size_t totalJumpSequenceSize = 0;
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < 2; channel += 1) {
      auto &compressedCodeSequence = compressedCodeSequences[subsong][channel];
      auto &jumpSequence = jumpSequences[subsong][channel];

      // update code frequencies
      for (size_t i = 0; i < compressedCodeSequence.size(); i++) {
        AlphaCode c = compressedCodeSequence[i];
        CODE_TYPE type = GET_CODE_TYPE(c);
        if (type == CODE_TYPE::JUMP) {
          abstractFrequencyMap[CODE_ABSTRACT_JUMP]++;
          jumpFrequencyMap[c]++;

        } else if (type == CODE_TYPE::POP) {
          abstractFrequencyMap[CODE_POP]++;

        } else if (type == CODE_TYPE::PAUSE) {
          abstractFrequencyMap[CODE_ABSTRACT_PAUSE]++;
          unsigned char duration = GET_CODE_WRITE_DURATION(c);
          durationFrequencyMap[(AlphaCode)duration]++;

        } else if (type == CODE_TYPE::SUSTAIN) {
          abstractFrequencyMap[CODE_ABSTRACT_SUSTAIN]++;
          unsigned char duration = GET_CODE_WRITE_DURATION(c);
          durationFrequencyMap[(AlphaCode)duration]++;

        } else {
          AlphaCode ac = GET_CODE_ABSTRACT_DELTA(c); 
          abstractFrequencyMap[ac]++;
          CHANGE_STATE cc = GET_CODE_WRITE_CC(c);
          if (cc == CHANGE_STATE::WRITE) {
            unsigned char cx = GET_CODE_WRITE_CX(c);
            controlFrequencyMap[ (cc << 8) | cx]++;
          }
          CHANGE_STATE fc = GET_CODE_WRITE_FC(c);
          if (fc == CHANGE_STATE::WRITE) {
            unsigned char fx = GET_CODE_WRITE_FX(c);
            frequencyFrequencyMap[ (fc << 8) | fx]++;
          }
          CHANGE_STATE vc = GET_CODE_WRITE_VC(c);
          if (vc == CHANGE_STATE::WRITE) {
            unsigned char vx = GET_CODE_WRITE_VX(c);
            volumeFrequencyMap[ (vc << 8) | vx]++;
          }
          unsigned char duration = GET_CODE_WRITE_DURATION(c);
          assert(duration == 1);
          // BUGBUG: testing
          //durationFrequencyMap[(AlphaCode)duration]++;
        }
      }
      totalCompressedCodeSequenceSize += compressedCodeSequence.size();

      // update jump frequencies
      for (size_t j = 0; j < jumpSequence.size(); j++) {
        AlphaCode jumpCode = jumpSequence[j];
        CODE_TYPE type = GET_CODE_TYPE(jumpCode);
        if (type == CODE_TYPE::JUMP) {
          jumpFrequencyMap[jumpCode]++;
        }
      }
      totalJumpSequenceSize += jumpSequence.size();
    }
  }

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
    if (index > 63) {
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

  HuffmanTree *abstractCodeTree = enableHuffmanCodes ?
    buildHuffmanTree(abstractFrequencyMap, maxHuffmanCodes, minWeight, CODE_ABSTRACT_WRITE) : new HuffmanTree(CODE_ABSTRACT_WRITE, 1);
  std::map<AlphaCode, std::vector<bool>> abstractCodeIndex;
  abstractCodeTree->buildIndex(abstractCodeIndex);
  SHOW_TREE(abstractFrequencyMap, abstractCodeIndex, CODE_ABSTRACT_WRITE);

  logD("control tree");
  HuffmanTree *controlTree = buildHuffmanTree(controlFrequencyMap, maxHuffmanCodes, minWeight, 0);
  std::map<AlphaCode, std::vector<bool>> controlCodeIndex;
  controlTree->buildIndex(controlCodeIndex);
  SHOW_TREE(controlFrequencyMap, controlCodeIndex, 0);

  logD("frequency tree");
  HuffmanTree *frequencyTree = buildHuffmanTree(frequencyFrequencyMap, maxHuffmanCodes, minWeight, 0);
  std::map<AlphaCode, std::vector<bool>> frequencyCodeIndex;
  frequencyTree->buildIndex(frequencyCodeIndex);
  SHOW_TREE(frequencyFrequencyMap, frequencyCodeIndex, 0);

  logD("volume tree");
  HuffmanTree *volumeTree = buildHuffmanTree(volumeFrequencyMap, maxHuffmanCodes, minWeight, 0);
  std::map<AlphaCode, std::vector<bool>> volumeCodeIndex;
  volumeTree->buildIndex(volumeCodeIndex);
  SHOW_TREE(volumeFrequencyMap, volumeCodeIndex, 0);

  logD("duration tree");
  HuffmanTree *durationTree = buildHuffmanTree(durationFrequencyMap, maxHuffmanCodes, minWeight, 0);
  std::map<AlphaCode, std::vector<bool>> durationCodeIndex;
  durationTree->buildIndex(durationCodeIndex);
  SHOW_TREE(durationFrequencyMap, durationCodeIndex, 0);

  // produce bitstreams
  size_t streamDataOffset = (dataOffset << 3);
  Bitstream *dataStreams[e->song.subsong.size()][2];
  Bitstream *jumpStreams[e->song.subsong.size()][2];
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
        auto it = abstractCodeIndex.find(c);
        if (it == abstractCodeIndex.end()) {
          CODE_TYPE type = GET_CODE_TYPE(c);
          switch (type) {
            case CODE_TYPE::JUMP: {
              dataStream->writeBits(abstractCodeIndex.at(CODE_ABSTRACT_JUMP));
              auto ij = jumpMap.find(c);
              if (ij != jumpMap.end()) {
                size_t index = (*ij).second;
                dataStream->writeBit(false); // lookup
                dataStream->writeBits(index, 6);

              } else {
                dataStream->writeBit(true); // direct
                size_t address = GET_CODE_ADDRESS(c);
                dataStreamPointerMap[dataStream->position()] = address;
                dataStream->writeBits(address, 15);
              }
              break;
            }
            case CODE_TYPE::WRITE_DELTA: {
              AlphaCode ac = GET_CODE_ABSTRACT_DELTA(c); 
              dataStream->writeBits(abstractCodeIndex.at(ac));
              CHANGE_STATE cc = GET_CODE_WRITE_CC(c);
              if (cc == CHANGE_STATE::WRITE) {
                unsigned char cx = GET_CODE_WRITE_CX(c);
                dataStream->writeBits(controlCodeIndex.at((cc << 8) | cx));
              }
              CHANGE_STATE fc = GET_CODE_WRITE_FC(c);
              if (fc == CHANGE_STATE::WRITE) {
                unsigned char fx = GET_CODE_WRITE_FX(c);
                dataStream->writeBits(fx, 5);
              }
              CHANGE_STATE vc = GET_CODE_WRITE_VC(c);
              if (vc == CHANGE_STATE::WRITE) {
                unsigned char vx = GET_CODE_WRITE_VX(c);
                dataStream->writeBits(volumeCodeIndex.at((vc << 8) | vx));
              }
              // duration is always 1
              // unsigned char duration = GET_CODE_WRITE_DURATION(c);
              // dataStream->writeBits(durationCodeIndex.at(duration));
              break;
            }
            case CODE_TYPE::PAUSE: {
              dataStream->writeBits(abstractCodeIndex.at(CODE_ABSTRACT_PAUSE));
              unsigned char duration = GET_CODE_WRITE_DURATION(c);
              dataStream->writeBits(durationCodeIndex.at(duration));
              break;
            }
            case CODE_TYPE::SUSTAIN: {
              dataStream->writeBits(abstractCodeIndex.at(CODE_ABSTRACT_SUSTAIN));
              unsigned char duration = GET_CODE_WRITE_DURATION(c);
              dataStream->writeBits(durationCodeIndex.at(duration));
              break;
            }

            default:
              assert(false);
          }
        } else {
          dataStream->writeBits((*it).second);
        }
      }

      // produce jump stream
      logD("encoding jump stream for %d %d", subsong, channel);
      auto &jumpSequence = jumpSequences[subsong][channel];
      std::map<size_t, size_t> jumpStreamPointerMap;
      Bitstream *jumpStream = new Bitstream(blockSize);
      jumpStreams[subsong][channel] = jumpStream;
      for (auto j: jumpSequence) {
        CODE_TYPE type = GET_CODE_TYPE(j);
        switch (type) {
          case CODE_TYPE::POP:
            jumpStream->writeBit(false);
            jumpStream->writeBit(false);
            jumpStream->writeBit(true);
            jumpStream->writeBits(0, 15); // BUGBUG: too many?
            break;

          case CODE_TYPE::SKIP: {
            bool skip = GET_CODE_SKIP(j);
            jumpStream->writeBit(skip);
            break;
          }

          case CODE_TYPE::RETURN: {
            jumpStream->writeBit(true); // is return
            jumpStream->writeBit(j == CODE_RETURN_FRONT);
            break;
          }

          case CODE_TYPE::JUMP: {
            jumpStream->writeBit(false); // no return
            size_t address = GET_CODE_ADDRESS(j);
            auto ij = jumpMap.find(j);
            if (ij != jumpMap.end()) {
              size_t index = (*ij).second;
              jumpStream->writeBit(false); // is lookup
              jumpStream->writeBits(index, 6);

            } else {
              jumpStream->writeBit(true); // no lookup
              jumpStreamPointerMap[jumpStream->position()] = address;
              jumpStream->writeBits(address, 15);

            }

            break;
          }

          default:
            // should not be present in jump stream
            assert(false);          
        }
      }

      for (auto& x : dataStreamPointerMap) {
        dataStream->seek(x.first);
        dataStream->writeBits(positionMap[x.second], 15);
        dataStream->seek(x.first);
      }

      for (auto& x : jumpStreamPointerMap) {
        jumpStream->seek(x.first);
        jumpStream->writeBits(positionMap[x.second], 15);
        jumpStream->seek(x.first);
      }

      for (auto& x : jumpMap) {
        if (subsong != GET_CODE_SUBSONG(x.first)) {
          continue;
        }
        if (channel != GET_CODE_CHANNEL(x.first)) {
          continue;
        }
        size_t address = GET_CODE_ADDRESS(x.first);
        jumpAddresses[x.second] = positionMap[address];
      }

      streamDataOffset += (dataStream->bytesUsed() << 3);
      
    }
  }

  // validate bitstream
  streamDataOffset = (dataOffset << 3);
  std::map<AlphaCode, size_t> jumpDistanceMap;
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < 2; channel += 1) {
      auto &codeSequence = codeSequences[subsong][channel];
      auto dataStream = dataStreams[subsong][channel];
      dataStream->seek(0);
      auto jumpStream = jumpStreams[subsong][channel];
      jumpStream->seek(0);
      size_t i = 0;
      size_t lastPos = 0;
      size_t maxPos = 0;
      while (dataStream->hasBits()) {
        size_t streamPosition = dataStream->position();
        AlphaCode code;
        AlphaCode nextCommand = abstractCodeTree->decode(dataStream);
        switch (nextCommand) {
          case CODE_WRITE_DELTA_111: {
            AlphaCode control = controlTree->decode(dataStream);
            CHANGE_STATE cc = (CHANGE_STATE) (control >> 8);
            unsigned char cx = control & 0x0f;

            // AlphaCode frequency = frequencyTree->decode(dataStream);
            // CHANGE_STATE fc = (CHANGE_STATE) (frequency >> 8);
            // unsigned char fx = frequency & 0x1f;
            CHANGE_STATE fc = CHANGE_STATE::WRITE;
            unsigned char fx = dataStream->readBits(5);

            AlphaCode volume = volumeTree->decode(dataStream);
            CHANGE_STATE vc = (CHANGE_STATE) (volume >> 8);
            unsigned char vx = volume & 0xff;

            code = CODE_WRITE_DELTA(cc, cx, fc, fx, vc, vx, 1);
            break;
          }

          case CODE_WRITE_DELTA_011: {

            // AlphaCode frequency = frequencyTree->decode(dataStream);
            // CHANGE_STATE fc = (CHANGE_STATE) (frequency >> 8);
            // unsigned char fx = frequency & 0x1f;
            CHANGE_STATE fc = CHANGE_STATE::WRITE;
            unsigned char fx = dataStream->readBits(5);

            AlphaCode volume = volumeTree->decode(dataStream);
            CHANGE_STATE vc = (CHANGE_STATE) (volume >> 8);
            unsigned char vx = volume & 0xff;

            code = CODE_WRITE_DELTA(CHANGE_STATE::NOOP, 0, fc, fx, vc, vx, 1);
            break;
          }

          case CODE_WRITE_DELTA_001: {

            AlphaCode volume = volumeTree->decode(dataStream);
            CHANGE_STATE vc = (CHANGE_STATE) (volume >> 8);
            unsigned char vx = volume & 0xff;

            code = CODE_WRITE_DELTA(CHANGE_STATE::NOOP, 0, CHANGE_STATE::NOOP, 0, vc, vx, 1);
            break;
          }

          case CODE_WRITE_DELTA_010: {

            // AlphaCode frequency = frequencyTree->decode(dataStream);
            // CHANGE_STATE fc = (CHANGE_STATE) (frequency >> 8);
            // unsigned char fx = frequency & 0x1f;
            CHANGE_STATE fc = CHANGE_STATE::WRITE;
            unsigned char fx = dataStream->readBits(5);

            code = CODE_WRITE_DELTA(CHANGE_STATE::NOOP, 0, fc, fx, CHANGE_STATE::NOOP, 0, 1);
            break;
          }

          case CODE_ABSTRACT_JUMP: {
            size_t nextAddress;
            bool isAddress = dataStream->readBit();
            if (isAddress) {
              nextAddress = dataStream->readBits(15);
            } else {
              size_t index = dataStream->readBits(6);
              nextAddress = jumpAddresses[index];
            }
            nextAddress -= streamDataOffset;
            long distance = ((long) nextAddress) - streamPosition;
            jumpDistanceMap[distance > 0 ? distance : (((uint64_t)1) << 56) | -distance]++;
            lastPos = dataStream->position();
            if (maxPos < lastPos) {
              maxPos = lastPos;
            }
            dataStream->seek(nextAddress);
            continue;
          }

          case CODE_ABSTRACT_PAUSE: {
            AlphaCode dx = durationTree->decode(dataStream);
            code = CODE_PAUSE((unsigned char) dx & 0x0f);
            break;
          }

          case CODE_ABSTRACT_SUSTAIN: {
            AlphaCode dx = durationTree->decode(dataStream);
            code = CODE_SUSTAIN((unsigned char) dx & 0x0f);
            break;
          }

          case CODE_POP: {
            // jump and seek
            bool isSkip = jumpStream->readBit();
            if (isSkip) {
              continue;
            }
            bool isReturn = jumpStream->readBit();
            if (isReturn) {
              bool frontOrLast = jumpStream->readBit();
              size_t nextAddress = frontOrLast ? maxPos : lastPos;
              dataStream->seek(nextAddress);
              continue;

            }
            size_t nextAddress;
            bool isAddress = jumpStream->readBit();
            if (isAddress) {
              nextAddress = jumpStream->readBits(15);
            } else {
              size_t index = jumpStream->readBits(6);
              nextAddress = jumpAddresses[index];
            }
            if (0 == nextAddress) {
              // POP
              code = 0;
              break;
            }
            nextAddress -= streamDataOffset;
            lastPos = dataStream->position();
            if (maxPos < lastPos) {
              maxPos = lastPos;
            }
            dataStream->seek(nextAddress);
            continue;
          }

          default:
            logD("bad code %08x", nextCommand);
            assert(false);
        }
        AlphaCode codeToCompare = codeSequence[i++];
        if (code != codeToCompare) {
          logD("%d/%d: (%d/%d) [%d, %d] %016x ?= %016x", i, codeSequence.size(), streamPosition, dataStream->size(), lastPos, maxPos, code, codeToCompare);
        }
        assert(code == codeToCompare);
      }
      logD("ss %d ch %d is valid at %d/%d %d", subsong, channel, i, codeSequence.size(), dataStream->position());
      assert(i == codeSequence.size());
      assert(!dataStream->hasBits());
      assert(!jumpStream->hasBits());
      logD("end of codes code %016x", codeSequence[codeSequence.back()]);
      logD("end of compressed code %016x", compressedCodeSequences[subsong][channel].back());
      logD("end of jumps code %016x", jumpSequences[subsong][channel].back());

      streamDataOffset += (dataStream->bytesUsed() << 3);
    }
  }

  SHOW_FREQUENCIES(jumpDistanceMap);

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
  delete abstractCodeTree;
  delete controlTree;
  delete frequencyTree;
  delete volumeTree;
  delete durationTree;


  trackData->writeText(fmt::sprintf("\n\n; Song data size: %d\n", songDataSize));
  trackData->writeText(fmt::sprintf("; Compressed Code Sequence Length: %d\n", totalCompressedCodeSequenceSize));
  trackData->writeText(fmt::sprintf("; Jump Sequence Length: %d\n", totalJumpSequenceSize));
  trackData->writeText(fmt::sprintf("; Compressed Bytes %d\n", totalCompressedBytes));

  ret.push_back(DivROMExportOutput("Track_data.asm", trackData));

}

void DivExportAtari2600::validateCodeSequence(
  int subsong,
  int channel,
  const std::vector<AlphaCode>&codeSequence,
  const std::vector<AlphaCode> &compressedCodeSequence,
  const std::vector<AlphaCode> &jumpSequence
) {
  // Test compression correctness 
  // std::vector<AlphaCode> uncompressedSequence;
  // uncompressedSequence.reserve(alphaSequence.size());
  auto it = jumpSequence.begin();
  size_t j = 0;
  size_t maxOffset = 0;
  size_t returnAddress = 0;
  for (size_t i = 0; i < compressedCodeSequence.size(); ) {
    AlphaCode c = compressedCodeSequence[i];
    CODE_TYPE type = GET_CODE_TYPE(c);
    if (type == CODE_TYPE::POP) {
      AlphaCode codeJump = *it++;
      CODE_TYPE jumpType = GET_CODE_TYPE(codeJump);

      if (jumpType == CODE_TYPE::POP) {
        AlphaCode x = codeSequence[j];
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
          if(codeJump == CODE_RETURN_LAST) {
            i = returnAddress;

          } else if(codeJump == CODE_RETURN_FRONT) {
            i = maxOffset;

          } else {
            assert(GET_CODE_TYPE(codeJump) == CODE_TYPE::JUMP);
            size_t jumpAddress = GET_CODE_ADDRESS(codeJump);
            returnAddress = i + 1;
            if (returnAddress >= maxOffset) {
              maxOffset = returnAddress;
            }
            i = jumpAddress;
          }
        }
      } else {
        logD("%d %d | %d: %08x bad jump code %08x (%d)",subsong, channel, i, c, codeJump, j);
        logD("fail at %d", i);
        assert(false);

      }
    } else if (type == CODE_TYPE::JUMP) {
      // GOTO
      size_t jumpAddress = GET_CODE_ADDRESS(c);
      returnAddress = i + 1;
      if (returnAddress >= maxOffset) {
        maxOffset = returnAddress;
      }
      i = jumpAddress;
    } else if (type == CODE_TYPE::RETURN) {
      if (c == CODE_RETURN_FRONT) {
        i = maxOffset;
      } else {
        i = returnAddress;
      }
      returnAddress = i + 1;
      if (returnAddress >= maxOffset) {
        maxOffset = returnAddress;
      }

    } else {
      AlphaCode x = codeSequence[j];
      if (c != x) {
        logD("%d %d | %d: %08x    %08x",subsong, channel, i-1, compressedCodeSequence[i-1], codeSequence[j-1]);
        logD("%d %d | %d: %08x <> %08x (%d)",subsong, channel, i, c, x, j);
        logD("%d %d | %d: %08x    %08x",subsong, channel, i+1, compressedCodeSequence[i+1], codeSequence[j+1]);
        logD("fail at %d", i);
        assert(false);
      }
      // uncompressedSequence.emplace_back(c);
      i++;
      j++;
    }
  } 
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

size_t DivExportAtari2600::encodeChannelStateCodes(
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
  CHANGE_STATE cc = audcx != last.registers[0] ? CHANGE_STATE::WRITE : CHANGE_STATE::NOOP;
  unsigned char audfx = next.registers[1];
  CHANGE_STATE fc = audfx != last.registers[1] ? CHANGE_STATE::WRITE : CHANGE_STATE::NOOP;
  unsigned char audvx = next.registers[2];
  CHANGE_STATE vc = audvx != last.registers[2] ? CHANGE_STATE::WRITE : CHANGE_STATE::NOOP;
  // BUGBUG INC/DEC
  if (audvx == last.registers[2] + 1) {
    audvx = 0x10;
  } else if (last.registers[2] == audvx + 1) {
    audvx = 0xf0;
  }

  // BUGBUG: this is important, a sustain is likely to come after a node
  // maybe not a pause
  unsigned char dx = 1; //framecount > 2 ? 2 : framecount;
  framecount = framecount - dx;

  // BUGBUG: this is also important, seldom make control changes by themselves
  if (cc > 0) {
    fc = vc = CHANGE_STATE::WRITE;
  };

  size_t codesWritten = 0;
  if (audvx == 0) {
    out.emplace_back(CODE_PAUSE(dx));
    codesWritten++;
  } else if (cc + fc + vc > 0) {
    out.emplace_back(CODE_WRITE_DELTA(
      cc,
      cc == CHANGE_STATE::NOOP ? 0 : audcx,
      fc,
      fc == CHANGE_STATE::NOOP ? 0 : audfx,
      vc,
      vc == CHANGE_STATE::NOOP ? 0 : audvx,
      dx
    ));
    codesWritten++;
  }

  while (framecount > 0) {
    unsigned char dx = framecount > 32 ? 32 : framecount;
    framecount = framecount - dx;
    out.emplace_back(CODE_SUSTAIN(dx));
    codesWritten++;
  } 

  return codesWritten;
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
