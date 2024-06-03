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

DivExportAtari2600::DivExportAtari2600(DivEngine *e) {
  String exportTypeString = e->getConfString("romout.tiaExportType", "COMPACT");
  logD("retrieving config exportType [%s]", exportTypeString);
  if (exportTypeString == "RAW") {
    exportType = DIV_EXPORT_TIA_RAW;
  } else if (exportTypeString == "BASIC") {
    exportType = DIV_EXPORT_TIA_BASIC;
  } else if (exportTypeString == "BASICX") {
    exportType = DIV_EXPORT_TIA_BASICX;
  } else if (exportTypeString == "DELTA") {
    exportType = DIV_EXPORT_TIA_DELTA;
  } else if (exportTypeString == "COMPACT") {
    exportType = DIV_EXPORT_TIA_COMPACT;
  } else if (exportTypeString == "CRUSHED") {
    exportType = DIV_EXPORT_TIA_CRUSHED;
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
    case DIV_EXPORT_TIA_BASICX:
      writeTrackDataBasic(e, true, true, registerWrites, ret);
      break;
    case DIV_EXPORT_TIA_DELTA:
      writeTrackDataDelta(e, registerWrites, ret);
      break;
    case DIV_EXPORT_TIA_COMPACT:
      writeTrackDataCompact(e, registerWrites, ret);
      break;
    case DIV_EXPORT_TIA_CRUSHED:
      writeTrackDataCrushed(e, registerWrites, ret);
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
    for (auto &write : registerWrites[subsong]) {
      dump->writeText(fmt::sprintf("; IDX%d %d.%d: SS%d ORD%d ROW%d SYS%d> %d = %d\n",
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

// Delta encoding
void DivExportAtari2600::writeTrackDataDelta(
  DivEngine* e,
  std::vector<RegisterWrite> *registerWrites,
  std::vector<DivROMExportOutput> &ret
) {
  size_t numSongs = e->song.subsong.size();

  // write track audio data
  SafeWriter* trackData = new SafeWriter;
  trackData->init();
  trackData->writeText("; Furnace Tracker audio data file\n");
  trackData->writeText("; Delta coded format\n");
  trackData->writeText(fmt::sprintf("; Song: %s\n", e->song.name));
  trackData->writeText(fmt::sprintf("; Author: %s\n", e->song.author));

  trackData->writeText(fmt::sprintf("\nAUDIO_NUM_TRACKS = %d\n", numSongs));
  
  trackData->writeText("\n#include \"cores/delta_player_core.asm\"\n");

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
        channel == 0 ? channel0AddressMap : channel1AddressMap,
        dumpSequence
      );
      trackData->writeText(fmt::sprintf("AUDIO_TRACK_%d_%d = . - AUDIO_DATA + 1\n", subsong, channel));
      ChannelState last(dumpSequence.initialState);
      std::vector<unsigned char> codeSeq;
      for (auto& n: dumpSequence.intervals) {
        codeSeq.clear();
        trackDataSize += encodeChannelState(n.state, n.duration, last, codeSeq);
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

// compacted encoding
void DivExportAtari2600::writeTrackDataCompact(
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
        channel == 0 ? channel0AddressMap : channel1AddressMap,
        channelSequences[channel],
        registerDumps);
    }
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

  trackData->writeText("\n#include \"cores/compact_player_core.asm\"\n");

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
    for (auto& n: dump.intervals) {
      codeSeq.clear();
      waveformDataSize += encodeChannelState(n.state, n.duration, last, codeSeq);
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

// BUGBUG: make macro/inline
AlphaCode CODE_LITERAL(unsigned char x) {
  return (AlphaCode)x;
}

// BUGBUG: make macro/inline
AlphaCode CODE_JUMP(size_t index) {
  return (AlphaCode)(0xff0000 | index);
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

// compacted encoding
void DivExportAtari2600::writeTrackDataCrushed(
  DivEngine* e, 
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

  trackData->writeText("\n#include \"cores/crushed_player_core.asm\"\n");

  // encode command streams
  size_t totalStates = 0;
  size_t totalBytes = 0;
  std::map<AlphaCode, size_t> frequencyMap;
  std::map<AlphaCode, std::map<AlphaCode, size_t>> branchMap;
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
        channel == 0 ? channel0AddressMap : channel1AddressMap,
        dumpSequence
      );

      // convert to code
      ChannelState last(dumpSequence.initialState);
      std::vector<unsigned char> codeSeq;
      AlphaCode lastCode = 0;
      for (auto& n: dumpSequence.intervals) {
        codeSeq.clear();
        totalStates++;
        encodeChannelState(n.state, n.duration, last, codeSeq);
        for (size_t i = 0; i < codeSeq.size(); i++) {
          AlphaCode c = 1 << 8 | codeSeq[i];
          totalBytes++;
          frequencyMap[c]++;
          branchMap[lastCode][c]++;
          codeSequences[subsong][channel].emplace_back(c);
          lastCode = c;
        }
        last = n.state;
      }      
      totalBytes++;
      frequencyMap[0]++;
      branchMap[lastCode][0]++;
      codeSequences[subsong][channel].emplace_back(0);
    }
  }

  // index all distinct codes into an "alphabet" so we can build a suffix tree
  std::vector<AlphaCode> alphabet;
  std::map<AlphaCode, AlphaChar> index;
  createAlphabet(
    frequencyMap,
    alphabet,
    index
  );

  // statistics
  size_t singletons = 0;
  size_t bigrams = 0;
  size_t maxbranch = 0;
  AlphaCode maxcode = 0;
  for (auto& x : branchMap) {
    bigrams += x.second.size();
    if (x.second.size() > maxbranch) {
      maxbranch = x.second.size();
      maxcode = x.first;
    }
    if (x.second.size() == 1) {
      singletons++;
    }
  }
  logD("total codes : %d ", frequencyMap.size());
  logD("maxbranch %08x : %d ", maxcode, maxbranch);
  logD("singletons : %d ", singletons);
  logD("bigrams : %d ", bigrams);

  // debugging: compute basic stats
  logD("total number of state transitions: %d", totalStates);
  logD("total number of byte codes: %d", totalBytes);
  logD("distinct codes: %d", alphabet.size());  
  for (auto a : alphabet) {
    logD("  %08x -> %d (rank %d)", a, frequencyMap[a], index.at(a));
  }
  CALC_ENTROPY(frequencyMap);
  

  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < 2; channel += 1) {
      std::vector<AlphaChar> alphaSequence;
      alphaSequence.reserve(codeSequences[subsong][channel].size());         

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

      // compress
      std::vector<Span> spans;
      Span currentSpan((int)subsong, channel, 0, 0);
      Span nextSpan((int)subsong, channel, 0, 0);
      std::vector<std::map<AlphaCode, size_t>> jumpMaps;
      jumpMaps.resize(alphaSequence.size());
      for (size_t i = 0; i < alphaSequence.size(); ) {
        root->find_prior(i, alphaSequence, nextSpan);
        if (nextSpan.length > 4) {
          // take prior span
          if (currentSpan.length > 0) {
            spans.emplace_back(currentSpan);
          }
          spans.emplace_back(nextSpan);
          jumpMaps[i][nextSpan.start] += 1;
          size_t returnPoint = i + nextSpan.length;
          size_t nextSpanEnd = nextSpan.start + nextSpan.length;
          for (size_t j = nextSpan.start; j < nextSpanEnd; j++) {
            jumpMaps[j][CODE_JUMP(j+1)]++;
          }
          i += nextSpan.length;
          currentSpan.start = i;
          currentSpan.length = 0;
          jumpMaps[nextSpanEnd][CODE_JUMP(returnPoint)] += 1;
        } else {
          // continue current span
          jumpMaps[i][CODE_JUMP(i+1)] = 1;
          currentSpan.length++;
          i++;
        }
      }
      if (currentSpan.length > 0) {
        spans.emplace_back(currentSpan);
      }
      size_t maxJumps = 0;
      for (auto &jumpMap : jumpMaps) {
        if (jumpMap.size() > maxJumps) {
          maxJumps = jumpMap.size();
        }
      }

     size_t bitsNeeded = 0;
      std::vector<AlphaCode> compressedSequence;
      std::vector<AlphaCode> jumps;
      size_t lastSpanEnd = 0;
      for (auto &span: spans) {
        size_t nextSpanEnd = span.start + span.length;
        if (span.start < lastSpanEnd) {
          // write a mandatory jump
          auto &jumpMap = jumpMaps[nextSpanEnd];
          bitsNeeded += 1;
          if (jumpMap.size() > 1) {
            logD("?? graph %d", jumpMap.size());
          }
          jumps.emplace_back(0xf0);
          jumps.emplace_back(0xf0);
        } else {
          for (size_t i = span.start; i < span.start + span.length; i++) {
            auto &jumpMap = jumpMaps[i];
            if (jumpMap.size() > 1) {
              bitsNeeded += CALC_ENTROPY(jumpMap);
            }
            for (size_t i = 1; i < jumpMap.size(); i++) {
              jumps.emplace_back(0xf0);
              jumps.emplace_back(0xf0);
            }
            compressedSequence.emplace_back(codeSequences[subsong][channel][i]);
          }
        }
        lastSpanEnd += span.length;
      }
      logD("maxbytes %d", maxJumps);
      logD("COMPRESSEDSIZE %d", compressedSequence.size());
      logD("JUMPS %d", jumps.size());
      logD("BITSTREAMESTIMATE %d (%d)", ((bitsNeeded + 8)/ 8), bitsNeeded);
      logD("total %d", jumps.size() + compressedSequence.size() + ((bitsNeeded + 8)/ 8));
      // for (size_t i = 0; i < codeSequences[subsong][channel].size(); i++) {
      //   assert(codeSequences[subsong][channel][i] == compressedSequence[i]);
      // }
      
      //  i = 0; i < alphaSequence.size(); ) {
      //   root->find_prior(i, alphaSequence, nextSta);
      //   if (s.length > 4) {
      //     jumpStreamBits += 1;
      //     for (int j = s.start; j < s.start + s.length; j++) {
      //       if (jumps[j].size() > 1) {
      //         jumpStreamBits += 1;
      //       }
      //     }
      //     jumpStreamBits += jumps[s.start + s.length].size();
      //     i += s.length;
      //   } else {
      //     i++;
      //   }
      // }
      // logD("ESTIMATED %d / %d", estimatedBytes, alphaSequence.size());
      // logD("JUMPSTREAM BYTES %d (%d) MAX=%d", ((jumpStreamBits + 8) / 8), jumpStreamBits, maxJumps);

      // clean up
      delete root;

    }
  }

  testCV("abaxcabaxabz");

  ret.push_back(DivROMExportOutput("Track_data.asm", trackData));

}

/**
 *  Write note data. Format 0:
 * 
 *   fffff010 ccccvvvv           frequency + control + volume, duration 1
 *   fffff110 ccccvvvv           " " ", duration 2
 *   ddddd100                    sustain d frames
 *   ddddd000                    pause d frames
 *   xxxx0001                    volume = x >> 4, duration 1 
 *   xxxx1001                    volume = x >> 4, duration 2
 *   xxxx0101                    control = x >> 4, duration 1
 *   xxxx1101                    control = x >> 4, duration 2
 *   xxxxx011                    frequency = x >> 3, duration 1
 *   xxxxx111                    frequency = x >> 3, duration 2
 *   00000000                    stop
 */
size_t DivExportAtari2600::encodeChannelState(
  const ChannelState& next,
  const char duration,
  const ChannelState& last,
  std::vector<unsigned char> &out)
{
  size_t bytesWritten = 0;

  // when duration is zero... some kind of rounding issue has happened... we force to 1...
  if (duration == 0) {
      logD("0 duration note");
  }
  char framecount = duration > 0 ? duration : 1; 

  unsigned char audfx, audcx, audvx;
  int cc, fc, vc;
  audcx = next.registers[0];
  cc = audcx != last.registers[0];
  audfx = next.registers[1];
  fc = audfx != last.registers[1];
  audvx = next.registers[2];
  vc = audvx != last.registers[2];
  int delta = (cc + fc + vc);
  
  //w->writeText(fmt::sprintf("    ;F%d C%d V%d D%d\n", audfx, audcx, audvx, duration));

  if (audvx == 0) {
    // volume is zero, pause
    unsigned char dmod = framecount > 31 ? 31 : framecount;
    framecount -= dmod;
    unsigned char rx = dmod << 3;
    //w->writeText(fmt::sprintf("    byte %d; PAUSE %d\n", rx, dmod));
    out.emplace_back(rx);
    bytesWritten += 1;
    
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
    bytesWritten += 1;

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
    bytesWritten += 1;

    // waveform and volume
    unsigned char cvx = (audcx << 4) + audvx;
    //w->writeText(fmt::sprintf(",%d\n", y));
    out.emplace_back(cvx);
    bytesWritten += 1;

  }

  // when delta is zero / we have leftover frames, sustain
  while (framecount > 0) {
    unsigned char dmod;
    if (framecount > 32) {
      dmod = 31;
      framecount -= 32;
    } else {
      dmod = framecount - 1;
      framecount = 0;
    }
    unsigned char sx =  dmod << 3 | 0x04;
    //w->writeText(fmt::sprintf("    byte %d; SUSTAIN %d\n", sx, dmod + 1));
    out.emplace_back(sx);
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
