CC := gcc
CXX := g++
CFLAGS := -Wall -O3 -std=gnu99
CXXFLAGS := -Wall -O3 -std=c++17
CPPFLAGS :=  -Iinc -MMD -MP
LDFLAGS := 
LDLIBS := 

.phony: all clean

all: bin/entangling_entangling

clean: 
	$(RM) inc/champsim_constants.h
	$(RM) src/core_inst.cc
	$(RM) inc/cache_modules.inc
	$(RM) inc/ooo_cpu_modules.inc
	 find . -name \*.o -delete
	 find . -name \*.d -delete
	 $(RM) -r obj

	 find replacement/lru -name \*.o -delete
	 find replacement/lru -name \*.d -delete
	 find prefetcher/no -name \*.o -delete
	 find prefetcher/no -name \*.d -delete
	 find prefetcher/entangling -name \*.o -delete
	 find prefetcher/entangling -name \*.d -delete
	 find prefetcher/next_line -name \*.o -delete
	 find prefetcher/next_line -name \*.d -delete
	 find prefetcher/entangling_tlb -name \*.o -delete
	 find prefetcher/entangling_tlb -name \*.d -delete
	 find branch/hashed_perceptron -name \*.o -delete
	 find branch/hashed_perceptron -name \*.d -delete
	 find btb/basic_btb -name \*.o -delete
	 find btb/basic_btb -name \*.d -delete

bin/entangling_entangling: $(patsubst %.cc,%.o,$(wildcard src/*.cc)) obj/repl_rreplacementDlru.a obj/pref_pprefetcherDno.a obj/pref_pprefetcherDentangling.a obj/pref_pprefetcherDnext_line.a obj/pref_pprefetcherDentangling_tlb.a obj/bpred_bbranchDhashed_perceptron.a obj/btb_bbtbDbasic_btb.a
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

replacement/lru/%.o: CFLAGS += -Ireplacement/lru
replacement/lru/%.o: CXXFLAGS += -Ireplacement/lru
replacement/lru/%.o: CXXFLAGS +=  -Dinitialize_replacement=repl_rreplacementDlru_initialize -Dfind_victim=repl_rreplacementDlru_victim -Dupdate_replacement_state=repl_rreplacementDlru_update -Dreplacement_final_stats=repl_rreplacementDlru_final_stats
obj/repl_rreplacementDlru.a: $(patsubst %.cc,%.o,$(wildcard replacement/lru/*.cc)) $(patsubst %.c,%.o,$(wildcard replacement/lru/*.c))
	@mkdir -p $(dir $@)
	ar -rcs $@ $^

prefetcher/no/%.o: CFLAGS += -Iprefetcher/no
prefetcher/no/%.o: CXXFLAGS += -Iprefetcher/no
prefetcher/no/%.o: CXXFLAGS +=  -Dprefetcher_initialize=pref_pprefetcherDno_initialize -Dprefetcher_cache_operate=pref_pprefetcherDno_cache_operate -Dprefetcher_cache_fill=pref_pprefetcherDno_cache_fill -Dprefetcher_cycle_operate=pref_pprefetcherDno_cycle_operate -Dprefetcher_final_stats=pref_pprefetcherDno_final_stats -Dl1d_prefetcher_initialize=pref_pprefetcherDno_initialize -Dl2c_prefetcher_initialize=pref_pprefetcherDno_initialize -Dllc_prefetcher_initialize=pref_pprefetcherDno_initialize -Dl1d_prefetcher_operate=pref_pprefetcherDno_cache_operate -Dl2c_prefetcher_operate=pref_pprefetcherDno_cache_operate -Dllc_prefetcher_operate=pref_pprefetcherDno_cache_operate -Dl1d_prefetcher_cache_fill=pref_pprefetcherDno_cache_fill -Dl2c_prefetcher_cache_fill=pref_pprefetcherDno_cache_fill -Dllc_prefetcher_cache_fill=pref_pprefetcherDno_cache_fill -Dl1d_prefetcher_final_stats=pref_pprefetcherDno_final_stats -Dl2c_prefetcher_final_stats=pref_pprefetcherDno_final_stats -Dllc_prefetcher_final_stats=pref_pprefetcherDno_final_stats
obj/pref_pprefetcherDno.a: $(patsubst %.cc,%.o,$(wildcard prefetcher/no/*.cc)) $(patsubst %.c,%.o,$(wildcard prefetcher/no/*.c))
	@mkdir -p $(dir $@)
	ar -rcs $@ $^

prefetcher/entangling/%.o: CFLAGS += -Iprefetcher/entangling
prefetcher/entangling/%.o: CXXFLAGS += -Iprefetcher/entangling
prefetcher/entangling/%.o: CXXFLAGS +=  -Dprefetcher_initialize=pref_pprefetcherDentangling_initialize -Dprefetcher_branch_operate=pref_pprefetcherDentangling_branch_operate -Dprefetcher_cache_operate=pref_pprefetcherDentangling_cache_operate -Dprefetcher_cycle_operate=pref_pprefetcherDentangling_cycle_operate -Dprefetcher_cache_fill=pref_pprefetcherDentangling_cache_fill -Dprefetcher_final_stats=pref_pprefetcherDentangling_final_stats -Dl1i_prefetcher_initialize=pref_pprefetcherDentangling_initialize -Dl1i_prefetcher_branch_operate=pref_pprefetcherDentangling_branch_operate -Dl1i_prefetcher_cache_operate=pref_pprefetcherDentangling_cache_operate -Dl1i_prefetcher_cycle_operate=pref_pprefetcherDentangling_cycle_operate -Dl1i_prefetcher_cache_fill=pref_pprefetcherDentangling_cache_fill -Dl1i_prefetcher_final_stats=pref_pprefetcherDentangling_final_stats
obj/pref_pprefetcherDentangling.a: $(patsubst %.cc,%.o,$(wildcard prefetcher/entangling/*.cc)) $(patsubst %.c,%.o,$(wildcard prefetcher/entangling/*.c))
	@mkdir -p $(dir $@)
	ar -rcs $@ $^

prefetcher/next_line/%.o: CFLAGS += -Iprefetcher/next_line
prefetcher/next_line/%.o: CXXFLAGS += -Iprefetcher/next_line
prefetcher/next_line/%.o: CXXFLAGS +=  -Dprefetcher_initialize=pref_pprefetcherDnext_line_initialize -Dprefetcher_cache_operate=pref_pprefetcherDnext_line_cache_operate -Dprefetcher_cache_fill=pref_pprefetcherDnext_line_cache_fill -Dprefetcher_cycle_operate=pref_pprefetcherDnext_line_cycle_operate -Dprefetcher_final_stats=pref_pprefetcherDnext_line_final_stats -Dl1d_prefetcher_initialize=pref_pprefetcherDnext_line_initialize -Dl2c_prefetcher_initialize=pref_pprefetcherDnext_line_initialize -Dllc_prefetcher_initialize=pref_pprefetcherDnext_line_initialize -Dl1d_prefetcher_operate=pref_pprefetcherDnext_line_cache_operate -Dl2c_prefetcher_operate=pref_pprefetcherDnext_line_cache_operate -Dllc_prefetcher_operate=pref_pprefetcherDnext_line_cache_operate -Dl1d_prefetcher_cache_fill=pref_pprefetcherDnext_line_cache_fill -Dl2c_prefetcher_cache_fill=pref_pprefetcherDnext_line_cache_fill -Dllc_prefetcher_cache_fill=pref_pprefetcherDnext_line_cache_fill -Dl1d_prefetcher_final_stats=pref_pprefetcherDnext_line_final_stats -Dl2c_prefetcher_final_stats=pref_pprefetcherDnext_line_final_stats -Dllc_prefetcher_final_stats=pref_pprefetcherDnext_line_final_stats
obj/pref_pprefetcherDnext_line.a: $(patsubst %.cc,%.o,$(wildcard prefetcher/next_line/*.cc)) $(patsubst %.c,%.o,$(wildcard prefetcher/next_line/*.c))
	@mkdir -p $(dir $@)
	ar -rcs $@ $^

prefetcher/entangling_tlb/%.o: CFLAGS += -Iprefetcher/entangling_tlb
prefetcher/entangling_tlb/%.o: CXXFLAGS += -Iprefetcher/entangling_tlb
prefetcher/entangling_tlb/%.o: CXXFLAGS +=  -Dprefetcher_initialize=pref_pprefetcherDentangling_tlb_initialize -Dprefetcher_cache_operate=pref_pprefetcherDentangling_tlb_cache_operate -Dprefetcher_cache_fill=pref_pprefetcherDentangling_tlb_cache_fill -Dprefetcher_cycle_operate=pref_pprefetcherDentangling_tlb_cycle_operate -Dprefetcher_final_stats=pref_pprefetcherDentangling_tlb_final_stats -Dl1d_prefetcher_initialize=pref_pprefetcherDentangling_tlb_initialize -Dl2c_prefetcher_initialize=pref_pprefetcherDentangling_tlb_initialize -Dllc_prefetcher_initialize=pref_pprefetcherDentangling_tlb_initialize -Dl1d_prefetcher_operate=pref_pprefetcherDentangling_tlb_cache_operate -Dl2c_prefetcher_operate=pref_pprefetcherDentangling_tlb_cache_operate -Dllc_prefetcher_operate=pref_pprefetcherDentangling_tlb_cache_operate -Dl1d_prefetcher_cache_fill=pref_pprefetcherDentangling_tlb_cache_fill -Dl2c_prefetcher_cache_fill=pref_pprefetcherDentangling_tlb_cache_fill -Dllc_prefetcher_cache_fill=pref_pprefetcherDentangling_tlb_cache_fill -Dl1d_prefetcher_final_stats=pref_pprefetcherDentangling_tlb_final_stats -Dl2c_prefetcher_final_stats=pref_pprefetcherDentangling_tlb_final_stats -Dllc_prefetcher_final_stats=pref_pprefetcherDentangling_tlb_final_stats
obj/pref_pprefetcherDentangling_tlb.a: $(patsubst %.cc,%.o,$(wildcard prefetcher/entangling_tlb/*.cc)) $(patsubst %.c,%.o,$(wildcard prefetcher/entangling_tlb/*.c))
	@mkdir -p $(dir $@)
	ar -rcs $@ $^

branch/hashed_perceptron/%.o: CFLAGS += -Ibranch/hashed_perceptron
branch/hashed_perceptron/%.o: CXXFLAGS += -Ibranch/hashed_perceptron
branch/hashed_perceptron/%.o: CXXFLAGS +=  -Dinitialize_branch_predictor=bpred_bbranchDhashed_perceptron_initialize -Dlast_branch_result=bpred_bbranchDhashed_perceptron_last_result -Dpredict_branch=bpred_bbranchDhashed_perceptron_predict
obj/bpred_bbranchDhashed_perceptron.a: $(patsubst %.cc,%.o,$(wildcard branch/hashed_perceptron/*.cc)) $(patsubst %.c,%.o,$(wildcard branch/hashed_perceptron/*.c))
	@mkdir -p $(dir $@)
	ar -rcs $@ $^

btb/basic_btb/%.o: CFLAGS += -Ibtb/basic_btb
btb/basic_btb/%.o: CXXFLAGS += -Ibtb/basic_btb
btb/basic_btb/%.o: CXXFLAGS +=  -Dinitialize_btb=btb_bbtbDbasic_btb_initialize -Dupdate_btb=btb_bbtbDbasic_btb_update -Dbtb_prediction=btb_bbtbDbasic_btb_predict
obj/btb_bbtbDbasic_btb.a: $(patsubst %.cc,%.o,$(wildcard btb/basic_btb/*.cc)) $(patsubst %.c,%.o,$(wildcard btb/basic_btb/*.c))
	@mkdir -p $(dir $@)
	ar -rcs $@ $^

-include $(wildcard src/*.d)
-include $(wildcard replacement/lru/*.d)
-include $(wildcard prefetcher/no/*.d)
-include $(wildcard prefetcher/entangling/*.d)
-include $(wildcard prefetcher/next_line/*.d)
-include $(wildcard prefetcher/entangling_tlb/*.d)
-include $(wildcard branch/hashed_perceptron/*.d)
-include $(wildcard btb/basic_btb/*.d)

