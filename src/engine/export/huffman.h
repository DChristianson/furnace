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

#ifndef _HUFFMAN_H
#define _HUFFMAN_H

#include "suffixTree.h"

unsigned long msb(unsigned long s);

/**
 * Helper class for writing bitstreams
 */
class Bitstream {
private:
  
  uint64_t* buffer;
  size_t capacity;
  size_t pos;
  size_t endPos;

public:

  Bitstream(size_t capacity) : capacity(capacity), pos(0), endPos(0) {
    size_t elements = capacity / 64;
    if (capacity % 64 > 0) {
      elements++;
    }
    buffer = new uint64_t[elements]; // BUGBUG: zero out?
  }

  ~Bitstream() {
    if (buffer != NULL) {
      delete(buffer);
    }
  }

  unsigned char readByte();

  bool hasBits() const {
    return pos < endPos;
  }

  size_t position() const {
    return pos;
  }
  size_t size() const {
    return endPos;
  }

  size_t bytesUsed() const {
    if (endPos % 8 > 0) {
      return (endPos / 8) + 1;
    } else {
      return endPos / 8;
    }
  }

  uint64_t inspect(size_t i) {
    return buffer[i];
  }

  size_t readBits(unsigned char bits);

  bool readBit();

  void writeBit(bool bit);

  size_t padByteBoundary();

  size_t writeBits(const std::vector<bool> &bits);

  void writeBits(size_t value, unsigned char bits);

  void seek(size_t index) {
    assert(index <= endPos);
    pos = index;
  }

};

struct CodebookEntry {

  AlphaCode code;
  size_t weight;
  size_t height;

  CodebookEntry(AlphaCode c, size_t weight, size_t height) : 
    code(c),
    weight(weight),
    height(height) {}

};

bool compareCodebookEntryHeight(CodebookEntry &a, CodebookEntry &b);

/**
 * Huffman code tree. Used to build a optimal encoding of an arbitrary set of codes based on their
 * relative frequencies.
 */
struct HuffmanTree {

  AlphaCode code;
  size_t weight;

  HuffmanTree *parent;
  HuffmanTree *left;
  HuffmanTree *right;

  HuffmanTree(AlphaCode c, size_t weight) : 
    code(c),
    weight(weight),
    parent(NULL),
    left(NULL),
    right(NULL) {}

  HuffmanTree(HuffmanTree *left, HuffmanTree *right) : 
    code(0),
    weight(left->weight + right->weight),
    left(left),
    right(right)
  {
    assert(left->parent == NULL);
    left->parent = this;
    assert(right->parent == NULL);
    right->parent = this;
  }

  HuffmanTree() : 
    code(0),
    weight(0),
    parent(NULL),
    left(NULL),
    right(NULL) {}

  ~HuffmanTree() {
    if (left != NULL) {
      delete(left);
    }
    if (right != NULL) {
      delete(right);
    }
  }

  void setLeft(HuffmanTree *tree) {
    assert(code == 0);
    assert(tree != NULL);
    assert(left == NULL);
    assert(tree->parent == NULL);
    left = tree;
    tree->parent = this;
    updateWeights();
  }

  void setRight(HuffmanTree *tree) {
    assert(code == 0);
    assert(tree != NULL);
    assert(right == NULL);
    assert(tree->parent == NULL);
    right = tree;
    tree->parent = this;
    updateWeights();
  }

  void setCode(AlphaCode c, size_t w) {
    assert(code == 0);
    assert(left == NULL);
    assert(right == NULL);
    code = c;
    weight = w;
  }

  void updateWeights() {
    HuffmanTree *current = this;
    while (current != NULL) {
      current->weight = 
        (current->left ? current->left->weight : 0) +
        (current->right ? current->right->weight : 0);
      current = current->parent;
    }
  }

  bool isLeaf() {
    return left == NULL && right == NULL;
  }

  AlphaCode decode(Bitstream *bitstream) {
    HuffmanTree *current = this;
    while (!current->isLeaf()) {
      bool isLeft = bitstream->readBit();
      current = isLeft ? current->left : current->right;
    }
    return current->code;
  }

  void writePath(std::vector<bool> &path);

  size_t height() {
    size_t height = 0;
    HuffmanTree *current = this;
    while (current->parent != NULL) {
      height++;
      current = current->parent;
    }
    return height;
  }

  /**
   * Convert a tree to a map of symbol -> corresponding code
   */
  void buildIndex(std::map<AlphaCode, std::vector<bool>> &index);

  /**
   * Convert a tree to canonical form.
   */
  void buildCanonicalCodebook(size_t maxBits, std::vector<CodebookEntry> &codebook);

};

class CompareHuffmanTreeWeights {
 public:

  bool operator()(const HuffmanTree *a, const HuffmanTree *b) const
  {
    if (a->weight != b->weight) return a->weight > b->weight;
    return a->code < b->code;
  } 

};

HuffmanTree *buildHuffmanTreeFromCodebook(
  const std::vector<CodebookEntry> &codebook
);

HuffmanTree *buildHuffmanTree(
  const std::map<AlphaCode, size_t> &frequencyMap,
  size_t nodeLimit,
  size_t minWeight,
  size_t maxBits,
  AlphaCode literalCode,
  std::vector<CodebookEntry> &codebook
);

#endif // _HUFFMAN_H