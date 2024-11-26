#!/bin/bash

# ./config.sh configFiles/default.json
# make
# ./config.sh configFiles/entangling.json
# make
# ./config.sh configFiles/next_line.json
# make
# ./config.sh configFiles/entangling_tlb.json
# make
# ./config.sh configFiles/asp_tlb.json
# make
# ./config.sh configFiles/next_line_tlb.json
# make
# ./config.sh configFiles/entangling_asp_tlb.json
# make
# ./config.sh configFiles/entangling_entangling_tlb.json
# make
# ./config.sh configFiles/entangling_next_line_tlb.json
# make


for file in {1..5} 
do
    echo $file  
    bin/entangling --warmup_instructions 25000000 --simulation_instructions 25000000 ipc1_public/client_00$file.champsimtrace.xz > logs/client_00$file\_entangling.txt 2> csvs/client_00$file\_entangling.csv
    bin/physical_entangling --warmup_instructions 25000000 --simulation_instructions 25000000 ipc1_public/client_00$file.champsimtrace.xz > logs/client_00$file\_physical_entangling.txt 2> csvs/client_00$file\_physical_entangling.csv
    bin/entangling_entangling --warmup_instructions 25000000 --simulation_instructions 25000000 ipc1_public/client_00$file.champsimtrace.xz > logs/client_00$file\_entangling_entangling.txt 2> csvs/client_00$file\_entangling_entangling.csv
    bin/physical_entangling_entangling --warmup_instructions 25000000 --simulation_instructions 25000000 ipc1_public/client_00$file.champsimtrace.xz > logs/client_00$file\_physical_entangling_entangling.txt 2> csvs/client_00$file\_physical_entangling_entangling.csv
done 


