#!/bin/bash
# usage: runflow.sh <design_dir> <tag> ; runs OpenLane1 in background, logs to <design_dir>/<tag>.log
source /home/claude/tools/env.sh
cd /home/claude/src/openlane1
nohup ./flow.tcl -design $1 -tag $2 -overwrite -ignore_mismatches > $1/$2.log 2>&1 &
echo $! > $1/$2.pid
