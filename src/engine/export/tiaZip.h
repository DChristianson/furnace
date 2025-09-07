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

const size_t NUM_ZIP_CHANNELS = 2;

class DivExportTIAZip : public DivROMExport {

  DivEngine* e;
  std::thread* exportThread = NULL;
  DivROMExportProgress progress[2];
  bool running, failed, mustAbort;

  int jumpMapBits;
  int compressionLevel;
  int minSpanLength;
  int maxSustain;
  size_t baseDataOffset;
  size_t blockSize;
  int addressBits;
  int addressIndexBits;


  // assembly area 

  std::vector<RegisterDump*> registerDumps;
  std::vector<std::vector<AlphaCode>> codeSequences;
  std::vector<std::vector<AlphaCode>> compressedCodeSequences;
  std::vector<std::vector<AlphaCode>> spanSequences;
  std::vector<Bitstream *> dataStreams;
  std::vector<Bitstream *> trackStreams;

  // huffman code generation

  std::map<AlphaCode, size_t> abstractFrequencyMap;
  std::vector<CodebookEntry> abstractCodebook;
  HuffmanTree *abstractCodeTree = NULL;
  std::map<AlphaCode, std::vector<bool>> abstractCodeIndex;

  std::map<AlphaCode, size_t> spanFrequencyMap;
  std::vector<CodebookEntry> spanCodebook;
  HuffmanTree *spanTree = NULL;
  std::map<AlphaCode, std::vector<bool>> spanCodeIndex;

  std::map<AlphaCode, size_t> controlFrequencyMap;
  std::vector<CodebookEntry> controlCodebook;
  HuffmanTree *controlTree = NULL;
  std::map<AlphaCode, std::vector<bool>> controlCodeIndex;
  
  std::map<AlphaCode, size_t> volumeFrequencyMap;
  std::vector<CodebookEntry> volumeCodebook;
  HuffmanTree *volumeTree = NULL;
  std::map<AlphaCode, std::vector<bool>> volumeCodeIndex;

  std::map<AlphaCode, size_t> durationFrequencyMap;
  std::vector<CodebookEntry> durationCodebook;
  HuffmanTree *durationTree = NULL;
  std::map<AlphaCode, std::vector<bool>> durationCodeIndex;

  std::map<AlphaCode, size_t> velocityFrequencyMap;
  std::vector<CodebookEntry> velocityCodebook;
  HuffmanTree *velocityTree = NULL;
  std::map<AlphaCode, std::vector<bool>> velocityCodeIndex;

  std::map<AlphaCode, std::map<AlphaCode, size_t>> initialFrequencyMap;
  std::map<AlphaCode, AlphaCode> controlCodeMergeMap;
  std::map<AlphaCode, std::map<AlphaCode, size_t>> mergedFrequencyMap;
  std::map<AlphaCode, std::vector<CodebookEntry>> mergedFrequencyCodebooks;
  std::map<AlphaCode, HuffmanTree *> mergedFrequencyTrees;
  std::map<AlphaCode, std::map<AlphaCode, std::vector<bool>>> mergedFrequencyCodeIndexes;

  // jump statistics
  std::map<AlphaCode, size_t> jumpFrequencyMap;
  std::map<AlphaCode, size_t> gotoFrequencyMap; 
  std::vector<size_t> jumpTableAddresses;

  //
  // LZ-type encoding 
  // compressed sequences
  //
  void writeTrackDataTIAZip(int compressionLevel, int minSpanLength, int maxSustain, int jumpMapBits);
  
  size_t encodeChannelStateCodes(
    const ChannelState& next,
    const char duration,
    const ChannelState& last,
    const int velocity,
    std::vector<AlphaCode> &out
  );

  void compressCodeSequence(
    size_t subsong,
    int channel,
    const std::vector<AlphaCode> &alphabet,
    const std::map<AlphaCode, AlphaChar> &index,
    const std::map<AlphaChar, size_t> &alphaCharWeights,
    size_t branchWeight,
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

  void encodeBitstreams();

  void validateBitstreams();

  void writeWaveformHeader(SafeWriter* w, const char* key);

  size_t writeCodebookLengths(
    SafeWriter* w,
    const char *label,
    const std::vector<CodebookEntry> &codebook,
    size_t &total
  );

  size_t writeCodebookFirstValues(
    SafeWriter* w,
    const char *label,
    const std::vector<CodebookEntry> &codebook,
    const std::map<AlphaCode, std::vector<bool>> &codeIndex
  );

  size_t writeCodebookLastValues(
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