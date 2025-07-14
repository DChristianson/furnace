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

#include "batariBasic.h"
#include "tiaZip.h"

#include <fmt/printf.h>
#include "../../ta-log.h"

static const std::map<unsigned int, unsigned int> batariChannel0AddressMap = {
  {AUDC0, 0},
  {AUDF0, 1},
  {AUDV0, 2},
};

static const std::map<unsigned int, unsigned int> batariChannel1AddressMap = {
  {AUDC1, 0},
  {AUDF1, 1},
  {AUDV1, 2},
};

bool DivExportBatari::go(DivEngine* eng) {
  progress[0].name = "Export";
  progress[0].amount = 0.0f;

  e = eng;
  running = true;
  failed = false;
  mustAbort = false;
  exportThread = new std::thread(&DivExportBatari::run, this);
  return true;
}

void DivExportBatari::wait() {
  if (exportThread!=NULL) {
    exportThread->join();
    delete exportThread;
  }
}

void DivExportBatari::abort() {
  mustAbort=true;
  wait();
}

bool DivExportBatari::isRunning() {
  return running;
}

bool DivExportBatari::hasFailed() {
  return failed;
}

DivROMExportProgress DivExportBatari::getProgress(int index) {
  return progress[0];
}

void DivExportBatari::run() {

  bool encodeDuration = conf.getBool("encodeDuration", true);

  // write track data
  writeTrackDataBasic(encodeDuration, true);

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
  output.push_back(DivROMExportOutput("Track_meta.asm", titleData));
  running = false;
}


// simple register dump with separate tables for frequency and control / volume
void DivExportBatari::writeTrackDataBasic(
  bool encodeDuration,
  bool independentChannelPlayback
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
    RegisterDump writes(e, subsong);
    for (int channel = 0; channel < 2; channel++) {
      // limit to 1 frame per note
      dumpSequences[subsong][channel].maxIntervalDuration = encodeDuration ? 8 : 1;
      writes.writeChannelStateSequence(
        channel,
        0,
        -1,
        channel == 0 ? batariChannel0AddressMap : batariChannel1AddressMap,
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

  output.push_back(DivROMExportOutput("Track_data.asm", trackData));

}