#!/bin/bash
# exports all files in test/export/ for testing.
# requires GNU parallel.

timestamp=$(date +%Y%m%d%H%M%S)

FURNACE_ROOT=".."
testDir="$FURNACE_ROOT/test"
templateDir="$FURNACE_ROOT/src/asm/6502/a2600"
declare -a results=()

while read -r line || [ -n "$line" ]; do
    testargs=($line)
    testname=${testargs[0]}
    sourceFile=${testargs[1]}
    romConf=${testargs[@]:2} 
    filename="$testDir/export/$sourceFile"
    targetDir="$testDir/output/$timestamp/$testname"
    echo "processing $sourceFile -> $targetDir"
    if [[ ! -e "$filename" ]]; then 
      results+=("FAILED: $testname: $filename not found"); 
      continue;
    fi
    mkdir -p $targetDir
    cp -r $templateDir/* $targetDir
    $FURNACE_ROOT/build/Debug/furnace --romconf debug=true $romConf --romout $targetDir $filename > $targetDir/furnace_export.log
    (cd $targetDir && make)
    romFile=$targetDir/roms/MiniPlayer_NTSC.a26
    if [[ ! -e "$romFile" ]]; then 
      results+=("FAILED: $testname: $filename did not compile"); 
      continue;
    fi
    stella -loglevel 2 -logtoconsole 1 -userdir . -debug $targetDir/roms/MiniPlayer_NTSC.a26 > $targetDir/stella.log.out
    python $testDir/diff_stella_log.py $targetDir > $targetDir/test.out
    if [ $? -ne 0 ]; then 
      results+=("FAILED: $testname: $filename did not pass stella test");
      continue;
    fi
    results+=("SUCCESS: $testname: $filename");
done < $testDir/tests.conf

for result in "${results[@]}"; do
  echo "$result";
done



# Coconut_Mall.conf		TIA_Spanish_Flea_Transposed.fur	hmove_or_die.fur		test03_08.fur
# Coconut_Mall.fur		TIA_Spanish_Flea_short.fur	leomelodi.fur			tia_BoogieWoogie.fur
# Coconut_Small.conf		TIA_Spanish_Flea_short_flat.fur	simple_repeat.fur		tia_EasterMix.fur
# Coconut_Small.fur		Tone_12.fur			steps.fur			tia_EasterMix_SM.fur
# Spanish_Flea_updated.fur	atari_breakbeat_TIA.conf	steps_short.conf		tia_entertainer.fur
# TIA_Percussion_Demo_01b.fur	atari_breakbeat_TIA.fur		steps_short.fur			tiacomp
# TIA_Spanish_Flea.fur		basic				steps_short_2.fur		zzb.fur
# TIA_Spanish_Flea_Full.fur	basicx				test02_03.fur
