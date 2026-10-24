#!/bin/bash
# usage: resumeflow.sh <design_dir> <tag> <from_step>
source /home/claude/tools/env.sh
export RESUME_DESIGN=$1 RESUME_TAG=$2 RESUME_FROM=$3
cd /home/claude/src/openlane1
nohup ./flow.tcl -interactive -file /home/claude/tools/resume.tcl > $1/$2.resume.log 2>&1 &
echo $! > $1/$2.pid
