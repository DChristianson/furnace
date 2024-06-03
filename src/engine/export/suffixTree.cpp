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

#include "suffixTree.h"
#include <queue>
#include "../../ta-log.h"

SuffixTree *SuffixTree::splice_node(size_t d, const std::vector<AlphaChar> &S) {
  assert(d < depth);
  SuffixTree *child = new SuffixTree(children.size(), d);
  size_t i = start;
  child->start = i;
  child->parent = parent;
  child->children[S.at(i + d)] = this;
  parent->children[S.at(i + parent->depth)] = child;
  parent = child;
  return child;
}

SuffixTree *SuffixTree::add_leaf(size_t i, size_t d, const std::vector<AlphaChar> &S) {
  SuffixTree *child = new SuffixTree(children.size(), S.size() - i);
  child->start = i;
  child->parent = this;
  children[S.at(i + d)] = child;
  isLeaf = false;
  return child;
}

void SuffixTree::compute_slink(const std::vector<AlphaChar> &S) {
  size_t d = depth;
  SuffixTree *v = parent->slink;
  while (v->depth < d - 1) {
    v = v->children.at(S[start + v->depth + 1]);
  }
  if (v->depth > d - 1) {
    v = v->splice_node(d - 1, S);
  }
  slink = v;
}

size_t SuffixTree::substring_start() const {
  return start + (NULL != parent ? parent->depth : 0);
}

size_t SuffixTree::substring_end() const {
  return start + depth;
}

size_t SuffixTree::substring_len() const {
  return substring_end() - substring_start();
}

SuffixTree *SuffixTree::find(const std::vector<AlphaChar> &K, const std::vector<AlphaChar> &S) {
  size_t i = 0;
  SuffixTree * u = this;
  while (i < K.size()) {
    SuffixTree * child = u->children.at(K.at(i));
    if (NULL == child) return NULL;
    u = child;
    size_t j = u->substring_start();
    while (i < K.size() && j < u->substring_end()) {
      if (K.at(i) != S.at(j)) {
        return NULL;
      }
      i++;
      j++;
    }
  }
  return u;
}

void SuffixTree::find_prior(const size_t i, const std::vector<AlphaChar> &S, Span &span) {
  SuffixTree * u = this;
  size_t p = 0;
  while (true) {
    SuffixTree * child = u->children.at(S.at(i + p));
    if (NULL == child) break;
    size_t cvp = child->substring_end();
    if (cvp > i) {
      break;
    }
    p = child->depth;
    u = child;
  }
  span.start = u->start;
  span.length = p;
}

size_t SuffixTree::gather_leaves(std::vector<SuffixTree *> &leaves) {
  std::vector<SuffixTree *> stack;
  stack.emplace_back(this);
  while (stack.size() > 0) {
    SuffixTree * u = stack.back();
    stack.pop_back();
    for (auto child : u->children) {
      if (NULL == child) continue;
      if (child->isLeaf) {
        leaves.emplace_back(child);
        continue;
      }
      stack.emplace_back(child);
    }
  }
  return leaves.size();
}

SuffixTree *SuffixTree::find_maximal_substring() {
  SuffixTree *candidate = NULL;
  std::vector<SuffixTree *> stack;
  stack.emplace_back(this);
  while (stack.size() > 0) {
    SuffixTree * u = stack.back();
    stack.pop_back();
    for (auto child : u->children) {
      if (NULL == child) continue;
      if (child->isLeaf) continue;
      if (NULL == candidate || (candidate->depth < child->depth)) {
        candidate = child;
      }
      stack.emplace_back(child);
    }
  }
  return candidate;
}

AlphaChar SuffixTree::gather_left(std::vector<SuffixTree *> &nodes, const std::vector<AlphaChar> &S) {
  AlphaChar leftChar = -1;
  bool isLeftDiverse = false;
  for (auto child : children) {
      if (NULL == child) continue;
      AlphaChar nextChar;
      if (child->isLeaf) {
        nextChar = child->start > 0 ? S.at(child->start - 1) : S.at(S.size() - 1);
      } else {
        nextChar = child->gather_left(nodes, S);
      }
      if (nextChar < 0) {
        isLeftDiverse = true;
      } else if (leftChar < 0) {
        leftChar = nextChar;
      } else if (leftChar != nextChar) {
        isLeftDiverse = true;
      }
  }
  if (isLeftDiverse && depth > 0) {
    nodes.emplace_back(this);
    return -1;
  }
  return leftChar;
}

/**
 * Compare by tree start
 */ 
bool compareStart(SuffixTree * a, SuffixTree * b) {
  return a->start < b->start;
}

bool compareFrequency(std::pair<AlphaCode, size_t> &a, std::pair<AlphaCode, size_t> &b) {
  if (a.second != b.second) return a.second > b.second;
  return a.first < b.first;
}

void createAlphabet(
  const std::map<AlphaCode, size_t> &frequencyMap,
  std::vector<AlphaCode> &alphabet,
  std::map<AlphaCode, AlphaChar> &index
) {
  // assert: 0 only once at end of sequence
  alphabet.reserve(frequencyMap.size());
  index.emplace(0, 0);
  alphabet.emplace_back(0);
  std::vector<std::pair<AlphaCode, size_t>> codes(frequencyMap.begin(), frequencyMap.end());
  std::sort(codes.begin(), codes.end(), compareFrequency);
  for (auto x : codes) {
    if (0 == x.first) {
      continue;
    }
    index.emplace(x.first, alphabet.size());
    alphabet.emplace_back(x.first);
  }
}

// build a suffix tree via McCreight's algorithm
// https://www.cs.helsinki.fi/u/tpkarkka/opetus/13s/spa/lecture09-2x4.pdf
//
SuffixTree * createSuffixTree(
    const std::vector<AlphaCode> &alphabet,
    const std::vector<AlphaChar> &alphaSequence
) {
  // construct suffix tree
  int ops = 0;
  size_t d = 0;
  SuffixTree *root = new SuffixTree(alphabet.size(), d);
  ops += 1;
  root->slink = root;
  SuffixTree *u = root;
  for (size_t i = 0; i < alphaSequence.size(); i++) {
    while (d == u->depth) {
      SuffixTree *child = u->children[alphaSequence[i + d]];
      ops += 1;
      if (NULL == child) break;
      u = child;
      d = d + 1;
      while ((d < u->depth) && (alphaSequence[u->start + d] == alphaSequence[i + d])) {
        ops += 1;
        d = d + 1;
      }
    } 
    if (d < u->depth) {
      ops += 1;
      u = u->splice_node(d, alphaSequence);
    }
    ops += 1;
    u->add_leaf(i, d, alphaSequence);
    if (NULL == u->slink) {
      ops += 1;
      u->compute_slink(alphaSequence);
    }
    u = u->slink;
    d = u->depth;
  }

  // stats
  logD("ops %d", ops);

  // produce root 
  return root;

}

struct Path {

  Path * prev;
  DuplicateSpans * state;
  Span span;
  size_t weight;

  Path(int subsong, int channel, size_t start, size_t length) :
    prev(NULL),
    state(NULL),
    span(subsong, channel, start, length),
    weight(0) {}

};

void compressSequence(
  SuffixTree *root,
  int subsong,
  int channel,
  const std::vector<AlphaChar> &alphaSequence,
  std::vector<Span> &copySequence
) {
  const size_t minRepeatDepth = 3;

  // find maximal repeats
  std::vector<std::vector<DuplicateSpans *>> spanStarts;
  std::vector<std::vector<DuplicateSpans *>> spanMids;
  std::vector<std::vector<DuplicateSpans *>> spanEnds;
  spanStarts.resize(alphaSequence.size());
  spanMids.resize(alphaSequence.size());
  spanEnds.resize(alphaSequence.size());
  std::vector<SuffixTree *> maximalRepeats;
  root->gather_left(maximalRepeats, alphaSequence);
  std::priority_queue<DuplicateSpans *, std::vector<DuplicateSpans *>, CompareDuplicateSpanWeights> priorityQueue;
  for (auto x : maximalRepeats) {
    if (x->depth < minRepeatDepth) {
      // skip short sequences
      continue;
    }
    std::vector<SuffixTree *> leaves;
    x->gather_leaves(leaves);
    std::sort(leaves.begin(), leaves.end(), compareStart);
    // remove any overlapping repeats
    size_t repeats = 0;
    for (size_t i = 0, lastEnd = 0; i < leaves.size(); i++) {
      auto l = leaves[i];
      if (l->start < lastEnd) {
        leaves[i] = (SuffixTree *)NULL;
        continue;
      }
      lastEnd = l->start + x->depth;
      repeats++;
    }
    size_t length = x->depth;
    size_t uncompressed_size = length * repeats;
    size_t overhead = length + repeats;
    if (overhead >= uncompressed_size) {
      continue; // not worth it
    }
    size_t score = uncompressed_size - overhead;
    DuplicateSpans *duplicates = new DuplicateSpans(length, score);
    priorityQueue.push(duplicates);
    for (size_t i = 0; i < leaves.size(); i++) {      
      auto l = leaves[i];
      if (NULL == l) continue;
      duplicates->spans.emplace_back(Span(subsong, channel, l->start, length));
      AlphaChar charIn = l->start > 0 ? alphaSequence[l->start - 1] : 0;
      duplicates->in[charIn]++;
      spanStarts[l->start].emplace_back(duplicates);
      size_t end = l->start + length;
      AlphaChar charOut = end < (alphaSequence.size() - 1) ? alphaSequence[end + 1] : 0;
      duplicates->out[charOut]++;
      spanEnds[end].emplace_back(duplicates);
      for (size_t j = l->start; j < end; j++) {
        spanMids[j].emplace_back(duplicates);
      }
    }
  }
  
  std::vector<Path *> paths;
  Path *start = new Path(subsong, channel, 0, 0);
  paths.emplace_back(start);
  std::queue<Path *> solutionQueue;
  solutionQueue.push(start);
  std::vector<Path *> solutions;
  while (!solutionQueue.empty()) {
    auto path = solutionQueue.front();
    solutionQueue.pop();
    logD("searching path %d: %d-%d (%d)", (size_t)path, path->span.start, path->span.length, path->weight);
    size_t nextStart = path->span.start + path->span.length;
    if (nextStart >= alphaSequence.size()) {
      solutions.emplace_back(path);
      continue;
    }
    auto &starts = spanStarts[nextStart];
    Path *next;
    if (starts.size() == 0) {
      if (path->state == NULL) {
        path->span.length++;
        logD("extending path %d: %d-%d ", (size_t)path, path->span.start, path->span.length);
        next = path;
      } else {
        next = new Path(subsong, channel, nextStart, 1);
        logD("new path %d: %d-%d", (size_t)next, nextStart, 1);
        paths.emplace_back(next);
        next->prev = path;
        next->weight = path->weight;
      }
      next->weight += 1;
      solutionQueue.push(next);
      continue;
    }
    for (auto dups : starts) {
      Path *next = new Path(subsong, channel, nextStart, dups->length);
      paths.emplace_back(next);
      logD("sub path %d: %d-%d", (size_t)next, next->span.start, next->span.length);
      next->prev = path;
      next->state = dups;
      next->weight = path->weight;
      if (dups->spans[0].start == nextStart) {
        next->weight += dups->length;
      } else {
        next->weight += 1;
      }
      solutionQueue.push(next);
    }
  }
  
  for (auto path : solutions) {
    logD("path: %d", path->weight);
  }

  for (auto path : paths) {
    delete path;
  }
  

  size_t uniqueSpans = 0;
  size_t minRepeats = 0;
  size_t minTransitions = 0;
  double minTransitionBits = 0;
  for (size_t i = 0; i < alphaSequence.size(); i++) {
    auto &mids = spanMids[i];
    logD("seq: %d [%d] - spans: %d", i, alphaSequence[i], mids.size());
    if (mids.size() == 0) {
      uniqueSpans++;
    }
    auto &ends = spanEnds[i];
    for (auto dups : ends) {
      logD(" end: %d - weight: %d spans: %d in: %d out: %d", dups->length, dups->weight, dups->spans.size(), dups->in.size(), dups->out.size());
    }
    auto &starts = spanStarts[i];
    DuplicateSpans *minCandidate = NULL;
    for (auto dups : starts) {
      if (minCandidate == NULL || minCandidate->length > dups->length) {
        minCandidate = dups;
      }
      logD(" start: %d - weight: %d spans: %d in: %d out: %d", dups->length, dups->weight, dups->spans.size(), dups->in.size(), dups->out.size());
    }
    if (minCandidate != NULL) {
      if (minCandidate->spans[0].start == i) {
        minRepeats += minCandidate->length + 1;
      }
      minTransitions += 1;
      size_t maxTransitions = spanMids[i].size();
      if (maxTransitions < minCandidate->in.size()) {
        maxTransitions = minCandidate->in.size();
      }
      if (maxTransitions < minCandidate->out.size()) {
        maxTransitions = minCandidate->out.size();
      }
      minTransitionBits += log2(maxTransitions);

    }
  }
  size_t totalSizeEstimate = uniqueSpans + minRepeats + (minTransitionBits / 8);
  logD("codes: %d estimate: %d unique:%d minRepeats: %d minTransitions: %d minTransitionBits: %f",
        alphaSequence.size(), totalSizeEstimate, uniqueSpans, minRepeats, minTransitions, minTransitionBits);

  // initialize 
  copySequence.reserve(alphaSequence.size());
  for (size_t i = 0; i < alphaSequence.size(); i++) {
    copySequence.emplace_back(Span(subsong, channel, i, 0));
  }

  while (priorityQueue.size() > 0) {
    // take the topmost set of duplicate spans
    auto top = priorityQueue.top();
    priorityQueue.pop();

    // check how many spans are still maximal
    size_t nonMaximalSpans = 0;
    for (size_t i = 0; i < top->spans.size(); i++) {
      auto &span = top->spans[i];
      size_t spanEnd = span.start + span.length;
      bool isMaximal = true;
      for (size_t j = span.start; isMaximal && j < spanEnd; j++) {
        if (copySequence[j].length > 0) {
          isMaximal = false;
        }
      }
      if (!isMaximal) {
        if (top->weight < span.length) {
          // no spans are valid
          top->weight = 0;
          break;

        }
        // take this span out of consideration
        top->weight -= span.length;
        span.length = 0;
        nonMaximalSpans++;
      }
    }

    // check if the total weight has changed
    if (0 == top->weight) {
      // dispose
      delete top;
      continue;

    } else if ((nonMaximalSpans > 0) && (priorityQueue.size() > 0) && (priorityQueue.top()->weight > top->weight)) {
      // candidate is no longer the most valuable, defer processing
      priorityQueue.push(top);
      continue;

    } 

    // apply compression
    logD("compressing: %d - weight: %d spans: %d in: %d out: %d", top->length, top->weight, top->spans.size(), top->in.size(), top->out.size());

    bool firstCopy = true;
    size_t firstCopyStart;
    for (size_t i = 0; i < top->spans.size(); i++) {
      auto &span = top->spans[i];
      if (0 == span.length) {
        // skip invalid spans
        continue;
      }
      size_t spanEnd = span.start + span.length;
      if (firstCopy) {
        // head of the first span marked as a literal span
        firstCopy = false;
        firstCopyStart = span.start;
        copySequence[span.start] = span;

      } else {
        // heads of subsequent spans marked as references to first span
        copySequence[span.start] = Span(subsong, channel, firstCopyStart, span.length);

      }
      // fill in the tail of the span as individual literal elements
      for (size_t j = span.start + 1; j < spanEnd; j++) {
        copySequence[j].length = 1;
      }
    }
    delete top;
  }

  // mark any untouched spans as individual literal elements
  for (size_t i = 0; i < copySequence.size(); i++) {
    if (0 == copySequence[i].length) copySequence[i].length = 1;
  }

}

void createAlphabet(
  const std::map<AlphaCode, String> &commonDumpSequences,
  std::vector<AlphaCode> &alphabet,
  std::map<String, AlphaChar> &index
) {
  // construct the alphabet
  alphabet.reserve(commonDumpSequences.size() + 1);
 
  alphabet.emplace_back(0);
  index.emplace("$", 0);
  for (auto x : commonDumpSequences) {
    index.emplace(x.second, alphabet.size());
    alphabet.emplace_back(x.first);
  }
}

void translateString(
    const std::vector<String> &sequence,
    const std::map<String, String> &representativeMap,
    const std::map<String, AlphaChar> &index,
    std::vector<AlphaChar> &alphaSequence
) {
    // copy string in alphabet
    alphaSequence.reserve(sequence.size() + 1); 
    for (auto key : sequence) {
      AlphaChar c = index.at(representativeMap.at(key));
      alphaSequence.emplace_back(c);
    }
    alphaSequence.emplace_back(0);
}

void testCommonSubsequences(const String &input) {

  std::vector<String> sequence;
  std::map<AlphaCode, String> commonDumpSequences;
  std::map<String, String> representativeMap;
  for (size_t i = 0; i < input.size(); i++) {
    char c = input[i];
    String key = input.substr(i, 1);
    sequence.emplace_back(key);
    representativeMap.emplace(key, key);
    uint64_t hash = (u_int64_t)c;
    commonDumpSequences.emplace(hash, key);
  }

  std::vector<AlphaCode> alphabet;
  std::map<String, AlphaChar> index;
  createAlphabet(
    commonDumpSequences,
    alphabet,
    index
  );

  std::vector<AlphaChar> alphaSequence;
  translateString(
    sequence,
    representativeMap,
    index,
    alphaSequence
  );

  SuffixTree *root = createSuffixTree(
    alphabet,
    alphaSequence
  );

  // format
  std::vector<std::pair<SuffixTree *, int>> stack;
  stack.emplace_back(std::pair<SuffixTree *, int>(root, 0));
  while (stack.size() > 0) {
    auto x = stack.back();
    stack.pop_back();
    SuffixTree * u = x.first;
    int treeDepth = x.second;
    String indent(treeDepth * 2, ' ');
    String label = input.substr(u->substring_start(), u->substring_len());
    for (auto child : u->children) {
      if (NULL == child) continue;
      stack.push_back(std::pair<SuffixTree *, int>(child, treeDepth + 1));
    }
    logD("%s%s (%d)", indent, label, u->start);
  }

}

void testCV(const String &input) {

  std::vector<String> sequence;
  std::map<AlphaCode, String> commonDumpSequences;
  std::map<String, String> representativeMap;
  for (size_t i = 0; i < input.size(); i++) {
    char c = input[i];
    String key = input.substr(i, 1);
    sequence.emplace_back(key);
    representativeMap.emplace(key, key);
    uint64_t hash = (u_int64_t)c;
    commonDumpSequences.emplace(hash, key);
  }

  std::vector<AlphaCode> alphabet;
  std::map<String, AlphaChar> index;
  createAlphabet(
    commonDumpSequences,
    alphabet,
    index
  );

  std::vector<AlphaChar> alphaSequence;
  translateString(
    sequence,
    representativeMap,
    index,
    alphaSequence
  );

  SuffixTree *root = createSuffixTree(
    alphabet,
    alphaSequence
  );

  // format
  logD("INPUT: %s", input);
  std::vector<std::pair<SuffixTree *, int>> stack;
  stack.emplace_back(std::pair<SuffixTree *, int>(root, 0));
  while (stack.size() > 0) {
    auto x = stack.back();
    stack.pop_back();
    SuffixTree * u = x.first;
    int treeDepth = x.second;
    String indent(treeDepth * 2, ' ');
    String label = input.substr(u->substring_start(), u->substring_len());
    for (auto child : u->children) {
      if (NULL == child) continue;
      stack.push_back(std::pair<SuffixTree *, int>(child, treeDepth + 1));
    }
    logD("%s%s (start=%d, cv=%d, depth=%d)", indent, label, u->start, u->substring_start(), u->depth);
  }

  // compress
  size_t i = 0;
  Span s(0, 0, 0, 0);
  for (i = 0; i < alphaSequence.size(); i++) {
    root->find_prior(i, alphaSequence, s);
    String label = input.substr(s.start, s.length);
    logD("PRIOR %d %s (%d, %d)", i, label, s.start, s.length);
  }

}