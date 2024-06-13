import re
import sys

traps = {
    'WTrap[00]:': 'AUDC0',
    'WTrap[01]:': 'AUDC1',
    'WTrap[02]:': 'AUDF0',
    'WTrap[03]:': 'AUDF1',
    'WTrap[04]:': 'AUDV0',
    'WTrap[05]:': 'AUDV1',
}
    
for line in sys.stdin:
    if not line.startswith('WTrap'):
        continue
    
    row = re.split(r'\s+', line)
    trigger, frame, scan, cycle, a = row[0], row[1], row[2], row[3], row[7]
    addr = traps[trigger]
    print(frame, scan, cycle, addr, a)


