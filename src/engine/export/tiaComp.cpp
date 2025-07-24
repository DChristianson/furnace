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

#include "tiaComp.h"

#include <fmt/printf.h>
#include "../../ta-log.h"

bool DivExportTIAComp::go(DivEngine* eng) {
  progress[0].name = "Export";
  progress[0].amount = 0.0f;

  e = eng;
  running = true;
  failed = false;
  mustAbort = false;
  exportThread = new std::thread(&DivExportTIAComp::run, this);
  return true;
}

void DivExportTIAComp::wait() {
  if (exportThread!=NULL) {
    exportThread->join();
    delete exportThread;
  }
}

void DivExportTIAComp::abort() {
  mustAbort=true;
  wait();
}

bool DivExportTIAComp::isRunning() {
  return running;
}

bool DivExportTIAComp::hasFailed() {
  return failed;
}

DivROMExportProgress DivExportTIAComp::getProgress(int index) {
  return progress[0];
}

void DivExportTIAComp::run() {

  auto codec = conf.getString("codec", "tiacomp");
  auto addressBits = conf.getInt("addressBits", 8);
  auto debugRegisterDump = conf.getBool("debug", false);

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
  // according to codec
  if (codec == "basic") {
    // basic encoding for each frame
    writeTrackDataBasic(false, true, addressBits);

  } else if (codec == "basicx") {
    // basic encoding, with durations
    writeTrackDataBasic(true, true, addressBits);

  } else if (codec == "tiacomp") {
    // compact delta encoding
    writeTrackDataTIAComp(addressBits);

  }else if (codec == "fseq") {
    // furnace sequence encoding
    writeTrackDataFSeq();

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


// simple register dump with separate tables for frequency and control / volume
void DivExportTIAComp::writeTrackDataBasic(
  bool encodeDuration,
  bool independentChannelPlayback,
  int addressBits
) {
  size_t numSongs = e->song.subsong.size();

  // only allow 256 byte or 4K address modes
  if (addressBits != 8 && addressBits != 12) {
      throw new std::runtime_error("only 8 bit (page) or 12 bit (4k bytes) addressing is supported");
  }

  // write track audio data
  SafeWriter* trackData = new SafeWriter;
  trackData->init();
  trackData->writeText("; Furnace Tracker audio data file\n");
  trackData->writeText("; Basic data format\n");
  trackData->writeText(fmt::sprintf("; Song: %s\n", e->song.name));
  trackData->writeText(fmt::sprintf("; Author: %s\n", e->song.author));

  trackData->writeText(fmt::sprintf("\nAUDIO_NUM_TRACKS = %d\n", numSongs));
  trackData->writeText(fmt::sprintf("\nAUDIO_TRACK_ADDRESS_BITS = %d\n", addressBits));

  if (encodeDuration) {
    trackData->writeText("\n#include \"cores/basicx_player_core.asm\"\n");
  } else {
    trackData->writeText("\n#include \"cores/basic_player_core.asm\"\n");
  }

  // create a relocatable lookup table (for use in player apps)
  trackData->writeText("    MAC AUDIO_CONTROL_TABLE\n");
  size_t songDataSize = 0;
  if (independentChannelPlayback) {
    // one track table per channel
    for (int channel = 0; channel < 2; channel++) {
      if (addressBits <= 8) {
        trackData->writeText(fmt::sprintf("AUDIO_TRACKS_%d:\n", channel));
        for (size_t subsong = 0; subsong < numSongs; subsong++) {
          trackData->writeText(fmt::sprintf("    byte AUDIO_TRACK_%d_%d\n", subsong, channel));
          songDataSize += 1;
        }
      } else {
        trackData->writeText(fmt::sprintf("AUDIO_TRACKS_%d_LO:\n", channel));
        for (size_t subsong = 0; subsong < numSongs; subsong++) {
          trackData->writeText(fmt::sprintf("    byte <AUDIO_TRACK_%d_%d\n", subsong, channel));
          songDataSize += 1;
        }
        trackData->writeText(fmt::sprintf("AUDIO_TRACKS_%d_HI:\n", channel));
        for (size_t subsong = 0; subsong < numSongs; subsong++) {
          trackData->writeText(fmt::sprintf("    byte >AUDIO_TRACK_%d_%d\n", subsong, channel));
          songDataSize += 1;
        }
      }
    }

  } else {
    if (addressBits <= 8) {
      // one track table for both channels
      trackData->writeText("AUDIO_TRACKS\n");
      for (size_t i = 0; i < e->song.subsong.size(); i++) {
        trackData->writeText(fmt::sprintf("    byte AUDIO_TRACK_%d\n", i));
        songDataSize += 1;
      }
    } else {
      // one track table for both channels
      trackData->writeText("AUDIO_TRACKS_LO\n");
      for (size_t i = 0; i < e->song.subsong.size(); i++) {
        trackData->writeText(fmt::sprintf("    byte <AUDIO_TRACK_%d\n", i));
        songDataSize += 1;
      }
      trackData->writeText("AUDIO_TRACKS_HI\n");
      for (size_t i = 0; i < e->song.subsong.size(); i++) {
        trackData->writeText(fmt::sprintf("    byte >AUDIO_TRACK_%d\n", i));
        songDataSize += 1;
      }
    }

  }
  trackData->writeText("    ENDM\n");

  // dump sequences
  size_t sizeOfAllSequences = 0;
  size_t sizeOfAllSequencesPerChannel[2] = {0, 0};
  ChannelStateSequence dumpSequences[numSongs][2];
  for (size_t subsong = 0; subsong < numSongs; subsong++) {
    auto registerDump = registerDumps[subsong];
    for (int channel = 0; channel < 2; channel++) {
      // if encodeDuration is false, limit to 1 frame per note
      // 1 frame per note will chew up a lot of ROM
      dumpSequences[subsong][channel].maxIntervalDuration = encodeDuration ? 8 : 1;
      registerDump->writeChannelStateSequence(
        channel,
        0,
        -1,
        channel == 0 ? tiaChannel0AddressMap : tiaChannel1AddressMap,
        dumpSequences[subsong][channel]
      );
      size_t totalDataPointsThisSequence = dumpSequences[subsong][channel].size() + 1;
      sizeOfAllSequences += totalDataPointsThisSequence;
      sizeOfAllSequencesPerChannel[channel] += totalDataPointsThisSequence;
    }
  }

  const size_t maxSequenceBytes = 1 << addressBits;
  if (independentChannelPlayback) {
    // channels do not have to be synchronized, can be played back independently
    if (sizeOfAllSequences > maxSequenceBytes) {
      String msg = fmt::sprintf(
        "cannot export data in this format: data sequence has %d > %d data points",
        sizeOfAllSequences,
        maxSequenceBytes
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
    if (sizeOfAllSequencesPerChannel[0] > maxSequenceBytes) {
      String msg = fmt::sprintf(
        "cannot export data in this format: data sequence has %d > %d data points",
        sizeOfAllSequencesPerChannel[0],
        maxSequenceBytes
      );
      logE(msg.c_str());
      throw new std::runtime_error(msg);
    }
  }

  // Frequencies table
  size_t freqTableSize = 0;
  trackData->writeText("\n    ; FREQUENCY TABLE\n");
  if (addressBits <=8 && independentChannelPlayback) {
    trackData->writeText("AUDIO_F:\n");
  }
  for (int channel = 0; channel < 2; channel++) {
    if (addressBits <=8 && !independentChannelPlayback) {
      trackData->writeText(fmt::sprintf("AUDIO_F_%d:\n", channel));
    }
    for (size_t subsong = 0; subsong < numSongs; subsong++) {
      trackData->writeText(fmt::sprintf("    ; TRACK %d, CHANNEL %d\n", subsong, channel));
      if (independentChannelPlayback) {
        if (addressBits > 8) {
          trackData->writeText(fmt::sprintf("AUDIO_TRACK_%d_%d\n", subsong, channel));
        } else {
          trackData->writeText(fmt::sprintf("AUDIO_TRACK_%d_%d = . - AUDIO_F + 1\n", subsong, channel));
        }
      } else if (channel == 0) {
        if (addressBits > 8) {
          trackData->writeText(fmt::sprintf("AUDIO_TRACK_%d\n", subsong, channel));
        } else {
          trackData->writeText(fmt::sprintf("AUDIO_TRACK_%d = . - AUDIO_F + 1\n", subsong, channel));
        }
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
        if (addressBits > 8) {
          // interleave frequency, cx, vx when using 2-byte addressing
          unsigned char cx = n.state.registers[0];
          unsigned char vx = n.state.registers[2];
          // if volume is zero, make cx nonzero
          unsigned char rx = (vx == 0 ? 0xf0 : cx << 4) | vx; 
          trackData->writeText(fmt::sprintf(",%d", rx));
        }
        freqTableSize += 1;
      }
      trackData->writeText(fmt::sprintf("\n    byte 0;\n"));
      freqTableSize += 1;
    }
  }

  // Control-volume table
  size_t cvTableSize = 0;
  if (addressBits <= 8) {
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
  }

  trackData->writeC('\n');
  trackData->writeText(fmt::sprintf("; Num Tracks %d\n", numSongs));
  trackData->writeText(fmt::sprintf("; All Tracks Sequence Length %d\n", sizeOfAllSequences));
  trackData->writeText(fmt::sprintf("; Track Table Size %d\n", songDataSize));
  trackData->writeText(fmt::sprintf("; Freq Table Size %d\n", freqTableSize));
  trackData->writeText(fmt::sprintf("; CV Table Size %d\n", cvTableSize));
  size_t totalDataSize = songDataSize + freqTableSize + cvTableSize;
  trackData->writeText(fmt::sprintf("; Total Data Size %d\n", totalDataSize));

  output.push_back(DivROMExportOutput("Track_data.asm", trackData));

}

// Compact delta encoding
void DivExportTIAComp::writeTrackDataTIAComp(int addressBits) {

  // write track audio data
  SafeWriter* trackData = new SafeWriter;
  trackData->init();
  trackData->writeText("; Furnace Tracker audio data file\n");
  trackData->writeText("; TIAComp delta encoding\n");
  trackData->writeText(fmt::sprintf("; Song: %s\n", e->song.name));
  trackData->writeText(fmt::sprintf("; Author: %s\n", e->song.author));

  trackData->writeText(fmt::sprintf("\nAUDIO_NUM_TRACKS = %d\n", registerDumps.size()));
  trackData->writeText(fmt::sprintf("\nAUDIO_TRACK_ADDRESS_BITS = %d\n", addressBits));
  
  trackData->writeText("\n#include \"cores/tiacomp_player_core.asm\"\n");


  // create a lookup table for use in player apps
  trackData->writeText("    MAC AUDIO_CONTROL_TABLE\n");
  size_t songDataSize = 0;
  // one track table per channel
  for (int channel = 0; channel < 2; channel++) {
    if (addressBits > 8) {
      trackData->writeText(fmt::sprintf("AUDIO_TRACKS_%d_LO:\n", channel));
      for (size_t subsong = 0; subsong < registerDumps.size(); subsong++) {
        trackData->writeText(fmt::sprintf("    byte <AUDIO_TRACK_%d_%d\n", subsong, channel));
        songDataSize += 1;
      }
      trackData->writeText(fmt::sprintf("AUDIO_TRACKS_%d_HI:\n", channel));
      for (size_t subsong = 0; subsong < registerDumps.size(); subsong++) {
        trackData->writeText(fmt::sprintf("    byte >AUDIO_TRACK_%d_%d\n", subsong, channel));
        songDataSize += 1;
      }

    } else {
      trackData->writeText(fmt::sprintf("AUDIO_TRACKS_%d:\n", channel));
      for (size_t subsong = 0; subsong < registerDumps.size(); subsong++) {
        trackData->writeText(fmt::sprintf("    byte AUDIO_TRACK_%d_%d\n", subsong, channel));
        songDataSize += 1;
      }
    }
  }
  trackData->writeText("    ENDM\n");

  // dump sequences
  size_t trackDataSize = 0;
  trackData->writeText("AUDIO_DATA:\n");
  for (size_t subsong = 0; subsong < registerDumps.size(); subsong++) {
    auto registerDump = registerDumps[subsong];
    for (int channel = 0; channel < 2; channel++) {
      ChannelStateSequence dumpSequence;
      registerDump->writeChannelStateSequence(
        channel,
        0,
        -1,
        channel == 0 ? tiaChannel0AddressMap : tiaChannel1AddressMap,
        dumpSequence
      );
      if (addressBits > 8) {
        trackData->writeText(fmt::sprintf("AUDIO_TRACK_%d_%d\n", subsong, channel));
      } else {
        trackData->writeText(fmt::sprintf("AUDIO_TRACK_%d_%d = . - AUDIO_DATA + 1\n", subsong, channel));
      }
      ChannelState last(dumpSequence.initialState);
      std::vector<unsigned char> codeSeq;
      for (auto& n: dumpSequence.intervals) {
        codeSeq.clear();
        trackData->writeText(
          fmt::sprintf(
            "    ;F%d C%d V%d D%d - SS:%d O:%d R:%d\n",
            n.state.registers[1],
            n.state.registers[0],
            n.state.registers[2],
            n.duration,
            n.row.subsong,
            n.row.ord,
            n.row.row
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
        if (0 == n.state.registers[2]) {
          last.registers[2] = 0;
        } else {
          last = n.state;
        }
      }
      trackData->writeText("    byte 0\n");
      trackDataSize++;
    }
  }

  trackData->writeC('\n');
  trackData->writeText(fmt::sprintf("; Num Tracks %d\n", registerDumps.size()));
  trackData->writeText(fmt::sprintf("; Track Table Size %d\n", songDataSize));
  trackData->writeText(fmt::sprintf("; Data Table Size %d\n", trackDataSize));
  size_t totalDataSize = songDataSize + trackDataSize;
  trackData->writeText(fmt::sprintf("; Total Data Size %d\n", totalDataSize));

  output.push_back(DivROMExportOutput("Track_data.asm", trackData));

}

// furnace sequence encoding
void DivExportTIAComp::writeTrackDataFSeq() {

  size_t numSongs = e->song.subsong.size();

  // convert to state sequences
  logD("extracting sequences");
  std::map<String, ChannelStateSequence> dumpSequenceMap;
  for (size_t subsong = 0; subsong < numSongs; subsong++) {
    auto registerDump = registerDumps[subsong];
    for (int channel = 0; channel < 2; channel++) {
      // capture the channel dump
      registerDump->writeChannelStateSequenceByRow(
        channel,
        0,
        -1,
        channel == 0 ? tiaChannel0AddressMap : tiaChannel1AddressMap,
        dumpSequenceMap
      );
    }
  }

  // replace any 0 volume subsequences
  logD("removing trivial waveforms");
  for (auto& x: dumpSequenceMap) {
    bool trivial = true;
    for (auto &s: x.second.intervals) {
      if (s.state.registers[2] != 0) {
        trivial = false;
        break;
      }
    }
    if (trivial) {
      for (auto &s: x.second.intervals) {
        s.state.write(0, 0);
        s.state.write(1, 0);
      }
    }
  }

  // compress the patterns into common subsequences
  logD("performing sequence compression");
  std::map<uint64_t, String> commonDumpSequences;
  std::map<uint64_t, unsigned int> frequencyMap;
  std::map<String, String> representativeMap;
  for (auto& x: dumpSequenceMap) {
    uint64_t hash = x.second.hash();
    logD("%s -> %d", x.first.c_str(), hash);
    auto it = commonDumpSequences.emplace(hash, x.first);
    if (it.second) {
      frequencyMap.emplace(hash, 1);
    } else {
      frequencyMap[hash] += 1;
    }
    representativeMap.emplace(x.first, it.first->second);
  }
  for (auto& x: frequencyMap) {
    auto &key = commonDumpSequences[x.first];
    logD("%s (%d): %d", key.c_str(), x.first, x.second);
  }

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
  trackData->writeText("    MAC AUDIO_CONTROL_TABLE\n");
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
  trackData->writeText("    ENDM\n");

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
    trackData->writeText(fmt::sprintf("%s_ADDR\n", x.second.c_str()));
    trackData->writeText(fmt::sprintf("; Hash %d, Freq %d\n", x.first, freq));
    auto& dump = dumpSequenceMap[x.second];
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
      waveformDataSize += encodeChannelState(n.state, n.duration, last, true, codeSeq);
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

  output.push_back(DivROMExportOutput("Track_data.asm", trackData));

}

/**
 *  Write note data using delta encoding.
 * 
 *   fffff010 ccccvvvv           frequency + control + volume, duration 1
 *   fffff110 ccccvvvv           " " ", duration 2
 *   ddddd100                    sustain d+1 frames
 *   ddddd000                    pause d frames
 *   xxxx0001                    volume = x >> 4, duration 1 
 *   xxxx1001                    volume = x >> 4, duration 2
 *   xxxx0101                    control = x >> 4, duration 1
 *   xxxx1101                    control = x >> 4, duration 2
 *   xxxxx011                    frequency = x >> 3, duration 1
 *   xxxxx111                    frequency = x >> 3, duration 2
 *   00000000                    stop
 */
int DivExportTIAComp::encodeChannelState(
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
    if (framecount > 32) {
      dmod = 31;
      framecount -= 32;
    } else {
      dmod = framecount - 1;
      framecount = 0;
    }
    unsigned char rx = (dmod > 0) ? dmod << 3 : 0x01; 
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
    out.emplace_back(fdx);

    // waveform and volume
    unsigned char cvx = (audcx << 4) + audvx;
    out.emplace_back(cvx);

  }

  if (delta > 0 && !encodeRemainder) {
    return framecount;
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
  }

  return 0;

}

DivExportTIAComp::~DivExportTIAComp() {
  for (auto registerDump : registerDumps) {
    delete registerDump;
  }
}
