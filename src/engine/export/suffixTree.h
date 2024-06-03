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

#ifndef _SUFFIXTREE_H
#define _SUFFIXTREE_H

#include "../../ta-utils.h"

typedef uint64_t AlphaCode;
typedef uint64_t SpanCode;
typedef int AlphaChar;

bool compareFrequency(std::pair<AlphaCode, size_t> &a, std::pair<AlphaCode, size_t> &b);

struct Span {

  int    subsong;
  int    channel;
  size_t start;
  size_t length;

  Span(int subsong, int channel, size_t start, size_t length) :
    subsong(subsong),
    channel(channel),
    start(start),
    length(length) {}

};

class CompareFrequencies {
 public:

  bool operator()(const std::pair<AlphaCode, size_t> &a, const std::pair<AlphaCode, size_t> &b) const
  {
    if (a.second != b.second) return a.second > b.second;
    return a.first < b.first;
  } 

};

class CompareSpans {
public:

  bool operator()(const Span &a, const Span &b) const
  {
    if (a.subsong != b.subsong) return a.subsong < b.subsong;
    if (a.channel != b.channel) return a.channel < b.channel;
    if (a.start != b.start) return a.start < b.start;
    return a.length < b.length;
  }
};

/**
 * Collection of duplicate spans
 */
struct DuplicateSpans {

  std::vector<Span> spans;
  size_t length;
  size_t weight;
  std::map<AlphaChar, size_t> in;
  std::map<AlphaChar, size_t> out;

  DuplicateSpans(size_t length, size_t weight) : length(length), weight(weight) {}

};

/**
 * Compare duplicates by length
 */
class CompareDuplicateLengths {
public:

  bool operator()(DuplicateSpans * a, DuplicateSpans * b) const {
    if (a->length != b->length) return a->length < b->length;
    if (a->weight != b->weight) return a->weight < b->weight;
    return a < b;
  }

};

/**
 * Compare duplicates by weight
 */
class CompareDuplicateSpanWeights {
public:

  bool operator()(DuplicateSpans * a, DuplicateSpans * b) const {
    if (a->weight != b->weight) return a->weight < b->weight;
    if (a->length != b->length) return a->length < b->length;
    return a < b;
  }

};

/**
 * Find duplicate spans in an arbitrary stream
 */
struct SuffixTree {

  SuffixTree *parent;
  SuffixTree *slink;
  std::vector<SuffixTree *> children;
  bool isLeaf;
  size_t start;
  size_t depth;

  SuffixTree(size_t alphabetSize, size_t d) : parent(NULL), slink(NULL), isLeaf(true), start(0), depth(d) {
    children.resize(alphabetSize);
    for (size_t i = 0; i < alphabetSize; i++) {
      children[i] = NULL;
    }
  }

  ~SuffixTree() {
    for (auto x : children) {
      delete x;
    }
  }

  SuffixTree *splice_node(size_t d, const std::vector<AlphaChar> &S);

  SuffixTree *add_leaf(size_t i, size_t d, const std::vector<AlphaChar> &S);

  void compute_slink(const std::vector<AlphaChar> &S);

  /**
   * Index of leftmost substring starting at parent node.
   */
  size_t substring_start() const;

  /**
   * End of leftmost substring starting at parent node.
   */
  size_t substring_end() const;

  size_t substring_len() const;

  SuffixTree *find(const std::vector<AlphaChar> &K, const std::vector<AlphaChar> &S);

  /**
   * Find Prior(i) - the longest prefix of S[i...n-1] that also occurs in S[0...i]
   */
  void find_prior(const size_t i, const std::vector<AlphaChar> &S, Span &span);

  /**
   * @brief find all the leaves in this node's subtree
   * 
   * @param leaves 
   * @return size_t 
   */
  size_t gather_leaves(std::vector<SuffixTree *> &leaves);

  SuffixTree *find_maximal_substring();

  /**
   * @brief gather all the left diverse nodes in this node's subtree
   *
   * a node is left diverse when at least two leaves in its subtree have different left characters
   * 
   * @param nodes 
   * @param S - reference string
   * @return AlphaChar 
   */
  AlphaChar gather_left(std::vector<SuffixTree *> &nodes, const std::vector<AlphaChar> &S);

};

void createAlphabet(
  const std::map<AlphaCode, size_t> &frequencyMap,
  std::vector<AlphaCode> &alphabet,
  std::map<AlphaCode, AlphaChar> &index
);

SuffixTree * createSuffixTree(
  const std::vector<AlphaCode> &alphabet,
  const std::vector<AlphaChar> &alphaSequence
);

void compressSequence(
  SuffixTree *root,
  int subsong,
  int channel,
  const std::vector<AlphaChar> &alphaSequence,
  std::vector<Span> &copySequence
);

void createAlphabet(
  const std::map<AlphaCode, String> &commonDumpSequences,
  std::vector<AlphaCode> &alphabet,
  std::map<String, AlphaChar> &index
);

void translateString(
  const std::vector<String> &sequence,
  const std::map<String, String> &representativeMap,
  const std::map<String, AlphaChar> &index,
  std::vector<AlphaChar> &alphaSequence
);

// debugging code
void testCommonSubsequences(const String &input);


void testCV(const String &input);


void encodeCopySequence(
  const std::vector<AlphaCode> &sequence, 
  const Span &bounds,
  const std::vector<Span> &copySequence,
  std::vector<AlphaCode> &encodedSequence);

void encodeDeltaSequence(
  const std::vector<AlphaCode> &sequence, 
  const Span &bounds,
  std::vector<AlphaCode> &encodedSequence);

#endif // _SUFFIXTREE_H