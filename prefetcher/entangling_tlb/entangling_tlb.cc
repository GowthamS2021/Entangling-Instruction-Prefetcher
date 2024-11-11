#include <cassert>
#include <iomanip>
#include <iostream>

#include "cache.h"

using namespace std;

#define TLB_MAX_NUM_SET 128
#define TLB_MAX_NUM_WAY 12

extern uint8_t all_warmup_complete;

// To access cpu in my functions
uint64_t tlb_current_cycle;
uint32_t tlb_cpu_id;

uint64_t tlb_last_basic_block;
uint32_t tlb_consecutive_count;
uint32_t tlb_basic_block_merge_diff;

bool tlb_all_warmed_up = false;

#define TLB_HIST_TABLE_ENTRIES 16

// LINE AND MERGE BASIC BLOCK SIZE

#define TLB_MERGE_BBSIZE_BITS 4
#define TLB_MERGE_BBSIZE_MAX_VALUE ((1 << TLB_MERGE_BBSIZE_BITS) - 1)

// TIME AND OVERFLOWS

#define TLB_TIME_DIFF_BITS 20
#define TLB_TIME_DIFF_OVERFLOW ((uint64_t)1 << TLB_TIME_DIFF_BITS)
#define TLB_TIME_DIFF_MASK (TLB_TIME_DIFF_OVERFLOW - 1)

#define TLB_TIME_BITS 12
#define TLB_TIME_OVERFLOW ((uint64_t)1 << TLB_TIME_BITS)
#define TLB_TIME_MASK (TLB_TIME_OVERFLOW - 1)

uint64_t tlb_get_latency(uint64_t cycle, uint64_t cycle_prev)
{
  uint64_t cycle_masked = cycle & TLB_TIME_MASK;
  uint64_t cycle_prev_masked = cycle_prev & TLB_TIME_MASK;
  if (cycle_prev_masked > cycle_masked) {
    return (cycle_masked + TLB_TIME_OVERFLOW) - cycle_prev_masked;
  }
  return cycle_masked - cycle_prev_masked;
}

// ENTANGLED COMPRESSION FORMAT

#define TLB_ENTANGLED_MAX_FORMATS 7

// STATS
#define TLB_STATS_TABLE_INDEX_BITS 16
#define TLB_STATS_TABLE_ENTRIES (1 << TLB_STATS_TABLE_INDEX_BITS)
#define TLB_STATS_TABLE_MASK (TLB_STATS_TABLE_ENTRIES - 1)

typedef struct __tlb_stats_entry {
  uint64_t accesses;
  uint64_t misses;
  uint64_t hits;
  uint64_t late;
  uint64_t wrong; // early
} tlb_stats_entry;

tlb_stats_entry tlb_stats_table[NUM_CPUS][TLB_STATS_TABLE_ENTRIES];
uint64_t tlb_stats_discarded_prefetches;
uint64_t tlb_stats_evict_entangled_j_table;
uint64_t tlb_stats_evict_entangled_k_table;
uint64_t tlb_stats_max_bb_size;
uint64_t tlb_stats_formats[TLB_ENTANGLED_MAX_FORMATS];
uint64_t tlb_stats_hist_lookups[TLB_HIST_TABLE_ENTRIES + 2];
uint64_t tlb_stats_basic_blocks[TLB_MERGE_BBSIZE_MAX_VALUE + 1];
uint64_t tlb_stats_entangled[TLB_ENTANGLED_MAX_FORMATS + 1];
uint64_t tlb_stats_basic_blocks_ent[TLB_MERGE_BBSIZE_MAX_VALUE + 1];

void tlb_init_stats_table()
{
  for (int i = 0; i < TLB_STATS_TABLE_ENTRIES; i++) {
    tlb_stats_table[tlb_cpu_id][i].accesses = 0;
    tlb_stats_table[tlb_cpu_id][i].misses = 0;
    tlb_stats_table[tlb_cpu_id][i].hits = 0;
    tlb_stats_table[tlb_cpu_id][i].late = 0;
    tlb_stats_table[tlb_cpu_id][i].wrong = 0;
  }
  tlb_stats_discarded_prefetches = 0;
  tlb_stats_evict_entangled_j_table = 0;
  tlb_stats_evict_entangled_k_table = 0;
  tlb_stats_max_bb_size = 0;
  for (int i = 0; i < TLB_ENTANGLED_MAX_FORMATS; i++) {
    tlb_stats_formats[i] = 0;
  }
  for (int i = 0; i <= TLB_HIST_TABLE_ENTRIES; i++) {
    tlb_stats_hist_lookups[i] = 0;
  }
  for (int i = 0; i <= TLB_MERGE_BBSIZE_MAX_VALUE; i++) {
    tlb_stats_basic_blocks[i] = 0;
  }
  for (int i = 0; i <= TLB_ENTANGLED_MAX_FORMATS; i++) {
    tlb_stats_entangled[i] = 0;
  }
  for (int i = 0; i <= TLB_MERGE_BBSIZE_MAX_VALUE; i++) {
    tlb_stats_basic_blocks_ent[i] = 0;
  }
}

void tlb_print_stats_table()
{
  cout << "IP accesses: ";
  uint64_t max = 0;
  uint64_t max_addr = 0;
  uint64_t total_accesses = 0;
  for (uint32_t i = 0; i < TLB_STATS_TABLE_ENTRIES; i++) {
    if (tlb_stats_table[tlb_cpu_id][i].accesses > max) {
      max = tlb_stats_table[tlb_cpu_id][i].accesses;
      max_addr = i;
    }
    total_accesses += tlb_stats_table[tlb_cpu_id][i].accesses;
  }
  cout << hex << max_addr << " " << (max_addr << LOG2_PAGE_SIZE) << dec << " " << max << " / " << total_accesses << endl;
  cout << "IP misses: ";
  max = 0;
  max_addr = 0;
  uint64_t total_misses = 0;
  for (uint32_t i = 0; i < TLB_STATS_TABLE_ENTRIES; i++) {
    if (tlb_stats_table[tlb_cpu_id][i].misses > max) {
      max = tlb_stats_table[tlb_cpu_id][i].misses;
      max_addr = i;
    }
    total_misses += tlb_stats_table[tlb_cpu_id][i].misses;
  }
  cout << hex << max_addr << " " << (max_addr << LOG2_PAGE_SIZE) << dec << " " << max << " / " << total_misses << endl;
  cout << "IP hits: ";
  max = 0;
  max_addr = 0;
  uint64_t total_hits = 0;
  for (uint32_t i = 0; i < TLB_STATS_TABLE_ENTRIES; i++) {
    if (tlb_stats_table[tlb_cpu_id][i].hits > max) {
      max = tlb_stats_table[tlb_cpu_id][i].hits;
      max_addr = i;
    }
    total_hits += tlb_stats_table[tlb_cpu_id][i].hits;
  }
  cout << hex << max_addr << " " << (max_addr << LOG2_PAGE_SIZE) << dec << " " << max << " / " << total_hits << endl;
  cout << "IP late: ";
  max = 0;
  max_addr = 0;
  uint64_t total_late = 0;
  for (uint32_t i = 0; i < TLB_STATS_TABLE_ENTRIES; i++) {
    if (tlb_stats_table[tlb_cpu_id][i].late > max) {
      max = tlb_stats_table[tlb_cpu_id][i].late;
      max_addr = i;
    }
    total_late += tlb_stats_table[tlb_cpu_id][i].late;
  }
  cout << hex << max_addr << " " << (max_addr << LOG2_PAGE_SIZE) << dec << " " << max << " / " << total_late << endl;
  cout << "IP wrong: ";
  max = 0;
  max_addr = 0;
  uint64_t total_wrong = 0;
  for (uint32_t i = 0; i < TLB_STATS_TABLE_ENTRIES; i++) {
    if (tlb_stats_table[tlb_cpu_id][i].wrong > max) {
      max = tlb_stats_table[tlb_cpu_id][i].wrong;
      max_addr = i;
    }
    total_wrong += tlb_stats_table[tlb_cpu_id][i].wrong;
  }
  cout << hex << max_addr << " " << (max_addr << LOG2_PAGE_SIZE) << dec << " " << max << " / " << total_wrong << endl;

  cout << "miss rate: " << ((double)total_misses / (double)total_accesses) << endl;
  cout << "coverage: " << ((double)total_hits / (double)(total_hits + total_misses)) << endl;
  cout << "coverage_late: " << ((double)(total_hits + total_late) / (double)(total_hits + total_misses)) << endl;
  cout << "accuracy: " << ((double)total_hits / (double)(total_hits + total_late + total_wrong)) << endl;
  cout << "accuracy_late: " << ((double)(total_hits + total_late) / (double)(total_hits + total_late + total_wrong)) << endl;
  cout << "discarded: " << tlb_stats_discarded_prefetches << endl;
  cout << "evicts entangled j table: " << tlb_stats_evict_entangled_j_table << endl;
  cout << "evicts entangled k table: " << tlb_stats_evict_entangled_k_table << endl;
  cout << "max bb size: " << tlb_stats_max_bb_size << endl;
  cout << "formats: ";
  for (uint32_t i = 0; i < TLB_ENTANGLED_MAX_FORMATS; i++) {
    cout << tlb_stats_formats[i] << " ";
  }
  cout << endl;
  cout << "hist_lookups: ";
  uint64_t total_hist_lookups = 0;
  for (uint32_t i = 0; i <= TLB_HIST_TABLE_ENTRIES + 1; i++) {
    cout << tlb_stats_hist_lookups[i] << " ";
    total_hist_lookups += tlb_stats_hist_lookups[i];
  }
  cout << endl;
  cout << "hist_lookups_evict: " << (double)tlb_stats_hist_lookups[TLB_HIST_TABLE_ENTRIES] * 100 / (double)(total_hist_lookups) << " %" << endl;
  cout << "hist_lookups_shortlat: " << (double)tlb_stats_hist_lookups[TLB_HIST_TABLE_ENTRIES + 1] * 100 / (double)(total_hist_lookups) << " %" << endl;

  cout << "bb_found_hist: ";
  uint64_t total_bb_found = 0;
  uint64_t total_bb_prefetches = 0;
  for (uint32_t i = 0; i <= TLB_MERGE_BBSIZE_MAX_VALUE; i++) {
    cout << tlb_stats_basic_blocks[i] << " ";
    total_bb_found += i * tlb_stats_basic_blocks[i];
    total_bb_prefetches += tlb_stats_basic_blocks[i];
  }
  cout << endl;
  cout << "bb_found_summary: " << total_bb_found << " " << total_bb_prefetches << " " << (double)total_bb_found / (double)total_bb_prefetches << endl;

  cout << "entangled_found_hist: ";
  uint64_t total_entangled_found = 0;
  uint64_t total_ent_prefetches = 0;
  for (uint32_t i = 0; i <= TLB_ENTANGLED_MAX_FORMATS; i++) {
    cout << tlb_stats_entangled[i] << " ";
    total_entangled_found += i * tlb_stats_entangled[i];
    total_ent_prefetches += tlb_stats_entangled[i];
  }
  cout << endl;
  cout << "entangled_found_summary: " << total_entangled_found << " " << total_ent_prefetches << " "
       << (double)total_entangled_found / (double)total_ent_prefetches << endl;

  cout << "bb_ent_found_hist: ";
  uint64_t total_bb_ent_found = 0;
  uint64_t total_bb_ent_prefetches = 0;
  for (uint32_t i = 0; i <= TLB_MERGE_BBSIZE_MAX_VALUE; i++) {
    cout << tlb_stats_basic_blocks_ent[i] << " ";
    total_bb_ent_found += i * tlb_stats_basic_blocks_ent[i];
    total_bb_ent_prefetches += tlb_stats_basic_blocks_ent[i];
  }
  cout << endl;
  cout << "bb_ent_found_summary: " << total_bb_ent_found << " " << total_bb_ent_prefetches << " "
       << (double)total_bb_ent_found / (double)total_bb_ent_prefetches << endl;
}

// HISTORY TABLE (BUFFER)

#define TLB_HIST_TABLE_MASK (TLB_HIST_TABLE_ENTRIES - 1)
#define TLB_BB_MERGE_ENTRIES 5
#define TLB_HIST_TAG_BITS 58
#define TLB_HIST_TAG_MASK (((uint64_t)1 << TLB_HIST_TAG_BITS) - 1)

typedef struct __tlb_hist_entry {
  uint64_t tag;       // TLB_HIST_TAG_BITS bits
  uint64_t time_diff; // TLB_TIME_DIFF_BITS bits
  uint32_t bb_size;   // TLB_MERGE_BBSIZE_BITS bits
} tlb_hist_entry;

tlb_hist_entry tlb_hist_table[NUM_CPUS][TLB_HIST_TABLE_ENTRIES];
uint64_t tlb_hist_table_head[NUM_CPUS];      // log_2 (TLB_HIST_TABLE_ENTRIES)
uint64_t tlb_hist_table_head_time[NUM_CPUS]; // 64 bits

void tlb_init_hist_table()
{
  tlb_hist_table_head[tlb_cpu_id] = 0;
  tlb_hist_table_head_time[tlb_cpu_id] = tlb_current_cycle;
  for (uint32_t i = 0; i < TLB_HIST_TABLE_ENTRIES; i++) {
    tlb_hist_table[tlb_cpu_id][i].tag = 0;
    tlb_hist_table[tlb_cpu_id][i].time_diff = 0;
    tlb_hist_table[tlb_cpu_id][i].bb_size = 0;
  }
}

uint64_t tlb_find_hist_entry(uint64_t page_addr)
{
  uint64_t tag = page_addr & TLB_HIST_TAG_MASK;
  for (uint32_t count = 0, i = (tlb_hist_table_head[tlb_cpu_id] + TLB_HIST_TABLE_MASK) % TLB_HIST_TABLE_ENTRIES; count < TLB_HIST_TABLE_ENTRIES;
       count++, i = (i + TLB_HIST_TABLE_MASK) % TLB_HIST_TABLE_ENTRIES) {
    if (tlb_hist_table[tlb_cpu_id][i].tag == tag)
      return i;
  }
  return TLB_HIST_TABLE_ENTRIES;
}

// It can have duplicated entries if the line was evicted in between
uint32_t tlb_add_hist_table(uint64_t page_addr)
{
  // Insert empty addresses in hist not to have timediff overflows
  while (tlb_current_cycle - tlb_hist_table_head_time[tlb_cpu_id] >= TLB_TIME_DIFF_OVERFLOW) {
    tlb_hist_table[tlb_cpu_id][tlb_hist_table_head[tlb_cpu_id]].tag = 0;
    tlb_hist_table[tlb_cpu_id][tlb_hist_table_head[tlb_cpu_id]].time_diff = TLB_TIME_DIFF_MASK;
    tlb_hist_table[tlb_cpu_id][tlb_hist_table_head[tlb_cpu_id]].bb_size = 0;
    tlb_hist_table_head[tlb_cpu_id] = (tlb_hist_table_head[tlb_cpu_id] + 1) % TLB_HIST_TABLE_ENTRIES;
    tlb_hist_table_head_time[tlb_cpu_id] += TLB_TIME_DIFF_MASK;
  }

  // Allocate a new entry (evict old one if necessary)
  tlb_hist_table[tlb_cpu_id][tlb_hist_table_head[tlb_cpu_id]].tag = page_addr & TLB_HIST_TAG_MASK;
  tlb_hist_table[tlb_cpu_id][tlb_hist_table_head[tlb_cpu_id]].time_diff =
      (tlb_current_cycle - tlb_hist_table_head_time[tlb_cpu_id]) & TLB_TIME_DIFF_MASK;
  tlb_hist_table[tlb_cpu_id][tlb_hist_table_head[tlb_cpu_id]].bb_size = 0;
  uint32_t pos = tlb_hist_table_head[tlb_cpu_id];
  tlb_hist_table_head[tlb_cpu_id] = (tlb_hist_table_head[tlb_cpu_id] + 1) % TLB_HIST_TABLE_ENTRIES;
  tlb_hist_table_head_time[tlb_cpu_id] = tlb_current_cycle;
  return pos;
}

void tlb_add_bb_size_hist_table(uint64_t page_addr, uint32_t bb_size)
{
  uint64_t index = tlb_find_hist_entry(page_addr);
  tlb_hist_table[tlb_cpu_id][index].bb_size = bb_size & TLB_MERGE_BBSIZE_MAX_VALUE;
}

uint32_t tlb_find_bb_merge_hist_table(uint64_t page_addr)
{
  uint64_t tag = page_addr & TLB_HIST_TAG_MASK;
  for (uint32_t count = 0, i = (tlb_hist_table_head[tlb_cpu_id] + TLB_HIST_TABLE_MASK) % TLB_HIST_TABLE_ENTRIES; count < TLB_HIST_TABLE_ENTRIES;
       count++, i = (i + TLB_HIST_TABLE_MASK) % TLB_HIST_TABLE_ENTRIES) {
    if (count >= TLB_BB_MERGE_ENTRIES) {
      return 0;
    }
    if (tag > tlb_hist_table[tlb_cpu_id][i].tag && (tag - tlb_hist_table[tlb_cpu_id][i].tag) <= tlb_hist_table[tlb_cpu_id][i].bb_size) {
      //&& (tag - tlb_hist_table[tlb_cpu_id][i].tag) == tlb_hist_table[tlb_cpu_id][i].bb_size) {
      return tag - tlb_hist_table[tlb_cpu_id][i].tag;
    }
  }
  assert(false);
}

// return src-entangled pair
uint64_t tlb_get_src_entangled_hist_table(uint64_t page_addr, uint32_t pos_hist, uint64_t latency, uint32_t skip = 0)
{
  assert(pos_hist < TLB_HIST_TABLE_ENTRIES);
  uint64_t tag = page_addr & TLB_HIST_TAG_MASK;
  assert(tag);
  if (tlb_hist_table[tlb_cpu_id][pos_hist].tag != tag) {
    tlb_stats_hist_lookups[TLB_HIST_TABLE_ENTRIES]++;
    return 0; // removed
  }
  uint32_t next_pos = (pos_hist + TLB_HIST_TABLE_MASK) % TLB_HIST_TABLE_ENTRIES;
  uint32_t first = (tlb_hist_table_head[tlb_cpu_id] + TLB_HIST_TABLE_MASK) % TLB_HIST_TABLE_ENTRIES;
  uint64_t time_i = tlb_hist_table[tlb_cpu_id][pos_hist].time_diff;
  uint32_t num_skipped = 0;
  for (uint32_t count = 0, i = next_pos; i != first; count++, i = (i + TLB_HIST_TABLE_MASK) % TLB_HIST_TABLE_ENTRIES) {
    // Against the time overflow
    if (tlb_hist_table[tlb_cpu_id][i].tag == tag) {
      return 0; // Second time it appeared (it was evicted in between) or many for the same set. No entangle
    }
    if (tlb_hist_table[tlb_cpu_id][i].tag && time_i >= latency) {
      if (skip == num_skipped) {
        tlb_stats_hist_lookups[count]++;
        return tlb_hist_table[tlb_cpu_id][i].tag;
      } else {
        num_skipped++;
      }
    }
    time_i += tlb_hist_table[tlb_cpu_id][i].time_diff;
  }
  tlb_stats_hist_lookups[TLB_HIST_TABLE_ENTRIES + 1]++;
  return 0;
}

// TIMING TABLES

#define TLB_SET_BITS 7
// #define TLB_TIMING_MSHR_SIZE (TLB_PQ_SIZE+TLB_MSHR_SIZE+64) // Not necessary +64 (perhaps +TLB_RQ_SIZE), just to track enough in-fligh requests
#define TLB_TIMING_MSHR_SIZE 256
#define TLB_TIMING_MSHR_TAG_BITS 42
#define TLB_TIMING_MSHR_TAG_MASK (((uint64_t)1 << TLB_HIST_TAG_BITS) - 1)
#define TLB_TIMING_CACHE_TAG_BITS (TLB_TIMING_MSHR_TAG_BITS - TLB_SET_BITS)
#define TLB_TIMING_CACHE_TAG_MASK (((uint64_t)1 << TLB_HIST_TAG_BITS) - 1)

#define TLB_ENTANGLED_TABLE_WAYS 16

// We do not have access to the MSHR, so we aproximate it using this structure
typedef struct __tlb_timing_mshr_entry {
  bool valid;          // 1 bit
  uint64_t tag;        // TLB_TIMING_MSHR_TAG_BITS bits
  uint32_t source_set; // 8 bits
  uint32_t source_way; // 6 bits
  uint64_t timestamp;  // TLB_TIME_BITS bits // time when issued
  bool accessed;       // 1 bit
  uint32_t pos_hist;   // 1 bit
} tlb_timing_mshr_entry;

// We do not have access to the cache, so we aproximate it using this structure
typedef struct __tlb_timing_cache_entry {
  bool valid;          // 1 bit
  uint64_t tag;        // TLB_TIMING_CACHE_TAG_BITS bits
  uint32_t source_set; // 8 bits
  uint32_t source_way; // 6 bits
  bool accessed;       // 1 bit
} tlb_timing_cache_entry;

tlb_timing_mshr_entry tlb_timing_mshr_table[NUM_CPUS][TLB_TIMING_MSHR_SIZE];
tlb_timing_cache_entry tlb_timing_cache_table[NUM_CPUS][TLB_MAX_NUM_SET][TLB_MAX_NUM_WAY];

void tlb_init_timing_tables()
{
  for (uint32_t i = 0; i < TLB_TIMING_MSHR_SIZE; i++) {
    tlb_timing_mshr_table[tlb_cpu_id][i].valid = 0;
  }
  for (uint32_t i = 0; i < TLB_MAX_NUM_SET; i++) {
    for (uint32_t j = 0; j < TLB_MAX_NUM_WAY; j++) {
      tlb_timing_cache_table[tlb_cpu_id][i][j].valid = 0;
    }
  }
}

uint64_t tlb_find_timing_mshr_entry(uint64_t page_addr)
{
  for (uint32_t i = 0; i < TLB_TIMING_MSHR_SIZE; i++) {
    if (tlb_timing_mshr_table[tlb_cpu_id][i].tag == (page_addr & TLB_TIMING_MSHR_TAG_MASK) && tlb_timing_mshr_table[tlb_cpu_id][i].valid)
      return i;
  }
  return TLB_TIMING_MSHR_SIZE;
}

uint64_t tlb_find_timing_cache_entry(uint64_t page_addr)
{
  uint64_t i = page_addr % TLB_MAX_NUM_SET;
  for (uint32_t j = 0; j < TLB_MAX_NUM_WAY; j++) {
    if (tlb_timing_cache_table[tlb_cpu_id][i][j].tag == ((page_addr >> TLB_SET_BITS) & TLB_TIMING_CACHE_TAG_MASK)
        && tlb_timing_cache_table[tlb_cpu_id][i][j].valid)
      return j;
  }
  return TLB_MAX_NUM_WAY;
}

uint32_t tlb_get_invalid_timing_mshr_entry()
{
  for (uint32_t i = 0; i < TLB_TIMING_MSHR_SIZE; i++) {
    if (!tlb_timing_mshr_table[tlb_cpu_id][i].valid)
      return i;
  }
  // cerr << 1 << endl;
  assert(false); // It must return a free entry
  return TLB_TIMING_MSHR_SIZE;
}

uint32_t tlb_get_invalid_timing_cache_entry(uint64_t page_addr)
{
  uint32_t i = page_addr % TLB_MAX_NUM_SET;
  for (uint32_t j = 0; j < TLB_MAX_NUM_WAY; j++) {
    if (!tlb_timing_cache_table[tlb_cpu_id][i][j].valid)
      return j;
  }
  // cerr << 1 << endl;
  assert(false); // It must return a free entry
  return TLB_MAX_NUM_WAY;
}

void tlb_add_timing_entry(uint64_t page_addr, uint32_t source_set, uint32_t source_way)
{
  // First find for coalescing
  if (tlb_find_timing_mshr_entry(page_addr) < TLB_TIMING_MSHR_SIZE)
    return;
  if (tlb_find_timing_cache_entry(page_addr) < TLB_MAX_NUM_WAY)
    return;

  uint32_t i = tlb_get_invalid_timing_mshr_entry();
  tlb_timing_mshr_table[tlb_cpu_id][i].valid = true;
  tlb_timing_mshr_table[tlb_cpu_id][i].tag = page_addr & TLB_TIMING_MSHR_TAG_MASK;
  tlb_timing_mshr_table[tlb_cpu_id][i].source_set = source_set;
  tlb_timing_mshr_table[tlb_cpu_id][i].source_way = source_way;
  tlb_timing_mshr_table[tlb_cpu_id][i].timestamp = tlb_current_cycle & TLB_TIME_MASK;
  tlb_timing_mshr_table[tlb_cpu_id][i].accessed = false;
}

void tlb_invalid_timing_mshr_entry(uint64_t page_addr)
{
  uint32_t index = tlb_find_timing_mshr_entry(page_addr);
  assert(index < TLB_TIMING_MSHR_SIZE);
  tlb_timing_mshr_table[tlb_cpu_id][index].valid = false;
}

void tlb_move_timing_entry(uint64_t page_addr)
{
  uint32_t index_mshr = tlb_find_timing_mshr_entry(page_addr);
  if (index_mshr == TLB_TIMING_MSHR_SIZE) {
    uint32_t set = page_addr % TLB_MAX_NUM_SET;
    uint32_t index_cache = tlb_get_invalid_timing_cache_entry(page_addr);
    tlb_timing_cache_table[tlb_cpu_id][set][index_cache].valid = true;
    tlb_timing_cache_table[tlb_cpu_id][set][index_cache].tag = (page_addr >> TLB_SET_BITS) & TLB_TIMING_CACHE_TAG_MASK;
    tlb_timing_cache_table[tlb_cpu_id][set][index_cache].source_way = TLB_ENTANGLED_TABLE_WAYS;
    tlb_timing_cache_table[tlb_cpu_id][set][index_cache].accessed = true;
    return;
  }
  uint64_t set = page_addr % TLB_MAX_NUM_SET;
  uint64_t index_cache = tlb_get_invalid_timing_cache_entry(page_addr);
  tlb_timing_cache_table[tlb_cpu_id][set][index_cache].valid = true;
  tlb_timing_cache_table[tlb_cpu_id][set][index_cache].tag = (page_addr >> TLB_SET_BITS) & TLB_TIMING_CACHE_TAG_MASK;
  tlb_timing_cache_table[tlb_cpu_id][set][index_cache].source_set = tlb_timing_mshr_table[tlb_cpu_id][index_mshr].source_set;
  tlb_timing_cache_table[tlb_cpu_id][set][index_cache].source_way = tlb_timing_mshr_table[tlb_cpu_id][index_mshr].source_way;
  tlb_timing_cache_table[tlb_cpu_id][set][index_cache].accessed = tlb_timing_mshr_table[tlb_cpu_id][index_mshr].accessed;
  tlb_invalid_timing_mshr_entry(page_addr);
}

// returns if accessed
bool tlb_invalid_timing_cache_entry(uint64_t page_addr, uint32_t& source_set, uint32_t& source_way)
{
  uint32_t set = page_addr % TLB_MAX_NUM_SET;
  uint32_t way = tlb_find_timing_cache_entry(page_addr);
  assert(way < TLB_MAX_NUM_WAY);
  tlb_timing_cache_table[tlb_cpu_id][set][way].valid = false;
  source_set = tlb_timing_cache_table[tlb_cpu_id][set][way].source_set;
  source_way = tlb_timing_cache_table[tlb_cpu_id][set][way].source_way;
  return tlb_timing_cache_table[tlb_cpu_id][set][way].accessed;
}

void tlb_access_timing_entry(uint64_t page_addr, uint32_t pos_hist, uint32_t& source_set, uint32_t& source_way)
{
  uint32_t index = tlb_find_timing_mshr_entry(page_addr);
  if (index < TLB_TIMING_MSHR_SIZE) {
    if (!tlb_timing_mshr_table[tlb_cpu_id][index].accessed) { // Prefetch accessed while in MSHR: late
      tlb_timing_mshr_table[tlb_cpu_id][index].accessed = true;
      tlb_timing_mshr_table[tlb_cpu_id][index].pos_hist = pos_hist;
      if (tlb_timing_mshr_table[tlb_cpu_id][index].source_way < TLB_ENTANGLED_TABLE_WAYS) {
        source_set = tlb_timing_mshr_table[tlb_cpu_id][index].source_set;
        source_way = tlb_timing_mshr_table[tlb_cpu_id][index].source_way;
        tlb_timing_mshr_table[tlb_cpu_id][index].source_set = 0;
        tlb_timing_mshr_table[tlb_cpu_id][index].source_way = TLB_ENTANGLED_TABLE_WAYS;
      }
    }
    return;
  }
  uint32_t set = page_addr % TLB_MAX_NUM_SET;
  uint32_t way = tlb_find_timing_cache_entry(page_addr);
  if (way < TLB_MAX_NUM_WAY) {
    tlb_timing_cache_table[tlb_cpu_id][set][way].accessed = true;
  }
}

bool tlb_is_accessed_timing_entry(uint64_t page_addr)
{
  uint32_t index = tlb_find_timing_mshr_entry(page_addr);
  if (index < TLB_TIMING_MSHR_SIZE) {
    return tlb_timing_mshr_table[tlb_cpu_id][index].accessed;
  }
  uint32_t set = page_addr % TLB_MAX_NUM_SET;
  uint32_t way = tlb_find_timing_cache_entry(page_addr);
  if (way < TLB_MAX_NUM_WAY) {
    return tlb_timing_cache_table[tlb_cpu_id][set][way].accessed;
  }
  return false;
}

bool tlb_completed_request(uint64_t page_addr) { return tlb_find_timing_cache_entry(page_addr) < TLB_MAX_NUM_WAY; }

bool tlb_ongoing_request(uint64_t page_addr) { return tlb_find_timing_mshr_entry(page_addr) < TLB_TIMING_MSHR_SIZE; }

bool tlb_ongoing_accessed_request(uint64_t page_addr)
{
  uint32_t index = tlb_find_timing_mshr_entry(page_addr);
  if (index == TLB_TIMING_MSHR_SIZE)
    return false;
  return tlb_timing_mshr_table[tlb_cpu_id][index].accessed;
}

uint64_t tlb_get_latency_timing_mshr(uint64_t page_addr, uint32_t& pos_hist)
{
  uint32_t index = tlb_find_timing_mshr_entry(page_addr);
  if (index == TLB_TIMING_MSHR_SIZE)
    return 0;
  if (!tlb_timing_mshr_table[tlb_cpu_id][index].accessed)
    return 0;
  pos_hist = tlb_timing_mshr_table[tlb_cpu_id][index].pos_hist;
  return tlb_get_latency(tlb_current_cycle, tlb_timing_mshr_table[tlb_cpu_id][index].timestamp);
}

// ENTANGLED TABLE

uint32_t TLB_ENTANGLED_FORMATS[TLB_ENTANGLED_MAX_FORMATS] = {58, 28, 18, 13, 10, 8, 6};
#define TLB_ENTANGLED_NUM_FORMATS 6

uint32_t tlb_get_format_entangled(uint64_t page_addr, uint64_t entangled_addr)
{
  for (uint32_t i = TLB_ENTANGLED_NUM_FORMATS; i != 0; i--) {
    if ((page_addr >> TLB_ENTANGLED_FORMATS[i - 1]) == (entangled_addr >> TLB_ENTANGLED_FORMATS[i - 1])) {
      return i;
    }
  }
  assert(false);
}

uint64_t tlb_extend_format_entangled(uint64_t page_addr, uint64_t entangled_addr, uint32_t format)
{
  return ((page_addr >> TLB_ENTANGLED_FORMATS[format - 1]) << TLB_ENTANGLED_FORMATS[format - 1])
         | (entangled_addr & (((uint64_t)1 << TLB_ENTANGLED_FORMATS[format - 1]) - 1));
}

uint64_t tlb_compress_format_entangled(uint64_t entangled_addr, uint32_t format)
{
  return entangled_addr & (((uint64_t)1 << TLB_ENTANGLED_FORMATS[format - 1]) - 1);
}

#define TLB_ENTANGLED_TABLE_INDEX_BITS 6
#define TLB_ENTANGLED_TABLE_SETS (1 << TLB_ENTANGLED_TABLE_INDEX_BITS)
#define TLB_MAX_ENTANGLED_PER_LINE TLB_ENTANGLED_NUM_FORMATS
#define TLB_TAG_BITS (19 - TLB_ENTANGLED_TABLE_INDEX_BITS)
#define TLB_TAG_MASK (((uint64_t)1 << TLB_TAG_BITS) - 1)
#define TLB_CONFIDENCE_COUNTER_BITS 2
#define TLB_CONFIDENCE_COUNTER_MAX_VALUE ((1 << TLB_CONFIDENCE_COUNTER_BITS) - 1)
#define TLB_CONFIDENCE_COUNTER_THRESHOLD 1
#define TLB_TRIES_AVAIL_ENTANGLED 2

typedef struct __tlb_entangled_entry {
  uint64_t tag;                                         // TLB_TAG_BITS bits
  uint32_t format;                                      // log2(TLB_ENTANGLED_NUM_FORMATS) bits
  uint64_t entangled_addr[TLB_MAX_ENTANGLED_PER_LINE]; // JUST DIFF
  uint32_t entangled_conf[TLB_MAX_ENTANGLED_PER_LINE]; // TLB_CONFIDENCE_COUNTER_BITS bits
  uint32_t bb_size;                                     // TLB_MERGE_BBSIZE_BITS bits
} tlb_entangled_entry;

tlb_entangled_entry tlb_entangled_table[NUM_CPUS][TLB_ENTANGLED_TABLE_SETS][TLB_ENTANGLED_TABLE_WAYS];
uint32_t tlb_entangled_fifo[NUM_CPUS][TLB_ENTANGLED_TABLE_SETS]; // log2(TLB_ENTANGLED_TABLE_WAYS) * TLB_ENTANGLED_TABLE_SETS bits

uint64_t tlb_hash(uint64_t page_addr) { return page_addr ^ (page_addr >> 2) ^ (page_addr >> 5); }

void tlb_init_entangled_table()
{
  for (uint32_t i = 0; i < TLB_ENTANGLED_TABLE_SETS; i++) {
    for (uint32_t j = 0; j < TLB_ENTANGLED_TABLE_WAYS; j++) {
      tlb_entangled_table[tlb_cpu_id][i][j].tag = 0;
      tlb_entangled_table[tlb_cpu_id][i][j].format = 1;
      for (uint32_t k = 0; k < TLB_MAX_ENTANGLED_PER_LINE; k++) {
        tlb_entangled_table[tlb_cpu_id][i][j].entangled_addr[k] = 0;
        tlb_entangled_table[tlb_cpu_id][i][j].entangled_conf[k] = 0;
      }
      tlb_entangled_table[tlb_cpu_id][i][j].bb_size = 0;
    }
    tlb_entangled_fifo[tlb_cpu_id][i] = 0;
  }
}

uint32_t tlb_get_way_entangled_table(uint64_t page_addr)
{
  uint64_t tag = (tlb_hash(page_addr) >> TLB_ENTANGLED_TABLE_INDEX_BITS) & TLB_TAG_MASK;
  uint32_t set = tlb_hash(page_addr) % TLB_ENTANGLED_TABLE_SETS;
  for (uint32_t i = 0; i < TLB_ENTANGLED_TABLE_WAYS; i++) {
    if (tlb_entangled_table[tlb_cpu_id][set][i].tag == tag) { // Found
      return i;
    }
  }
  return TLB_ENTANGLED_TABLE_WAYS;
}

void tlb_try_realocate_evicted_in_available_entangled_table(uint32_t set)
{
  uint64_t way = tlb_entangled_fifo[tlb_cpu_id][set];
  bool dest_free_way = true;
  for (uint32_t k = 0; k < TLB_MAX_ENTANGLED_PER_LINE; k++) {
    if (tlb_entangled_table[tlb_cpu_id][set][way].entangled_conf[k] >= TLB_CONFIDENCE_COUNTER_THRESHOLD) {
      dest_free_way = false;
      break;
    }
  }
  if (dest_free_way && tlb_entangled_table[tlb_cpu_id][set][way].bb_size == 0)
    return;
  uint32_t free_way = way;
  bool free_with_size = false;
  for (uint32_t i = (way + 1) % TLB_ENTANGLED_TABLE_WAYS; i != way; i = (i + 1) % TLB_ENTANGLED_TABLE_WAYS) {
    bool dest_free = true;
    for (uint32_t k = 0; k < TLB_MAX_ENTANGLED_PER_LINE; k++) {
      if (tlb_entangled_table[tlb_cpu_id][set][i].entangled_conf[k] >= TLB_CONFIDENCE_COUNTER_THRESHOLD) {
        dest_free = false;
        break;
      }
    }
    if (dest_free) {
      if (free_way == way) {
        free_way = i;
        free_with_size = (tlb_entangled_table[tlb_cpu_id][set][i].bb_size != 0);
      } else if (free_with_size && tlb_entangled_table[tlb_cpu_id][set][i].bb_size == 0) {
        free_way = i;
        free_with_size = false;
        break;
      }
    }
  }
  if (free_way != way && ((!free_with_size) || (free_with_size && !dest_free_way))) { // Only evict if it has more information
    tlb_entangled_table[tlb_cpu_id][set][free_way].tag = tlb_entangled_table[tlb_cpu_id][set][way].tag;
    tlb_entangled_table[tlb_cpu_id][set][free_way].format = tlb_entangled_table[tlb_cpu_id][set][way].format;
    for (uint32_t k = 0; k < TLB_MAX_ENTANGLED_PER_LINE; k++) {
      tlb_entangled_table[tlb_cpu_id][set][free_way].entangled_addr[k] = tlb_entangled_table[tlb_cpu_id][set][way].entangled_addr[k];
      tlb_entangled_table[tlb_cpu_id][set][free_way].entangled_conf[k] = tlb_entangled_table[tlb_cpu_id][set][way].entangled_conf[k];
    }
    tlb_entangled_table[tlb_cpu_id][set][free_way].bb_size = tlb_entangled_table[tlb_cpu_id][set][way].bb_size;
  }
}

void tlb_add_entangled_table(uint64_t page_addr, uint64_t entangled_addr)
{
  uint64_t tag = (tlb_hash(page_addr) >> TLB_ENTANGLED_TABLE_INDEX_BITS) & TLB_TAG_MASK;
  uint32_t set = tlb_hash(page_addr) % TLB_ENTANGLED_TABLE_SETS;
  uint32_t way = tlb_get_way_entangled_table(page_addr);
  if (way == TLB_ENTANGLED_TABLE_WAYS) {
    tlb_try_realocate_evicted_in_available_entangled_table(set);
    way = tlb_entangled_fifo[tlb_cpu_id][set];
    tlb_entangled_table[tlb_cpu_id][set][way].tag = tag;
    tlb_entangled_table[tlb_cpu_id][set][way].format = 1;
    for (uint32_t k = 0; k < TLB_MAX_ENTANGLED_PER_LINE; k++) {
      tlb_entangled_table[tlb_cpu_id][set][way].entangled_addr[k] = 0;
      tlb_entangled_table[tlb_cpu_id][set][way].entangled_conf[k] = 0;
    }
    tlb_entangled_table[tlb_cpu_id][set][way].bb_size = 0;
    tlb_entangled_fifo[tlb_cpu_id][set] = (tlb_entangled_fifo[tlb_cpu_id][set] + 1) % TLB_ENTANGLED_TABLE_WAYS;
  }
  for (uint32_t k = 0; k < TLB_MAX_ENTANGLED_PER_LINE; k++) {
    if (tlb_entangled_table[tlb_cpu_id][set][way].entangled_conf[k] >= TLB_CONFIDENCE_COUNTER_THRESHOLD
        && tlb_extend_format_entangled(page_addr, tlb_entangled_table[tlb_cpu_id][set][way].entangled_addr[k],
                                        tlb_entangled_table[tlb_cpu_id][set][way].format)
               == entangled_addr) {
      tlb_entangled_table[tlb_cpu_id][set][way].entangled_conf[k] = TLB_CONFIDENCE_COUNTER_MAX_VALUE;
      return;
    }
  }

  // Adding a new entangled
  uint32_t format_new = tlb_get_format_entangled(page_addr, entangled_addr);
  tlb_stats_formats[format_new - 1]++;

  // Check for evictions
  while (true) {
    uint32_t min_format = format_new;
    uint32_t num_valid = 1;
    uint32_t min_value = TLB_CONFIDENCE_COUNTER_MAX_VALUE + 1;
    uint32_t min_pos = 0;
    for (uint32_t k = 0; k < TLB_MAX_ENTANGLED_PER_LINE; k++) {
      if (tlb_entangled_table[tlb_cpu_id][set][way].entangled_conf[k] >= TLB_CONFIDENCE_COUNTER_THRESHOLD) {
        num_valid++;
        uint32_t format_k =
            tlb_get_format_entangled(page_addr, tlb_extend_format_entangled(page_addr, tlb_entangled_table[tlb_cpu_id][set][way].entangled_addr[k],
                                                                              tlb_entangled_table[tlb_cpu_id][set][way].format));
        if (format_k < min_format) {
          min_format = format_k;
        }
        if (tlb_entangled_table[tlb_cpu_id][set][way].entangled_conf[k] < min_value) {
          min_value = tlb_entangled_table[tlb_cpu_id][set][way].entangled_conf[k];
          min_pos = k;
        }
      }
    }
    if (num_valid > min_format) { // Eviction is necessary. We chose the lower confidence one
      tlb_stats_evict_entangled_k_table++;
      tlb_entangled_table[tlb_cpu_id][set][way].entangled_conf[min_pos] = 0;
    } else {
      // Reformat
      for (uint32_t k = 0; k < TLB_MAX_ENTANGLED_PER_LINE; k++) {
        if (tlb_entangled_table[tlb_cpu_id][set][way].entangled_conf[k] >= TLB_CONFIDENCE_COUNTER_THRESHOLD) {
          tlb_entangled_table[tlb_cpu_id][set][way].entangled_addr[k] =
              tlb_compress_format_entangled(tlb_extend_format_entangled(page_addr, tlb_entangled_table[tlb_cpu_id][set][way].entangled_addr[k],
                                                                          tlb_entangled_table[tlb_cpu_id][set][way].format),
                                             min_format);
        }
      }
      tlb_entangled_table[tlb_cpu_id][set][way].format = min_format;
      break;
    }
  }
  for (uint32_t k = 0; k < TLB_MAX_ENTANGLED_PER_LINE; k++) {
    if (tlb_entangled_table[tlb_cpu_id][set][way].entangled_conf[k] < TLB_CONFIDENCE_COUNTER_THRESHOLD) {
      tlb_entangled_table[tlb_cpu_id][set][way].entangled_addr[k] =
          tlb_compress_format_entangled(entangled_addr, tlb_entangled_table[tlb_cpu_id][set][way].format);
      tlb_entangled_table[tlb_cpu_id][set][way].entangled_conf[k] = TLB_CONFIDENCE_COUNTER_MAX_VALUE;
      return;
    }
  }
}

bool tlb_avail_entangled_table(uint64_t page_addr, uint64_t entangled_addr, bool insert_not_present)
{
  uint32_t set = tlb_hash(page_addr) % TLB_ENTANGLED_TABLE_SETS;
  uint32_t way = tlb_get_way_entangled_table(page_addr);
  if (way == TLB_ENTANGLED_TABLE_WAYS)
    return insert_not_present;
  for (uint32_t k = 0; k < TLB_MAX_ENTANGLED_PER_LINE; k++) {
    if (tlb_entangled_table[tlb_cpu_id][set][way].entangled_conf[k] >= TLB_CONFIDENCE_COUNTER_THRESHOLD
        && tlb_extend_format_entangled(page_addr, tlb_entangled_table[tlb_cpu_id][set][way].entangled_addr[k],
                                        tlb_entangled_table[tlb_cpu_id][set][way].format)
               == entangled_addr) {
      return true;
    }
  }
  // Check for availability
  uint32_t min_format = tlb_get_format_entangled(page_addr, entangled_addr);
  uint32_t num_valid = 1;
  for (uint32_t k = 0; k < TLB_MAX_ENTANGLED_PER_LINE; k++) {
    if (tlb_entangled_table[tlb_cpu_id][set][way].entangled_conf[k] >= TLB_CONFIDENCE_COUNTER_THRESHOLD) {
      num_valid++;
      uint32_t format_k =
          tlb_get_format_entangled(page_addr, tlb_extend_format_entangled(page_addr, tlb_entangled_table[tlb_cpu_id][set][way].entangled_addr[k],
                                                                            tlb_entangled_table[tlb_cpu_id][set][way].format));
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

void tlb_add_bbsize_table(uint64_t page_addr, uint32_t bb_size)
{
  uint64_t tag = (tlb_hash(page_addr) >> TLB_ENTANGLED_TABLE_INDEX_BITS) & TLB_TAG_MASK;
  uint32_t set = tlb_hash(page_addr) % TLB_ENTANGLED_TABLE_SETS;
  uint32_t way = tlb_get_way_entangled_table(page_addr);
  if (way == TLB_ENTANGLED_TABLE_WAYS) {
    tlb_try_realocate_evicted_in_available_entangled_table(set);
    way = tlb_entangled_fifo[tlb_cpu_id][set];
    tlb_entangled_table[tlb_cpu_id][set][way].tag = tag;
    tlb_entangled_table[tlb_cpu_id][set][way].format = 1;
    for (uint32_t k = 0; k < TLB_MAX_ENTANGLED_PER_LINE; k++) {
      tlb_entangled_table[tlb_cpu_id][set][way].entangled_addr[k] = 0;
      tlb_entangled_table[tlb_cpu_id][set][way].entangled_conf[k] = 0;
    }
    tlb_entangled_table[tlb_cpu_id][set][way].bb_size = 0;
    tlb_entangled_fifo[tlb_cpu_id][set] = (tlb_entangled_fifo[tlb_cpu_id][set] + 1) % TLB_ENTANGLED_TABLE_WAYS;
  }
  if (bb_size > tlb_entangled_table[tlb_cpu_id][set][way].bb_size) {
    tlb_entangled_table[tlb_cpu_id][set][way].bb_size = bb_size & TLB_MERGE_BBSIZE_MAX_VALUE;
  }
  if (bb_size > tlb_stats_max_bb_size) {
    tlb_stats_max_bb_size = bb_size;
  }
}

uint64_t tlb_get_entangled_addr_entangled_table(uint64_t page_addr, uint32_t index_k, uint32_t& set, uint32_t& way)
{
  set = tlb_hash(page_addr) % TLB_ENTANGLED_TABLE_SETS;
  way = tlb_get_way_entangled_table(page_addr);
  if (way < TLB_ENTANGLED_TABLE_WAYS) {
    if (tlb_entangled_table[tlb_cpu_id][set][way].entangled_conf[index_k] >= TLB_CONFIDENCE_COUNTER_THRESHOLD) {
      return tlb_extend_format_entangled(page_addr, tlb_entangled_table[tlb_cpu_id][set][way].entangled_addr[index_k],
                                          tlb_entangled_table[tlb_cpu_id][set][way].format);
    }
  }
  return 0;
}

uint32_t tlb_get_bbsize_entangled_table(uint64_t page_addr)
{
  uint32_t set = tlb_hash(page_addr) % TLB_ENTANGLED_TABLE_SETS;
  uint32_t way = tlb_get_way_entangled_table(page_addr);
  if (way < TLB_ENTANGLED_TABLE_WAYS) {
    return tlb_entangled_table[tlb_cpu_id][set][way].bb_size;
  }
  return 0;
}

void tlb_update_confidence_entangled_table(uint32_t set, uint32_t way, uint64_t entangled_addr, bool accessed)
{
  if (way < TLB_ENTANGLED_TABLE_WAYS) {
    for (uint32_t k = 0; k < TLB_MAX_ENTANGLED_PER_LINE; k++) {
      if (tlb_entangled_table[tlb_cpu_id][set][way].entangled_conf[k] >= TLB_CONFIDENCE_COUNTER_THRESHOLD
          && tlb_compress_format_entangled(tlb_entangled_table[tlb_cpu_id][set][way].entangled_addr[k], tlb_entangled_table[tlb_cpu_id][set][way].format)
                 == tlb_compress_format_entangled(entangled_addr, tlb_entangled_table[tlb_cpu_id][set][way].format)) {
        if (accessed && tlb_entangled_table[tlb_cpu_id][set][way].entangled_conf[k] < TLB_CONFIDENCE_COUNTER_MAX_VALUE) {
          tlb_entangled_table[tlb_cpu_id][set][way].entangled_conf[k]++;
        }
        if (!accessed && tlb_entangled_table[tlb_cpu_id][set][way].entangled_conf[k] > 0) {
          tlb_entangled_table[tlb_cpu_id][set][way].entangled_conf[k]--;
        }
      }
    }
  }
}

// INTERFACE

void CACHE::prefetcher_initialize()
{
  cout << "CPU " << cpu << " Entangling TLB prefetcher" << endl;

  tlb_cpu_id = cpu;
  tlb_init_stats_table();
  tlb_last_basic_block = 0;
  tlb_consecutive_count = 0;
  tlb_basic_block_merge_diff = 0;
  tlb_current_cycle = current_cycle;

  tlb_init_hist_table();
  tlb_init_timing_tables();
  tlb_init_entangled_table();
}

// (uint64_t addr, uint64_t ip, uint8_t cache_hit, bool useful_prefetch, uint8_t type, uint32_t metadata_in)
// (uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type, uint32_t metadata_in)
uint32_t CACHE::prefetcher_cache_operate(uint64_t v_addr, uint64_t ip, uint8_t cache_hit, uint8_t prefetch_hit, uint32_t metadata_in)
{
//   cerr << "notify" << endl;
  tlb_cpu_id = cpu;
  tlb_current_cycle = current_cycle;
  uint64_t page_addr = v_addr >> LOG2_PAGE_SIZE;

  if (!cache_hit)
    assert(!prefetch_hit);
  if (!cache_hit)
    assert(tlb_find_timing_cache_entry(page_addr) == TLB_MAX_NUM_WAY);
  if (cache_hit)
    assert(tlb_find_timing_cache_entry(page_addr) < TLB_MAX_NUM_WAY);

  tlb_stats_table[cpu][(page_addr & TLB_STATS_TABLE_MASK)].accesses++;
  if (!cache_hit) {
    tlb_stats_table[cpu][(page_addr & TLB_STATS_TABLE_MASK)].misses++;
    if (tlb_ongoing_request(page_addr) && !tlb_is_accessed_timing_entry(page_addr)) {
      tlb_stats_table[cpu][(page_addr & TLB_STATS_TABLE_MASK)].late++;
    }
  }
  if (prefetch_hit) {
    tlb_stats_table[cpu][(page_addr & TLB_STATS_TABLE_MASK)].hits++;
  }

  bool consecutive = false;

  if (tlb_last_basic_block + tlb_consecutive_count == page_addr) { // Same
    return metadata_in;
  } else if (tlb_last_basic_block + tlb_consecutive_count + 1 == page_addr) { // Consecutive
    tlb_consecutive_count++;
    consecutive = true;
  }

  // Queue basic block prefetches
  uint32_t bb_size = tlb_get_bbsize_entangled_table(page_addr);
  if (bb_size)
    tlb_stats_basic_blocks[bb_size]++;
  for (uint32_t i = 1; i <= bb_size; i++) {
    uint64_t pf_addr = v_addr + i * (1 << LOG2_PAGE_SIZE);
    if (!tlb_ongoing_request(pf_addr >> LOG2_PAGE_SIZE)) {
      if (prefetch_line(pf_addr, true, metadata_in)) {
        tlb_add_timing_entry(pf_addr >> LOG2_PAGE_SIZE, 0, TLB_ENTANGLED_TABLE_WAYS);
      }
    }
  }

  // Queue entangled and basic block of entangled prefetches
  uint32_t num_entangled = 0;
  for (uint32_t k = 0; k < TLB_MAX_ENTANGLED_PER_LINE; k++) {
    uint32_t source_set = 0;
    uint32_t source_way = TLB_ENTANGLED_TABLE_WAYS;
    uint64_t entangled_page_addr = tlb_get_entangled_addr_entangled_table(page_addr, k, source_set, source_way);
    if (entangled_page_addr && (entangled_page_addr != page_addr)) {
      num_entangled++;
      uint32_t bb_size = tlb_get_bbsize_entangled_table(entangled_page_addr);
      if (bb_size)
        tlb_stats_basic_blocks_ent[bb_size]++;
      for (uint32_t i = 0; i <= bb_size; i++) {
        uint64_t pf_page_addr = entangled_page_addr + i;
        if (!tlb_ongoing_request(pf_page_addr)) {
          if (prefetch_line(pf_page_addr << LOG2_PAGE_SIZE, true, metadata_in)) {
            tlb_add_timing_entry(pf_page_addr, source_set, (i == 0) ? source_way : TLB_ENTANGLED_TABLE_WAYS);
          }
        }
      }
    }
  }
  if (num_entangled)
    tlb_stats_entangled[num_entangled]++;

  if (!consecutive) { // New basic block found
    uint32_t max_bb_size = tlb_get_bbsize_entangled_table(tlb_last_basic_block);

    // Check for merging bb opportunities
    if (tlb_consecutive_count) { // single blocks no need to merge and are not inserted in the entangled table
      if (tlb_basic_block_merge_diff > 0) {
        tlb_add_bbsize_table(tlb_last_basic_block - tlb_basic_block_merge_diff, tlb_consecutive_count + tlb_basic_block_merge_diff);
        tlb_add_bb_size_hist_table(tlb_last_basic_block - tlb_basic_block_merge_diff, tlb_consecutive_count + tlb_basic_block_merge_diff);
      } else {
        tlb_add_bbsize_table(tlb_last_basic_block, max(max_bb_size, tlb_consecutive_count));
        tlb_add_bb_size_hist_table(tlb_last_basic_block, max(max_bb_size, tlb_consecutive_count));
      }
    }
  }

  if (!consecutive) { // New basic block found
    tlb_consecutive_count = 0;
    tlb_last_basic_block = page_addr;
  }

  if (!consecutive) {
    tlb_basic_block_merge_diff = tlb_find_bb_merge_hist_table(tlb_last_basic_block);
  }

  // Add the request in the history buffer
  uint32_t pos_hist = TLB_HIST_TABLE_ENTRIES;
  if (!consecutive && tlb_basic_block_merge_diff == 0) {
    if ((tlb_find_hist_entry(page_addr) == TLB_HIST_TABLE_ENTRIES)) {
      pos_hist = tlb_add_hist_table(page_addr);
    } else {
      if (!cache_hit && !tlb_ongoing_accessed_request(page_addr)) {
        pos_hist = tlb_add_hist_table(page_addr);
      }
    }
  }

  // Add miss in the latency table
  if (!cache_hit && !tlb_ongoing_request(page_addr)) {
    tlb_add_timing_entry(page_addr, 0, TLB_ENTANGLED_TABLE_WAYS);
  }

  uint32_t source_set = 0;
  uint32_t source_way = TLB_ENTANGLED_TABLE_WAYS;
  tlb_access_timing_entry(page_addr, pos_hist, source_set, source_way);

  // Update confidence if late
  if (source_way < TLB_ENTANGLED_TABLE_WAYS) {
    tlb_update_confidence_entangled_table(source_set, source_way, page_addr, false);
  }
  return metadata_in;
}

void CACHE::prefetcher_cycle_operate()
{
  if (!tlb_all_warmed_up && all_warmup_complete > NUM_CPUS) {
    tlb_init_stats_table();
    tlb_all_warmed_up = true;
  }
}

// impl_prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in)
uint32_t CACHE::prefetcher_cache_fill(uint64_t v_addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_v_addr, uint32_t metadata_in)
{
  tlb_cpu_id = cpu;
  tlb_current_cycle = current_cycle;
  uint64_t page_addr = (v_addr >> LOG2_PAGE_SIZE);
  uint64_t evicted_page_addr = (evicted_v_addr >> LOG2_PAGE_SIZE);
  // Line is in cache
  if (evicted_v_addr) {
    uint32_t source_set = 0;
    uint32_t source_way = TLB_ENTANGLED_TABLE_WAYS;
    bool accessed = tlb_invalid_timing_cache_entry(evicted_page_addr, source_set, source_way);
    if (!accessed) {
      tlb_stats_table[cpu][(evicted_page_addr & TLB_STATS_TABLE_MASK)].wrong++;
    }
    if (source_way < TLB_ENTANGLED_TABLE_WAYS) {
      // If accessed hit, but if not wrong
      tlb_update_confidence_entangled_table(source_set, source_way, evicted_page_addr, accessed);
    }
  }

  uint32_t pos_hist = TLB_HIST_TABLE_ENTRIES;
  uint64_t latency = tlb_get_latency_timing_mshr(page_addr, pos_hist);

  tlb_move_timing_entry(page_addr);

  // Get and update entangled
  if (latency && pos_hist < TLB_HIST_TABLE_ENTRIES) {
    bool inserted = false;
    for (uint32_t i = 0; i < TLB_TRIES_AVAIL_ENTANGLED; i++) {
      uint64_t src_entangled = tlb_get_src_entangled_hist_table(page_addr, pos_hist, latency, i);
      if (src_entangled && page_addr != src_entangled) {
        if (tlb_avail_entangled_table(src_entangled, page_addr, false)) {
          tlb_add_entangled_table(src_entangled, page_addr);
          inserted = true;
          break;
        }
      }
    }
    if (!inserted) {
      uint64_t src_entangled = tlb_get_src_entangled_hist_table(page_addr, pos_hist, latency);
      if (src_entangled && page_addr != src_entangled) {
        tlb_add_entangled_table(src_entangled, page_addr);
      }
    }
  }
  return metadata_in;
}

void CACHE::prefetcher_final_stats()
{
  cout << "CPU " << cpu << " TLB Entangling prefetcher final stats" << endl;
  tlb_print_stats_table();
}
