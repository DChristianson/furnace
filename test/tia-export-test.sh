#!/bin/bash
# exports all files in test/export/ for testing.
# requires GNU parallel.

timestamp=$(date +%Y%m%d%H%M%S)

FURNACE_ROOT=".."
testDir="$FURNACE_ROOT/test"
templateDir="$FURNACE_ROOT/src/asm/6502/atari2600"

for filename in $testDir/export/*.fur; do
    if [[ ! -e "$filename" ]]; then continue; fi
    sourceFile=$(basename $filename)
    target=${sourceFile%.fur}
    configFile="$testDir/export/$target.conf"
    targetDir="$testDir/output/$timestamp/$target"
    echo "processing $sourceFile -> $targetDir"
    configOverride="romout.tiaExportType=COMPACT"
    mkdir -p $targetDir
    cp -r $templateDir/* $targetDir
    if [ -e "$configFile" ]; then configOverride=`paste -sd "," $configFile`; fi
    $FURNACE_ROOT/build/Debug/furnace --conf "romout.debugOutput=true,$configOverride" --romout $targetDir $filename > $targetDir/furnace_export.log
    (cd $targetDir && make)
    romFile=$targetDir/roms/MiniPlayer_NTSC.a26
    if [[ ! -e "$romFile" ]]; then echo "FAIL: $filename did not compile"; continue; fi
    stella -loglevel 2 -logtoconsole 1 -userdir . -debug $targetDir/roms/MiniPlayer_NTSC.a26 > $targetDir/stella.log.out
    python $testDir/diff_stella_log.py $targetDir > $targetDir/test.out
    if [ $? -ne 0 ]; then echo "FAIL: $filename did not pass stella test"; continue; fi
done  

