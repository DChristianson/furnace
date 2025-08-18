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

RegisterDump::RegisterDump(
  DivEngine* e,
  size_t subsong
) : subsong(subsong) {
  for (int i=0; i<e->song.systemLen; i++) {
    e->getDispatch(i)->toggleRegisterDump(true);
  }
  e->changeSongP(subsong);
  e->stop();
  e->setRepeatPattern(false);
  e->setOrder(0);
  e->play();
  long nextTickCount = -1;
  RowIndex curRowIndex(subsong, 0, 0);
  bool done=false;
  std::map<int, int> currentRegisterValue;
  while (!done && e->isPlaying()) {
    
    done = e->nextTick(false, true);
    nextTickCount += 1;
    if (done) break;

    if (curRowIndex.advance(e->getCurrentSubSong(), e->getOrder(), e->getRow())) {
      // write new row marker
      writes.emplace_back(
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
    }

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
            e->getCurHz(),
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
void RegisterDump::writeChannelStateSequence(
  int systemIndex,
  int suppressVolumeRegister,
  const std::map<unsigned int, unsigned int> &addressMap,
  ChannelStateSequence &dumpSequence 
) {

  RowIndex curRowIndex(subsong, 0, 0);
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
        if (suppressVolumeRegister >= 0) {
          if (lastState.registers[suppressVolumeRegister] == 0) {
            lastState.clear();
          }
        }
        dumpSequence.updateState(lastState, curRowIndex);
        deltaTicksR = dumpSequence.addDuration(deltaTicks, deltaTicksR, freq, curRowIndex);
        deltaTicks = 0;
      }
      lastWriteIndex = currentWriteIndex;
      lastWriteTicks = currentTicks;
      lastWriteSeconds = currentSeconds;
    }
    
    curRowIndex.advance(write.rowIndex.subsong, write.rowIndex.ord, write.rowIndex.row);
    
    // skip markers
    if (write.systemIndex < 0) {
      continue;
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
void RegisterDump::writeChannelStateSequenceByRow(
  int channel,
  int systemIndex,
  int suppressVolumeRegister,
  const std::map<unsigned int, unsigned int> &addressMap,
  std::map<String, ChannelStateSequence> &dumpSequenceMap 
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
        if (suppressVolumeRegister >= 0) {
          if (lastState.registers[suppressVolumeRegister] == 0) {
            lastState.clear();
          }
        }
        currentDumpSequence->updateState(lastState, curRowIndex);
        deltaTicksR = currentDumpSequence->addDuration(deltaTicks, deltaTicksR, freq, curRowIndex);
        deltaTicks = 0;
      }
      lastWriteIndex = currentWriteIndex;
      lastWriteTicks = currentTicks;
      lastWriteSeconds = currentSeconds;
    }

    bool atNewRow = curRowIndex.advance(write.rowIndex.subsong, write.rowIndex.ord, write.rowIndex.row);

    // check if we've changed rows
    if (NULL == currentDumpSequence || atNewRow) {
      // new sequence
      String key = getSequenceKey(curRowIndex.subsong, curRowIndex.ord, curRowIndex.row, channel);
      auto nextIt = dumpSequenceMap.emplace(key, ChannelStateSequence());
      ChannelStateSequence *nextDumpSequence = &(nextIt.first->second);
      currentDumpSequence = nextDumpSequence;
    }

    // skip markers
    if (write.systemIndex < 0) {
      continue;
    }

    // process write
    auto it = addressMap.find(write.addr);
    if (it == addressMap.end()) {
      continue;
    }
    currentState.write(it->second, write.val);
  }
}

void RegisterDump::writeText(SafeWriter* w) {

  int maxFrames = 0;

  for (auto &write : writes) {
    int currentTicks = write.ticks;
    int currentSeconds = write.seconds;
    int freq = ((float)TICKS_PER_SECOND) / write.hz;

    int totalTicks = currentTicks  + 
      (TICKS_PER_SECOND * currentSeconds);
    int totalFrames = totalTicks / freq;
    int totalFramesR = totalTicks - (totalFrames * freq);
    if (totalFrames > maxFrames) {
      maxFrames = totalFrames;
    }

    w->writeText(fmt::sprintf("; %d T%d.%d H%f F%d.%d: SS%d ORD%d ROW%d SYS%d> %d = %d\n",
      write.writeIndex,
      write.seconds,
      write.ticks,
      write.hz,
      totalFrames,
      totalFramesR,
      write.rowIndex.subsong,
      write.rowIndex.ord,
      write.rowIndex.row,
      write.systemIndex,
      write.addr,
      write.val
    ));  
  }

  w->writeText("\n");
  w->writeText(fmt::sprintf("; Writes: %d\n", writes.size()));
  w->writeText(fmt::sprintf("; Frames: %d\n", maxFrames));
  w->writeText("\n");

}

int getFontIndex(const char c) {
  if ('0' <= c && c <= '9') return c - '0';
  if (c == ' ' || c == 0) return 10;
  if (c == '.') return 12;
  if (c == '<') return 13;
  if (c == '>') return 14;
  if ('a' <= c && c <= 'z') return 15 + c - 'a';
  if ('A' <= c && c <= 'Z') return 15 + c - 'A';
  return 11;
}

// 4x6 font data used to encode title
unsigned char FONT_DATA[41][6] = {
  {0x00, 0x04, 0x0a, 0x0a, 0x0a, 0x04}, // SYMBOL_ZERO
  {0x00, 0x0e, 0x04, 0x04, 0x04, 0x0c}, // SYMBOL_ONE
  {0x00, 0x0e, 0x08, 0x06, 0x02, 0x0c}, // SYMBOL_TWO
  {0x00, 0x0c, 0x02, 0x06, 0x02, 0x0c}, // SYMBOL_THREE
  {0x00, 0x02, 0x02, 0x0e, 0x0a, 0x0a}, // SYMBOL_FOUR
  {0x00, 0x0c, 0x02, 0x0c, 0x08, 0x06}, // SYMBOL_FIVE
  {0x00, 0x06, 0x0a, 0x0c, 0x08, 0x06}, // SYMBOL_SIX
  {0x00, 0x08, 0x08, 0x04, 0x02, 0x0e}, // SYMBOL_SEVEN
  {0x00, 0x06, 0x0a, 0x0e, 0x0a, 0x0c}, // SYMBOL_EIGHT
  {0x00, 0x02, 0x02, 0x0e, 0x0a, 0x0c}, // SYMBOL_NINE
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // SYMBOL_SPACE
  {0x00, 0x0e, 0x00, 0x00, 0x00, 0x00}, // SYMBOL_UNDERSCORE
  {0x00, 0x04, 0x00, 0x00, 0x00, 0x00}, // SYMBOL_DOT
  {0x00, 0x02, 0x04, 0x08, 0x04, 0x02}, // SYMBOL_LT
  {0x00, 0x08, 0x04, 0x02, 0x04, 0x08}, // SYMBOL_GT
  {0x00, 0x0a, 0x0a, 0x0e, 0x0a, 0x0e}, // SYMBOL_A
  {0x00, 0x0e, 0x0a, 0x0c, 0x0a, 0x0e}, // SYMBOL_B
  {0x00, 0x0e, 0x08, 0x08, 0x08, 0x0e}, // SYMBOL_C
  {0x00, 0x0c, 0x0a, 0x0a, 0x0a, 0x0c}, // SYMBOL_D
  {0x00, 0x0e, 0x08, 0x0c, 0x08, 0x0e}, // SYMBOL_E
  {0x00, 0x08, 0x08, 0x0c, 0x08, 0x0e}, // SYMBOL_F
  {0x00, 0x0e, 0x0a, 0x08, 0x08, 0x0e}, // SYMBOL_G
  {0x00, 0x0a, 0x0a, 0x0e, 0x0a, 0x0a}, // SYMBOL_H
  {0x00, 0x04, 0x04, 0x04, 0x04, 0x04}, // SYMBOL_I
  {0x00, 0x0e, 0x0a, 0x02, 0x02, 0x02}, // SYMBOL_J
  {0x00, 0x0a, 0x0a, 0x0c, 0x0a, 0x0a}, // SYMBOL_K
  {0x00, 0x0e, 0x08, 0x08, 0x08, 0x08}, // SYMBOL_L
  {0x00, 0x0a, 0x0a, 0x0e, 0x0e, 0x0e}, // SYMBOL_M
  {0x00, 0x0a, 0x0a, 0x0a, 0x0a, 0x0e}, // SYMBOL_N
  {0x00, 0x0e, 0x0a, 0x0a, 0x0a, 0x0e}, // SYMBOL_O
  {0x00, 0x08, 0x08, 0x0e, 0x0a, 0x0e}, // SYMBOL_P
  {0x00, 0x06, 0x08, 0x0a, 0x0a, 0x0e}, // SYMBOL_Q
  {0x00, 0x0a, 0x0a, 0x0c, 0x0a, 0x0e}, // SYMBOL_R
  {0x00, 0x0e, 0x02, 0x0e, 0x08, 0x0e}, // SYMBOL_S
  {0x00, 0x04, 0x04, 0x04, 0x04, 0x0e}, // SYMBOL_T
  {0x00, 0x0e, 0x0a, 0x0a, 0x0a, 0x0a}, // SYMBOL_U
  {0x00, 0x04, 0x04, 0x0e, 0x0a, 0x0a}, // SYMBOL_V
  {0x00, 0x0e, 0x0e, 0x0e, 0x0a, 0x0a}, // SYMBOL_W
  {0x00, 0x0a, 0x0e, 0x04, 0x0e, 0x0a}, // SYMBOL_X
  {0x00, 0x04, 0x04, 0x0e, 0x0a, 0x0a}, // SYMBOL_Y
  {0x00, 0x0e, 0x08, 0x04, 0x02, 0x0e}  // SYMBOL_Z
};

size_t writeTextGraphics(SafeWriter* w, const char* value) {
  size_t bytesWritten = 0;

  bool end = false;
  size_t len = 0; 
  while (len < 6 || !end) {
    w->writeText(fmt::sprintf("TITLE_GRAPHICS_%d\n    byte ", len));
    len++;
    char ax = 0;
    if (!end) {
      ax = *value++;
      if (0 == ax) {
        end = true;
      }
    } 
    char bx = 0;
    if (!end) {
      bx = *value++;
      if (0 == bx) end = true;
    }
    auto ai = getFontIndex(ax);
    auto bi = getFontIndex(bx);
    for (int i = 0; i < 6; i++) {
      if (i > 0) {
        w->writeText(",");
      }
      const unsigned char c = (FONT_DATA[ai][i] << 4) + FONT_DATA[bi][i];
      w->writeText(fmt::sprintf("%d", c));
      bytesWritten += 1;
    }
    w->writeText("\n");
  }
  w->writeText(fmt::sprintf("TITLE_LENGTH = %d\n", len));
  return bytesWritten;
}

void writeRegisterDumps(SafeWriter* dump, const std::vector<RegisterDump*> &registerDumps) {

  for (size_t subsong = 0; subsong < registerDumps.size(); subsong++) {
    dump->writeText(fmt::sprintf("\n; Song %d\n", subsong));
    registerDumps[subsong]->writeText(dump);
  }

}