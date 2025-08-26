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

// BUGBUG: this is not the best good way
unsigned long msb(unsigned long s) {
  unsigned long i = 0;
  while (s > 0) {
    s >>= 1;
    i++;
  }
  return i;
}


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
  while (current->parent != NULL) {
    bool isLeft = current == current->parent->left;
    path.emplace_back(isLeft);
    current = current->parent;
  }
}

bool compareCodebookEntryHeight(CodebookEntry &a, CodebookEntry &b) {
  if (a.height != b.height) return a.height < b.height;
  return a.code < b.code;
}

// build canonical codebook with
// approach adapted from (BSD-license)
// https://github.com/Cyan4973/FiniteStateEntropy/blob/dev/lib/huf_compress.c
//
void HuffmanTree::buildCanonicalCodebook(size_t maxBits, std::vector<CodebookEntry> &codebook) {
  std::vector<HuffmanTree *> stack;
  stack.emplace_back(this);
  
  while (stack.size() > 0) {
    HuffmanTree *n = stack.back();
    stack.pop_back();
    if (n->isLeaf()) {
      codebook.push_back(CodebookEntry(n->code, n->weight, n->height()));
    } else {
      if (n->left != NULL) {
        stack.push_back(n->left);
      }
      if (n->right != NULL) {
        stack.push_back(n->right);
      }
    }
  }
  std::sort(codebook.begin(), codebook.end(), compareCodebookEntryHeight);

  // short circuit if we are below maxBits
  size_t mostBits = codebook.back().height;
  if (mostBits <= maxBits) {
    return;
  }

  long totalWeightToRecover = 0;
  long baseWeight = 1 << (mostBits - maxBits);

  // find too-large nodes
  int n = codebook.size() - 1;
  while (n >= 0) {
    CodebookEntry &entry = codebook.at(n);
    logD("checking entry %d height bits %d to recover %d", entry.code, mostBits - entry.height, totalWeightToRecover);
    if (entry.height <= maxBits) {
      break;
    }
    totalWeightToRecover += baseWeight - (1 << (mostBits - entry.height));
    entry.height = maxBits;
    n--;
  }
  // stop when height < maxBits
  while (codebook.at(n).height == maxBits) {
    n--;
  }

  // renormalize total weight
  totalWeightToRecover >>= mostBits - maxBits;

  // find smallest symbol at each rank
  logD("computing ranks");
  const size_t NO_RANK = 0xff;
  size_t ranks[maxBits+1];
  memset(ranks, NO_RANK, maxBits+1);
  size_t currentBits = maxBits;
  for (int p = n; p >= 0; p--) {
    logD("ranks for %d start at %d", currentBits, p);
    CodebookEntry &entry = codebook.at(p);
    if (entry.height >= currentBits) continue;
    currentBits = entry.height;
    size_t rank = maxBits - currentBits;
    logD("rank %d for %d found at %d", rank, currentBits, p);
    assert(rank < maxBits + 1);
    ranks[rank] = p;
  }

  logD("recovering bits, need %d", totalWeightToRecover);
  while (totalWeightToRecover > 0) {
    unsigned long bitsToDecrease = msb((unsigned long) totalWeightToRecover);
    logD("recovering %d, %d", totalWeightToRecover, bitsToDecrease);
    for ( ; bitsToDecrease > 1; bitsToDecrease--) {
      const long highPos = ranks[bitsToDecrease];
      const long lowPos = ranks[bitsToDecrease - 1];
      logD("searching %d: %d, %d", bitsToDecrease, highPos, lowPos);
      if (highPos == NO_RANK) continue;
      if (lowPos == NO_RANK) break;
      const long highWeight = codebook.at(highPos).weight;
      const long lowWeight = 2 * codebook.at(lowPos).weight;
      if (highWeight <= lowWeight) break;
    }
    while (bitsToDecrease <= maxBits && (ranks[bitsToDecrease] == NO_RANK)) {
      bitsToDecrease++;
    }
    totalWeightToRecover -= 1 << (bitsToDecrease - 1);
    if (ranks[bitsToDecrease - 1] == NO_RANK) {
        ranks[bitsToDecrease - 1] = ranks[bitsToDecrease]; 
    }
    codebook.at(ranks[bitsToDecrease]).height++;
    if (ranks[bitsToDecrease] == 0) {
      ranks[bitsToDecrease] = NO_RANK;
    } else {
      ranks[bitsToDecrease]--;
      if (codebook.at(ranks[bitsToDecrease]).height != maxBits - bitsToDecrease) {
        ranks[bitsToDecrease] = NO_RANK;
      }
    }
  }
  // handle overshoot
  while (totalWeightToRecover < 0) { 
    if (ranks[1] == NO_RANK) {
      while (codebook.at(n).height == maxBits) {
        n--;
      }
      codebook.at(n+1).height--;
      ranks[1] = (n+1);
      totalWeightToRecover++;
      continue;
    }
    codebook.at(ranks[1] + 1).height--;
    ranks[1]++;
    totalWeightToRecover ++;
  }

  // re-sort
  std::sort(codebook.begin(), codebook.end(), compareCodebookEntryHeight);

}

HuffmanTree *buildHuffmanTree(
  const std::map<AlphaCode, size_t> &frequencyMap,
  size_t nodeLimit,
  size_t minWeight,
  size_t maxBits,
  AlphaCode literalCode,
  std::vector<CodebookEntry> &codebook
) {
  if (frequencyMap.empty()) {
    return NULL;
  }
  std::priority_queue<HuffmanTree *, std::vector<HuffmanTree *>, CompareHuffmanTreeWeights> heap;

  size_t literalWeight = 0;
  for (auto &x:frequencyMap) {
    if (x.second < minWeight) {
      literalWeight += 1;
      continue;
    }
    HuffmanTree *node = new HuffmanTree(x.first, x.second);
    heap.emplace(node);
  }

  while (heap.size() > nodeLimit) {
    auto node = heap.top();
    heap.pop();
    literalWeight += node->weight;
    delete node;
  }

  if (literalWeight > 0) {
    HuffmanTree *node = new HuffmanTree(literalCode, literalWeight);
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
  initialTree->buildCanonicalCodebook(maxBits, codebook);
  if (frequencyMap.size() == 1) {
    logD("FREQ MAP SIZE %d CODE TREE SIZE: %d IS LEAF ROOT: %d", frequencyMap.size(), codebook.size(), initialTree->isLeaf() ? 1 : 0);
    auto it = codebook[0];
    logD("LENGTH CODE 0: %d / FREQ %d", it.height, frequencyMap.at(it.code));
  }
  delete initialTree;
  HuffmanTree* canonicalTree = buildHuffmanTreeFromCodebook(codebook);
  if (frequencyMap.size() == 1) {
    logD("CANONICAL TREE IS LEAF ROOT: %d, HEIGHT: %d", canonicalTree->isLeaf() ? 1 : 0, canonicalTree->height());

  }
  return canonicalTree;
}

HuffmanTree *buildHuffmanTreeFromCodebook(const std::vector<CodebookEntry> &codebook) {
  HuffmanTree* canonicalTree = new HuffmanTree();
  if (codebook.size() == 1) {
    auto &p = codebook.at(0);
    assert(p.height == 0);
    canonicalTree->setCode(p.code, 0);
    return canonicalTree;
  }

  size_t currentHeight = 0;
  size_t currentCode = SIZE_MAX;
  for (auto &p : codebook) {
    currentCode += 1;
    if (p.height > currentHeight) {
      currentCode = currentCode << (p.height - currentHeight);
      currentHeight = p.height;
    }
    HuffmanTree* current = canonicalTree;
    size_t mask = 1 << (currentHeight - 1);
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
    current->setCode(p.code, 0);
  }
  return canonicalTree;
}