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

#include "huffman.h"
#include <queue>
#include "../../ta-log.h"

unsigned char Bitstream::readByte() {
  size_t s = pos % 64;
  size_t address = pos >> 6;
  pos = pos + 8;
  size_t next = pos >> 6;
  unsigned char result = (buffer[address] >> s) & 0xff;
  if (next > address) {
    s = 8 - (pos % 64);
    result |= (buffer[address] << s) & 0xff;
  }
  return result;
}

bool Bitstream::readBit() {
  size_t s = pos % 64;
  size_t address = pos >> 6;
  uint64_t mask = ((uint64_t)1) << s;
  pos++;
  return buffer[address] & mask;
}

void Bitstream::writeBit(bool bit) {
  assert(pos < capacity);
  size_t s = pos % 64;
  size_t address = pos >> 6;
  uint64_t mask = ((uint64_t)1) << s;
  if (bit) {
    buffer[address] = buffer[address] | mask;
  } else {
    buffer[address] = buffer[address] & (~mask);
  }
  pos++;
  if (pos > endPos) {
    endPos = pos;
  }
}

size_t Bitstream::padByteBoundary() {
    size_t s = pos % 8;
    if (s > 0) {
      s = 8 - s;
      pos += s;
    }
    if (pos > endPos) {
     endPos = pos;
    }
    return s;
}

size_t Bitstream::writeBits(const std::vector<bool> &bits) {
  if (0 == bits.size()) {
    return 0;
  }
  for (int i = bits.size(); --i >= 0; ) {
    writeBit(bits[i]);
  }
  return bits.size();
}

size_t Bitstream::readBits(unsigned char bits) {
  size_t value = 0;
  while (bits != 0) {
    value = value << 1;
    value |= readBit() ? 1 : 0;
    bits--;
  }
  return value;
}

void Bitstream::writeBits(size_t value, unsigned char bits) {
  if (0 == bits) return;
  uint64_t mask = ((uint64_t) 1) << (bits - 1);
  while (mask > 0) {
    writeBit((value & mask) > 0);
    mask = mask >> 1;
  }
}

void HuffmanTree::buildIndex(std::map<AlphaCode, std::vector<bool>> &index) {
  std::vector<HuffmanTree *> stack;
  stack.emplace_back(this);
  while (stack.size() > 0) {
    HuffmanTree *n = stack.back();
    stack.pop_back();
    if (n->isLeaf()) {
      n->writePath(index[n->code]);
    } else {
      if (n->left != NULL) {
        stack.push_back(n->left);
      }
      if (n->right != NULL) {
        stack.push_back(n->right);
      }
    }
  }
}

  void HuffmanTree::writePath(std::vector<bool> &path) {
    HuffmanTree *current = this;
    logD("PATH SIZE IN: %d", path.size());
    while (current->parent != NULL) {
      bool isLeft = current == current->parent->left;
      path.emplace_back(isLeft);
      current = current->parent;
    }
    logD("PATH SIZE OUT: %d", path.size());
  }

void HuffmanTree::buildCanonicalCodebook(std::vector<std::pair<AlphaCode, size_t>> &codeLengths) {
  std::vector<HuffmanTree *> stack;
  stack.emplace_back(this);
  while (stack.size() > 0) {
    HuffmanTree *n = stack.back();
    stack.pop_back();
    if (n->isLeaf()) {
      codeLengths.push_back(std::pair<AlphaCode, int>(n->code, n->height()));
    } else {
      if (n->left != NULL) {
        stack.push_back(n->left);
      }
      if (n->right != NULL) {
        stack.push_back(n->right);
      }
    }
  }
  std::sort(codeLengths.begin(), codeLengths.end(), compareCodeLength);
}

HuffmanTree *buildHuffmanTree(
  const std::map<AlphaCode, size_t> &frequencyMap,
  size_t limit,
  size_t minWeight,
  AlphaCode literal,
  std::vector<std::pair<AlphaCode, size_t>> &codebook
) {

  std::priority_queue<HuffmanTree *, std::vector<HuffmanTree *>, CompareHuffmanTreeWeights> heap;

  size_t literal_weight = 0;
  for (auto &x:frequencyMap) {
    if (x.second < minWeight) {
      literal_weight += 1;
      continue;
    }
    HuffmanTree *node = new HuffmanTree(x.first, x.second);
    heap.emplace(node);
  }

  while (heap.size() > limit) {
    auto node = heap.top();
    heap.pop();
    literal_weight += node->weight;
    delete node;
  }

  if (literal_weight > 0) {
    HuffmanTree *node = new HuffmanTree(literal, literal_weight);
    heap.emplace(node);
  }

  while (heap.size() > 1) {
    auto left = heap.top();
    heap.pop();
    auto right = heap.top();
    heap.pop();
    HuffmanTree *node = new HuffmanTree(left, right);
    heap.emplace(node);
  }
  
  HuffmanTree* initialTree = heap.top();
  initialTree->buildCanonicalCodebook(codebook);
  if (frequencyMap.size() == 1) {
    logD("FREQ MAP SIZE %d CODE TREE SIZE: %d IS LEAF ROOT: %d", frequencyMap.size(), codebook.size(), initialTree->isLeaf() ? 1 : 0);
    auto it = codebook[0];
    logD("LENGTH CODE 0: %d / FREQ %d", it.second, frequencyMap.at(it.first));
  }
  delete initialTree;
  HuffmanTree* canonicalTree = buildHuffmanTreeFromCodebook(codebook);
  if (frequencyMap.size() == 1) {
    logD("CANONICAL TREE IS LEAF ROOT: %d, HEIGHT: %d", canonicalTree->isLeaf() ? 1 : 0, canonicalTree->height());

  }
  return canonicalTree;
}

HuffmanTree *buildHuffmanTreeFromCodebook(const std::vector<std::pair<AlphaCode, size_t>> &codeLengths) {
  size_t codeLength = 0;
  size_t currentCode = SIZE_MAX;
  HuffmanTree* canonicalTree = new HuffmanTree();
  if (codeLengths.size() == 1) {
    auto &p = codeLengths.at(0);
    assert(p.second == 0);
    canonicalTree->setCode(p.first, 0);
    return canonicalTree;
  }
  for (auto &p : codeLengths) {
    currentCode += 1;
    if (p.second > codeLength) {
      currentCode = currentCode << (p.second - codeLength);
      codeLength = p.second;
    }
    HuffmanTree* current = canonicalTree;
    size_t mask = 1 << (codeLength - 1);
    while (mask) {
      if (mask & currentCode) {
        if (current->left == NULL) {
          HuffmanTree* leftBranch = new HuffmanTree();
          current->setLeft(leftBranch);
        }
        current = current->left;
      } else {
        if (current->right == NULL) {
          HuffmanTree* rightBranch = new HuffmanTree();
          current->setRight(rightBranch);
        }
        current = current->right;
      }
      mask = mask >> 1;
    }
    current->setCode(p.first, 0);
  }
  return canonicalTree;
}