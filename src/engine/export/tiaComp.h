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

#ifndef _TIACOMP_EXPORT_H
#define _TIACOMP_EXPORT_H

#include "../engine.h"
#include "registerDump.h"

class DivExportTIAComp: public DivROMExport {

  DivEngine* e;
  std::vector<RegisterDump*> registerDumps;
  std::thread* exportThread;
  DivROMExportProgress progress[2];
  bool running, failed, mustAbort;

  // 
  // simple encoding suitable for sound effects and
  // short game music sequences with low RAM
  //
  void writeTrackDataBasic(
    bool encodeDuration,
    bool independentChannelPlayback,
    int addressBits
  );

  // 
  // compact delta encoding suitable for longer sequences
  // also using low RAM
  //
  void writeTrackDataTIAComp(
    int addressBits
  );

  // 
  // Delta Encoding with common sequence compression
  //
  void writeTrackDataFSeq();

  int encodeChannelState(
    const ChannelState& next,
    const char duration,
    const ChannelState& last,
    bool encodeRemainder,
    std::vector<unsigned char> &out
  );

  void run();

public:

  ~DivExportTIAComp();

  bool go(DivEngine* eng) override;
  bool isRunning() override;
  bool hasFailed() override;
  void abort() override;
  void wait() override;
  DivROMExportProgress getProgress(int index=0) override;

};

#endif // _TIACOMP_EXPORT_H