import re
import sys

AUDC0 = 21 # 0x15
AUDC1 = 22 # 0x16
AUDF0 = 23 # 0x17
AUDF1 = 24 # 0x18
AUDV0 = 25 # 0x19
AUDV1 = 26 # 0x1a

DATA_0_LO = 131
DATA_0_HI = 132
DATA_1_LO = 133
DATA_1_HI = 134
SPAN_0_LO = 135
SPAN_0_HI = 136
SPAN_1_LO = 137
SPAN_1_HI = 138


trigger_address_map = {
    'WTrap[00]': AUDC0,
    'WTrap[01]': AUDC1,
    'WTrap[02]': AUDF0,
    'WTrap[03]': AUDF1,
    'WTrap[04]': AUDV0,
    'WTrap[05]': AUDV1,
    'WTrap[06]': DATA_0_LO,
    'WTrap[07]': DATA_0_HI,
    'WTrap[08]': DATA_1_LO,
    'WTrap[09]': DATA_1_HI,
    'WTrap[0a]': SPAN_0_LO,
    'WTrap[0b]': SPAN_0_HI,
    'WTrap[0c]': SPAN_1_LO,
    'WTrap[0d]': SPAN_1_HI,
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
        addr = trigger_address_map[m.group('trigger')]
        if addr >= 128:
            continue
        yield (
            int(m.group('frame')),
            int(m.group('scanline')),
            int(m.group('cycle')),
            addr,
            int(m.group('accumulator'), 16)
        )

# Parse RegisterWrites data
# ; 562 T9.382958 F563.0: SS0 ORD4 ROW23 SYS0> 23 = 31
re_song = re.compile(r'^; Song (\d+)$')
re_regwrites = re.compile(r'^; (?P<write_index>\d+) T(?P<ticks>[0-9.]+) H(?P<hz>[0-9.]+) F(?P<frame_partial>[0-9.]+): (?P<rowid>SS\d+ ORD\d+ ROW\d+ SYS\d+)> (?P<address>\d+) = (?P<value>\d+)$')
def parse_regwrite(fp):
    # skip to start of song 0
    for line in fp:
        if re_song.match(line):
            break
    for line in fp:
        if re_song.match(line):
            break
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


def sequence_edit_distance(actual, expected):
    # Trim shared prefix and suffix first to keep the dynamic program small.
    start = 0
    while start < len(actual) and start < len(expected) and compare_registers(actual[start], expected[start]):
        start += 1

    end_actual = len(actual)
    end_expected = len(expected)
    while end_actual > start and end_expected > start and compare_registers(actual[end_actual - 1], expected[end_expected - 1]):
        end_actual -= 1
        end_expected -= 1

    actual = actual[start:end_actual]
    expected = expected[start:end_expected]

    if not actual:
        return len(expected)
    if not expected:
        return len(actual)

    previous = list(range(len(expected) + 1))
    for i, actual_frame in enumerate(actual, start=1):
        current = [i]
        for j, expected_frame in enumerate(expected, start=1):
            substitution_cost = 0 if compare_registers(actual_frame, expected_frame) else 1
            current.append(min(
                previous[j] + 1,
                current[j - 1] + 1,
                previous[j - 1] + substitution_cost
            ))
        previous = current
    return previous[-1]

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
    if first_stella_write < 0:
        first_stella_write = 0
    print(f"read {len(stella_writes)} frames, first write at {first_stella_write}")
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
    if first_expected_write < 0:
        first_expected_write = 0
    print(f"read {len(expected_writes)} frames, first write at {first_expected_write}")
    expected_writes = expected_writes[first_expected_write:]
    rows = rows[first_expected_write:]
    same = True
    frame_count = max(len(stella_writes), len(expected_writes))
    for index in range(frame_count):
        a = stella_writes[index] if index < len(stella_writes) else None
        b = expected_writes[index] if index < len(expected_writes) else None
        rowid = rows[index] if index < len(rows) else '<missing row>'
        if a is not None and b is not None and compare_registers(a, b):
            label = 'good'
        else:
            same = False
            label = '----'
        print(label, index, a, b, rowid)
    edits = sequence_edit_distance(stella_writes, expected_writes)
    normalizer = max(len(stella_writes), len(expected_writes))
    normalized_distance = (edits / normalizer) if normalizer else 0.0
    print(f"sequence edits={edits} normalized_edit_distance={normalized_distance:.6f}")
    if not same:
        exit(-1)
    

