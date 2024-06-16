import re
import sys

AUDC0 = 21 # 0x15
AUDC1 = 22 # 0x16
AUDF0 = 23 # 0x17
AUDF1 = 24 # 0x18
AUDV0 = 25 # 0x19
AUDV1 = 26 # 0x1a


trigger_address_map = {
    'WTrap[00]': AUDC0,
    'WTrap[01]': AUDC1,
    'WTrap[02]': AUDF0,
    'WTrap[03]': AUDF1,
    'WTrap[04]': AUDV0,
    'WTrap[05]': AUDV1,
}

register_masks = [
    0x0f,
    0x0f,
    0x1f,
    0x1f,
    0x0f,
    0x0f
]

# Parse Stella log
# Trigger:  Frame Scn Cy Pxl | PS       A  X  Y  SP | Addr Code     Disam
# WTrap[05]:   69  32 29  19 | nV-BdIzc 00 5a 00 59 | f008 d0 fb    bne    Lf005
re_stellalog = re.compile(r'^(?P<trigger>WTrap.*): +(?P<frame>\d+) +(?P<scanline>\d+) +(?P<cycle>\d+) +(\S+) +[|] (\S+) (?P<accumulator>[0-9a-f]+) .*$')
def parse_stella_log(fp):
    for line in fp:
        m = re_stellalog.match(line)
        if not m:
          # if line.startswith("WTrap"):
          #     raise Exception('failed to parse line ' + line)
          continue
        yield (
            int(m.group('frame')),
            int(m.group('scanline')),
            int(m.group('cycle')),
            trigger_address_map[m.group('trigger')],
            int(m.group('accumulator'), 16)
        )

# Parse RegisterWrites data
# ; 562 T9.382958 F563.0: SS0 ORD4 ROW23 SYS0> 23 = 31
re_regwrites = re.compile(r'^; (?P<write_index>\d+) T(?P<ticks>[0-9.]+) F(?P<frame_partial>[0-9.]+): (?P<rowid>SS\d+ ORD\d+ ROW\d+ SYS\d+)> (?P<address>\d+) = (?P<value>\d+)$')
def parse_regwrite(fp):
    for line in fp:
        m = re_regwrites.match(line)
        if not m:
            # if "ORD" in line:
            #     raise Exception('failed to parse line ' + line)
            continue
        yield (
            int(m.group('write_index')),
            float(m.group('ticks')),
            float(m.group('frame_partial')),
            m.group('rowid'),
            int(m.group('address')),
            int(m.group('value'))
        )

def extend_frames(frames, index):
    while len(frames) <= index:
        frames.append(list(frames[-1]))

def compare_registers(a, b):
    for channel in [0, 1]:
        for register in [4, 2, 0]:
          i = register + channel
          va = a[i]
          vb = b[i]
          if va != vb:
              return False
              continue
          elif register == 4 and va == 0:
              # short circuit volume
              break
    return True

if __name__ == '__main__':
    stella_writes = [[0, 0, 0, 0, 0, 0]]
    first_stella_write = -1
    testDir = sys.argv[1]
    with open(f'{testDir}/stella.log.out') as fp:
        for frame, scanline, cycle, address, value in parse_stella_log(fp):
            if first_stella_write < 0 and value > 0:
                first_stella_write = frame
            extend_frames(stella_writes, frame)
            ri = address - AUDC0
            stella_writes[frame][ri] = value & register_masks[ri]
    print(f"read {len(stella_writes)} frames")
    stella_writes = stella_writes[first_stella_write:]
    expected_writes = [[0, 0, 0, 0, 0, 0]]
    rows = ['']
    first_expected_write = -1
    with open(f'{testDir}/RegisterDump.txt') as fp:
        for write_index, ticks, frame_partial, rowid, address, value in parse_regwrite(fp):
            frame = int(frame_partial)
            if first_expected_write < 0 and value > 0:
                first_expected_write = frame
            extend_frames(expected_writes, frame)
            if len(rows) < len(expected_writes):
                while len(rows) < (len(expected_writes) - 1):
                    rows.append(rows[-1])
                rows.append(rowid)
            ri = address - AUDC0
            expected_writes[frame][ri] = value & register_masks[ri]
    print(f"read {len(expected_writes)} frames")
    expected_writes = expected_writes[first_expected_write:]
    rows = rows[first_expected_write:]
    same = True
    for index, (a, b, rowid) in enumerate(zip(stella_writes, expected_writes, rows)):
        if compare_registers(a, b):
            label = 'good'
        else:
            same = False
            label = '----'
        print(label, index, a, b, rowid)
    if not same:
        exit(-1)
    

