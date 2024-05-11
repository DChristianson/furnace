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
#include <bitset>

const size_t NUM_TIA_REGISTERS = 6;

typedef std::bitset<NUM_TIA_REGISTERS> TiaRegisterMask;

const unsigned char DEFAULT_STACK_DEPTH  = 2;
const unsigned char DEFAULT_LITERAL_DICTIONARY_SIZE = 64;
const unsigned char DEFAULT_SEQUENCE_DICTIONARY_SIZE = 64;

enum DivExportTIAType {
  DIV_EXPORT_TIA_DUMP,
  DIV_EXPORT_TIA_SIMPLE,
  DIV_EXPORT_TIA_COMPACT
};

class DivExportAtari2600 : public DivROMExport {

  DivExportTIAType exportType = DIV_EXPORT_TIA_COMPACT; 

  size_t stackDepth = DEFAULT_STACK_DEPTH;
  size_t literalDictionarySize = DEFAULT_LITERAL_DICTIONARY_SIZE;
  size_t sequenceDictionarySize = DEFAULT_SEQUENCE_DICTIONARY_SIZE;

  size_t writeTextGraphics(SafeWriter* w, const char* value);

  void writeWaveformHeader(SafeWriter* w, const char* key);

  // raw data dump
  void writeTrackDataDump(
    DivEngine* e, 
    std::vector<RegisterWrite> &registerWrites,
    std::vector<DivROMExportOutput> &ret
  );

  // uncompressed run length encoding
  void writeTrackDataSimple(
    DivEngine* e, 
    std::vector<RegisterWrite> &registerWrites,
    std::vector<DivROMExportOutput> &ret
  );

  // compacted encoding
  void writeTrackDataCompact(
    DivEngine* e, 
    std::vector<RegisterWrite> &registerWrites,
    std::vector<DivROMExportOutput> &ret
  );

  size_t writeNoteF0(SafeWriter* w, const ChannelState& next, const char duration, const ChannelState& last);
  size_t writeNoteF1(SafeWriter* w, const ChannelState& next, const char duration, const ChannelState& last);

public:

  ~DivExportAtari2600() {}

  std::vector<DivROMExportOutput> go(DivEngine* e) override;

};

#endif // _ATARI2600_EXPORT_H