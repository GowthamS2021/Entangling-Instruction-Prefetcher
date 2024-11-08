#!/bin/bash

for file in {1..5} 
do
    echo $file  
    # bin/no_instr --warmup_instructions 25000000 --simulation_instructions 25000000 ipc1_public/server_00$file.champsimtrace.xz >  logs/server_00$file\_no_instr.txt
    # bin/no_instr --warmup_instructions 25000000 --simulation_instructions 25000000 ipc1_public/client_00$file.champsimtrace.xz > logs/client_00$file\_no_instr.txt
    # bin/next_line --warmup_instructions 25000000 --simulation_instructions 25000000 ipc1_public/server_00$file.champsimtrace.xz > logs/server_00$file\_next_line.txt
    # bin/next_line --warmup_instructions 25000000 --simulation_instructions 25000000 ipc1_public/client_00$file.champsimtrace.xz > logs/client_00$file\_next_line.txt
    bin/entangling --warmup_instructions 25000000 --simulation_instructions 25000000 ipc1_public/server_00$file.champsimtrace.xz > logs/server_00$file\_entangling.txt
    bin/entangling --warmup_instructions 25000000 --simulation_instructions 25000000 ipc1_public/client_00$file.champsimtrace.xz > logs/client_00$file\_entangling.txt
done 

    # bin/next_line_tlb --warmup_instructions 25000000 --simulation_instructions 25000000 ipc1_public/server-001.champsimtrace.xz > logs/server-001_next_line_tlb.txt
