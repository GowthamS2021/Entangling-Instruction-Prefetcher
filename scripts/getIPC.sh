#!/bin/bash

echo "trace,method,IPC,Avg Miss Latency"
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
    filename=$(echo $file | cut -d '/' -f2)
    trace=$(echo $filename | cut -d '_' -f1-2)
    method=$(echo $filename | cut -d '_' -f3- | cut -d '.' -f1)
    echo $trace,$method,$IPC,$AML 
done 