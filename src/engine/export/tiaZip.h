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

#ifndef _TIAZIP_EXPORT_H
#define _TIAZIP_EXPORT_H

#include "../engine.h"
#include "registerDump.h"
#include "suffixTree.h"
#include "huffman.h"

class DivExportTIAZip : public DivROMExport {

  DivEngine* e;
  std::vector<RegisterDump*> registerDumps;
  std::thread* exportThread;
  DivROMExportProgress progress[2];
  bool running, failed, mustAbort;

  // 
  // compact encoding suitable for sound effects and
  // short game music sequences
  //
  // 2 bytes per channel
  // 
  void writeTrackDataTIAComp();

  //
  // LZ-type encoding 
  // compressed sequences
  //
  void writeTrackDataTIAZip(int compressionLevel);
  
  void encodeBitstreamDynamic(
    const std::vector<AlphaCode> (*codeSequences)[2],
    const std::vector<AlphaCode> (*compressedCodeSequences)[2],
    const std::vector<AlphaCode> (*spanSequences)[2],
    size_t dataOffset,
    size_t blockSize
  );

  void compressCodeSequence(
    size_t subsong,
    int channel,
    const std::vector<AlphaCode> &alphabet,
    const std::map<AlphaCode, AlphaChar> &index,
    const std::vector<AlphaCode>&codeSequence,
    int compressionLevel,
    std::vector<AlphaCode> &compressedCodeSequence,
    std::vector<AlphaCode> &spanSequence
  );

  void validateCodeSequence(
    size_t subsong,
    int channel,
    const std::vector<AlphaCode> &codeSequence,
    const std::vector<AlphaCode> &compressedCodeSequence,
    const std::vector<AlphaCode> &spanSequence
  );

  size_t encodeChannelStateCodes(
    const ChannelState& next,
    const char duration,
    const ChannelState& last,
    std::vector<AlphaCode> &out
  );

  void writeWaveformHeader(SafeWriter* w, const char* key);
  size_t writeCodebookLengths(
    SafeWriter* w,
    const char *label,
    const std::vector<CodebookEntry> &codebook
  );
  size_t writeCodebookLadder(
    SafeWriter* w,
    const char *label,
    const std::vector<CodebookEntry> &codebook,
    const std::map<AlphaCode, std::vector<bool>> &codeIndex
  );
  size_t writeCommandCodes(
    SafeWriter* w,
    const char *label,
    const std::vector<CodebookEntry> &codebook,
    const std::map<AlphaCode, std::vector<bool>> &codeIndex
  );
  size_t writeDataCodes(
    SafeWriter* w,
    const char *label,
    const std::vector<CodebookEntry> &codebook,
    const std::map<AlphaCode, std::vector<bool>> &codeIndex
  );
  void writeCodebookMacro(
    SafeWriter* w,
    const char *label,
    const char *track,
    const std::vector<CodebookEntry> &codebook
  );
  void run();

public:

  ~DivExportTIAZip();

  bool go(DivEngine* eng) override;
  bool isRunning() override;
  bool hasFailed() override;
  void abort() override;
  void wait() override;
  DivROMExportProgress getProgress(int index=0) override;

};

#endif // _TIAZIP_EXPORT_H