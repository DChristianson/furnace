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

#include "registerDump.h"

void registerDump(
  DivEngine* e, 
  int subsong,
  std::vector<RegisterWrite> &writes
) {
 for (int i=0; i<e->song.systemLen; i++) {
    e->getDispatch(i)->toggleRegisterDump(true);
  }
  e->changeSongP(subsong);
  e->stop();
  e->setRepeatPattern(false);
  e->setOrder(0);
  e->play();
  int nextTickCount = -1;
  bool done=false;
  std::map<int, int> currentRegisterValue;
  while (!done && e->isPlaying()) {
    
    done = e->nextTick(false, true);
    if (done) break;
    nextTickCount += 1;
    // get register writes
    for (int i=0; i<e->song.systemLen; i++) {
      std::vector<DivRegWrite>& registerWrites=e->getDispatch(i)->getRegisterWrites();
      DivSystem system = e->song.system[i];
      for (DivRegWrite& registerWrite: registerWrites) {
        writes.emplace_back (
          RegisterWrite(
            nextTickCount,
            e->getCurrentSubSong(),
            e->getOrder(),
            e->getRow(),
            i,
            system,
            e->getTotalSeconds(),
            e->getTotalTicks(),
            registerWrite.addr,
            registerWrite.val
          )
        );
      }
      registerWrites.clear();
    }

  }
  for (int i=0; i<e->song.systemLen; i++) {
    e->getDispatch(i)->toggleRegisterDump(false);
  }

}

void captureSequence(
  DivEngine* e, 
  int subsong,
  int channel,
  DivSystem system, 
  std::map<unsigned int, unsigned int> &addressMap,
  std::vector<String> &sequence,
  std::map<String, DumpSequence> &registerDumps
) {
  for (int i=0; i<e->song.systemLen; i++) {
    e->getDispatch(i)->toggleRegisterDump(true);
  }
  e->changeSongP(subsong);
  e->stop();
  e->setRepeatPattern(false);
  e->setOrder(0);
  e->play();
  
  int lastWriteTicks = e->getTotalTicks();
  int lastWriteSeconds = e->getTotalSeconds();
  int deltaTicksR = 0;
  int deltaTicks = 0;

  bool needsRegisterDump = false;
  bool needsWriteDuration = false;      

  RowIndex curRowIndex(e->getCurrentSubSong(), e->getOrder(), e->getRow());

  ChannelState currentState(0);
  String key = getSequenceKey(curRowIndex.subsong, curRowIndex.ord, curRowIndex.row, channel);
  sequence.emplace_back(key);
  auto it = registerDumps.emplace(key, DumpSequence());
  DumpSequence *currentDumpSequence = &(it.first->second);


  bool done=false;
  while (!done && e->isPlaying()) {
    
    done = e->nextTick(false, true);
    if (done) break;
    
    int currentTicks = e->getTotalTicks();
    int currentSeconds = e->getTotalSeconds();
    deltaTicks = 
      currentTicks - lastWriteTicks + 
      (TICKS_PER_SECOND * (currentSeconds - lastWriteSeconds));

    // check if we've changed rows
    if (curRowIndex.advance(e->getCurrentSubSong(), e->getOrder(), e->getRow())) {
      if (needsRegisterDump) {
        currentDumpSequence->dumpRegisters(currentState);
        needsWriteDuration = true;
      }
      if (needsWriteDuration) {
        // prev seq
        deltaTicksR = currentDumpSequence->writeDuration(deltaTicks, deltaTicksR, TICKS_AT_60HZ);
        deltaTicks = 0;
        lastWriteTicks = currentTicks;
        lastWriteSeconds = currentSeconds;
        needsWriteDuration = false;
      }
      // new sequence
      key = getSequenceKey(curRowIndex.subsong, curRowIndex.ord, curRowIndex.row, channel);
      sequence.emplace_back(key);
      auto nextIt = registerDumps.emplace(key, DumpSequence());
      currentDumpSequence = &(nextIt.first->second);
      needsRegisterDump = true;
    }

    // get register dumps
    bool isDirty = false;
    for (int i=0; i<e->song.systemLen; i++) {
      std::vector<DivRegWrite>& registerWrites=e->getDispatch(i)->getRegisterWrites();
      for (DivRegWrite& registerWrite: registerWrites) {
        auto it = addressMap.find(registerWrite.addr);
        if (it == addressMap.end()) {
          continue;
        }
        isDirty |= currentState.write(it->second, registerWrite.val);
      }
      registerWrites.clear();
    }

    if (isDirty || (currentDumpSequence->size() == 0)) {
      // end last seq
      if (needsWriteDuration) {
        deltaTicksR = currentDumpSequence->writeDuration(deltaTicks, deltaTicksR, TICKS_AT_60HZ);
        lastWriteTicks = currentTicks;
        lastWriteSeconds = currentSeconds;
        deltaTicks = 0;
      }
      // start next seq
      needsWriteDuration = true;
      currentDumpSequence->dumpRegisters(currentState);
      needsRegisterDump = false;
    }
  }
  if (needsRegisterDump) {
    currentDumpSequence->dumpRegisters(currentState);
    needsWriteDuration = true;
  }
  if (needsWriteDuration) {
    int currentTicks = e->getTotalTicks();
    int currentSeconds = e->getTotalSeconds();
    deltaTicks = 
      currentTicks - lastWriteTicks + 
      (TICKS_PER_SECOND * (currentSeconds - lastWriteSeconds));
    // final seq
    currentDumpSequence->writeDuration(deltaTicks, deltaTicksR, TICKS_AT_60HZ);
  }
  for (int i=0; i<e->song.systemLen; i++) {
    e->getDispatch(i)->toggleRegisterDump(false);
  }
}



// TODO: remap sequence to common subsequences
// TODO: find common suffixes
// TODO: compress the common suffixes
// TODO: emit the song with merged dumps and common phrases

void findCommonDumpSequences(
  const std::map<String, DumpSequence> &registerDumps,
  std::map<uint64_t, String> &commonDumpSequences,
  std::map<uint64_t, unsigned int> &frequencyMap,
  std::map<String, String> &representativeMap
) {
  for (auto& x: registerDumps) {
    uint64_t hash = x.second.hash();
    auto it = commonDumpSequences.emplace(hash, x.first);
    if (it.second) {
      frequencyMap.emplace(hash, 1);
    } else {
      frequencyMap[hash] += 1;
    }
    // TODO: verify no hash collision...?
    representativeMap.emplace(x.first, it.first->second);
  }
}

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
 * Compare by start
 */ 
bool compareStart(SuffixTree * a, SuffixTree * b) {
  return a->start < b->start;
}

/**
 * Compare by score
 */ 
bool compareScore(Span a, Span b) {
  return a.score > b.score;
}

void SuffixTree::gather_repeated_subsequences(
  const std::vector<AlphaChar> &alphaSequence,
  std::vector<Span> &uniqueSubsequences,
  std::vector<Span> &copySequence
) {
  // initialize 
  for (int i = 0; i < alphaSequence.size(); i++) {
    copySequence.emplace_back(Span(i, 1, 1));
  }
  // find maximal repeats
  const size_t minRepeatDepth = 2;
  std::vector<SuffixTree *> maximalRepeats;
  this->gather_left(maximalRepeats, alphaSequence);
  for (auto x : maximalRepeats) {
    if (x->depth < minRepeatDepth) {
      continue;
    }
    std::vector<SuffixTree *> leaves;
    x->gather_leaves(leaves);
    std::sort(leaves.begin(), leaves.end(), compareStart);
    // find non-overlapping repeats
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
    size_t firstCopyStart = leaves[0]->start;
    size_t length = x->depth;
    size_t score = length * repeats * 2;
    size_t overhead = repeats * 2 + 1;
    if (overhead > score) {
      continue; // not worth it
    }
    uniqueSubsequences.emplace_back(Span(firstCopyStart, length, score));
    for (auto l : leaves) {
      if (NULL == l || score <= copySequence[l->start].score) {
        continue;
      }
      copySequence[l->start] = Span(firstCopyStart, length, score);
    }
  }

  // BUGBUG: remove overlaps

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

void createAlphabet(
  const std::map<AlphaCode, unsigned int> &frequencyMap,
  std::vector<AlphaCode> &alphabet,
  std::map<AlphaCode, AlphaChar> &index
) {
  // assert: 0 only once at end of sequence
  alphabet.reserve(frequencyMap.size());
  index.emplace(0, 0);
  alphabet.emplace_back(0);
  for (auto x : frequencyMap) {
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


