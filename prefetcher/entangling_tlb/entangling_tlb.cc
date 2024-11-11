#include <cassert>
#include <iomanip>
#include <iostream>

#include "cache.h"

using namespace std;

#define MAX_NUM_SET 16
#define MAX_NUM_WAY 4

uint8_t all_warmup_complete;

// To access cpu in my functions
uint64_t itlb_current_cycle;
uint32_t itlb_cpu_id;

uint64_t itlb_last_basic_block;
uint32_t itlb_consecutive_count;
uint32_t itlb_basic_block_merge_diff;

bool debug = 0;
bool all_warmed_up = false;

#define ITLB_HIST_TABLE_ENTRIES 16

// LINE AND MERGE BASIC BLOCK SIZE

#define ITLB_MERGE_BBSIZE_BITS 4
#define ITLB_MERGE_BBSIZE_MAX_VALUE ((1 << ITLB_MERGE_BBSIZE_BITS) - 1)

// TIME AND OVERFLOWS

#define ITLB_TIME_DIFF_BITS 20
#define ITLB_TIME_DIFF_OVERFLOW ((uint64_t)1 << ITLB_TIME_DIFF_BITS)
#define ITLB_TIME_DIFF_MASK (ITLB_TIME_DIFF_OVERFLOW - 1)

#define ITLB_TIME_BITS 12
#define ITLB_TIME_OVERFLOW ((uint64_t)1 << ITLB_TIME_BITS)
#define ITLB_TIME_MASK (ITLB_TIME_OVERFLOW - 1)

uint64_t itlb_get_latency(uint64_t cycle, uint64_t cycle_prev)
{
  uint64_t cycle_masked = cycle & ITLB_TIME_MASK;
  uint64_t cycle_prev_masked = cycle_prev & ITLB_TIME_MASK;
  if (cycle_prev_masked > cycle_masked) {
    return (cycle_masked + ITLB_TIME_OVERFLOW) - cycle_prev_masked;
  }
  return cycle_masked - cycle_prev_masked;
}

// ENTANGLED COMPRESSION FORMAT

#define ITLB_ENTANGLED_MAX_FORMATS 7

// STATS
#define ITLB_STATS_TABLE_INDEX_BITS 16
#define ITLB_STATS_TABLE_ENTRIES (1 << ITLB_STATS_TABLE_INDEX_BITS)
#define ITLB_STATS_TABLE_MASK (ITLB_STATS_TABLE_ENTRIES - 1)

typedef struct __itlb_stats_entry {
  uint64_t accesses;
  uint64_t misses;
  uint64_t hits;
  uint64_t late;
  uint64_t wrong; // early
} itlb_stats_entry;

itlb_stats_entry itlb_stats_table[NUM_CPUS][ITLB_STATS_TABLE_ENTRIES];
uint64_t itlb_stats_discarded_prefetches;
uint64_t itlb_stats_evict_entangled_j_table;
uint64_t itlb_stats_evict_entangled_k_table;
uint64_t itlb_stats_max_bb_size;
uint64_t itlb_stats_formats[ITLB_ENTANGLED_MAX_FORMATS];
uint64_t itlb_stats_hist_lookups[ITLB_HIST_TABLE_ENTRIES + 2];
uint64_t itlb_stats_basic_blocks[ITLB_MERGE_BBSIZE_MAX_VALUE + 1];
uint64_t itlb_stats_entangled[ITLB_ENTANGLED_MAX_FORMATS + 1];
uint64_t itlb_stats_basic_blocks_ent[ITLB_MERGE_BBSIZE_MAX_VALUE + 1];

void itlb_init_stats_table()
{
  for (int i = 0; i < ITLB_STATS_TABLE_ENTRIES; i++) {
    itlb_stats_table[itlb_cpu_id][i].accesses = 0;
    itlb_stats_table[itlb_cpu_id][i].misses = 0;
    itlb_stats_table[itlb_cpu_id][i].hits = 0;
    itlb_stats_table[itlb_cpu_id][i].late = 0;
    itlb_stats_table[itlb_cpu_id][i].wrong = 0;
  }
  itlb_stats_discarded_prefetches = 0;
  itlb_stats_evict_entangled_j_table = 0;
  itlb_stats_evict_entangled_k_table = 0;
  itlb_stats_max_bb_size = 0;
  for (int i = 0; i < ITLB_ENTANGLED_MAX_FORMATS; i++) {
    itlb_stats_formats[i] = 0;
  }
  for (int i = 0; i <= ITLB_HIST_TABLE_ENTRIES; i++) {
    itlb_stats_hist_lookups[i] = 0;
  }
  for (int i = 0; i <= ITLB_MERGE_BBSIZE_MAX_VALUE; i++) {
    itlb_stats_basic_blocks[i] = 0;
  }
  for (int i = 0; i <= ITLB_ENTANGLED_MAX_FORMATS; i++) {
    itlb_stats_entangled[i] = 0;
  }
  for (int i = 0; i <= ITLB_MERGE_BBSIZE_MAX_VALUE; i++) {
    itlb_stats_basic_blocks_ent[i] = 0;
  }
}

void itlb_print_stats_table()
{
  cout << "IP accesses: ";
  uint64_t max = 0;
  uint64_t max_addr = 0;
  uint64_t total_accesses = 0;
  for (uint32_t i = 0; i < ITLB_STATS_TABLE_ENTRIES; i++) {
    if (itlb_stats_table[itlb_cpu_id][i].accesses > max) {
      max = itlb_stats_table[itlb_cpu_id][i].accesses;
      max_addr = i;
    }
    total_accesses += itlb_stats_table[itlb_cpu_id][i].accesses;
  }
  cout << hex << max_addr << " " << (max_addr << LOG2_PAGE_SIZE) << dec << " " << max << " / " << total_accesses << endl;
  cout << "IP misses: ";
  max = 0;
  max_addr = 0;
  uint64_t total_misses = 0;
  for (uint32_t i = 0; i < ITLB_STATS_TABLE_ENTRIES; i++) {
    if (itlb_stats_table[itlb_cpu_id][i].misses > max) {
      max = itlb_stats_table[itlb_cpu_id][i].misses;
      max_addr = i;
    }
    total_misses += itlb_stats_table[itlb_cpu_id][i].misses;
  }
  cout << hex << max_addr << " " << (max_addr << LOG2_PAGE_SIZE) << dec << " " << max << " / " << total_misses << endl;
  cout << "IP hits: ";
  max = 0;
  max_addr = 0;
  uint64_t total_hits = 0;
  for (uint32_t i = 0; i < ITLB_STATS_TABLE_ENTRIES; i++) {
    if (itlb_stats_table[itlb_cpu_id][i].hits > max) {
      max = itlb_stats_table[itlb_cpu_id][i].hits;
      max_addr = i;
    }
    total_hits += itlb_stats_table[itlb_cpu_id][i].hits;
  }
  cout << hex << max_addr << " " << (max_addr << LOG2_PAGE_SIZE) << dec << " " << max << " / " << total_hits << endl;
  cout << "IP late: ";
  max = 0;
  max_addr = 0;
  uint64_t total_late = 0;
  for (uint32_t i = 0; i < ITLB_STATS_TABLE_ENTRIES; i++) {
    if (itlb_stats_table[itlb_cpu_id][i].late > max) {
      max = itlb_stats_table[itlb_cpu_id][i].late;
      max_addr = i;
    }
    total_late += itlb_stats_table[itlb_cpu_id][i].late;
  }
  cout << hex << max_addr << " " << (max_addr << LOG2_PAGE_SIZE) << dec << " " << max << " / " << total_late << endl;
  cout << "IP wrong: ";
  max = 0;
  max_addr = 0;
  uint64_t total_wrong = 0;
  for (uint32_t i = 0; i < ITLB_STATS_TABLE_ENTRIES; i++) {
    if (itlb_stats_table[itlb_cpu_id][i].wrong > max) {
      max = itlb_stats_table[itlb_cpu_id][i].wrong;
      max_addr = i;
    }
    total_wrong += itlb_stats_table[itlb_cpu_id][i].wrong;
  }
  cout << hex << max_addr << " " << (max_addr << LOG2_PAGE_SIZE) << dec << " " << max << " / " << total_wrong << endl;

  cout << "miss rate: " << ((double)total_misses / (double)total_accesses) << endl;
  cout << "coverage: " << ((double)total_hits / (double)(total_hits + total_misses)) << endl;
  cout << "coverage_late: " << ((double)(total_hits + total_late) / (double)(total_hits + total_misses)) << endl;
  cout << "accuracy: " << ((double)total_hits / (double)(total_hits + total_late + total_wrong)) << endl;
  cout << "accuracy_late: " << ((double)(total_hits + total_late) / (double)(total_hits + total_late + total_wrong)) << endl;
  cout << "discarded: " << itlb_stats_discarded_prefetches << endl;
  cout << "evicts entangled j table: " << itlb_stats_evict_entangled_j_table << endl;
  cout << "evicts entangled k table: " << itlb_stats_evict_entangled_k_table << endl;
  cout << "max bb size: " << itlb_stats_max_bb_size << endl;
  cout << "formats: ";
  for (uint32_t i = 0; i < ITLB_ENTANGLED_MAX_FORMATS; i++) {
    cout << itlb_stats_formats[i] << " ";
  }
  cout << endl;
  cout << "hist_lookups: ";
  uint64_t total_hist_lookups = 0;
  for (uint32_t i = 0; i <= ITLB_HIST_TABLE_ENTRIES + 1; i++) {
    cout << itlb_stats_hist_lookups[i] << " ";
    total_hist_lookups += itlb_stats_hist_lookups[i];
  }
  cout << endl;
  cout << "hist_lookups_evict: " << (double)itlb_stats_hist_lookups[ITLB_HIST_TABLE_ENTRIES] * 100 / (double)(total_hist_lookups) << " %" << endl;
  cout << "hist_lookups_shortlat: " << (double)itlb_stats_hist_lookups[ITLB_HIST_TABLE_ENTRIES + 1] * 100 / (double)(total_hist_lookups) << " %" << endl;

  cout << "bb_found_hist: ";
  uint64_t total_bb_found = 0;
  uint64_t total_bb_prefetches = 0;
  for (uint32_t i = 0; i <= ITLB_MERGE_BBSIZE_MAX_VALUE; i++) {
    cout << itlb_stats_basic_blocks[i] << " ";
    total_bb_found += i * itlb_stats_basic_blocks[i];
    total_bb_prefetches += itlb_stats_basic_blocks[i];
  }
  cout << endl;
  cout << "bb_found_summary: " << total_bb_found << " " << total_bb_prefetches << " " << (double)total_bb_found / (double)total_bb_prefetches << endl;

  cout << "entangled_found_hist: ";
  uint64_t total_entangled_found = 0;
  uint64_t total_ent_prefetches = 0;
  for (uint32_t i = 0; i <= ITLB_ENTANGLED_MAX_FORMATS; i++) {
    cout << itlb_stats_entangled[i] << " ";
    total_entangled_found += i * itlb_stats_entangled[i];
    total_ent_prefetches += itlb_stats_entangled[i];
  }
  cout << endl;
  cout << "entangled_found_summary: " << total_entangled_found << " " << total_ent_prefetches << " "
       << (double)total_entangled_found / (double)total_ent_prefetches << endl;

  cout << "bb_ent_found_hist: ";
  uint64_t total_bb_ent_found = 0;
  uint64_t total_bb_ent_prefetches = 0;
  for (uint32_t i = 0; i <= ITLB_MERGE_BBSIZE_MAX_VALUE; i++) {
    cout << itlb_stats_basic_blocks_ent[i] << " ";
    total_bb_ent_found += i * itlb_stats_basic_blocks_ent[i];
    total_bb_ent_prefetches += itlb_stats_basic_blocks_ent[i];
  }
  cout << endl;
  cout << "bb_ent_found_summary: " << total_bb_ent_found << " " << total_bb_ent_prefetches << " "
       << (double)total_bb_ent_found / (double)total_bb_ent_prefetches << endl;
}

// HISTORY TABLE (BUFFER)

#define ITLB_HIST_TABLE_MASK (ITLB_HIST_TABLE_ENTRIES - 1)
#define ITLB_BB_MERGE_ENTRIES 5
#define ITLB_HIST_TAG_BITS 58
#define ITLB_HIST_TAG_MASK (((uint64_t)1 << ITLB_HIST_TAG_BITS) - 1)

typedef struct __itlb_hist_entry {
  uint64_t tag;       // ITLB_HIST_TAG_BITS bits
  uint64_t time_diff; // ITLB_TIME_DIFF_BITS bits
  uint32_t bb_size;   // ITLB_MERGE_BBSIZE_BITS bits
} itlb_hist_entry;

itlb_hist_entry itlb_hist_table[NUM_CPUS][ITLB_HIST_TABLE_ENTRIES];
uint64_t itlb_hist_table_head[NUM_CPUS];      // log_2 (ITLB_HIST_TABLE_ENTRIES)
uint64_t itlb_hist_table_head_time[NUM_CPUS]; // 64 bits

void itlb_init_hist_table()
{
  itlb_hist_table_head[itlb_cpu_id] = 0;
  itlb_hist_table_head_time[itlb_cpu_id] = itlb_current_cycle;
  for (uint32_t i = 0; i < ITLB_HIST_TABLE_ENTRIES; i++) {
    itlb_hist_table[itlb_cpu_id][i].tag = 0;
    itlb_hist_table[itlb_cpu_id][i].time_diff = 0;
    itlb_hist_table[itlb_cpu_id][i].bb_size = 0;
  }
}

uint64_t itlb_find_hist_entry(uint64_t page_addr)
{
  uint64_t tag = page_addr & ITLB_HIST_TAG_MASK;
  for (uint32_t count = 0, i = (itlb_hist_table_head[itlb_cpu_id] + ITLB_HIST_TABLE_MASK) % ITLB_HIST_TABLE_ENTRIES; count < ITLB_HIST_TABLE_ENTRIES;
       count++, i = (i + ITLB_HIST_TABLE_MASK) % ITLB_HIST_TABLE_ENTRIES) {
    if (itlb_hist_table[itlb_cpu_id][i].tag == tag)
      return i;
  }
  return ITLB_HIST_TABLE_ENTRIES;
}

// It can have duplicated entries if the line was evicted in between
uint32_t itlb_add_hist_table(uint64_t page_addr)
{
  // Insert empty addresses in hist not to have timediff overflows
  while (itlb_current_cycle - itlb_hist_table_head_time[itlb_cpu_id] >= ITLB_TIME_DIFF_OVERFLOW) {
    itlb_hist_table[itlb_cpu_id][itlb_hist_table_head[itlb_cpu_id]].tag = 0;
    itlb_hist_table[itlb_cpu_id][itlb_hist_table_head[itlb_cpu_id]].time_diff = ITLB_TIME_DIFF_MASK;
    itlb_hist_table[itlb_cpu_id][itlb_hist_table_head[itlb_cpu_id]].bb_size = 0;
    itlb_hist_table_head[itlb_cpu_id] = (itlb_hist_table_head[itlb_cpu_id] + 1) % ITLB_HIST_TABLE_ENTRIES;
    itlb_hist_table_head_time[itlb_cpu_id] += ITLB_TIME_DIFF_MASK;
  }

  // Allocate a new entry (evict old one if necessary)
  itlb_hist_table[itlb_cpu_id][itlb_hist_table_head[itlb_cpu_id]].tag = page_addr & ITLB_HIST_TAG_MASK;
  itlb_hist_table[itlb_cpu_id][itlb_hist_table_head[itlb_cpu_id]].time_diff =
      (itlb_current_cycle - itlb_hist_table_head_time[itlb_cpu_id]) & ITLB_TIME_DIFF_MASK;
  itlb_hist_table[itlb_cpu_id][itlb_hist_table_head[itlb_cpu_id]].bb_size = 0;
  uint32_t pos = itlb_hist_table_head[itlb_cpu_id];
  itlb_hist_table_head[itlb_cpu_id] = (itlb_hist_table_head[itlb_cpu_id] + 1) % ITLB_HIST_TABLE_ENTRIES;
  itlb_hist_table_head_time[itlb_cpu_id] = itlb_current_cycle;
  return pos;
}

void itlb_add_bb_size_hist_table(uint64_t page_addr, uint32_t bb_size)
{
  uint64_t index = itlb_find_hist_entry(page_addr);
  itlb_hist_table[itlb_cpu_id][index].bb_size = bb_size & ITLB_MERGE_BBSIZE_MAX_VALUE;
}

uint32_t itlb_find_bb_merge_hist_table(uint64_t page_addr)
{
  uint64_t tag = page_addr & ITLB_HIST_TAG_MASK;
  for (uint32_t count = 0, i = (itlb_hist_table_head[itlb_cpu_id] + ITLB_HIST_TABLE_MASK) % ITLB_HIST_TABLE_ENTRIES; count < ITLB_HIST_TABLE_ENTRIES;
       count++, i = (i + ITLB_HIST_TABLE_MASK) % ITLB_HIST_TABLE_ENTRIES) {
    if (count >= ITLB_BB_MERGE_ENTRIES) {
      return 0;
    }
    if (tag > itlb_hist_table[itlb_cpu_id][i].tag && (tag - itlb_hist_table[itlb_cpu_id][i].tag) <= itlb_hist_table[itlb_cpu_id][i].bb_size) {
      //&& (tag - itlb_hist_table[itlb_cpu_id][i].tag) == itlb_hist_table[itlb_cpu_id][i].bb_size) {
      return tag - itlb_hist_table[itlb_cpu_id][i].tag;
    }
  }
  assert(false);
}

// return src-entangled pair
uint64_t itlb_get_src_entangled_hist_table(uint64_t page_addr, uint32_t pos_hist, uint64_t latency, uint32_t skip = 0)
{
  assert(pos_hist < ITLB_HIST_TABLE_ENTRIES);
  uint64_t tag = page_addr & ITLB_HIST_TAG_MASK;
  assert(tag);
  if (itlb_hist_table[itlb_cpu_id][pos_hist].tag != tag) {
    itlb_stats_hist_lookups[ITLB_HIST_TABLE_ENTRIES]++;
    return 0; // removed
  }
  uint32_t next_pos = (pos_hist + ITLB_HIST_TABLE_MASK) % ITLB_HIST_TABLE_ENTRIES;
  uint32_t first = (itlb_hist_table_head[itlb_cpu_id] + ITLB_HIST_TABLE_MASK) % ITLB_HIST_TABLE_ENTRIES;
  uint64_t time_i = itlb_hist_table[itlb_cpu_id][pos_hist].time_diff;
  uint32_t num_skipped = 0;
  for (uint32_t count = 0, i = next_pos; i != first; count++, i = (i + ITLB_HIST_TABLE_MASK) % ITLB_HIST_TABLE_ENTRIES) {
    // Against the time overflow
    if (itlb_hist_table[itlb_cpu_id][i].tag == tag) {
      return 0; // Second time it appeared (it was evicted in between) or many for the same set. No entangle
    }
    if (itlb_hist_table[itlb_cpu_id][i].tag && time_i >= latency) {
      if (skip == num_skipped) {
        itlb_stats_hist_lookups[count]++;
        return itlb_hist_table[itlb_cpu_id][i].tag;
      } else {
        num_skipped++;
      }
    }
    time_i += itlb_hist_table[itlb_cpu_id][i].time_diff;
  }
  itlb_stats_hist_lookups[ITLB_HIST_TABLE_ENTRIES + 1]++;
  return 0;
}

// TIMING TABLES

#define ITLB_SET_BITS 4
// #define ITLB_TIMING_MSHR_SIZE (ITLB_PQ_SIZE+ITLB_MSHR_SIZE+64) // Not necessary +64 (perhaps +ITLB_RQ_SIZE), just to track enough in-fligh requests
#define ITLB_TIMING_MSHR_SIZE 64
#define ITLB_TIMING_MSHR_TAG_BITS 42
#define ITLB_TIMING_MSHR_TAG_MASK (((uint64_t)1 << ITLB_HIST_TAG_BITS) - 1)
#define ITLB_TIMING_CACHE_TAG_BITS (ITLB_TIMING_MSHR_TAG_BITS - ITLB_SET_BITS)
#define ITLB_TIMING_CACHE_TAG_MASK (((uint64_t)1 << ITLB_HIST_TAG_BITS) - 1)

#define ITLB_ENTANGLED_TABLE_WAYS 4

// We do not have access to the MSHR, so we aproximate it using this structure
typedef struct __itlb_timing_mshr_entry {
  bool valid;          // 1 bit
  uint64_t tag;        // ITLB_TIMING_MSHR_TAG_BITS bits
  uint32_t source_set; // 8 bits
  uint32_t source_way; // 6 bits
  uint64_t timestamp;  // ITLB_TIME_BITS bits // time when issued
  bool accessed;       // 1 bit
  uint32_t pos_hist;   // 1 bit
} itlb_timing_mshr_entry;

// We do not have access to the cache, so we aproximate it using this structure
typedef struct __itlb_timing_cache_entry {
  bool valid;          // 1 bit
  uint64_t tag;        // ITLB_TIMING_CACHE_TAG_BITS bits
  uint32_t source_set; // 8 bits
  uint32_t source_way; // 6 bits
  bool accessed;       // 1 bit
} itlb_timing_cache_entry;

itlb_timing_mshr_entry itlb_timing_mshr_table[NUM_CPUS][ITLB_TIMING_MSHR_SIZE];
itlb_timing_cache_entry itlb_timing_cache_table[NUM_CPUS][MAX_NUM_SET][MAX_NUM_WAY];

void itlb_init_timing_tables()
{
  for (uint32_t i = 0; i < ITLB_TIMING_MSHR_SIZE; i++) {
    itlb_timing_mshr_table[itlb_cpu_id][i].valid = 0;
  }
  for (uint32_t i = 0; i < MAX_NUM_SET; i++) {
    for (uint32_t j = 0; j < MAX_NUM_WAY; j++) {
      itlb_timing_cache_table[itlb_cpu_id][i][j].valid = 0;
    }
  }
}

uint64_t itlb_find_timing_mshr_entry(uint64_t page_addr)
{
  for (uint32_t i = 0; i < ITLB_TIMING_MSHR_SIZE; i++) {
    if (itlb_timing_mshr_table[itlb_cpu_id][i].tag == (page_addr & ITLB_TIMING_MSHR_TAG_MASK) && itlb_timing_mshr_table[itlb_cpu_id][i].valid)
      return i;
  }
  return ITLB_TIMING_MSHR_SIZE;
}

uint64_t itlb_find_timing_cache_entry(uint64_t page_addr)
{
  uint64_t i = page_addr % MAX_NUM_SET;
  for (uint32_t j = 0; j < MAX_NUM_WAY; j++) {
    if (itlb_timing_cache_table[itlb_cpu_id][i][j].tag == ((page_addr >> ITLB_SET_BITS) & ITLB_TIMING_CACHE_TAG_MASK)
        && itlb_timing_cache_table[itlb_cpu_id][i][j].valid)
      return j;
  }
  return MAX_NUM_WAY;
}

uint32_t itlb_get_invalid_timing_mshr_entry()
{
  for (uint32_t i = 0; i < ITLB_TIMING_MSHR_SIZE; i++) {
    if (!itlb_timing_mshr_table[itlb_cpu_id][i].valid)
      return i;
  }
  // cerr << 1 << endl;
  assert(false); // It must return a free entry
  return ITLB_TIMING_MSHR_SIZE;
}

uint32_t itlb_get_invalid_timing_cache_entry(uint64_t page_addr)
{
  uint32_t i = page_addr % MAX_NUM_SET;
  for (uint32_t j = 0; j < MAX_NUM_WAY; j++) {
    if (!itlb_timing_cache_table[itlb_cpu_id][i][j].valid)
      return j;
  }
  // cerr << 1 << endl;
  assert(false); // It must return a free entry
  return MAX_NUM_WAY;
}

void itlb_add_timing_entry(uint64_t page_addr, uint32_t source_set, uint32_t source_way)
{
  // First find for coalescing
  if (itlb_find_timing_mshr_entry(page_addr) < ITLB_TIMING_MSHR_SIZE)
    return;
  if (itlb_find_timing_cache_entry(page_addr) < MAX_NUM_WAY)
    return;

  uint32_t i = itlb_get_invalid_timing_mshr_entry();
  itlb_timing_mshr_table[itlb_cpu_id][i].valid = true;
  itlb_timing_mshr_table[itlb_cpu_id][i].tag = page_addr & ITLB_TIMING_MSHR_TAG_MASK;
  itlb_timing_mshr_table[itlb_cpu_id][i].source_set = source_set;
  itlb_timing_mshr_table[itlb_cpu_id][i].source_way = source_way;
  itlb_timing_mshr_table[itlb_cpu_id][i].timestamp = itlb_current_cycle & ITLB_TIME_MASK;
  itlb_timing_mshr_table[itlb_cpu_id][i].accessed = false;
}

void itlb_invalid_timing_mshr_entry(uint64_t page_addr)
{
  uint32_t index = itlb_find_timing_mshr_entry(page_addr);
  assert(index < ITLB_TIMING_MSHR_SIZE);
  itlb_timing_mshr_table[itlb_cpu_id][index].valid = false;
}

void itlb_move_timing_entry(uint64_t page_addr)
{
  uint32_t index_mshr = itlb_find_timing_mshr_entry(page_addr);
  if (index_mshr == ITLB_TIMING_MSHR_SIZE) {
    uint32_t set = page_addr % MAX_NUM_SET;
    uint32_t index_cache = itlb_get_invalid_timing_cache_entry(page_addr);
    itlb_timing_cache_table[itlb_cpu_id][set][index_cache].valid = true;
    itlb_timing_cache_table[itlb_cpu_id][set][index_cache].tag = (page_addr >> ITLB_SET_BITS) & ITLB_TIMING_CACHE_TAG_MASK;
    itlb_timing_cache_table[itlb_cpu_id][set][index_cache].source_way = ITLB_ENTANGLED_TABLE_WAYS;
    itlb_timing_cache_table[itlb_cpu_id][set][index_cache].accessed = true;
    return;
  }
  uint64_t set = page_addr % MAX_NUM_SET;
  uint64_t index_cache = itlb_get_invalid_timing_cache_entry(page_addr);
  itlb_timing_cache_table[itlb_cpu_id][set][index_cache].valid = true;
  itlb_timing_cache_table[itlb_cpu_id][set][index_cache].tag = (page_addr >> ITLB_SET_BITS) & ITLB_TIMING_CACHE_TAG_MASK;
  itlb_timing_cache_table[itlb_cpu_id][set][index_cache].source_set = itlb_timing_mshr_table[itlb_cpu_id][index_mshr].source_set;
  itlb_timing_cache_table[itlb_cpu_id][set][index_cache].source_way = itlb_timing_mshr_table[itlb_cpu_id][index_mshr].source_way;
  itlb_timing_cache_table[itlb_cpu_id][set][index_cache].accessed = itlb_timing_mshr_table[itlb_cpu_id][index_mshr].accessed;
  itlb_invalid_timing_mshr_entry(page_addr);
}

// returns if accessed
bool itlb_invalid_timing_cache_entry(uint64_t page_addr, uint32_t& source_set, uint32_t& source_way)
{
  uint32_t set = page_addr % MAX_NUM_SET;
  uint32_t way = itlb_find_timing_cache_entry(page_addr);
  assert(way < MAX_NUM_WAY);
  itlb_timing_cache_table[itlb_cpu_id][set][way].valid = false;
  source_set = itlb_timing_cache_table[itlb_cpu_id][set][way].source_set;
  source_way = itlb_timing_cache_table[itlb_cpu_id][set][way].source_way;
  return itlb_timing_cache_table[itlb_cpu_id][set][way].accessed;
}

void itlb_access_timing_entry(uint64_t page_addr, uint32_t pos_hist, uint32_t& source_set, uint32_t& source_way)
{
  uint32_t index = itlb_find_timing_mshr_entry(page_addr);
  if (index < ITLB_TIMING_MSHR_SIZE) {
    if (!itlb_timing_mshr_table[itlb_cpu_id][index].accessed) { // Prefetch accessed while in MSHR: late
      itlb_timing_mshr_table[itlb_cpu_id][index].accessed = true;
      itlb_timing_mshr_table[itlb_cpu_id][index].pos_hist = pos_hist;
      if (itlb_timing_mshr_table[itlb_cpu_id][index].source_way < ITLB_ENTANGLED_TABLE_WAYS) {
        source_set = itlb_timing_mshr_table[itlb_cpu_id][index].source_set;
        source_way = itlb_timing_mshr_table[itlb_cpu_id][index].source_way;
        itlb_timing_mshr_table[itlb_cpu_id][index].source_set = 0;
        itlb_timing_mshr_table[itlb_cpu_id][index].source_way = ITLB_ENTANGLED_TABLE_WAYS;
      }
    }
    return;
  }
  uint32_t set = page_addr % MAX_NUM_SET;
  uint32_t way = itlb_find_timing_cache_entry(page_addr);
  if (way < MAX_NUM_WAY) {
    itlb_timing_cache_table[itlb_cpu_id][set][way].accessed = true;
  }
}

bool itlb_is_accessed_timing_entry(uint64_t page_addr)
{
  uint32_t index = itlb_find_timing_mshr_entry(page_addr);
  if (index < ITLB_TIMING_MSHR_SIZE) {
    return itlb_timing_mshr_table[itlb_cpu_id][index].accessed;
  }
  uint32_t set = page_addr % MAX_NUM_SET;
  uint32_t way = itlb_find_timing_cache_entry(page_addr);
  if (way < MAX_NUM_WAY) {
    return itlb_timing_cache_table[itlb_cpu_id][set][way].accessed;
  }
  return false;
}

bool itlb_completed_request(uint64_t page_addr) { return itlb_find_timing_cache_entry(page_addr) < MAX_NUM_WAY; }

bool itlb_ongoing_request(uint64_t page_addr) { return itlb_find_timing_mshr_entry(page_addr) < ITLB_TIMING_MSHR_SIZE; }

bool itlb_ongoing_accessed_request(uint64_t page_addr)
{
  uint32_t index = itlb_find_timing_mshr_entry(page_addr);
  if (index == ITLB_TIMING_MSHR_SIZE)
    return false;
  return itlb_timing_mshr_table[itlb_cpu_id][index].accessed;
}

uint64_t itlb_get_latency_timing_mshr(uint64_t page_addr, uint32_t& pos_hist)
{
  uint32_t index = itlb_find_timing_mshr_entry(page_addr);
  if (index == ITLB_TIMING_MSHR_SIZE)
    return 0;
  if (!itlb_timing_mshr_table[itlb_cpu_id][index].accessed)
    return 0;
  pos_hist = itlb_timing_mshr_table[itlb_cpu_id][index].pos_hist;
  return itlb_get_latency(itlb_current_cycle, itlb_timing_mshr_table[itlb_cpu_id][index].timestamp);
}

// ENTANGLED TABLE

uint32_t ITLB_ENTANGLED_FORMATS[ITLB_ENTANGLED_MAX_FORMATS] = {58, 28, 18, 13, 10, 8, 6};
#define ITLB_ENTANGLED_NUM_FORMATS 6

uint32_t itlb_get_format_entangled(uint64_t page_addr, uint64_t entangled_addr)
{
  for (uint32_t i = ITLB_ENTANGLED_NUM_FORMATS; i != 0; i--) {
    if ((page_addr >> ITLB_ENTANGLED_FORMATS[i - 1]) == (entangled_addr >> ITLB_ENTANGLED_FORMATS[i - 1])) {
      return i;
    }
  }
  assert(false);
}

uint64_t itlb_extend_format_entangled(uint64_t page_addr, uint64_t entangled_addr, uint32_t format)
{
  return ((page_addr >> ITLB_ENTANGLED_FORMATS[format - 1]) << ITLB_ENTANGLED_FORMATS[format - 1])
         | (entangled_addr & (((uint64_t)1 << ITLB_ENTANGLED_FORMATS[format - 1]) - 1));
}

uint64_t itlb_compress_format_entangled(uint64_t entangled_addr, uint32_t format)
{
  return entangled_addr & (((uint64_t)1 << ITLB_ENTANGLED_FORMATS[format - 1]) - 1);
}

#define ITLB_ENTANGLED_TABLE_INDEX_BITS 4
#define ITLB_ENTANGLED_TABLE_SETS (1 << ITLB_ENTANGLED_TABLE_INDEX_BITS)
#define ITLB_MAX_ENTANGLED_PER_LINE ITLB_ENTANGLED_NUM_FORMATS
#define ITLB_TAG_BITS (19 - ITLB_ENTANGLED_TABLE_INDEX_BITS)
#define ITLB_TAG_MASK (((uint64_t)1 << ITLB_TAG_BITS) - 1)
#define ITLB_CONFIDENCE_COUNTER_BITS 2
#define ITLB_CONFIDENCE_COUNTER_MAX_VALUE ((1 << ITLB_CONFIDENCE_COUNTER_BITS) - 1)
#define ITLB_CONFIDENCE_COUNTER_THRESHOLD 1
#define ITLB_TRIES_AVAIL_ENTANGLED 2

typedef struct __itlb_entangled_entry {
  uint64_t tag;                                         // ITLB_TAG_BITS bits
  uint32_t format;                                      // log2(ITLB_ENTANGLED_NUM_FORMATS) bits
  uint64_t entangled_addr[ITLB_MAX_ENTANGLED_PER_LINE]; // JUST DIFF
  uint32_t entangled_conf[ITLB_MAX_ENTANGLED_PER_LINE]; // ITLB_CONFIDENCE_COUNTER_BITS bits
  uint32_t bb_size;                                     // ITLB_MERGE_BBSIZE_BITS bits
} itlb_entangled_entry;

itlb_entangled_entry itlb_entangled_table[NUM_CPUS][ITLB_ENTANGLED_TABLE_SETS][ITLB_ENTANGLED_TABLE_WAYS];
uint32_t itlb_entangled_fifo[NUM_CPUS][ITLB_ENTANGLED_TABLE_SETS]; // log2(ITLB_ENTANGLED_TABLE_WAYS) * ITLB_ENTANGLED_TABLE_SETS bits

uint64_t itlb_hash(uint64_t page_addr) { return page_addr ^ (page_addr >> 2) ^ (page_addr >> 5); }

void itlb_init_entangled_table()
{
  for (uint32_t i = 0; i < ITLB_ENTANGLED_TABLE_SETS; i++) {
    for (uint32_t j = 0; j < ITLB_ENTANGLED_TABLE_WAYS; j++) {
      itlb_entangled_table[itlb_cpu_id][i][j].tag = 0;
      itlb_entangled_table[itlb_cpu_id][i][j].format = 1;
      for (uint32_t k = 0; k < ITLB_MAX_ENTANGLED_PER_LINE; k++) {
        itlb_entangled_table[itlb_cpu_id][i][j].entangled_addr[k] = 0;
        itlb_entangled_table[itlb_cpu_id][i][j].entangled_conf[k] = 0;
      }
      itlb_entangled_table[itlb_cpu_id][i][j].bb_size = 0;
    }
    itlb_entangled_fifo[itlb_cpu_id][i] = 0;
  }
}

uint32_t itlb_get_way_entangled_table(uint64_t page_addr)
{
  uint64_t tag = (itlb_hash(page_addr) >> ITLB_ENTANGLED_TABLE_INDEX_BITS) & ITLB_TAG_MASK;
  uint32_t set = itlb_hash(page_addr) % ITLB_ENTANGLED_TABLE_SETS;
  for (uint32_t i = 0; i < ITLB_ENTANGLED_TABLE_WAYS; i++) {
    if (itlb_entangled_table[itlb_cpu_id][set][i].tag == tag) { // Found
      return i;
    }
  }
  return ITLB_ENTANGLED_TABLE_WAYS;
}

void itlb_try_realocate_evicted_in_available_entangled_table(uint32_t set)
{
  uint64_t way = itlb_entangled_fifo[itlb_cpu_id][set];
  bool dest_free_way = true;
  for (uint32_t k = 0; k < ITLB_MAX_ENTANGLED_PER_LINE; k++) {
    if (itlb_entangled_table[itlb_cpu_id][set][way].entangled_conf[k] >= ITLB_CONFIDENCE_COUNTER_THRESHOLD) {
      dest_free_way = false;
      break;
    }
  }
  if (dest_free_way && itlb_entangled_table[itlb_cpu_id][set][way].bb_size == 0)
    return;
  uint32_t free_way = way;
  bool free_with_size = false;
  for (uint32_t i = (way + 1) % ITLB_ENTANGLED_TABLE_WAYS; i != way; i = (i + 1) % ITLB_ENTANGLED_TABLE_WAYS) {
    bool dest_free = true;
    for (uint32_t k = 0; k < ITLB_MAX_ENTANGLED_PER_LINE; k++) {
      if (itlb_entangled_table[itlb_cpu_id][set][i].entangled_conf[k] >= ITLB_CONFIDENCE_COUNTER_THRESHOLD) {
        dest_free = false;
        break;
      }
    }
    if (dest_free) {
      if (free_way == way) {
        free_way = i;
        free_with_size = (itlb_entangled_table[itlb_cpu_id][set][i].bb_size != 0);
      } else if (free_with_size && itlb_entangled_table[itlb_cpu_id][set][i].bb_size == 0) {
        free_way = i;
        free_with_size = false;
        break;
      }
    }
  }
  if (free_way != way && ((!free_with_size) || (free_with_size && !dest_free_way))) { // Only evict if it has more information
    itlb_entangled_table[itlb_cpu_id][set][free_way].tag = itlb_entangled_table[itlb_cpu_id][set][way].tag;
    itlb_entangled_table[itlb_cpu_id][set][free_way].format = itlb_entangled_table[itlb_cpu_id][set][way].format;
    for (uint32_t k = 0; k < ITLB_MAX_ENTANGLED_PER_LINE; k++) {
      itlb_entangled_table[itlb_cpu_id][set][free_way].entangled_addr[k] = itlb_entangled_table[itlb_cpu_id][set][way].entangled_addr[k];
      itlb_entangled_table[itlb_cpu_id][set][free_way].entangled_conf[k] = itlb_entangled_table[itlb_cpu_id][set][way].entangled_conf[k];
    }
    itlb_entangled_table[itlb_cpu_id][set][free_way].bb_size = itlb_entangled_table[itlb_cpu_id][set][way].bb_size;
  }
}

void itlb_add_entangled_table(uint64_t page_addr, uint64_t entangled_addr)
{
  uint64_t tag = (itlb_hash(page_addr) >> ITLB_ENTANGLED_TABLE_INDEX_BITS) & ITLB_TAG_MASK;
  uint32_t set = itlb_hash(page_addr) % ITLB_ENTANGLED_TABLE_SETS;
  uint32_t way = itlb_get_way_entangled_table(page_addr);
  if (way == ITLB_ENTANGLED_TABLE_WAYS) {
    itlb_try_realocate_evicted_in_available_entangled_table(set);
    way = itlb_entangled_fifo[itlb_cpu_id][set];
    itlb_entangled_table[itlb_cpu_id][set][way].tag = tag;
    itlb_entangled_table[itlb_cpu_id][set][way].format = 1;
    for (uint32_t k = 0; k < ITLB_MAX_ENTANGLED_PER_LINE; k++) {
      itlb_entangled_table[itlb_cpu_id][set][way].entangled_addr[k] = 0;
      itlb_entangled_table[itlb_cpu_id][set][way].entangled_conf[k] = 0;
    }
    itlb_entangled_table[itlb_cpu_id][set][way].bb_size = 0;
    itlb_entangled_fifo[itlb_cpu_id][set] = (itlb_entangled_fifo[itlb_cpu_id][set] + 1) % ITLB_ENTANGLED_TABLE_WAYS;
  }
  for (uint32_t k = 0; k < ITLB_MAX_ENTANGLED_PER_LINE; k++) {
    if (itlb_entangled_table[itlb_cpu_id][set][way].entangled_conf[k] >= ITLB_CONFIDENCE_COUNTER_THRESHOLD
        && itlb_extend_format_entangled(page_addr, itlb_entangled_table[itlb_cpu_id][set][way].entangled_addr[k],
                                        itlb_entangled_table[itlb_cpu_id][set][way].format)
               == entangled_addr) {
      itlb_entangled_table[itlb_cpu_id][set][way].entangled_conf[k] = ITLB_CONFIDENCE_COUNTER_MAX_VALUE;
      return;
    }
  }

  // Adding a new entangled
  uint32_t format_new = itlb_get_format_entangled(page_addr, entangled_addr);
  itlb_stats_formats[format_new - 1]++;

  // Check for evictions
  while (true) {
    uint32_t min_format = format_new;
    uint32_t num_valid = 1;
    uint32_t min_value = ITLB_CONFIDENCE_COUNTER_MAX_VALUE + 1;
    uint32_t min_pos = 0;
    for (uint32_t k = 0; k < ITLB_MAX_ENTANGLED_PER_LINE; k++) {
      if (itlb_entangled_table[itlb_cpu_id][set][way].entangled_conf[k] >= ITLB_CONFIDENCE_COUNTER_THRESHOLD) {
        num_valid++;
        uint32_t format_k =
            itlb_get_format_entangled(page_addr, itlb_extend_format_entangled(page_addr, itlb_entangled_table[itlb_cpu_id][set][way].entangled_addr[k],
                                                                              itlb_entangled_table[itlb_cpu_id][set][way].format));
        if (format_k < min_format) {
          min_format = format_k;
        }
        if (itlb_entangled_table[itlb_cpu_id][set][way].entangled_conf[k] < min_value) {
          min_value = itlb_entangled_table[itlb_cpu_id][set][way].entangled_conf[k];
          min_pos = k;
        }
      }
    }
    if (num_valid > min_format) { // Eviction is necessary. We chose the lower confidence one
      itlb_stats_evict_entangled_k_table++;
      itlb_entangled_table[itlb_cpu_id][set][way].entangled_conf[min_pos] = 0;
    } else {
      // Reformat
      for (uint32_t k = 0; k < ITLB_MAX_ENTANGLED_PER_LINE; k++) {
        if (itlb_entangled_table[itlb_cpu_id][set][way].entangled_conf[k] >= ITLB_CONFIDENCE_COUNTER_THRESHOLD) {
          itlb_entangled_table[itlb_cpu_id][set][way].entangled_addr[k] =
              itlb_compress_format_entangled(itlb_extend_format_entangled(page_addr, itlb_entangled_table[itlb_cpu_id][set][way].entangled_addr[k],
                                                                          itlb_entangled_table[itlb_cpu_id][set][way].format),
                                             min_format);
        }
      }
      itlb_entangled_table[itlb_cpu_id][set][way].format = min_format;
      break;
    }
  }
  for (uint32_t k = 0; k < ITLB_MAX_ENTANGLED_PER_LINE; k++) {
    if (itlb_entangled_table[itlb_cpu_id][set][way].entangled_conf[k] < ITLB_CONFIDENCE_COUNTER_THRESHOLD) {
      itlb_entangled_table[itlb_cpu_id][set][way].entangled_addr[k] =
          itlb_compress_format_entangled(entangled_addr, itlb_entangled_table[itlb_cpu_id][set][way].format);
      itlb_entangled_table[itlb_cpu_id][set][way].entangled_conf[k] = ITLB_CONFIDENCE_COUNTER_MAX_VALUE;
      return;
    }
  }
}

bool itlb_avail_entangled_table(uint64_t page_addr, uint64_t entangled_addr, bool insert_not_present)
{
  uint32_t set = itlb_hash(page_addr) % ITLB_ENTANGLED_TABLE_SETS;
  uint32_t way = itlb_get_way_entangled_table(page_addr);
  if (way == ITLB_ENTANGLED_TABLE_WAYS)
    return insert_not_present;
  for (uint32_t k = 0; k < ITLB_MAX_ENTANGLED_PER_LINE; k++) {
    if (itlb_entangled_table[itlb_cpu_id][set][way].entangled_conf[k] >= ITLB_CONFIDENCE_COUNTER_THRESHOLD
        && itlb_extend_format_entangled(page_addr, itlb_entangled_table[itlb_cpu_id][set][way].entangled_addr[k],
                                        itlb_entangled_table[itlb_cpu_id][set][way].format)
               == entangled_addr) {
      return true;
    }
  }
  // Check for availability
  uint32_t min_format = itlb_get_format_entangled(page_addr, entangled_addr);
  uint32_t num_valid = 1;
  for (uint32_t k = 0; k < ITLB_MAX_ENTANGLED_PER_LINE; k++) {
    if (itlb_entangled_table[itlb_cpu_id][set][way].entangled_conf[k] >= ITLB_CONFIDENCE_COUNTER_THRESHOLD) {
      num_valid++;
      uint32_t format_k =
          itlb_get_format_entangled(page_addr, itlb_extend_format_entangled(page_addr, itlb_entangled_table[itlb_cpu_id][set][way].entangled_addr[k],
                                                                            itlb_entangled_table[itlb_cpu_id][set][way].format));
      if (format_k < min_format) {
        min_format = format_k;
      }
    }
  }
  if (num_valid > min_format) { // Eviction is necessary
    return false;
  } else {
    return true;
  }
}

void itlb_add_bbsize_table(uint64_t page_addr, uint32_t bb_size)
{
  uint64_t tag = (itlb_hash(page_addr) >> ITLB_ENTANGLED_TABLE_INDEX_BITS) & ITLB_TAG_MASK;
  uint32_t set = itlb_hash(page_addr) % ITLB_ENTANGLED_TABLE_SETS;
  uint32_t way = itlb_get_way_entangled_table(page_addr);
  if (way == ITLB_ENTANGLED_TABLE_WAYS) {
    itlb_try_realocate_evicted_in_available_entangled_table(set);
    way = itlb_entangled_fifo[itlb_cpu_id][set];
    itlb_entangled_table[itlb_cpu_id][set][way].tag = tag;
    itlb_entangled_table[itlb_cpu_id][set][way].format = 1;
    for (uint32_t k = 0; k < ITLB_MAX_ENTANGLED_PER_LINE; k++) {
      itlb_entangled_table[itlb_cpu_id][set][way].entangled_addr[k] = 0;
      itlb_entangled_table[itlb_cpu_id][set][way].entangled_conf[k] = 0;
    }
    itlb_entangled_table[itlb_cpu_id][set][way].bb_size = 0;
    itlb_entangled_fifo[itlb_cpu_id][set] = (itlb_entangled_fifo[itlb_cpu_id][set] + 1) % ITLB_ENTANGLED_TABLE_WAYS;
  }
  if (bb_size > itlb_entangled_table[itlb_cpu_id][set][way].bb_size) {
    itlb_entangled_table[itlb_cpu_id][set][way].bb_size = bb_size & ITLB_MERGE_BBSIZE_MAX_VALUE;
  }
  if (bb_size > itlb_stats_max_bb_size) {
    itlb_stats_max_bb_size = bb_size;
  }
}

uint64_t itlb_get_entangled_addr_entangled_table(uint64_t page_addr, uint32_t index_k, uint32_t& set, uint32_t& way)
{
  set = itlb_hash(page_addr) % ITLB_ENTANGLED_TABLE_SETS;
  way = itlb_get_way_entangled_table(page_addr);
  if (way < ITLB_ENTANGLED_TABLE_WAYS) {
    if (itlb_entangled_table[itlb_cpu_id][set][way].entangled_conf[index_k] >= ITLB_CONFIDENCE_COUNTER_THRESHOLD) {
      return itlb_extend_format_entangled(page_addr, itlb_entangled_table[itlb_cpu_id][set][way].entangled_addr[index_k],
                                          itlb_entangled_table[itlb_cpu_id][set][way].format);
    }
  }
  return 0;
}

uint32_t itlb_get_bbsize_entangled_table(uint64_t page_addr)
{
  uint32_t set = itlb_hash(page_addr) % ITLB_ENTANGLED_TABLE_SETS;
  uint32_t way = itlb_get_way_entangled_table(page_addr);
  if (way < ITLB_ENTANGLED_TABLE_WAYS) {
    return itlb_entangled_table[itlb_cpu_id][set][way].bb_size;
  }
  return 0;
}

void itlb_update_confidence_entangled_table(uint32_t set, uint32_t way, uint64_t entangled_addr, bool accessed)
{
  if (way < ITLB_ENTANGLED_TABLE_WAYS) {
    for (uint32_t k = 0; k < ITLB_MAX_ENTANGLED_PER_LINE; k++) {
      if (itlb_entangled_table[itlb_cpu_id][set][way].entangled_conf[k] >= ITLB_CONFIDENCE_COUNTER_THRESHOLD
          && itlb_compress_format_entangled(itlb_entangled_table[itlb_cpu_id][set][way].entangled_addr[k], itlb_entangled_table[itlb_cpu_id][set][way].format)
                 == itlb_compress_format_entangled(entangled_addr, itlb_entangled_table[itlb_cpu_id][set][way].format)) {
        if (accessed && itlb_entangled_table[itlb_cpu_id][set][way].entangled_conf[k] < ITLB_CONFIDENCE_COUNTER_MAX_VALUE) {
          itlb_entangled_table[itlb_cpu_id][set][way].entangled_conf[k]++;
        }
        if (!accessed && itlb_entangled_table[itlb_cpu_id][set][way].entangled_conf[k] > 0) {
          itlb_entangled_table[itlb_cpu_id][set][way].entangled_conf[k]--;
        }
      }
    }
  }
}

// INTERFACE

void CACHE::prefetcher_initialize()
{
  cout << "CPU " << cpu << " Entangling ITLB prefetcher" << endl;

  itlb_cpu_id = cpu;
  itlb_init_stats_table();
  itlb_last_basic_block = 0;
  itlb_consecutive_count = 0;
  itlb_basic_block_merge_diff = 0;
  itlb_current_cycle = current_cycle;

  itlb_init_hist_table();
  itlb_init_timing_tables();
  itlb_init_entangled_table();
}

// (uint64_t addr, uint64_t ip, uint8_t cache_hit, bool useful_prefetch, uint8_t type, uint32_t metadata_in)
// (uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type, uint32_t metadata_in)
uint32_t CACHE::prefetcher_cache_operate(uint64_t v_addr, uint64_t ip, uint8_t cache_hit, bool prefetch_hit, uint8_t type, uint32_t metadata_in)
{
//   cerr << "notify" << endl;
  itlb_cpu_id = cpu;
  itlb_current_cycle = current_cycle;
  uint64_t page_addr = v_addr >> LOG2_PAGE_SIZE;

  if (!cache_hit)
    assert(!prefetch_hit);
  if (!cache_hit)
    assert(itlb_find_timing_cache_entry(page_addr) == MAX_NUM_WAY);
  if (cache_hit)
    assert(itlb_find_timing_cache_entry(page_addr) < MAX_NUM_WAY);

  itlb_stats_table[cpu][(page_addr & ITLB_STATS_TABLE_MASK)].accesses++;
  if (!cache_hit) {
    itlb_stats_table[cpu][(page_addr & ITLB_STATS_TABLE_MASK)].misses++;
    if (itlb_ongoing_request(page_addr) && !itlb_is_accessed_timing_entry(page_addr)) {
      itlb_stats_table[cpu][(page_addr & ITLB_STATS_TABLE_MASK)].late++;
    }
  }
  if (prefetch_hit) {
    itlb_stats_table[cpu][(page_addr & ITLB_STATS_TABLE_MASK)].hits++;
  }

  bool consecutive = false;

  if (itlb_last_basic_block + itlb_consecutive_count == page_addr) { // Same
    return metadata_in;
  } else if (itlb_last_basic_block + itlb_consecutive_count + 1 == page_addr) { // Consecutive
    itlb_consecutive_count++;
    consecutive = true;
  }

  // Queue basic block prefetches
  uint32_t bb_size = itlb_get_bbsize_entangled_table(page_addr);
  if (bb_size)
    itlb_stats_basic_blocks[bb_size]++;
  for (uint32_t i = 1; i <= bb_size; i++) {
    uint64_t pf_addr = v_addr + i * (1 << LOG2_PAGE_SIZE);
    if (!itlb_ongoing_request(pf_addr >> LOG2_PAGE_SIZE)) {
      if (prefetch_line(pf_addr, true, metadata_in)) {
        itlb_add_timing_entry(pf_addr >> LOG2_PAGE_SIZE, 0, ITLB_ENTANGLED_TABLE_WAYS);
      }
    }
  }

  // Queue entangled and basic block of entangled prefetches
  uint32_t num_entangled = 0;
  for (uint32_t k = 0; k < ITLB_MAX_ENTANGLED_PER_LINE; k++) {
    uint32_t source_set = 0;
    uint32_t source_way = ITLB_ENTANGLED_TABLE_WAYS;
    uint64_t entangled_page_addr = itlb_get_entangled_addr_entangled_table(page_addr, k, source_set, source_way);
    if (entangled_page_addr && (entangled_page_addr != page_addr)) {
      num_entangled++;
      uint32_t bb_size = itlb_get_bbsize_entangled_table(entangled_page_addr);
      if (bb_size)
        itlb_stats_basic_blocks_ent[bb_size]++;
      for (uint32_t i = 0; i <= bb_size; i++) {
        uint64_t pf_page_addr = entangled_page_addr + i;
        if (!itlb_ongoing_request(pf_page_addr)) {
          if (prefetch_line(pf_page_addr << LOG2_PAGE_SIZE, true, metadata_in)) {
            itlb_add_timing_entry(pf_page_addr, source_set, (i == 0) ? source_way : ITLB_ENTANGLED_TABLE_WAYS);
          }
        }
      }
    }
  }
  if (num_entangled)
    itlb_stats_entangled[num_entangled]++;

  if (!consecutive) { // New basic block found
    uint32_t max_bb_size = itlb_get_bbsize_entangled_table(itlb_last_basic_block);

    // Check for merging bb opportunities
    if (itlb_consecutive_count) { // single blocks no need to merge and are not inserted in the entangled table
      if (itlb_basic_block_merge_diff > 0) {
        itlb_add_bbsize_table(itlb_last_basic_block - itlb_basic_block_merge_diff, itlb_consecutive_count + itlb_basic_block_merge_diff);
        itlb_add_bb_size_hist_table(itlb_last_basic_block - itlb_basic_block_merge_diff, itlb_consecutive_count + itlb_basic_block_merge_diff);
      } else {
        itlb_add_bbsize_table(itlb_last_basic_block, max(max_bb_size, itlb_consecutive_count));
        itlb_add_bb_size_hist_table(itlb_last_basic_block, max(max_bb_size, itlb_consecutive_count));
      }
    }
  }

  if (!consecutive) { // New basic block found
    itlb_consecutive_count = 0;
    itlb_last_basic_block = page_addr;
  }

  if (!consecutive) {
    itlb_basic_block_merge_diff = itlb_find_bb_merge_hist_table(itlb_last_basic_block);
  }

  // Add the request in the history buffer
  uint32_t pos_hist = ITLB_HIST_TABLE_ENTRIES;
  if (!consecutive && itlb_basic_block_merge_diff == 0) {
    if ((itlb_find_hist_entry(page_addr) == ITLB_HIST_TABLE_ENTRIES)) {
      pos_hist = itlb_add_hist_table(page_addr);
    } else {
      if (!cache_hit && !itlb_ongoing_accessed_request(page_addr)) {
        pos_hist = itlb_add_hist_table(page_addr);
      }
    }
  }

  // Add miss in the latency table
  if (!cache_hit && !itlb_ongoing_request(page_addr)) {
    itlb_add_timing_entry(page_addr, 0, ITLB_ENTANGLED_TABLE_WAYS);
  }

  uint32_t source_set = 0;
  uint32_t source_way = ITLB_ENTANGLED_TABLE_WAYS;
  itlb_access_timing_entry(page_addr, pos_hist, source_set, source_way);

  // Update confidence if late
  if (source_way < ITLB_ENTANGLED_TABLE_WAYS) {
    itlb_update_confidence_entangled_table(source_set, source_way, page_addr, false);
  }
  return metadata_in;
}

void CACHE::prefetcher_cycle_operate()
{
  if (!all_warmed_up && all_warmup_complete > NUM_CPUS) {
    itlb_init_stats_table();
    all_warmed_up = true;
  }
}

// impl_prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in)
uint32_t CACHE::prefetcher_cache_fill(uint64_t v_addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_v_addr, uint32_t metadata_in)
{
  itlb_cpu_id = cpu;
  itlb_current_cycle = current_cycle;
  uint64_t page_addr = (v_addr >> LOG2_PAGE_SIZE);
  uint64_t evicted_page_addr = (evicted_v_addr >> LOG2_PAGE_SIZE);
  // Line is in cache
  if (evicted_v_addr) {
    uint32_t source_set = 0;
    uint32_t source_way = ITLB_ENTANGLED_TABLE_WAYS;
    bool accessed = itlb_invalid_timing_cache_entry(evicted_page_addr, source_set, source_way);
    if (!accessed) {
      itlb_stats_table[cpu][(evicted_page_addr & ITLB_STATS_TABLE_MASK)].wrong++;
    }
    if (source_way < ITLB_ENTANGLED_TABLE_WAYS) {
      // If accessed hit, but if not wrong
      itlb_update_confidence_entangled_table(source_set, source_way, evicted_page_addr, accessed);
    }
  }

  uint32_t pos_hist = ITLB_HIST_TABLE_ENTRIES;
  uint64_t latency = itlb_get_latency_timing_mshr(page_addr, pos_hist);

  itlb_move_timing_entry(page_addr);

  // Get and update entangled
  if (latency && pos_hist < ITLB_HIST_TABLE_ENTRIES) {
    bool inserted = false;
    for (uint32_t i = 0; i < ITLB_TRIES_AVAIL_ENTANGLED; i++) {
      uint64_t src_entangled = itlb_get_src_entangled_hist_table(page_addr, pos_hist, latency, i);
      if (src_entangled && page_addr != src_entangled) {
        if (itlb_avail_entangled_table(src_entangled, page_addr, false)) {
          itlb_add_entangled_table(src_entangled, page_addr);
          inserted = true;
          break;
        }
      }
    }
    if (!inserted) {
      uint64_t src_entangled = itlb_get_src_entangled_hist_table(page_addr, pos_hist, latency);
      if (src_entangled && page_addr != src_entangled) {
        itlb_add_entangled_table(src_entangled, page_addr);
      }
    }
  }
  return metadata_in;
}

void CACHE::prefetcher_final_stats()
{
  cout << "CPU " << cpu << " ITLB Entangling prefetcher final stats" << endl;
  itlb_print_stats_table();
}
