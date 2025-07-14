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

const int AUDC0 = 0x15;
const int AUDC1 = 0x16;
const int AUDF0 = 0x17;
const int AUDF1 = 0x18;
const int AUDV0 = 0x19;
const int AUDV1 = 0x1A;

class DivExportTIAZip : public DivROMExport {

  DivEngine* e;
  std::vector<RegisterDump*> registerDumps;
  std::thread* exportThread;
  DivROMExportProgress progress[2];
  bool running, failed, mustAbort;

  // debugging
  void writeRegisterDumps();

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
  void writeTrackDataTIAZip();

  
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

  size_t compileCommands(
    const HuffmanTree *tree,
    SafeWriter *w
  );

  size_t writeTextGraphics(SafeWriter* w, const char* value);
  void writeWaveformHeader(SafeWriter* w, const char* key);
  size_t writeCodebook(SafeWriter* w, const char *label, const std::vector<std::pair<AlphaCode, size_t>> &codebook);

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