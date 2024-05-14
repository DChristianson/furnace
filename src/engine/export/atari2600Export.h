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

#ifndef _ATARI2600_EXPORT_H
#define _ATARI2600_EXPORT_H

#include "../engine.h"
#include "registerDump.h"

enum DivExportTIAType {
  DIV_EXPORT_TIA_SIMPLE,
  DIV_EXPORT_TIA_COMPACT
};

class DivExportAtari2600 : public DivROMExport {

  // BUGBUG: allow setting options
  DivExportTIAType exportType = DIV_EXPORT_TIA_COMPACT; 
  bool debugRegisterDump = true;

  size_t writeTextGraphics(SafeWriter* w, const char* value);

  void writeWaveformHeader(SafeWriter* w, const char* key);

  // raw data dump
  void writeRegisterDump(
    DivEngine* e, 
    std::vector<RegisterWrite> *registerWrites,
    std::vector<DivROMExportOutput> &ret
  );

  // uncompressed encoding 
  // 1 byte per register
  // optionally use 1 byte to encode duration
  void writeTrackDataSimple(
    DivEngine* e, 
    bool encodeDuration,
    std::vector<RegisterWrite> *registerWrites,
    std::vector<DivROMExportOutput> &ret
  );

  // compacted encoding 
  void writeTrackDataCompact(
    DivEngine* e, 
    std::vector<RegisterWrite> *registerWrites,
    std::vector<DivROMExportOutput> &ret
  );

  size_t writeNoteF0(SafeWriter* w, const ChannelState& next, const char duration, const ChannelState& last);
  size_t writeNoteF1(SafeWriter* w, const ChannelState& next, const char duration, const ChannelState& last);

public:

  ~DivExportAtari2600() {}

  std::vector<DivROMExportOutput> go(DivEngine* e) override;

};

#endif // _ATARI2600_EXPORT_H