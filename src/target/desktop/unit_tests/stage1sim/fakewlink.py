#!/usr/bin/env python3
"""Stands in for wlink in test_hil.py: `fakewlink.py DIR reset` asks the
rig there to reset the simulated board, and waits until it has."""
import os
import sys
import time

d = sys.argv[1]
req, done = os.path.join(d, 'reset.req'), os.path.join(d, 'reset.done')
if os.path.exists(done):
    os.remove(done)
open(req, 'w').close()
end = time.time() + 10
while not os.path.exists(done):
    if time.time() > end:
        sys.exit('fakewlink: the rig did not reset the board')
    time.sleep(0.02)
os.remove(done)
print('reset')
