/*
 * Copyright (c) 2001, 2018, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef SHARE_VM_GC_G1_G1COLLECTEDHEAP_HPP
#define SHARE_VM_GC_G1_G1COLLECTEDHEAP_HPP

#include "gc/g1/evacuationInfo.hpp"
#include "gc/g1/g1BarrierSet.hpp"
#include "gc/g1/g1BiasedArray.hpp"
#include "gc/g1/g1CardTable.hpp"
#include "gc/g1/g1CollectionSet.hpp"
#include "gc/g1/g1CollectorState.hpp"
#include "gc/g1/g1ConcurrentMark.hpp"
#include "gc/g1/g1EdenRegions.hpp"
#include "gc/g1/g1EvacFailure.hpp"
#include "gc/g1/g1EvacStats.hpp"
#include "gc/g1/g1GCPhaseTimes.hpp"
#include "gc/g1/g1HeapTransition.hpp"
#include "gc/g1/g1HeapVerifier.hpp"
#include "gc/g1/g1HRPrinter.hpp"
#include "gc/g1/g1InCSetState.hpp"
#include "gc/g1/g1MonitoringSupport.hpp"
#include "gc/g1/g1SurvivorRegions.hpp"
#include "gc/g1/g1YCTypes.hpp"
#include "gc/g1/heapRegionManager.hpp"
#include "gc/g1/heapRegionSet.hpp"
#include "gc/g1/heterogeneousHeapRegionManager.hpp"
#include "gc/shared/barrierSet.hpp"
#include "gc/shared/collectedHeap.hpp"
#include "gc/shared/gcHeapSummary.hpp"
#include "gc/shared/plab.hpp"
#include "gc/shared/preservedMarks.hpp"
#include "gc/shared/softRefPolicy.hpp"
#include "memory/memRegion.hpp"
#include "utilities/stack.hpp"

// Haoran: Modify
// #include "gc/g1/g1ConcurrentPrefetch.hpp"
// #include "gc/g1/g1ConcurrentPrefetchThread.hpp"
class G1ConcurrentPrefetch;
class G1ConcurrentPrefetchThread;



// A "G1CollectedHeap" is an implementation of a java heap for HotSpot.
// It uses the "Garbage First" heap organization and algorithm, which
// may combine concurrent marking with parallel, incremental compaction of
// heap subsets that will yield large amounts of garbage.

// Forward declarations
class HeapRegion;
class GenerationSpec;
class G1ParScanThreadState;
class G1ParScanThreadStateSet;
class G1ParScanThreadState;
class MemoryPool;
class MemoryManager;
class ObjectClosure;
class SpaceClosure;
class CompactibleSpaceClosure;
class Space;
class G1CollectionSet;
class G1CollectorPolicy;
class G1Policy;
class G1HotCardCache;
class G1RemSet;
class G1YoungRemSetSamplingThread;
class HeapRegionRemSetIterator;
class G1ConcurrentMark;
class G1ConcurrentMarkThread;
class G1ConcurrentRefine;
class GenerationCounters;
class STWGCTimer;
class G1NewTracer;
class EvacuationFailedInfo;
class nmethod;
class WorkGang;
class G1Allocator;
class G1ArchiveAllocator;
class G1FullGCScope;
class G1HeapVerifier;
class G1HeapSizingPolicy;
class G1HeapSummary;
class G1EvacSummary;

typedef OverflowTaskQueue<StarTask, mtGC>         RefToScanQueue;
typedef GenericTaskQueueSet<RefToScanQueue, mtGC> RefToScanQueueSet;

typedef int RegionIdx_t;   // needs to hold [ 0..max_regions() )
typedef int CardIdx_t;     // needs to hold [ 0..CardsPerRegion )

// The G1 STW is alive closure.
// An instance is embedded into the G1CH and used as the
// (optional) _is_alive_non_header closure in the STW
// reference processor. It is also extensively used during
// reference processing during STW evacuation pauses.
class G1STWIsAliveClosure : public BoolObjectClosure {
  G1CollectedHeap* _g1h;
public:
  G1STWIsAliveClosure(G1CollectedHeap* g1h) : _g1h(g1h) {}
  bool do_object_b(oop p);
};

class G1STWSubjectToDiscoveryClosure : public BoolObjectClosure {
  G1CollectedHeap* _g1h;
public:
  G1STWSubjectToDiscoveryClosure(G1CollectedHeap* g1h) : _g1h(g1h) {}
  bool do_object_b(oop p);
};

class G1RegionMappingChangedListener : public G1MappingChangedListener {
 private:
  void reset_from_card_cache(uint start_idx, size_t num_regions);
 public:
  virtual void on_commit(uint start_idx, size_t num_regions, bool zero_filled);
};

class G1CollectedHeap : public CollectedHeap {
  friend class G1FreeCollectionSetTask;
  friend class VM_CollectForMetadataAllocation;
  friend class VM_G1CollectForAllocation;
  friend class VM_G1CollectFull;
  friend class VMStructs;
  friend class MutatorAllocRegion;
  friend class G1FullCollector;
  friend class G1GCAllocRegion;
  friend class G1HeapVerifier;
  friend class G1CMTask;
  friend class G1ConcurrentMark;

  // Closures used in implementation.
  friend class G1ParScanThreadState;
  friend class G1ParScanThreadStateSet;
  friend class G1ParTask;
  friend class G1PLABAllocator;
  friend class G1PrepareCompactClosure;

  // Other related classes.
  friend class HeapRegionClaimer;

  // Testing classes.
  friend class G1CheckCSetFastTableClosure;

public:
  size_t _remark_reclaimed_bytes;
  struct epoch_struct* user_buf;
private:
  uint _have_done;
  G1YoungRemSetSamplingThread* _young_gen_sampling_thread;

  WorkGang* _workers;
  G1CollectorPolicy* _collector_policy;
  G1CardTable* _card_table;

  SoftRefPolicy      _soft_ref_policy;

  static size_t _humongous_object_threshold_in_words;

  // These sets keep track of old, archive and humongous regions respectively.
  HeapRegionSet _old_set;
  HeapRegionSet _archive_set;
  HeapRegionSet _humongous_set;

  void eagerly_reclaim_humongous_regions();
  // Start a new incremental collection set for the next pause.
  void start_new_collection_set();

  // The block offset table for the G1 heap.
  G1BlockOffsetTable* _bot;

  // Tears down the region sets / lists so that they are empty and the
  // regions on the heap do not belong to a region set / list. The
  // only exception is the humongous set which we leave unaltered. If
  // free_list_only is true, it will only tear down the master free
  // list. It is called before a Full GC (free_list_only == false) or
  // before heap shrinking (free_list_only == true).
  void tear_down_region_sets(bool free_list_only);

  // Rebuilds the region sets / lists so that they are repopulated to
  // reflect the contents of the heap. The only exception is the
  // humongous set which was not torn down in the first place. If
  // free_list_only is true, it will only rebuild the master free
  // list. It is called after a Full GC (free_list_only == false) or
  // after heap shrinking (free_list_only == true).
  void rebuild_region_sets(bool free_list_only);

  // Callback for region mapping changed events.
  G1RegionMappingChangedListener _listener;

  // The sequence of all heap regions in the heap.
  HeapRegionManager* _hrm;

  // Manages all allocations with regions except humongous object allocations.
  G1Allocator* _allocator;

  // Manages all heap verification.
  G1HeapVerifier* _verifier;

  // Outside of GC pauses, the number of bytes used in all regions other
  // than the current allocation region(s).
  size_t _summary_bytes_used;

  void increase_used(size_t bytes);
  void decrease_used(size_t bytes);

  void set_used(size_t bytes);

  // Class that handles archive allocation ranges.
  G1ArchiveAllocator* _archive_allocator;

  // GC allocation statistics policy for survivors.
  G1EvacStats _survivor_evac_stats;

  // GC allocation statistics policy for tenured objects.
  G1EvacStats _old_evac_stats;

  // It specifies whether we should attempt to expand the heap after a
  // region allocation failure. If heap expansion fails we set this to
  // false so that we don't re-attempt the heap expansion (it's likely
  // that subsequent expansion attempts will also fail if one fails).
  // Currently, it is only consulted during GC and it's reset at the
  // start of each GC.
  bool _expand_heap_after_alloc_failure;

  // Helper for monitoring and management support.
  G1MonitoringSupport* _g1mm;

  // Records whether the region at the given index is (still) a
  // candidate for eager reclaim.  Only valid for humongous start
  // regions; other regions have unspecified values.  Humongous start
  // regions are initialized at start of collection pause, with
  // candidates removed from the set as they are found reachable from
  // roots or the young generation.
  class HumongousReclaimCandidates : public G1BiasedMappedArray<bool> {
   protected:
    bool default_value() const { return false; }
   public:
    void clear() { G1BiasedMappedArray<bool>::clear(); }
    void set_candidate(uint region, bool value) {
      set_by_index(region, value);
    }
    bool is_candidate(uint region) {
      return get_by_index(region);
    }
  };

  HumongousReclaimCandidates _humongous_reclaim_candidates;
  // Stores whether during humongous object registration we found candidate regions.
  // If not, we can skip a few steps.
  bool _has_humongous_reclaim_candidates;

  G1HRPrinter _hr_printer;

  // It decides whether an explicit GC should start a concurrent cycle
  // instead of doing a STW GC. Currently, a concurrent cycle is
  // explicitly started if:
  // (a) cause == _gc_locker and +GCLockerInvokesConcurrent, or
  // (b) cause == _g1_humongous_allocation
  // (c) cause == _java_lang_system_gc and +ExplicitGCInvokesConcurrent.
  // (d) cause == _dcmd_gc_run and +ExplicitGCInvokesConcurrent.
  // (e) cause == _wb_conc_mark
  bool should_do_concurrent_full_gc(GCCause::Cause cause);

  // Return true if should upgrade to full gc after an incremental one.
  bool should_upgrade_to_full_gc(GCCause::Cause cause);

  // indicates whether we are in young or mixed GC mode
  G1CollectorState _collector_state;

  // Keeps track of how many "old marking cycles" (i.e., Full GCs or
  // concurrent cycles) we have started.
  volatile uint _old_marking_cycles_started;

  // Keeps track of how many "old marking cycles" (i.e., Full GCs or
  // concurrent cycles) we have completed.
  volatile uint _old_marking_cycles_completed;

  // This is a non-product method that is helpful for testing. It is
  // called at the end of a GC and artificially expands the heap by
  // allocating a number of dead regions. This way we can induce very
  // frequent marking cycles and stress the cleanup / concurrent
  // cleanup code more (as all the regions that will be allocated by
  // this method will be found dead by the marking cycle).
  void allocate_dummy_regions() PRODUCT_RETURN;

  // If the HR printer is active, dump the state of the regions in the
  // heap after a compaction.
  void print_hrm_post_compaction();

  // Create a memory mapper for auxiliary data structures of the given size and
  // translation factor.
  static G1RegionToSpaceMapper* create_aux_memory_mapper(const char* description,
                                                         size_t size,
                                                         size_t translation_factor);

  void trace_heap(GCWhen::Type when, const GCTracer* tracer);

  // These are macros so that, if the assert fires, we get the correct
  // line number, file, etc.

#define heap_locking_asserts_params(_extra_message_)                          \
  "%s : Heap_lock locked: %s, at safepoint: %s, is VM thread: %s",            \
  (_extra_message_),                                                          \
  BOOL_TO_STR(Heap_lock->owned_by_self()),                                    \
  BOOL_TO_STR(SafepointSynchronize::is_at_safepoint()),                       \
  BOOL_TO_STR(Thread::current()->is_VM_thread())

#define assert_heap_locked()                                                  \
  do {                                                                        \
    assert(Heap_lock->owned_by_self(),                                        \
           heap_locking_asserts_params("should be holding the Heap_lock"));   \
  } while (0)

#define assert_heap_locked_or_at_safepoint(_should_be_vm_thread_)             \
  do {                                                                        \
    assert(Heap_lock->owned_by_self() ||                                      \
           (SafepointSynchronize::is_at_safepoint() &&                        \
             ((_should_be_vm_thread_) == Thread::current()->is_VM_thread())), \
           heap_locking_asserts_params("should be holding the Heap_lock or "  \
                                        "should be at a safepoint"));         \
  } while (0)

#define assert_heap_locked_and_not_at_safepoint()                             \
  do {                                                                        \
    assert(Heap_lock->owned_by_self() &&                                      \
                                    !SafepointSynchronize::is_at_safepoint(), \
          heap_locking_asserts_params("should be holding the Heap_lock and "  \
                                       "should not be at a safepoint"));      \
  } while (0)

#define assert_heap_not_locked()                                              \
  do {                                                                        \
    assert(!Heap_lock->owned_by_self(),                                       \
        heap_locking_asserts_params("should not be holding the Heap_lock"));  \
  } while (0)

#define assert_heap_not_locked_and_not_at_safepoint()                         \
  do {                                                                        \
    assert(!Heap_lock->owned_by_self() &&                                     \
                                    !SafepointSynchronize::is_at_safepoint(), \
      heap_locking_asserts_params("should not be holding the Heap_lock and "  \
                                   "should not be at a safepoint"));          \
  } while (0)

#define assert_at_safepoint_on_vm_thread()                                    \
  do {                                                                        \
    assert_at_safepoint();                                                    \
    assert(Thread::current_or_null() != NULL, "no current thread");           \
    assert(Thread::current()->is_VM_thread(), "current thread is not VM thread"); \
  } while (0)

  // The young region list.
  G1EdenRegions _eden;
  G1SurvivorRegions _survivor;

  STWGCTimer* _gc_timer_stw;

  G1NewTracer* _gc_tracer_stw;

  // The current policy object for the collector.
  G1Policy* _g1_policy;
  G1HeapSizingPolicy* _heap_sizing_policy;

  G1CollectionSet _collection_set;

  // Try to allocate a single non-humongous HeapRegion sufficient for
  // an allocation of the given word_size. If do_expand is true,
  // attempt to expand the heap if necessary to satisfy the allocation
  // request. 'type' takes the type of region to be allocated. (Use constants
  // Old, Eden, Humongous, Survivor defined in HeapRegionType.)
  HeapRegion* new_region(size_t word_size, HeapRegionType type, bool do_expand);

  // Initialize a contiguous set of free regions of length num_regions
  // and starting at index first so that they appear as a single
  // humongous region.
  HeapWord* humongous_obj_allocate_initialize_regions(uint first,
                                                      uint num_regions,
                                                      size_t word_size);

  // Attempt to allocate a humongous object of the given size. Return
  // NULL if unsuccessful.
  HeapWord* humongous_obj_allocate(size_t word_size);

  // The following two methods, allocate_new_tlab() and
  // mem_allocate(), are the two main entry points from the runtime
  // into the G1's allocation routines. They have the following
  // assumptions:
  //
  // * They should both be called outside safepoints.
  //
  // * They should both be called without holding the Heap_lock.
  //
  // * All allocation requests for new TLABs should go to
  //   allocate_new_tlab().
  //
  // * All non-TLAB allocation requests should go to mem_allocate().
  //
  // * If either call cannot satisfy the allocation request using the
  //   current allocating region, they will try to get a new one. If
  //   this fails, they will attempt to do an evacuation pause and
  //   retry the allocation.
  //
  // * If all allocation attempts fail, even after trying to schedule
  //   an evacuation pause, allocate_new_tlab() will return NULL,
  //   whereas mem_allocate() will attempt a heap expansion and/or
  //   schedule a Full GC.
  //
  // * We do not allow humongous-sized TLABs. So, allocate_new_tlab
  //   should never be called with word_size being humongous. All
  //   humongous allocation requests should go to mem_allocate() which
  //   will satisfy them with a special path.

  virtual HeapWord* allocate_new_tlab(size_t min_size,
                                      size_t requested_size,
                                      size_t* actual_size);

  virtual HeapWord* mem_allocate(size_t word_size,
                                 bool*  gc_overhead_limit_was_exceeded);

  // First-level mutator allocation attempt: try to allocate out of
  // the mutator alloc region without taking the Heap_lock. This
  // should only be used for non-humongous allocations.
  inline HeapWord* attempt_allocation(size_t min_word_size,
                                      size_t desired_word_size,
                                      size_t* actual_word_size);

  // Second-level mutator allocation attempt: take the Heap_lock and
  // retry the allocation attempt, potentially scheduling a GC
  // pause. This should only be used for non-humongous allocations.
  HeapWord* attempt_allocation_slow(size_t word_size);

  // Takes the Heap_lock and attempts a humongous allocation. It can
  // potentially schedule a GC pause.
  HeapWord* attempt_allocation_humongous(size_t word_size);

  // Allocation attempt that should be called during safepoints (e.g.,
  // at the end of a successful GC). expect_null_mutator_alloc_region
  // specifies whether the mutator alloc region is expected to be NULL
  // or not.
  HeapWord* attempt_allocation_at_safepoint(size_t word_size,
                                            bool expect_null_mutator_alloc_region);

  // These methods are the "callbacks" from the G1AllocRegion class.

  // For mutator alloc regions.
  HeapRegion* new_mutator_alloc_region(size_t word_size, bool force);
  void retire_mutator_alloc_region(HeapRegion* alloc_region,
                                   size_t allocated_bytes);

  // For GC alloc regions.
  bool has_more_regions(InCSetState dest);
  HeapRegion* new_gc_alloc_region(size_t word_size, InCSetState dest);
  void retire_gc_alloc_region(HeapRegion* alloc_region,
                              size_t allocated_bytes, InCSetState dest);

  // - if explicit_gc is true, the GC is for a System.gc() etc,
  //   otherwise it's for a failed allocation.
  // - if clear_all_soft_refs is true, all soft references should be
  //   cleared during the GC.
  // - it returns false if it is unable to do the collection due to the
  //   GC locker being active, true otherwise.
  bool do_full_collection(bool explicit_gc,
                          bool clear_all_soft_refs);

  // Callback from VM_G1CollectFull operation, or collect_as_vm_thread.
  virtual void do_full_collection(bool clear_all_soft_refs);

  // Callback from VM_G1CollectForAllocation operation.
  // This function does everything necessary/possible to satisfy a
  // failed allocation request (including collection, expansion, etc.)
  HeapWord* satisfy_failed_allocation(size_t word_size,
                                      bool* succeeded);
  // Internal helpers used during full GC to split it up to
  // increase readability.
  void abort_concurrent_cycle();
  void verify_before_full_collection(bool explicit_gc);
  void prepare_heap_for_full_collection();
  void prepare_heap_for_mutators();
  void abort_refinement();
  void verify_after_full_collection();
  void print_heap_after_full_collection(G1HeapTransition* heap_transition);

  // Helper method for satisfy_failed_allocation()
  HeapWord* satisfy_failed_allocation_helper(size_t word_size,
                                             bool do_gc,
                                             bool clear_all_soft_refs,
                                             bool expect_null_mutator_alloc_region,
                                             bool* gc_succeeded);

  // Attempting to expand the heap sufficiently
  // to support an allocation of the given "word_size".  If
  // successful, perform the allocation and return the address of the
  // allocated block, or else "NULL".
  HeapWord* expand_and_allocate(size_t word_size);

  // Process any reference objects discovered.
  void process_discovered_references(G1ParScanThreadStateSet* per_thread_states);

  // If during an initial mark pause we may install a pending list head which is not
  // otherwise reachable ensure that it is marked in the bitmap for concurrent marking
  // to discover.
  void make_pending_list_reachable();

  // Merges the information gathered on a per-thread basis for all worker threads
  // during GC into global variables.
  void merge_per_thread_state_info(G1ParScanThreadStateSet* per_thread_states);
public:
  G1YoungRemSetSamplingThread* sampling_thread() const { return _young_gen_sampling_thread; }

  WorkGang* workers() const { return _workers; }

  G1Allocator* allocator() {
    return _allocator;
  }

  G1HeapVerifier* verifier() {
    return _verifier;
  }

  G1MonitoringSupport* g1mm() {
    assert(_g1mm != NULL, "should have been initialized");
    return _g1mm;
  }

  void resize_heap_if_necessary();

  // Expand the garbage-first heap by at least the given size (in bytes!).
  // Returns true if the heap was expanded by the requested amount;
  // false otherwise.
  // (Rounds up to a HeapRegion boundary.)
  bool expand(size_t expand_bytes, WorkGang* pretouch_workers = NULL, double* expand_time_ms = NULL);

  // Returns the PLAB statistics for a given destination.
  inline G1EvacStats* alloc_buffer_stats(InCSetState dest);

  // Determines PLAB size for a given destination.
  inline size_t desired_plab_sz(InCSetState dest);

  // Do anything common to GC's.
  void gc_prologue(bool full);
  void gc_epilogue(bool full);

  // Does the given region fulfill remembered set based eager reclaim candidate requirements?
  bool is_potential_eager_reclaim_candidate(HeapRegion* r) const;

  // Modify the reclaim candidate set and test for presence.
  // These are only valid for starts_humongous regions.
  inline void set_humongous_reclaim_candidate(uint region, bool value);
  inline bool is_humongous_reclaim_candidate(uint region);

  // Remove from the reclaim candidate set.  Also remove from the
  // collection set so that later encounters avoid the slow path.
  inline void set_humongous_is_live(oop obj);

  // Register the given region to be part of the collection set.
  inline void register_humongous_region_with_cset(uint index);
  // Register regions with humongous objects (actually on the start region) in
  // the in_cset_fast_test table.
  void register_humongous_regions_with_cset();
  // We register a region with the fast "in collection set" test. We
  // simply set to true the array slot corresponding to this region.
  void register_young_region_with_cset(HeapRegion* r) {
    _in_cset_fast_test.set_in_young(r->hrm_index());
  }
  void register_old_region_with_cset(HeapRegion* r) {
    _in_cset_fast_test.set_in_old(r->hrm_index());
  }
  void register_optional_region_with_cset(HeapRegion* r) {
    _in_cset_fast_test.set_optional(r->hrm_index());
  }
  void clear_in_cset(const HeapRegion* hr) {
    _in_cset_fast_test.clear(hr);
  }

  void clear_cset_fast_test() {
    _in_cset_fast_test.clear();
  }

  bool is_user_requested_concurrent_full_gc(GCCause::Cause cause);

  // This is called at the start of either a concurrent cycle or a Full
  // GC to update the number of old marking cycles started.
  void increment_old_marking_cycles_started();

  // This is called at the end of either a concurrent cycle or a Full
  // GC to update the number of old marking cycles completed. Those two
  // can happen in a nested fashion, i.e., we start a concurrent
  // cycle, a Full GC happens half-way through it which ends first,
  // and then the cycle notices that a Full GC happened and ends
  // too. The concurrent parameter is a boolean to help us do a bit
  // tighter consistency checking in the method. If concurrent is
  // false, the caller is the inner caller in the nesting (i.e., the
  // Full GC). If concurrent is true, the caller is the outer caller
  // in this nesting (i.e., the concurrent cycle). Further nesting is
  // not currently supported. The end of this call also notifies
  // the FullGCCount_lock in case a Java thread is waiting for a full
  // GC to happen (e.g., it called System.gc() with
  // +ExplicitGCInvokesConcurrent).
  void increment_old_marking_cycles_completed(bool concurrent);

  uint old_marking_cycles_completed() {
    return _old_marking_cycles_completed;
  }

  G1HRPrinter* hr_printer() { return &_hr_printer; }

  // Allocates a new heap region instance.
  HeapRegion* new_heap_region(uint hrs_index, MemRegion mr);

  // Allocate the highest free region in the reserved heap. This will commit
  // regions as necessary.
  HeapRegion* alloc_highest_free_region();

  // Frees a non-humongous region by initializing its contents and
  // adding it to the free list that's passed as a parameter (this is
  // usually a local list which will be appended to the master free
  // list later). The used bytes of freed regions are accumulated in
  // pre_used. If skip_remset is true, the region's RSet will not be freed
  // up. If skip_hot_card_cache is true, the region's hot card cache will not
  // be freed up. The assumption is that this will be done later.
  // The locked parameter indicates if the caller has already taken
  // care of proper synchronization. This may allow some optimizations.
  void free_region(HeapRegion* hr,
                   FreeRegionList* free_list,
                   bool skip_remset,
                   bool skip_hot_card_cache = false,
                   bool locked = false);

  // It dirties the cards that cover the block so that the post
  // write barrier never queues anything when updating objects on this
  // block. It is assumed (and in fact we assert) that the block
  // belongs to a young region.
  inline void dirty_young_block(HeapWord* start, size_t word_size);

  // Frees a humongous region by collapsing it into individual regions
  // and calling free_region() for each of them. The freed regions
  // will be added to the free list that's passed as a parameter (this
  // is usually a local list which will be appended to the master free
  // list later).
  // The method assumes that only a single thread is ever calling
  // this for a particular region at once.
  void free_humongous_region(HeapRegion* hr,
                             FreeRegionList* free_list);

  // Facility for allocating in 'archive' regions in high heap memory and
  // recording the allocated ranges. These should all be called from the
  // VM thread at safepoints, without the heap lock held. They can be used
  // to create and archive a set of heap regions which can be mapped at the
  // same fixed addresses in a subsequent JVM invocation.
  void begin_archive_alloc_range(bool open = false);

  // Check if the requested size would be too large for an archive allocation.
  bool is_archive_alloc_too_large(size_t word_size);

  // Allocate memory of the requested size from the archive region. This will
  // return NULL if the size is too large or if no memory is available. It
  // does not trigger a garbage collection.
  HeapWord* archive_mem_allocate(size_t word_size);

  // Optionally aligns the end address and returns the allocated ranges in
  // an array of MemRegions in order of ascending addresses.
  void end_archive_alloc_range(GrowableArray<MemRegion>* ranges,
                               size_t end_alignment_in_bytes = 0);

  // Facility for allocating a fixed range within the heap and marking
  // the containing regions as 'archive'. For use at JVM init time, when the
  // caller may mmap archived heap data at the specified range(s).
  // Verify that the MemRegions specified in the argument array are within the
  // reserved heap.
  bool check_archive_addresses(MemRegion* range, size_t count);

  // Commit the appropriate G1 regions containing the specified MemRegions
  // and mark them as 'archive' regions. The regions in the array must be
  // non-overlapping and in order of ascending address.
  bool alloc_archive_regions(MemRegion* range, size_t count, bool open);

  // Insert any required filler objects in the G1 regions around the specified
  // ranges to make the regions parseable. This must be called after
  // alloc_archive_regions, and after class loading has occurred.
  void fill_archive_regions(MemRegion* range, size_t count);

  // For each of the specified MemRegions, uncommit the containing G1 regions
  // which had been allocated by alloc_archive_regions. This should be called
  // rather than fill_archive_regions at JVM init time if the archive file
  // mapping failed, with the same non-overlapping and sorted MemRegion array.
  void dealloc_archive_regions(MemRegion* range, size_t count, bool is_open);

  oop materialize_archived_object(oop obj);

private:

  // Shrink the garbage-first heap by at most the given size (in bytes!).
  // (Rounds down to a HeapRegion boundary.)
  void shrink(size_t expand_bytes);
  void shrink_helper(size_t expand_bytes);

  #if TASKQUEUE_STATS
  static void print_taskqueue_stats_hdr(outputStream* const st);
  void print_taskqueue_stats() const;
  void reset_taskqueue_stats();
  #endif // TASKQUEUE_STATS

  // Schedule the VM operation that will do an evacuation pause to
  // satisfy an allocation request of word_size. *succeeded will
  // return whether the VM operation was successful (it did do an
  // evacuation pause) or not (another thread beat us to it or the GC
  // locker was active). Given that we should not be holding the
  // Heap_lock when we enter this method, we will pass the
  // gc_count_before (i.e., total_collections()) as a parameter since
  // it has to be read while holding the Heap_lock. Currently, both
  // methods that call do_collection_pause() release the Heap_lock
  // before the call, so it's easy to read gc_count_before just before.
  HeapWord* do_collection_pause(size_t         word_size,
                                uint           gc_count_before,
                                bool*          succeeded,
                                GCCause::Cause gc_cause);

  void wait_for_root_region_scanning();

  // The guts of the incremental collection pause, executed by the vm
  // thread. It returns false if it is unable to do the collection due
  // to the GC locker being active, true otherwise
  bool do_collection_pause_at_safepoint(double target_pause_time_ms);

  // Actually do the work of evacuating the collection set.
  void evacuate_collection_set(G1ParScanThreadStateSet* per_thread_states);
  void evacuate_optional_collection_set(G1ParScanThreadStateSet* per_thread_states);
  void evacuate_optional_regions(G1ParScanThreadStateSet* per_thread_states, G1OptionalCSet* ocset);

  void pre_evacuate_collection_set();
  void post_evacuate_collection_set(EvacuationInfo& evacuation_info, G1ParScanThreadStateSet* pss);

  // Print the header for the per-thread termination statistics.
  static void print_termination_stats_hdr();
  // Print actual per-thread termination statistics.
  void print_termination_stats(uint worker_id,
                               double elapsed_ms,
                               double strong_roots_ms,
                               double term_ms,
                               size_t term_attempts,
                               size_t alloc_buffer_waste,
                               size_t undo_waste) const;
  // Update object copying statistics.
  void record_obj_copy_mem_stats();

  // The hot card cache for remembered set insertion optimization.
  // Delay the processing of the cards in Hot Card Cache.
  // Because these cards are modified frequently. 
  //
  G1HotCardCache* _hot_card_cache;

  // The g1 remembered set of the heap.
  // [?]a. Univeral RemSet handler ?
  // Each region has a RemSet ？
  //
  G1RemSet* _g1_rem_set;

  // A set of cards that cover the objects for which the Rsets should be updated
  // concurrently after the collection.
  // b. 2 levels Dirty Card queue。
  //    Thread Local dirty card queue, 64 entries ?
  //    Gobal dirty card queue;
  //
  DirtyCardQueueSet _dirty_card_queue_set;

  // Haoran: modify
  BufferNode::Allocator _prefetch_mark_queue_buffer_allocator;
  G1PrefetchQueueSet  _prefetch_queue_set;


  // After a collection pause, convert the regions in the collection set into free
  // regions.
  void free_collection_set(G1CollectionSet* collection_set, EvacuationInfo& evacuation_info, const size_t* surviving_young_words);

  // Abandon the current collection set without recording policy
  // statistics or updating free lists.
  void abandon_collection_set(G1CollectionSet* collection_set);

  // The concurrent marker (and the thread it runs in.)
  G1ConcurrentMark* _cm;
  G1ConcurrentMarkThread* _cm_thread;
  
  // Haoran: modify
  G1ConcurrentPrefetch* _pf;
  G1ConcurrentPrefetchThread* _pf_thread;

  // The concurrent refiner.
  G1ConcurrentRefine* _cr;

  // The parallel task queues
  RefToScanQueueSet *_task_queues;

  // True iff a evacuation has failed in the current collection.
  bool _evacuation_failed;

  EvacuationFailedInfo* _evacuation_failed_info_array;

  // Failed evacuations cause some logical from-space objects to have
  // forwarding pointers to themselves.  Reset them.
  void remove_self_forwarding_pointers();

  // Restore the objects in the regions in the collection set after an
  // evacuation failure.
  void restore_after_evac_failure();

  PreservedMarksSet _preserved_marks_set;

  // Preserve the mark of "obj", if necessary, in preparation for its mark
  // word being overwritten with a self-forwarding-pointer.
  void preserve_mark_during_evac_failure(uint worker_id, oop obj, markOop m);

#ifndef PRODUCT
  // Support for forcing evacuation failures. Analogous to
  // PromotionFailureALot for the other collectors.

  // Records whether G1EvacuationFailureALot should be in effect
  // for the current GC
  bool _evacuation_failure_alot_for_current_gc;

  // Used to record the GC number for interval checking when
  // determining whether G1EvaucationFailureALot is in effect
  // for the current GC.
  size_t _evacuation_failure_alot_gc_number;

  // Count of the number of evacuations between failures.
  volatile size_t _evacuation_failure_alot_count;

  // Set whether G1EvacuationFailureALot should be in effect
  // for the current GC (based upon the type of GC and which
  // command line flags are set);
  inline bool evacuation_failure_alot_for_gc_type(bool for_young_gc,
                                                  bool during_initial_mark,
                                                  bool mark_or_rebuild_in_progress);

  inline void set_evacuation_failure_alot_for_current_gc();

  // Return true if it's time to cause an evacuation failure.
  inline bool evacuation_should_fail();

  // Reset the G1EvacuationFailureALot counters.  Should be called at
  // the end of an evacuation pause in which an evacuation failure occurred.
  inline void reset_evacuation_should_fail();
#endif // !PRODUCT

  // ("Weak") Reference processing support.
  //
  // G1 has 2 instances of the reference processor class. One
  // (_ref_processor_cm) handles reference object discovery
  // and subsequent processing during concurrent marking cycles.
  //
  // The other (_ref_processor_stw) handles reference object
  // discovery and processing during full GCs and incremental
  // evacuation pauses.
  //
  // During an incremental pause, reference discovery will be
  // temporarily disabled for _ref_processor_cm and will be
  // enabled for _ref_processor_stw. At the end of the evacuation
  // pause references discovered by _ref_processor_stw will be
  // processed and discovery will be disabled. The previous
  // setting for reference object discovery for _ref_processor_cm
  // will be re-instated.
  //
  // At the start of marking:
  //  * Discovery by the CM ref processor is verified to be inactive
  //    and it's discovered lists are empty.
  //  * Discovery by the CM ref processor is then enabled.
  //
  // At the end of marking:
  //  * Any references on the CM ref processor's discovered
  //    lists are processed (possibly MT).
  //
  // At the start of full GC we:
  //  * Disable discovery by the CM ref processor and
  //    empty CM ref processor's discovered lists
  //    (without processing any entries).
  //  * Verify that the STW ref processor is inactive and it's
  //    discovered lists are empty.
  //  * Temporarily set STW ref processor discovery as single threaded.
  //  * Temporarily clear the STW ref processor's _is_alive_non_header
  //    field.
  //  * Finally enable discovery by the STW ref processor.
  //
  // The STW ref processor is used to record any discovered
  // references during the full GC.
  //
  // At the end of a full GC we:
  //  * Enqueue any reference objects discovered by the STW ref processor
  //    that have non-live referents. This has the side-effect of
  //    making the STW ref processor inactive by disabling discovery.
  //  * Verify that the CM ref processor is still inactive
  //    and no references have been placed on it's discovered
  //    lists (also checked as a precondition during initial marking).

  // The (stw) reference processor...
  ReferenceProcessor* _ref_processor_stw;

  // During reference object discovery, the _is_alive_non_header
  // closure (if non-null) is applied to the referent object to
  // determine whether the referent is live. If so then the
  // reference object does not need to be 'discovered' and can
  // be treated as a regular oop. This has the benefit of reducing
  // the number of 'discovered' reference objects that need to
  // be processed.
  //
  // Instance of the is_alive closure for embedding into the
  // STW reference processor as the _is_alive_non_header field.
  // Supplying a value for the _is_alive_non_header field is
  // optional but doing so prevents unnecessary additions to
  // the discovered lists during reference discovery.
  G1STWIsAliveClosure _is_alive_closure_stw;

  G1STWSubjectToDiscoveryClosure _is_subject_to_discovery_stw;

  // The (concurrent marking) reference processor...
  ReferenceProcessor* _ref_processor_cm;

  // Instance of the concurrent mark is_alive closure for embedding
  // into the Concurrent Marking reference processor as the
  // _is_alive_non_header field. Supplying a value for the
  // _is_alive_non_header field is optional but doing so prevents
  // unnecessary additions to the discovered lists during reference
  // discovery.
  G1CMIsAliveClosure _is_alive_closure_cm;

  G1CMSubjectToDiscoveryClosure _is_subject_to_discovery_cm;
public:

  RefToScanQueue *task_queue(uint i) const;

  uint num_task_queues() const;

  // A set of cards where updates happened during the GC
  DirtyCardQueueSet& dirty_card_queue_set() { return _dirty_card_queue_set; }
  
  // Haoran: modify
  G1PrefetchQueueSet& prefetch_queue_set() { return _prefetch_queue_set; }

  // Create a G1CollectedHeap with the specified policy.
  // Must call the initialize method afterwards.
  // May not return if something goes wrong.
  G1CollectedHeap(G1CollectorPolicy* policy);

private:
  jint initialize_concurrent_refinement();
  jint initialize_young_gen_sampling_thread();
public:
  // Initialize the G1CollectedHeap to have the initial and
  // maximum sizes and remembered and barrier sets
  // specified by the policy object.
  jint initialize();

  virtual void stop();
  virtual void safepoint_synchronize_begin();
  virtual void safepoint_synchronize_end();

  // Return the (conservative) maximum heap alignment for any G1 heap
  static size_t conservative_max_heap_alignment();

  // Does operations required after initialization has been done.
  void post_initialize();

  // Initialize weak reference processing.
  void ref_processing_init();

  virtual Name kind() const {
    return CollectedHeap::G1;
  }

  virtual const char* name() const {
    return "G1";
  }

  const G1CollectorState* collector_state() const { return &_collector_state; }
  G1CollectorState* collector_state() { return &_collector_state; }

  // The current policy object for the collector.
  G1Policy* g1_policy() const { return _g1_policy; }

  HeapRegionManager* hrm() const { return _hrm; }

  const G1CollectionSet* collection_set() const { return &_collection_set; }
  G1CollectionSet* collection_set() { return &_collection_set; }

  virtual CollectorPolicy* collector_policy() const;
  virtual G1CollectorPolicy* g1_collector_policy() const;

  virtual SoftRefPolicy* soft_ref_policy();

  virtual void initialize_serviceability();
  virtual MemoryUsage memory_usage();
  virtual GrowableArray<GCMemoryManager*> memory_managers();
  virtual GrowableArray<MemoryPool*> memory_pools();

  // The rem set and barrier set.
  G1RemSet* g1_rem_set() const { return _g1_rem_set; }

  // Try to minimize the remembered set.
  void scrub_rem_set();

  // Apply the given closure on all cards in the Hot Card Cache, emptying it.
  void iterate_hcc_closure(CardTableEntryClosure* cl, uint worker_i);

  // Apply the given closure on all cards in the Dirty Card Queue Set, emptying it.
  void iterate_dirty_card_closure(CardTableEntryClosure* cl, uint worker_i);

  // The shared block offset table array.
  G1BlockOffsetTable* bot() const { return _bot; }

  // Reference Processing accessors

  // The STW reference processor....
  ReferenceProcessor* ref_processor_stw() const { return _ref_processor_stw; }

  G1NewTracer* gc_tracer_stw() const { return _gc_tracer_stw; }

  // The Concurrent Marking reference processor...
  ReferenceProcessor* ref_processor_cm() const { return _ref_processor_cm; }

  size_t unused_committed_regions_in_bytes() const;
  virtual size_t capacity() const;
  virtual size_t used() const;
  // This should be called when we're not holding the heap lock. The
  // result might be a bit inaccurate.
  size_t used_unlocked() const;
  size_t recalculate_used() const;

  // These virtual functions do the actual allocation.
  // Some heaps may offer a contiguous region for shared non-blocking
  // allocation, via inlined code (by exporting the address of the top and
  // end fields defining the extent of the contiguous allocation region.)
  // But G1CollectedHeap doesn't yet support this.

  virtual bool is_maximal_no_gc() const {
    return _hrm->available() == 0;
  }

  // Returns whether there are any regions left in the heap for allocation.
  bool has_regions_left_for_allocation() const {
    return !is_maximal_no_gc() || num_free_regions() != 0;
  }

  // The current number of regions in the heap.
  uint num_regions() const { return _hrm->length(); }

  // The max number of regions in the heap.
  uint max_regions() const { return _hrm->max_length(); }

  // Max number of regions that can be comitted.
  uint max_expandable_regions() const { return _hrm->max_expandable_length(); }

  // The number of regions that are completely free.
  uint num_free_regions() const { return _hrm->num_free_regions(); }

  // The number of regions that can be allocated into.
  uint num_free_or_available_regions() const { return num_free_regions() + _hrm->available(); }

  MemoryUsage get_auxiliary_data_memory_usage() const {
    return _hrm->get_auxiliary_data_memory_usage();
  }

  // The number of regions that are not completely free.
  uint num_used_regions() const { return num_regions() - num_free_regions(); }

#ifdef ASSERT
  bool is_on_master_free_list(HeapRegion* hr) {
    return _hrm->is_free(hr);
  }
#endif // ASSERT

  inline void old_set_add(HeapRegion* hr);
  inline void old_set_remove(HeapRegion* hr);

  inline void archive_set_add(HeapRegion* hr);

  size_t non_young_capacity_bytes() {
    return (old_regions_count() + _archive_set.length() + humongous_regions_count()) * HeapRegion::GrainBytes;
  }

  // Determine whether the given region is one that we are using as an
  // old GC alloc region.
  bool is_old_gc_alloc_region(HeapRegion* hr);

  // Perform a collection of the heap; intended for use in implementing
  // "System.gc".  This probably implies as full a collection as the
  // "CollectedHeap" supports.
  virtual void collect(GCCause::Cause cause);

  // Perform a collection of the heap with the given cause; if the VM operation
  // fails to execute for any reason, retry only if retry_on_gc_failure is set.
  // Returns whether this collection actually executed.
  bool try_collect(GCCause::Cause cause, bool retry_on_gc_failure);

  // True iff an evacuation has failed in the most-recent collection.
  bool evacuation_failed() { return _evacuation_failed; }

  void remove_from_old_sets(const uint old_regions_removed, const uint humongous_regions_removed);
  void prepend_to_freelist(FreeRegionList* list);
  void decrement_summary_bytes(size_t bytes);

  virtual bool is_in(const void* p) const;
#ifdef ASSERT
  // Returns whether p is in one of the available areas of the heap. Slow but
  // extensive version.
  bool is_in_exact(const void* p) const;
#endif

  // Return "TRUE" iff the given object address is within the collection
  // set. Assumes that the reference points into the heap.
  inline bool is_in_cset(const HeapRegion *hr);
  inline bool is_in_cset(oop obj);
  inline bool is_in_cset(HeapWord* addr);

  inline bool is_in_cset_or_humongous(const oop obj);

 private:
  // This array is used for a quick test on whether a reference points into
  // the collection set or not. Each of the array's elements denotes whether the
  // corresponding region is in the collection set or not.
  G1InCSetStateFastTestBiasedMappedArray _in_cset_fast_test;

 public:

  inline InCSetState in_cset_state(const oop obj);

  // Return "TRUE" iff the given object address is in the reserved
  // region of g1.
  bool is_in_g1_reserved(const void* p) const {
    return _hrm->reserved().contains(p);
  }

  // Returns a MemRegion that corresponds to the space that has been
  // reserved for the heap
  MemRegion g1_reserved() const {
    return _hrm->reserved();
  }

  virtual bool is_in_closed_subset(const void* p) const;

  G1HotCardCache* g1_hot_card_cache() const { return _hot_card_cache; }

  G1CardTable* card_table() const {
    return _card_table;
  }

  // Iteration functions.

  // Iterate over all objects, calling "cl.do_object" on each.
  virtual void object_iterate(ObjectClosure* cl);

  virtual void safe_object_iterate(ObjectClosure* cl) {
    object_iterate(cl);
  }

  // Iterate over heap regions, in address order, terminating the
  // iteration early if the "do_heap_region" method returns "true".
  void heap_region_iterate(HeapRegionClosure* blk) const;

  // Return the region with the given index. It assumes the index is valid.
  inline HeapRegion* region_at(uint index) const;
  inline HeapRegion* region_at_or_null(uint index) const;

  // Return the next region (by index) that is part of the same
  // humongous object that hr is part of.
  inline HeapRegion* next_region_in_humongous(HeapRegion* hr) const;

  // Calculate the region index of the given address. Given address must be
  // within the heap.
  inline uint addr_to_region(HeapWord* addr) const;

  inline HeapWord* bottom_addr_for_region(uint index) const;

  // Two functions to iterate over the heap regions in parallel. Threads
  // compete using the HeapRegionClaimer to claim the regions before
  // applying the closure on them.
  // The _from_worker_offset version uses the HeapRegionClaimer and
  // the worker id to calculate a start offset to prevent all workers to
  // start from the point.
  void heap_region_par_iterate_from_worker_offset(HeapRegionClosure* cl,
                                                  HeapRegionClaimer* hrclaimer,
                                                  uint worker_id) const;

  void heap_region_par_iterate_from_start(HeapRegionClosure* cl,
                                          HeapRegionClaimer* hrclaimer) const;

  // Iterate over the regions (if any) in the current collection set.
  void collection_set_iterate(HeapRegionClosure* blk);

  // Iterate over the regions (if any) in the current collection set. Starts the
  // iteration over the entire collection set so that the start regions of a given
  // worker id over the set active_workers are evenly spread across the set of
  // collection set regions.
  void collection_set_iterate_from(HeapRegionClosure *blk, uint worker_id);

  // Returns the HeapRegion that contains addr. addr must not be NULL.
  template <class T>
  inline HeapRegion* heap_region_containing(const T addr) const;

  // Returns the HeapRegion that contains addr, or NULL if that is an uncommitted
  // region. addr must not be NULL.
  template <class T>
  inline HeapRegion* heap_region_containing_or_null(const T addr) const;

  // A CollectedHeap is divided into a dense sequence of "blocks"; that is,
  // each address in the (reserved) heap is a member of exactly
  // one block.  The defining characteristic of a block is that it is
  // possible to find its size, and thus to progress forward to the next
  // block.  (Blocks may be of different sizes.)  Thus, blocks may
  // represent Java objects, or they might be free blocks in a
  // free-list-based heap (or subheap), as long as the two kinds are
  // distinguishable and the size of each is determinable.

  // Returns the address of the start of the "block" that contains the
  // address "addr".  We say "blocks" instead of "object" since some heaps
  // may not pack objects densely; a chunk may either be an object or a
  // non-object.
  virtual HeapWord* block_start(const void* addr) const;

  // Requires "addr" to be the start of a chunk, and returns its size.
  // "addr + size" is required to be the start of a new chunk, or the end
  // of the active area of the heap.
  virtual size_t block_size(const HeapWord* addr) const;

  // Requires "addr" to be the start of a block, and returns "TRUE" iff
  // the block is an object.
  virtual bool block_is_obj(const HeapWord* addr) const;

  // Section on thread-local allocation buffers (TLABs)
  // See CollectedHeap for semantics.

  bool supports_tlab_allocation() const;
  size_t tlab_capacity(Thread* ignored) const;
  size_t tlab_used(Thread* ignored) const;
  size_t max_tlab_size() const;
  size_t unsafe_max_tlab_alloc(Thread* ignored) const;

  inline bool is_in_young(const oop obj);

  // Returns "true" iff the given word_size is "very large".
  static bool is_humongous(size_t word_size) {
    // Note this has to be strictly greater-than as the TLABs
    // are capped at the humongous threshold and we want to
    // ensure that we don't try to allocate a TLAB as
    // humongous and that we don't allocate a humongous
    // object in a TLAB.
    return word_size > _humongous_object_threshold_in_words;
  }

  // Returns the humongous threshold for a specific region size
  static size_t humongous_threshold_for(size_t region_size) {
    return (region_size / 2);
  }

  // Returns the number of regions the humongous object of the given word size
  // requires.
  static size_t humongous_obj_size_in_regions(size_t word_size);

  // Print the maximum heap capacity.
  virtual size_t max_capacity() const;

  // Return the size of reserved memory. Returns different value than max_capacity() when AllocateOldGenAt is used.
  virtual size_t max_reserved_capacity() const;

  virtual jlong millis_since_last_gc();


  // Convenience function to be used in situations where the heap type can be
  // asserted to be this type.
  static G1CollectedHeap* heap();

  void set_region_short_lived_locked(HeapRegion* hr);
  // add appropriate methods for any other surv rate groups

  const G1SurvivorRegions* survivor() const { return &_survivor; }

  uint eden_regions_count() const { return _eden.length(); }
  uint survivor_regions_count() const { return _survivor.length(); }
  uint young_regions_count() const { return _eden.length() + _survivor.length(); }
  uint old_regions_count() const { return _old_set.length(); }
  uint archive_regions_count() const { return _archive_set.length(); }
  uint humongous_regions_count() const { return _humongous_set.length(); }

#ifdef ASSERT
  bool check_young_list_empty();
#endif

  // *** Stuff related to concurrent marking.  It's not clear to me that so
  // many of these need to be public.

  // The functions below are helper functions that a subclass of
  // "CollectedHeap" can use in the implementation of its virtual
  // functions.
  // This performs a concurrent marking of the live objects in a
  // bitmap off to the side.
  void do_concurrent_mark();

  bool is_marked_next(oop obj) const;

  // Determine if an object is dead, given the object and also
  // the region to which the object belongs. An object is dead
  // iff a) it was not allocated since the last mark, b) it
  // is not marked, and c) it is not in an archive region.
  bool is_obj_dead(const oop obj, const HeapRegion* hr) const {
    return
      hr->is_obj_dead(obj, _cm->prev_mark_bitmap()) &&
      !hr->is_archive();
  }

  // This function returns true when an object has been
  // around since the previous marking and hasn't yet
  // been marked during this marking, and is not in an archive region.
  bool is_obj_ill(const oop obj, const HeapRegion* hr) const {
    return
      !hr->obj_allocated_since_next_marking(obj) &&
      !is_marked_next(obj) &&
      !hr->is_archive();
  }

  // Determine if an object is dead, given only the object itself.
  // This will find the region to which the object belongs and
  // then call the region version of the same function.

  // Added if it is NULL it isn't dead.

  inline bool is_obj_dead(const oop obj) const;

  inline bool is_obj_ill(const oop obj) const;

  inline bool is_obj_dead_full(const oop obj, const HeapRegion* hr) const;
  inline bool is_obj_dead_full(const oop obj) const;

  G1ConcurrentMark* concurrent_mark() const { return _cm; }
  // Haoran: modify
  G1ConcurrentPrefetch* concurrent_prefetch() const { return _pf; }

  // Refinement

  G1ConcurrentRefine* concurrent_refine() const { return _cr; }

  // Optimized nmethod scanning support routines

  // Is an oop scavengeable
  virtual bool is_scavengable(oop obj);

  // Register the given nmethod with the G1 heap.
  virtual void register_nmethod(nmethod* nm);

  // Unregister the given nmethod from the G1 heap.
  virtual void unregister_nmethod(nmethod* nm);

  // Free up superfluous code root memory.
  void purge_code_root_memory();

  // Rebuild the strong code root lists for each region
  // after a full GC.
  void rebuild_strong_code_roots();

  // Partial cleaning used when class unloading is disabled.
  // Let the caller choose what structures to clean out:
  // - StringTable
  // - StringDeduplication structures
  void partial_cleaning(BoolObjectClosure* is_alive, bool unlink_strings, bool unlink_string_dedup);

  // Complete cleaning used when class unloading is enabled.
  // Cleans out all structures handled by partial_cleaning and also the CodeCache.
  void complete_cleaning(BoolObjectClosure* is_alive, bool class_unloading_occurred);

  // Redirty logged cards in the refinement queue.
  void redirty_logged_cards();
  // Verification

  // Deduplicate the string
  virtual void deduplicate_string(oop str);

  // Perform any cleanup actions necessary before allowing a verification.
  virtual void prepare_for_verify();

  // function paramter enqueu
  // G1CollectedHeap implementation
  virtual void prefetch_enque(JavaThread* jthread, oop obj1, oop obj2, oop obj3, oop obj4, oop obj5, int num_of_valid_param );


  // Perform verification.

  // vo == UsePrevMarking -> use "prev" marking information,
  // vo == UseNextMarking -> use "next" marking information
  // vo == UseFullMarking -> use "next" marking bitmap but no TAMS
  //
  // NOTE: Only the "prev" marking information is guaranteed to be
  // consistent most of the time, so most calls to this should use
  // vo == UsePrevMarking.
  // Currently, there is only one case where this is called with
  // vo == UseNextMarking, which is to verify the "next" marking
  // information at the end of remark.
  // Currently there is only one place where this is called with
  // vo == UseFullMarking, which is to verify the marking during a
  // full GC.
  void verify(VerifyOption vo);

  // WhiteBox testing support.
  virtual bool supports_concurrent_phase_control() const;
  virtual const char* const* concurrent_phases() const;
  virtual bool request_concurrent_phase(const char* phase);

  virtual WorkGang* get_safepoint_workers() { return _workers; }

  // The methods below are here for convenience and dispatch the
  // appropriate method depending on value of the given VerifyOption
  // parameter. The values for that parameter, and their meanings,
  // are the same as those above.

  bool is_obj_dead_cond(const oop obj,
                        const HeapRegion* hr,
                        const VerifyOption vo) const;

  bool is_obj_dead_cond(const oop obj,
                        const VerifyOption vo) const;

  G1HeapSummary create_g1_heap_summary();
  G1EvacSummary create_g1_evac_summary(G1EvacStats* stats);

  // Printing
private:
  void print_heap_regions() const;
  void print_regions_on(outputStream* st) const;

public:
  virtual void print_on(outputStream* st) const;
  virtual void print_extended_on(outputStream* st) const;
  virtual void print_on_error(outputStream* st) const;

  virtual void print_gc_threads_on(outputStream* st) const;
  virtual void gc_threads_do(ThreadClosure* tc) const;

  // Override
  void print_tracing_info() const;

  // The following two methods are helpful for debugging RSet issues.
  void print_cset_rsets() PRODUCT_RETURN;
  void print_all_rsets() PRODUCT_RETURN;

  size_t pending_card_num();
};

class G1ParEvacuateFollowersClosure : public VoidClosure {
private:
  double _start_term;
  double _term_time;
  size_t _term_attempts;

  void start_term_time() { _term_attempts++; _start_term = os::elapsedTime(); }
  void end_term_time() { _term_time += os::elapsedTime() - _start_term; }
protected:
  G1CollectedHeap*              _g1h;
  G1ParScanThreadState*         _par_scan_state;
  RefToScanQueueSet*            _queues;
  ParallelTaskTerminator*       _terminator;
  G1GCPhaseTimes::GCParPhases   _phase;

  G1ParScanThreadState*   par_scan_state() { return _par_scan_state; }
  RefToScanQueueSet*      queues()         { return _queues; }
  ParallelTaskTerminator* terminator()     { return _terminator; }

public:
  G1ParEvacuateFollowersClosure(G1CollectedHeap* g1h,
                                G1ParScanThreadState* par_scan_state,
                                RefToScanQueueSet* queues,
                                ParallelTaskTerminator* terminator,
                                G1GCPhaseTimes::GCParPhases phase)
    : _start_term(0.0), _term_time(0.0), _term_attempts(0),
      _g1h(g1h), _par_scan_state(par_scan_state),
      _queues(queues), _terminator(terminator), _phase(phase) {}

  void do_void();

  double term_time() const { return _term_time; }
  size_t term_attempts() const { return _term_attempts; }

private:
  inline bool offer_termination();
};

#endif // SHARE_VM_GC_G1_G1COLLECTEDHEAP_HPP
