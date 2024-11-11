#!/bin/bash

echo "filename,IPC,Accuracy,Avg Miss Latency,LOAD MPKI"
for file in logs/*.txt
do
    IPC=$(cat $file | grep -F -e "IPC" | tail -1 | cut -d ' ' -f 5)
    AML=$(cat $file | grep -F -e "L1I AVERAGE MISS LATENCY" | cut -d ' ' -f 5)
    Miss=$(cat $file | grep -F -e "L1I LOAD" | sed 's/ \+ / /g' | cut  -d ' ' -f 8)
    Instr=$(cat $file | grep -F -e "instructions" | tail -1 | cut -d ' ' -f 7)
    MPKI=$(bc -l <<< "scale=3;(($Miss*1000/$Instr))")
    Useful=$(cat $file | grep -F -e "L1I PREFETCH REQUESTED" | sed 's/ \+ / /g' | cut -d ' ' -f 8)
    Useless=$(cat $file | grep -F -e "L1I PREFETCH REQUESTED" | sed 's/ \+ / /g' | cut -d ' ' -f 10)
    if [[ $Useful -eq 0 ]] && [[ $Useless -eq 0 ]]
    then
        acc='NA'
    else 
        acc=$(bc -l <<< "scale=3;($Useful*100/($Useful+$Useless))")
    fi
    echo $file,$IPC,$acc,$AML,$MPKI 
done 