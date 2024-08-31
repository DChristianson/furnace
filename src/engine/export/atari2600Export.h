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

  DivEngine* e;
  DivExportTIAType exportType; 
  bool debugRegisterDump;

  size_t writeTextGraphics(SafeWriter* w, const char* value);

  void writeWaveformHeader(SafeWriter* w, const char* key);

  // dump all register writes
  void writeRegisterDump(
    std::vector<RegisterWrite> (*registerWrites)
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
    bool encodeDuration,
    std::vector<RegisterWrite> (*registerWrites)
  );

  // 
  // simple encoding suitable for sound effects and
  // short game music sequences
  //
  // 2 bytes per channel
  // 
  void writeTrackDataBasic(
    bool encodeDuration,
    bool independentChannelPlayback,
    std::vector<RegisterWrite> (*registerWrites)
  );

  // 
  // compact encoding suitable for sound effects and
  // short game music sequences
  //
  // 2 bytes per channel
  // 
  void writeTrackDataTIAComp(
    std::vector<RegisterWrite> (*registerWrites)
  );

  //
  // Sequenced encoding 
  // uncompressed sequences
  //
  void writeTrackDataFSeq(
    std::vector<RegisterWrite> *registerWrites
  );

  //
  // LZ-type encoding 
  // compressed sequences
  //
  void writeTrackDataTIAZip(
    const std::vector<RegisterWrite> (*registerWrites),
    bool fixedCodes
  );

  int encodeChannelState(
    const ChannelState& next,
    const char duration,
    const ChannelState& last,
    bool encodeRemainder,
    std::vector<unsigned char> &out
  );

  size_t encodeChannelStateCodes(
    const ChannelState& next,
    const char duration,
    const ChannelState& last,
    std::vector<AlphaCode> &out
  );

  void compressCodeSequence(
    int subsong,
    int channel,
    const std::vector<AlphaCode> &alphabet,
    const std::map<AlphaCode, AlphaChar> &index,
    const std::vector<AlphaCode>&codeSequence,
    std::vector<AlphaCode> &compressedCodeSequence,
    std::vector<AlphaCode> &spanSequence
  );
  
  void encodeBitstreamDynamic(
    const std::vector<AlphaCode> (*codeSequences)[2],
    const std::vector<AlphaCode> (*compressedCodeSequences)[2],
    const std::vector<AlphaCode> (*spanSequences)[2],
    size_t dataOffset,
    size_t blockSize
  );

  void validateCodeSequence(
    int subsong,
    int channel,
    const std::vector<AlphaCode>&codeSequence,
    const std::vector<AlphaCode> &compressedCodeSequence,
    const std::vector<AlphaCode> &spanSequence
  );

  void run();

public:

  ~DivExportAtari2600() {}

  bool go(DivEngine* eng) override;
  bool isRunning() override;
  bool hasFailed() override;
  void abort() override;
  void wait() override;
  DivROMExportProgress getProgress(int index=0) override;

};

#endif // _ATARI2600_EXPORT_H