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
#include "suffixTree.h"

enum DivExportTIAType {
  DIV_EXPORT_TIA_RAW,       // raw data export - no driver support 
  DIV_EXPORT_TIA_BASIC,     // simple 2 channel sound driver
  DIV_EXPORT_TIA_BASIC_RLE, // simple 2 channel sound driver with duration
  DIV_EXPORT_TIA_TIACOMP,   // TIAComp compact delta encoding
  DIV_EXPORT_TIA_FSEQ,      // Furnace sequence pattern (DEPRECATED)
  DIV_EXPORT_TIA_TIAZIP     // TIAZip LZ-based compression
};

class DivExportAtari2600 : public DivROMExport {

  DivExportTIAType exportType; 
  bool debugRegisterDump;

  size_t writeTextGraphics(SafeWriter* w, const char* value);

  void writeWaveformHeader(SafeWriter* w, const char* key);

  // dump all register writes
  void writeRegisterDump(
    DivEngine* e, 
    std::vector<RegisterWrite> *registerWrites,
    std::vector<DivROMExportOutput> &ret
  );

  //
  // basic uncompressed (raw) encoding
  // 3-4 bytes per channel
  //
  //  AUDCx, AUDFx, AUDVx [, duration]
  //  AUDCx, AUDFx, AUDVx [, duration]
  //  AUDCx, AUDFx, AUDVx [, duration]
  //  ...
  //
  void writeTrackDataRaw(
    DivEngine* e, 
    bool encodeDuration,
    std::vector<RegisterWrite> *registerWrites,
    std::vector<DivROMExportOutput> &ret
  );

  // 
  // simple encoding suitable for sound effects and
  // short game music sequences
  //
  // 2 bytes per channel
  // 
  void writeTrackDataBasic(
    DivEngine* e, 
    bool encodeDuration,
    bool independentChannelPlayback,
    std::vector<RegisterWrite> *registerWrites,
    std::vector<DivROMExportOutput> &ret
  );

  // 
  // compact encoding suitable for sound effects and
  // short game music sequences
  //
  // 2 bytes per channel
  // 
  void writeTrackDataTIAComp(
    DivEngine* e, 
    std::vector<RegisterWrite> *registerWrites,
    std::vector<DivROMExportOutput> &ret
  );

  //
  // Sequenced encoding 
  // uncompressed sequences
  //
  void writeTrackDataFSeq(
    DivEngine* e, 
    std::vector<RegisterWrite> *registerWrites,
    std::vector<DivROMExportOutput> &ret
  );

  //
  // LZ-type encoding 
  // compressed sequences
  //
  void writeTrackDataTIAZip(
    DivEngine* e, 
    std::vector<RegisterWrite> *registerWrites,
    std::vector<DivROMExportOutput> &ret
  );

  int encodeChannelState(
    const ChannelState& next,
    const char duration,
    const ChannelState& last,
    bool encodeRemainder,
    std::vector<unsigned char> &out
  );

  size_t encodeCommandAlphabet(
    const ChannelState& next,
    const char duration,
    const ChannelState& last,
    std::vector<AlphaCode> &out
  );

public:

  DivExportAtari2600(DivEngine * e);
  ~DivExportAtari2600() {}

  std::vector<DivROMExportOutput> go(DivEngine* e) override;

};

#endif // _ATARI2600_EXPORT_H