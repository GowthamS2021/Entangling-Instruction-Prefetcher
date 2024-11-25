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
    bin/no_instr --warmup_instructions 25000000 --simulation_instructions 25000000 ipc1_public/client_00$file.champsimtrace.xz >  logs/client_00$file\_no_instr.txt
    bin/next_line --warmup_instructions 25000000 --simulation_instructions 25000000 ipc1_public/client_00$file.champsimtrace.xz > logs/client_00$file\_next_line.txt
    bin/entangling --warmup_instructions 25000000 --simulation_instructions 25000000 ipc1_public/client_00$file.champsimtrace.xz > logs/client_00$file\_entangling.txt
    bin/physical_entangling --warmup_instructions 25000000 --simulation_instructions 25000000 ipc1_public/client_00$file.champsimtrace.xz > logs/client_00$file\_physical_entangling.txt
    bin/improved_entangling --warmup_instructions 25000000 --simulation_instructions 25000000 ipc1_public/client_00$file.champsimtrace.xz > logs/client_00$file\_improved_entangling.txt
    bin/entangling_tlb --warmup_instructions 25000000 --simulation_instructions 25000000 ipc1_public/client_00$file.champsimtrace.xz > logs/client_00$file\_entangling_tlb.txt
    bin/asp_tlb --warmup_instructions 25000000 --simulation_instructions 25000000 ipc1_public/client_00$file.champsimtrace.xz > logs/client_00$file\_asp.txt
    bin/next_line_tlb --warmup_instructions 25000000 --simulation_instructions 25000000 ipc1_public/client_00$file.champsimtrace.xz > logs/client_00$file\_next_line_tlb.txt
    bin/entangling_asp --warmup_instructions 25000000 --simulation_instructions 25000000 ipc1_public/client_00$file.champsimtrace.xz > logs/client_00$file\_entangling_asp.txt
    bin/entangling_next_line --warmup_instructions 25000000 --simulation_instructions 25000000 ipc1_public/client_00$file.champsimtrace.xz > logs/client_00$file\_entangling_next_line.txt
    bin/entangling_entangling --warmup_instructions 25000000 --simulation_instructions 25000000 ipc1_public/client_00$file.champsimtrace.xz > logs/client_00$file\_entangling_entangling.txt
    bin/physical_entangling_asp --warmup_instructions 25000000 --simulation_instructions 25000000 ipc1_public/client_00$file.champsimtrace.xz > logs/client_00$file\_physical_entangling_asp.txt
    bin/physical_entangling_next_line --warmup_instructions 25000000 --simulation_instructions 25000000 ipc1_public/client_00$file.champsimtrace.xz > logs/client_00$file\_physical_entangling_next_line.txt
    bin/physical_entangling_entangling --warmup_instructions 25000000 --simulation_instructions 25000000 ipc1_public/client_00$file.champsimtrace.xz > logs/client_00$file\_physical_entangling_entangling.txt
done 


