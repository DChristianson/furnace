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
  long nextTickCount = -1;
  bool done=false;
  std::map<int, int> currentRegisterValue;
  while (!done && e->isPlaying()) {
    
    done = e->nextTick(false, true);
    nextTickCount += 1;
    if (done) break;

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
            e->getHz(),
            registerWrite.addr,
            registerWrite.val
          )
        );
      }
      registerWrites.clear();
    }
  }
  // write end of song marker
  writes.emplace_back (
    RegisterWrite(
      nextTickCount,
      e->getCurrentSubSong(),
      e->getOrder(),
      e->getRow(),
      -1,
      DivSystem::DIV_SYSTEM_NULL,
      e->getTotalSeconds(),
      e->getTotalTicks(),
      e->getCurHz(),
      -1,
      -1
    )
  );
  for (int i=0; i<e->song.systemLen; i++) {
    e->getDispatch(i)->toggleRegisterDump(false);
  }

}


/**
 * Extract channel states from register writes.
 */
void writeChannelStateSequence(
  const std::vector<RegisterWrite> &writes,
  int subsong,
  int channel,
  int systemIndex,
  int suppressVolume,
  const std::map<unsigned int, unsigned int> &addressMap,
  ChannelStateSequence &dumpSequence 
) {

  long lastWriteIndex = -1;
  int lastWriteTicks = 0;
  int lastWriteSeconds = 0;
  int deltaTicksR = 0;
  int deltaTicks = 0;

  ChannelState currentState(0);
  for (auto &write : writes) {
    
    long currentWriteIndex = write.writeIndex;
    int currentTicks = write.ticks;
    int currentSeconds = write.seconds;
    int freq = ((float)TICKS_PER_SECOND) / write.hz;

    deltaTicks = 
      currentTicks - lastWriteTicks + 
      (TICKS_PER_SECOND * (currentSeconds - lastWriteSeconds));

    // check if we've moved in time
    if (lastWriteIndex < currentWriteIndex) {
      if (lastWriteIndex >= 0) {
        auto lastState = currentState;
        // if volume register is zero, clear all registers
        if (suppressVolume >= 0) {
          if (lastState.registers[suppressVolume] == 0) {
            lastState.clear();
          }
        }
        dumpSequence.updateState(lastState);
        deltaTicksR = dumpSequence.addDuration(deltaTicks, deltaTicksR, freq);
        deltaTicks = 0;
      }
      lastWriteIndex = currentWriteIndex;
      lastWriteTicks = currentTicks;
      lastWriteSeconds = currentSeconds;
    }

    // stop if we reach the end marker
    if (write.systemIndex < 0) {
      break;
    }

    // process write
    auto it = addressMap.find(write.addr);
    if (it == addressMap.end()) {
      continue;
    }
    currentState.write(it->second, write.val);

  }
}

/**
 * Extract channel states in a song, keyed on subsong, ord, row and channel.
 */
void writeChannelStateSequenceByRow(
  const std::vector<RegisterWrite> &writes,
  int subsong,
  int channel,
  int systemIndex,
  int suppressVolume,
  const std::map<unsigned int, unsigned int> &addressMap,
  std::vector<String> &sequence,
  std::map<String, ChannelStateSequence> &registerDumps 
) {
  
  long lastWriteIndex = -1;
  int lastWriteTicks = 0;
  int lastWriteSeconds = 0;
  int deltaTicksR = 0;
  int deltaTicks = 0;

  RowIndex curRowIndex(subsong, 0, 0);

  ChannelState currentState(0);
  ChannelStateSequence *currentDumpSequence = NULL;
  
  for (auto &write : writes) {
    
    long currentWriteIndex = write.writeIndex;
    int currentTicks = write.ticks;
    int currentSeconds = write.seconds;
    int freq = ((float)TICKS_PER_SECOND) / write.hz;

    deltaTicks = 
      currentTicks - lastWriteTicks + 
      (TICKS_PER_SECOND * (currentSeconds - lastWriteSeconds));

    // check if we've moved in time
    if (lastWriteIndex < currentWriteIndex) {
      if (lastWriteIndex >= 0) {
        auto lastState = currentState;
        // if volume register is zero, clear all registers
        if (suppressVolume >= 0) {
          if (lastState.registers[suppressVolume] == 0) {
            lastState.clear();
          }
        }
        currentDumpSequence->updateState(lastState);
        deltaTicksR = currentDumpSequence->addDuration(deltaTicks, deltaTicksR, freq);
      }
      deltaTicks = 0;
      lastWriteIndex = currentWriteIndex;
      lastWriteTicks = currentTicks;
      lastWriteSeconds = currentSeconds;
    }

    // stop if we reach the end marker
    if (write.systemIndex < 0) {
      break;
    }

    // check if we've changed rows
    if (NULL == currentDumpSequence || curRowIndex.advance(write.rowIndex.subsong, write.rowIndex.ord, write.rowIndex.row)) {
      // new sequence
      String key = getSequenceKey(curRowIndex.subsong, curRowIndex.ord, curRowIndex.row, channel);
      sequence.emplace_back(key);
      auto nextIt = registerDumps.emplace(key, ChannelStateSequence());
      ChannelStateSequence *nextDumpSequence = &(nextIt.first->second);
      currentDumpSequence = nextDumpSequence;
    }

    // process write
    auto it = addressMap.find(write.addr);
    if (it == addressMap.end()) {
      continue;
    }
    currentState.write(it->second, write.val);
  }
}

void findCommonSequences(
  const std::map<String, ChannelStateSequence> &registerDumps,
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
    representativeMap.emplace(x.first, it.first->second);
  }
}




