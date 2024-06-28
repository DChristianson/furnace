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
  uint64_t mask = 1 << (pos % 64);
  size_t address = pos >> 64;
  pos++;
  return buffer[address] & mask;
}

void Bitstream::writeBit(bool bit) {
  assert(pos < capacity);
  size_t s = pos % 64;
  size_t address = pos >> 6;
  uint64_t mask = 1 << s;
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

void Bitstream::writeByte(unsigned char byte) {
  unsigned char mask = 0x80;
  while (mask > 0) {
    writeBit(byte & mask > 0);
    mask = mask >> 1;
  }
}

HuffmanTree *buildHuffmanTree(const std::map<AlphaCode, size_t> &frequencyMap, size_t limit, AlphaCode literal) {

    std::priority_queue<HuffmanTree *, std::vector<HuffmanTree *>, CompareHuffmanTreeWeights> heap;

    size_t literal_weight = 0;
    for (auto &x:frequencyMap) {
      if (x.second == 1) {
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

    return heap.top();
    
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
