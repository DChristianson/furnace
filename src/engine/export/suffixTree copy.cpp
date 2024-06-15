/** 
 * Write track data 
 * 
 * Stream data format:
 * 
 * 00000000 stop
 * 000ddddd sustain for d, 31 >= d >= 1
 * rrrxxxxx r <- x, 6 >= r >= 1, 31 >= x >= 0
 * 
 * 0000cccc 0000dddd 000fffff 000ggggg 0000vvvv 0000wwww
 * 
 */
void DivExportAtari2600::writeTrackData(
  DivEngine* e, 
  std::vector<RegisterWrite> &registerWrites,
  std::vector<DivROMExportOutput> &ret
) {


  // create a frequency map of all codes in all songs
  //BUGBUG: for testing
  SafeWriter* uncompressedSequenceData =new SafeWriter;
  SafeWriter* uncompressedBinaryData =new SafeWriter;
  uncompressedSequenceData->init();
  uncompressedBinaryData->init();
  std::map<AlphaCode, size_t> fakeCommandDictionary;
  std::map<AlphaCode, size_t> frequencyMap;
  std::map<AlphaCode, std::map<AlphaCode, size_t>> frequencyFollowMap;
  size_t totalSizeAllSequences = 0;
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < 2; channel += 1) {
      totalSizeAllSequences += codeSequences[subsong][channel].size();
      size_t i = 0;
      AlphaCode lastCode = 0;
      for (auto code : codeSequences[subsong][channel]) {

        writeAlphaCode(uncompressedSequenceData, uncompressedBinaryData, code, fakeCommandDictionary);
        frequencyMap[code]++;
        frequencyFollowMap[lastCode][code]++;
        lastCode = code;
        i++;
      }
    }
  }
  ret.push_back(DivROMExportOutput("Track_uncompressed.asm", uncompressedSequenceData));
  ret.push_back(DivROMExportOutput("Track_uncompressed.bin", uncompressedBinaryData));

  // put all distinct codes into an "alphabet" so we can build a suffix tree
  std::vector<AlphaCode> alphabet;
  std::map<AlphaCode, AlphaChar> index;
  createAlphabet(
    frequencyMap,
    alphabet,
    index
  );

  size_t singletons = 0;
  size_t maxbranch = 0;
  AlphaCode maxcode = 0;
  for (auto& x : frequencyFollowMap) {
    if (x.second.size() > maxbranch) {
      maxbranch = x.second.size();
      maxcode = x.first;
    }
    if (x.second.size() == 1) {
      singletons++;
    }
  }
  logD("total codes : %d ", frequencyMap.size());
  logD("maxbranch %08x : %d ", maxcode, maxbranch);
  logD("singletons : %d ", singletons);

  // debugging: compute basic stats
  logD("total size of all sequences: %d", totalSizeAllSequences);
  logD("distinct codes: %d", alphabet.size());
  for (auto a : alphabet) {
    logD("  %08x -> %d (rank %d)", a, frequencyMap[a], index.at(a));
  }
  double entropy = 0;
  const double symbolCount = totalSizeAllSequences;
  for (auto &x : frequencyMap) {
    if (0 == x.first) {
      continue;
    }
    const double p = ((double) x.second) / symbolCount;
    const double logp = log2(p);
    entropy = entropy - (p * logp);
  }
  const double expectedBits = entropy * symbolCount;
  const double expectedBytes = expectedBits / 8;
  logD("entropy: %lf (%lf bits / %lf bytes)", entropy, expectedBits, expectedBytes);

  // compress sequences
  std::vector<Span> copySequences[e->song.subsong.size()][2];
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < 2; channel += 1) {
      std::vector<AlphaChar> alphaSequence;
      alphaSequence.reserve(codeSequences[subsong][channel].size());         

      // copy string into alphabet
      for (auto code : codeSequences[subsong][channel]) {
        AlphaChar c = index.at(code);
        alphaSequence.emplace_back(c);
      }

      // create suffix tree 
      SuffixTree *root = createSuffixTree(
        alphabet,
        alphaSequence
      );

      // find maximal repeats
      compressSequence(
        root,
        subsong,
        channel,
        alphaSequence,
        copySequences[subsong][channel]
      );

      delete root;

    }
  }

  std::map<AlphaCode, size_t> commandFrequency;
  size_t totalEncodedSize = 0;
  std::vector<AlphaCode> encodedSequences[e->song.subsong.size()][2];
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < 2; channel += 1) {
      encodeCopySequence(
        codeSequences[subsong][channel], 
        Span(subsong, channel, 0, codeSequences[subsong][channel].size()),
        copySequences[subsong][channel], 
        encodedSequences[subsong][channel]);

      for (auto code : encodedSequences[subsong][channel]) {
        commandFrequency[code]++;
      }

      const size_t encodingSize = encodedSequences[subsong][channel].size();
      totalEncodedSize += encodingSize;
      logD("sequence estimated size: %d", encodingSize);

    }
  }

  logD("command frequencies");
  std::priority_queue<std::pair<AlphaCode, size_t>, std::vector<std::pair<AlphaCode, size_t>>, CompareFrequencies> priorityQueue;
  for (auto &x : commandFrequency) {
    logD("  %010x -> %d", x.first, x.second);
    unsigned char codeType = (x.first >> 32);
    if ((codeType != 7) && (codeType != 9)) {
      continue;
    }
    if (priorityQueue.size() == 128) {
      if (priorityQueue.top().second < x.second) {
        priorityQueue.pop();
        priorityQueue.push(x);
      }
    } else {
      priorityQueue.push(x);
    }
  }
  
  std::map<AlphaCode, size_t> commandDictionary;
  logD("dictionary");
  size_t rank = 0;
  while (!priorityQueue.empty()) {
    auto top = priorityQueue.top();
    logD("  (%d): %010x -> %d", rank, top.first, top.second);
    commandDictionary[top.first] = rank++;
    priorityQueue.pop();
  }

  std::map<AlphaCode, size_t> literalFrequency;

  SafeWriter* sequenceData = new SafeWriter;
  SafeWriter* binarySequenceData =new SafeWriter;
  sequenceData->init();
  binarySequenceData->init();
  size_t totalBinarySize = 0;
  for (size_t subsong = 0; subsong < e->song.subsong.size(); subsong++) {
    for (int channel = 0; channel < 2; channel += 1) {
      sequenceData->writeText(fmt::sprintf("SONG_%d_CHANNEL_%d\n", subsong, channel));
      for (auto code : encodedSequences[subsong][channel]) {
        totalBinarySize += writeAlphaCode(sequenceData, binarySequenceData, code, commandDictionary);
      }
    }
  }
  ret.push_back(DivROMExportOutput("Track_sequences.asm", sequenceData));
  ret.push_back(DivROMExportOutput("Track_binary.bin", binarySequenceData));
      
  logD("song encoded size/sequence length: %d / %d / %d", totalEncodedSize, totalSizeAllSequences, totalBinarySize);

  // BUGBUG: Not production 
  // just testing 
//  xabxac -  bxa<c$
//            10 1
//            10
//  xabxabxac bxa<c$
//            10 1
//            110
//  xabxacxab cxa<b<$
//            10 1 0
//            0110
//  xabcyiiizabcqabcyr xabc<y<iiiz@q@r$
//                      1  2 3    1213
//                     00101
//  testCommonSubsequences("banana");
 // testCommonSubsequences("abcdeabcdefghijfghijabcdexyxyxyx");
 // testCommonSubsequences("xabcyiiiza_bcqabcyr");
//  testCommonSubsequencesBrute("banana");
//  testCommonSubsequencesBrute("xabcyiiizabcqabcyr");
  // findCommonSubSequences(
  //   channelSequences[0],
  //   commonDumpSequences,
  //   representativeMap
    // );


  // create track data
  logD("writing song audio data");
  SafeWriter* songData=new SafeWriter;
  songData->init();
  songData->writeText(fmt::sprintf("; Song: %s\n", e->song.name));
  songData->writeText(fmt::sprintf("; Author: %s\n", e->song.author));

  // emit song table
  logD("writing song table");
  size_t songTableSize = 0;
  songData->writeText("\n; Song Lookup Table\n");
  songData->writeText(fmt::sprintf("NUM_SONGS = %d\n", e->song.subsong.size()));
  songData->writeText("SONG_TABLE_START_LO\n");
  for (size_t i = 0; i < e->song.subsong.size(); i++) {
    songData->writeText(fmt::sprintf("    byte <SONG_%d_ADDR\n", i));
    songTableSize++;
  }
  songData->writeText("SONG_TABLE_START_HI\n");
  for (size_t i = 0; i < e->song.subsong.size(); i++) {
    songData->writeText(fmt::sprintf("    byte >SONG_%d_ADDR\n", i));
    songTableSize++;
  }

  // audio metadata
  songData->writeC('\n');
  songData->writeText(fmt::sprintf("; Song Table Size %d\n", songTableSize));
  ret.push_back(DivROMExportOutput("Track_song_table.asm", songData));

}

/**
 * @brief write a set of registers
 * 
 * delta literal encoding:
 * -----------------
 * 11sfffff ccccvvvv - embedded register values with sustain 0/1
 * 10dddddd          - literal dictionary lookup 0-63
 * 01sfffff          - change frequency with sustain 0/1
 * 001svvvv          - change volume with sustain 0/1
 * 000sssss          - sustain 1-31
 * 00000000          - end block
 * 
 * @param channel 
 * @param registerMask 
 * @param values 
 * @param framesToWrite 
 * @param codeSequence 
 */
void DivExportAtari2600::writeAlphaCodesToChannel(
  int channel,
  const TiaRegisterMask &registerMask,
  int *values,
  int framesToWrite,
  std::vector<AlphaCode> &codeSequence
) {

  // encode a register update as a 32 bit integer containing all the register updates for a single channel
  // 0000cfv 0000cccc 000fffff 0000vvvv mask bits + cx + fx + vx
  AlphaCode rx = 0;
  const int setBits = registerMask.count();
  for (size_t r = channel; r < NUM_TIA_REGISTERS; r += 2) {
    rx = (rx << 1) | (setBits > 1 ? 1 : registerMask[r]);
  }
  for (size_t r = channel; r < NUM_TIA_REGISTERS; r += 2) {
    rx = (rx << 8) | ((setBits > 1 || registerMask[r]) ? values[r] : 0);
  }
  const unsigned char sx = framesToWrite > 4 ? 4 : 1;
  rx = (rx << 8) | sx;
  codeSequence.emplace_back(rx);
  framesToWrite -= sx;

  // encode any additional frames as 5 bits indicating the number of frames to skip
  while (framesToWrite > 0) {
    const unsigned char skip = framesToWrite > 15 ? 15 : framesToWrite;
    codeSequence.emplace_back(skip);
    framesToWrite = framesToWrite - skip;
  }

}

AlphaCode AC_SPAN_LABEL(const Span &span) {
  return ((AlphaCode) 8 << 32) | ((AlphaCode) span.subsong << 24) | ((AlphaCode) span.channel << 16) | (AlphaCode) span.start;
}

AlphaCode AC_SPAN_REF(const Span &span) {
  return ((AlphaCode) 9 << 32) | ((AlphaCode) span.subsong << 24) | ((AlphaCode) span.channel << 16) | (AlphaCode) span.start;
}

const AlphaCode AC_POP = 0;

void DivExportAtari2600::encodeCopySequence(
  const std::vector<AlphaCode> &sequence, 
  const Span &bounds,
  const std::vector<Span> &copySequence,
  std::vector<AlphaCode> &encodedSequence)
{
  size_t currentIndex = bounds.start;
  size_t endIndex = bounds.start + bounds.length;
  while (currentIndex < endIndex) {
    if (copySequence[currentIndex].start == currentIndex && copySequence[currentIndex].length == 1) {
      size_t spanEndIndex = currentIndex + 1;
      while (spanEndIndex < endIndex && copySequence[spanEndIndex].start == spanEndIndex && copySequence[spanEndIndex].length == 1) {
        spanEndIndex++;
      }
      encodeDeltaSequence(
        sequence, 
        Span(bounds.subsong, bounds.channel, currentIndex, spanEndIndex - currentIndex),
        encodedSequence);
      currentIndex = spanEndIndex;
      continue;
    }

    if (copySequence[currentIndex].start == currentIndex) {
      // push copy block
      encodedSequence.emplace_back(AC_SPAN_LABEL(copySequence[currentIndex]));
      encodeDeltaSequence(
        sequence, 
        copySequence[currentIndex],
        encodedSequence);
      encodedSequence.emplace_back(AC_POP);
      //logD("label %d/%d start %d len %d", copySequence[currentIndex].subsong, copySequence[currentIndex].channel, copySequence[currentIndex].start, copySequence[currentIndex].length);
    } else {
      // emit block ref
      encodedSequence.emplace_back(AC_SPAN_REF(copySequence[currentIndex]));
      //logD("ref %d/%d start %d len %d", copySequence[currentIndex].subsong, copySequence[currentIndex].channel, copySequence[currentIndex].start, copySequence[currentIndex].length);
    }

    currentIndex += copySequence[currentIndex].length;
  }

}

void DivExportAtari2600::encodeDeltaSequence(
  const std::vector<AlphaCode> &sequence, 
  const Span &bounds,
  std::vector<AlphaCode> &encodedSequence)
{
  size_t currentIndex = bounds.start;
  size_t endIndex = bounds.start + bounds.length;
  while (currentIndex < endIndex) {
    AlphaCode cx = sequence[currentIndex];
    currentIndex++;
    AlphaCode skip = 0;
    while (currentIndex < endIndex && ((sequence[currentIndex] >> 32) == 0)) {
      skip += sequence[currentIndex];
      currentIndex++;
    }
    cx += skip;
    encodedSequence.emplace_back(cx);
  }
}

size_t DivExportAtari2600::writeAlphaCode(SafeWriter* w, SafeWriter* b, AlphaCode code, const std::map<AlphaCode, size_t> &commandDictionary) {
  unsigned char rx = code >> 32;
  if (rx == 9) {
    int subsong = (code >> 24) & 0xff;
    int channel = (code >> 16) & 0xff;
    size_t start = code & 0xffff;
    w->writeText(fmt::sprintf("    ; SPAN_REF(%d, %d, %d)\n", subsong, channel, start));
    auto index = commandDictionary.find(code);
    if (index != commandDictionary.end()) {
      w->writeText(fmt::sprintf("    byte %d\n", index->second));
      b->writeC(index->second);
      return 1;
    } else {
      w->writeText(fmt::sprintf("    word SPAN_START_%d_%d_%d\n", subsong, channel, start));
      b->writeI(0xf000 + start);
      return 2;
    }

  } else if (rx == 8) {
    int subsong = (code >> 24) & 0xff;
    int channel = (code >> 16) & 0xff;
    size_t start = code & 0xffff;
    w->writeText(fmt::sprintf("    ; SPAN_START(%d, %d, %d)\n", subsong, channel, start));
    return 0;

  } else if (rx == 7) {
    unsigned char cx = (code >> 24) & 0xff;
    unsigned char fx = (code >> 16) & 0xff;
    unsigned char vx = (code >> 8) & 0xff;
    unsigned char sx = code & 0xff;
    w->writeText(fmt::sprintf("    ; C%0d F%0d V%0d S%0d\n", cx, fx, vx, sx));
    auto index = commandDictionary.find(code);
    if (index != commandDictionary.end()) {
      w->writeText(fmt::sprintf("    byte %d\n", index->second));
      b->writeC(index->second);
      return 1;
    } else {
      w->writeText(fmt::sprintf("    byte %d, %d\n", 0xa0 + fx, (cx << 4) | vx));
      b->writeC(0xa0 + fx);
      b->writeC((cx << 4) | vx);

      if (sx > 1) {
        w->writeText(fmt::sprintf("    byte %d\n", 0x80 + (sx - 1)));
        b->writeC(0x80 + (sx - 1));
        return 3;
      }
      return 2;
    }

  } else if (rx == 4) {
    unsigned char cx = (code >> 24) & 0xff;
    unsigned char sx = code & 0xff;
    w->writeText(fmt::sprintf("    ; C%0d S%0d\n", cx, sx));
    w->writeText(fmt::sprintf("    byte %d\n", 0x90 + cx));
    b->writeC(0x90 + cx);
    if (sx > 1) {
      w->writeText(fmt::sprintf("    byte %d\n", 0x80 + (sx - 1)));
      b->writeC(0x80 + (sx - 1));
      return 2;
    }
    return 1;

  } else if (rx == 2) {
    unsigned char fx = (code >> 16) & 0xff;
    unsigned char sx = code & 0xff;
    w->writeText(fmt::sprintf("    ; F%0d S%0d\n", fx, sx));
    w->writeText(fmt::sprintf("    byte %d\n", 0xc0 + fx));
    b->writeC(0xc0 + fx);
    if (sx > 1) {
      w->writeText(fmt::sprintf("    byte %d\n", 0x80 + (sx - 1)));
      b->writeC(0x80 + (sx - 1));
      return 2;
    }
    return 1;

  } else if (rx == 1) {
    unsigned char vx = (code >> 8) & 0xff;
    unsigned char sx = code & 0xff;
    w->writeText(fmt::sprintf("    ; V%0d S%0d\n", vx, sx));
    w->writeText(fmt::sprintf("    byte %d\n", 0xe0 + vx));
    b->writeC(0xe0 + vx);
    if (sx > 1) {
      w->writeText(fmt::sprintf("    byte %d\n", 0x80 + (sx - 1)));
      b->writeC(0x80 + (sx - 1));
      return 2;
    }
    return 1;

  } else if (code == 0) {
    w->writeText("    ; POP\n");
    w->writeText(fmt::sprintf("    byte 0\n"));
    b->writeC(0);
    return 1;

  } else {
    w->writeText(fmt::sprintf("    ; SKIP %0d\n", code));
    w->writeText(fmt::sprintf("    byte %d\n", 0x80 + code));
    b->writeC(0x80 + code);
    return 1;
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

void createAlphabet(
  const std::map<AlphaCode, size_t> &frequencyMap,
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