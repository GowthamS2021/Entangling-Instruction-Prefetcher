#include <iostream>

#include "cache.h"

#define IP_TRACKER_COUNT 1024        
#define PREFETCH_DEGREE 5 

enum states {INTITAL, STEADY, TRANSIENT};

class IP_TRACKER {
  public:
    uint64_t ip;
    uint64_t addr;
    int64_t stride; 
    enum states state;
    
    IP_TRACKER(){
        ip = 0;
        addr = 0;
        stride = 0;
        state = INTITAL;
    }
};

IP_TRACKER trackers[IP_TRACKER_COUNT];

void CACHE::prefetcher_initialize() { std::cout << NAME << " arbitrary stride prefetcher" << std::endl; }

uint32_t CACHE::prefetcher_cache_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type, uint32_t metadata_in)
{
    if (!warmup_complete) return metadata_in;
    if (cache_hit) return metadata_in;
    uint64_t pg_addr = addr >> LOG2_PAGE_SIZE;
    uint64_t index = (ip)%IP_TRACKER_COUNT;
    if (trackers[index].ip != ip){
        trackers[index].ip = ip;
        trackers[index].stride = 0;
        trackers[index].addr = pg_addr;
        trackers[index].state = INTITAL;
    }
    else{
        int64_t new_stride;
        if (pg_addr > trackers[index].addr){
            new_stride = pg_addr - trackers[index].addr;
        }
        else {
            new_stride = trackers[index].addr - pg_addr;
            new_stride *= -1;
        }
        // cerr << "IP: " << ip << " INDEX:" << index << " PG_ADDR:" <<  pg_addr << " ADDR:" << trackers[index].addr << " STRIDE:" << new_stride << " PREV_STRIDE:" << trackers[index].stride << " PREV_STATE:" << trackers[index].state << endl; 
        if (new_stride == 0) return metadata_in;
        if (trackers[index].state == INTITAL){
            if (new_stride == trackers[index].stride){
                trackers[index].state = STEADY;
            }
            else {
                trackers[index].state = TRANSIENT;
                trackers[index].stride = new_stride;
            }
        }
        else if (trackers[index].state == STEADY){
            if (new_stride == trackers[index].stride){
                if (trackers[index].stride != 0){
                    for(int i=0;i<PREFETCH_DEGREE;i++){
                        prefetch_line((pg_addr + (i+1)*trackers[index].stride) << LOG2_PAGE_SIZE, true, metadata_in);
                    }
                }
            }
            else {
                trackers[index].state = INTITAL;
            }
        }
        else if (trackers[index].state == TRANSIENT){
            if (new_stride == trackers[index].stride){
                trackers[index].state = STEADY;
            }
            else {
                trackers[index].state = INTITAL;
                trackers[index].stride = new_stride;
            }
        }
        // cerr << "STATE:" << trackers[index].state << endl;
        trackers[index].addr = pg_addr;

    }
    return metadata_in;
}

uint32_t CACHE::prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in)
{
  return metadata_in;
}

void CACHE::prefetcher_cycle_operate() {}

void CACHE::prefetcher_final_stats() {}
