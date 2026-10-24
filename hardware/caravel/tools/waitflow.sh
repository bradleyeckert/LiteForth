#!/bin/bash
# usage: waitflow.sh <design_dir> <tag> <max_seconds>
pid=$(cat $1/$2.pid); t=0
while kill -0 $pid 2>/dev/null && [ $t -lt $3 ]; do sleep 20; t=$((t+20)); done
kill -0 $pid 2>/dev/null && echo "STILL RUNNING after $t s" || echo "FINISHED after $t s"
grep -E "INFO: Running|ERROR|Flow complete" $1/$2.log | sed 's/\x1b\[[0-9;]*m//g' | tail -${4:-6}
