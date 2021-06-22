// Copyright (c) 2021 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <absl/strings/substitute.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>

#include "GrpcProtos/Constants.h"
#include "MemoryTracingUtils.h"

using orbit_grpc_protos::CGroupMemoryUsage;
using orbit_grpc_protos::kMissingInfo;
using orbit_grpc_protos::ProcessMemoryUsage;
using orbit_grpc_protos::SystemMemoryUsage;

namespace {

void ExpectSystemMemoryUsageEq(const SystemMemoryUsage& system_memory_usage,
                               int total_kb = kMissingInfo, int free_kb = kMissingInfo,
                               int available_kb = kMissingInfo, int buffers_kb = kMissingInfo,
                               int cached_kb = kMissingInfo, int pgfault = kMissingInfo,
                               int pgmajfault = kMissingInfo) {
  EXPECT_EQ(system_memory_usage.total_kb(), total_kb);
  EXPECT_EQ(system_memory_usage.free_kb(), free_kb);
  EXPECT_EQ(system_memory_usage.available_kb(), available_kb);
  EXPECT_EQ(system_memory_usage.buffers_kb(), buffers_kb);
  EXPECT_EQ(system_memory_usage.cached_kb(), cached_kb);
  EXPECT_EQ(system_memory_usage.pgfault(), pgfault);
  EXPECT_EQ(system_memory_usage.pgmajfault(), pgmajfault);
}

void ExpectProcessMemoryUsageEq(const ProcessMemoryUsage& process_memory_usage,
                                int minflt = kMissingInfo, int majflt = kMissingInfo,
                                int rss_anon_kb = kMissingInfo) {
  EXPECT_EQ(process_memory_usage.minflt(), minflt);
  EXPECT_EQ(process_memory_usage.majflt(), majflt);
  EXPECT_EQ(process_memory_usage.rss_anon_kb(), rss_anon_kb);
}

void ExpectCGroupMemoryUsageEq(const CGroupMemoryUsage& cgroup_memory_usage,
                               int limit_bytes = kMissingInfo, int rss_bytes = kMissingInfo,
                               int mapped_file_bytes = kMissingInfo, int pgfault = kMissingInfo,
                               int pgmajfault = kMissingInfo, int unevictable_bytes = kMissingInfo,
                               int inactive_anon_bytes = kMissingInfo,
                               int active_anon_bytes = kMissingInfo,
                               int inactive_file_bytes = kMissingInfo,
                               int active_file_bytes = kMissingInfo) {
  EXPECT_EQ(cgroup_memory_usage.limit_bytes(), limit_bytes);
  EXPECT_EQ(cgroup_memory_usage.rss_bytes(), rss_bytes);
  EXPECT_EQ(cgroup_memory_usage.mapped_file_bytes(), mapped_file_bytes);
  EXPECT_EQ(cgroup_memory_usage.pgfault(), pgfault);
  EXPECT_EQ(cgroup_memory_usage.pgmajfault(), pgmajfault);
  EXPECT_EQ(cgroup_memory_usage.unevictable_bytes(), unevictable_bytes);
  EXPECT_EQ(cgroup_memory_usage.inactive_anon_bytes(), inactive_anon_bytes);
  EXPECT_EQ(cgroup_memory_usage.active_anon_bytes(), active_anon_bytes);
  EXPECT_EQ(cgroup_memory_usage.inactive_file_bytes(), inactive_file_bytes);
  EXPECT_EQ(cgroup_memory_usage.active_file_bytes(), active_file_bytes);
}

}  // namespace

namespace orbit_memory_tracing {

TEST(MemoryUtils, UpdateSystemMemoryUsageFromMemInfo) {
  const int kMemTotal = 16396576;
  const int kMemFree = 11493816;
  const int kMemAvailable = 14378752;
  const int kBuffers = 71540;
  const int kCached = 3042860;

  const std::string kValidMeminfo =
      absl::Substitute(R"(MemTotal:       $0 kB
MemFree:        $1 kB
MemAvailable:   $2 kB
Buffers:        $3 kB
Cached:         $4 kB
SwapCached:            0 kB
Active:          3350508 kB
Inactive:        1190988 kB
Active(anon):    1444908 kB
Inactive(anon):      516 kB
Active(file):    1905600 kB
Inactive(file):  1190472 kB
Unevictable:       56432 kB
Mlocked:           56432 kB
SwapTotal:       1953788 kB
SwapFree:        1953788 kB
Dirty:               492 kB
Writeback:             0 kB
AnonPages:       1326896 kB
Mapped:           716656 kB
Shmem:               796 kB
KReclaimable:      84864 kB
Slab:             194376 kB
SReclaimable:      84864 kB
SUnreclaim:       109512 kB
KernelStack:       24724 kB
PageTables:        13164 kB
NFS_Unstable:          0 kB
Bounce:                0 kB
WritebackTmp:          0 kB
CommitLimit:    10152076 kB
Committed_AS:    6324736 kB
VmallocTotal:   34359738367 kB
VmallocUsed:       38264 kB
VmallocChunk:          0 kB
Percpu:             3072 kB
HardwareCorrupted:     0 kB
AnonHugePages:    782336 kB
ShmemHugePages:        0 kB
ShmemPmdMapped:        0 kB
FileHugePages:         0 kB
FilePmdMapped:         0 kB
HugePages_Total:       0
HugePages_Free:        0
HugePages_Rsvd:        0
HugePages_Surp:        0
Hugepagesize:       2048 kB
Hugetlb:               0 kB
DirectMap4k:      201960 kB
DirectMap2M:     5040128 kB
DirectMap1G:    13631488 kB)",
                       kMemTotal, kMemFree, kMemAvailable, kBuffers, kCached);

  const std::string kPartialMeminfo = absl::Substitute(R"(MemTotal:       $0 kB
MemFree:        $1 kB
SwapCached:      0 kB)",
                                                       kMemTotal, kMemFree);

  const std::string kEmptyMeminfo = "";

  {
    SystemMemoryUsage system_memory_usage = CreateAndInitializeSystemMemoryUsage();
    ErrorMessageOr<void> updating_result =
        UpdateSystemMemoryUsageFromMemInfo(kValidMeminfo, &system_memory_usage);
    EXPECT_FALSE(updating_result.has_error());
    ExpectSystemMemoryUsageEq(system_memory_usage, kMemTotal, kMemFree, kMemAvailable, kBuffers,
                              kCached);
  }

  {
    SystemMemoryUsage system_memory_usage = CreateAndInitializeSystemMemoryUsage();
    ErrorMessageOr<void> updating_result =
        UpdateSystemMemoryUsageFromMemInfo(kPartialMeminfo, &system_memory_usage);
    EXPECT_FALSE(updating_result.has_error());
    ExpectSystemMemoryUsageEq(system_memory_usage, kMemTotal, kMemFree);
  }

  {
    SystemMemoryUsage system_memory_usage = CreateAndInitializeSystemMemoryUsage();
    ErrorMessageOr<void> updating_result =
        UpdateSystemMemoryUsageFromMemInfo(kEmptyMeminfo, &system_memory_usage);
    EXPECT_TRUE(updating_result.has_error());
    ExpectSystemMemoryUsageEq(system_memory_usage);
  }
}

TEST(MemoryUtils, UpdateSystemMemoryUsageFromVmStat) {
  const int kPageFaults = 123456789;
  const int kMajorPageFaults = 123456;

  const std::string kValidProcVmStat = absl::Substitute(R"(nr_free_pages 2258933
nr_zone_inactive_anon 655781
nr_zone_active_anon 265654
nr_zone_inactive_file 103608
nr_zone_active_file 682986
nr_zone_unevictable 14789
nr_zone_write_pending 504
nr_mlock 14789
nr_page_table_pages 14006
nr_bounce 0
nr_zspages 0
nr_free_cma 0
numa_hit 1640599383
numa_miss 0
numa_foreign 0
numa_interleave 61517
numa_local 1640599383
numa_other 0
nr_inactive_anon 655795
nr_active_anon 265654
nr_inactive_file 103608
nr_active_file 682986
nr_unevictable 14789
nr_slab_reclaimable 39573
nr_slab_unreclaimable 29913
nr_isolated_anon 0
nr_isolated_file 0
workingset_nodes 10052
workingset_refault_anon 482478
workingset_refault_file 4691743
workingset_activate_anon 83978
workingset_activate_file 3712979
workingset_restore_anon 31279
workingset_restore_file 2506434
workingset_nodereclaim 23964
nr_anon_pages 779841
nr_mapped 238243
nr_file_pages 882760
nr_dirty 480
nr_writeback 0
nr_writeback_temp 0
nr_shmem 66116
nr_shmem_hugepages 0
nr_shmem_pmdmapped 0
nr_file_hugepages 0
nr_file_pmdmapped 0
nr_anon_transparent_hugepages 755
nr_vmscan_write 1246151
nr_vmscan_immediate_reclaim 732
nr_dirtied 110747698
nr_written 96424883
nr_kernel_misc_reclaimable 0
nr_foll_pin_acquired 0
nr_foll_pin_released 0
nr_kernel_stack 39280
nr_dirty_threshold 600497
nr_dirty_background_threshold 299882
pgpgin 70153910
pgpgout 478359020
pswpin 482479
pswpout 1226100
pgalloc_dma 0
pgalloc_dma32 206502602
pgalloc_normal 2867571518
pgalloc_movable 0
allocstall_dma 0
allocstall_dma32 0
allocstall_normal 61
allocstall_movable 574
pgskip_dma 0
pgskip_dma32 0
pgskip_normal 255855
pgskip_movable 0
pgfree 3077305458
pgactivate 59489152
pgdeactivate 13444038
pglazyfree 176961
pgfault $0
pgmajfault $1
pglazyfreed 86974
pgrefill 14648260
pgreuse 150268511
pgsteal_kswapd 25809003
pgsteal_direct 109534
pgscan_kswapd 42547232
pgscan_direct 182478
pgscan_direct_throttle 0
pgscan_anon 16823270
pgscan_file 25906440
pgsteal_anon 1236888
pgsteal_file 24681649
zone_reclaim_failed 0
pginodesteal 7256
slabs_scanned 15016420
kswapd_inodesteal 8299045
kswapd_low_wmark_hit_quickly 3520
kswapd_high_wmark_hit_quickly 1113
pageoutrun 5198
pgrotated 1183212
drop_pagecache 0
drop_slab 0
oom_kill 0
numa_pte_updates 0
numa_huge_pte_updates 78
numa_hint_faults 0
numa_hint_faults_local 0
numa_pages_migrated 0
pgmigrate_success 835315
pgmigrate_fail 141734
thp_migration_success 0
thp_migration_fail 0
thp_migration_split 0
compact_migrate_scanned 22847132
compact_free_scanned 22310540
compact_isolated 1850479
compact_stall 209
compact_fail 7
compact_success 202
compact_daemon_wake 1419
compact_daemon_migrate_scanned 333848
compact_daemon_free_scanned 6526252
htlb_buddy_alloc_success 0
htlb_buddy_alloc_fail 0
unevictable_pgs_culled 207448
unevictable_pgs_scanned 0
unevictable_pgs_rescued 133162
unevictable_pgs_mlocked 160277
unevictable_pgs_munlocked 133138
unevictable_pgs_cleared 5564
unevictable_pgs_stranded 5534
thp_fault_alloc 2578050
thp_fault_fallback 2462
thp_fault_fallback_charge 0
thp_collapse_alloc 59381
thp_collapse_alloc_failed 2
thp_file_alloc 0
thp_file_fallback 0
thp_file_fallback_charge 0
thp_file_mapped 0
thp_split_page 1816
thp_split_page_failed 0
thp_deferred_split_page 224583
thp_split_pmd 660273
thp_split_pud 0
thp_zero_page_alloc 1
thp_zero_page_alloc_failed 0
thp_swpout 0
thp_swpout_fallback 782
balloon_inflate 209231935
balloon_deflate 209231935
balloon_migrate 3482
swap_ra 277950
swap_ra_hit 207052
nr_unstable 0)",
                                                        kPageFaults, kMajorPageFaults);

  const std::string kPartialProcVmStat = absl::Substitute(R"(pgfault $0)", kPageFaults);

  const std::string kEmptyProcVmStat = "";

  {
    SystemMemoryUsage system_memory_usage = CreateAndInitializeSystemMemoryUsage();
    ErrorMessageOr<void> updating_result =
        UpdateSystemMemoryUsageFromVmStat(kValidProcVmStat, &system_memory_usage);
    EXPECT_FALSE(updating_result.has_error());
    ExpectSystemMemoryUsageEq(system_memory_usage, kMissingInfo, kMissingInfo, kMissingInfo,
                              kMissingInfo, kMissingInfo, kPageFaults, kMajorPageFaults);
  }

  {
    SystemMemoryUsage system_memory_usage = CreateAndInitializeSystemMemoryUsage();
    ErrorMessageOr<void> updating_result =
        UpdateSystemMemoryUsageFromVmStat(kPartialProcVmStat, &system_memory_usage);
    EXPECT_FALSE(updating_result.has_error());
    ExpectSystemMemoryUsageEq(system_memory_usage, kMissingInfo, kMissingInfo, kMissingInfo,
                              kMissingInfo, kMissingInfo, kPageFaults);
  }

  {
    SystemMemoryUsage system_memory_usage = CreateAndInitializeSystemMemoryUsage();
    ErrorMessageOr<void> updating_result =
        UpdateSystemMemoryUsageFromVmStat(kEmptyProcVmStat, &system_memory_usage);
    EXPECT_TRUE(updating_result.has_error());
    ExpectSystemMemoryUsageEq(system_memory_usage);
  }
}

TEST(MemoryUtils, UpdateProcessMemoryUsageFromProcessStat) {
  const int kMinorPageFaults = 20;
  const int kMajorPageFaults = 1;

  const std::string kValidProcessStat = absl::Substitute(
      R"(9562 (TargetProcess) S 9561 9561 9561 0 -1 123456789 $0 3173 $1 0 7 18 1 7 20 0 10 0 123456789 123456789 2793 123456789 1 1 0 0 0 0 0 0 2 0 0 0 17 6 0 0 0 0 0 0 0 0 0 0 0 0 0)",
      kMinorPageFaults, kMajorPageFaults);
  const std::string kPartialProcessStat = R"(9562 (TargetProcess) S 9561 9561 9561)";
  const std::string kEmptyProcessStat = "";

  {
    ProcessMemoryUsage process_memory_usage = CreateAndInitializeProcessMemoryUsage();
    ErrorMessageOr<void> updating_result =
        UpdateProcessMemoryUsageFromProcessStat(kValidProcessStat, &process_memory_usage);
    EXPECT_FALSE(updating_result.has_error());
    ExpectProcessMemoryUsageEq(process_memory_usage, kMinorPageFaults, kMajorPageFaults);
  }

  {
    ProcessMemoryUsage process_memory_usage = CreateAndInitializeProcessMemoryUsage();
    ErrorMessageOr<void> updating_result =
        UpdateProcessMemoryUsageFromProcessStat(kPartialProcessStat, &process_memory_usage);
    EXPECT_TRUE(updating_result.has_error());
    ExpectProcessMemoryUsageEq(process_memory_usage);
  }

  {
    ProcessMemoryUsage process_memory_usage = CreateAndInitializeProcessMemoryUsage();
    ErrorMessageOr<void> updating_result =
        UpdateProcessMemoryUsageFromProcessStat(kEmptyProcessStat, &process_memory_usage);
    EXPECT_TRUE(updating_result.has_error());
    ExpectProcessMemoryUsageEq(process_memory_usage);
  }
}

TEST(MemoryUtils, UpdateProcessMemoryUsageFromProcessStatus) {
  const int kRssAnonKb = 10264;

  const std::string kValidProcessStatus = absl::Substitute(
      R"(Name:   bash
Umask:  0022
State:  S (sleeping)
Tgid:   17248
Ngid:   0
Pid:    17248
PPid:   17200
TracerPid:      0
Uid:    1000    1000    1000    1000
Gid:    100     100     100     100
FDSize: 256
Groups: 16 33 100
NStgid: 17248
NSpid:  17248
NSpgid: 17248
NSsid:  17200
VmPeak:     131168 kB
VmSize:     131168 kB
VmLck:           0 kB
VmPin:           0 kB
VmHWM:       13484 kB
VmRSS:       13484 kB
RssAnon:     $0 kB
RssFile:      3220 kB
RssShmem:        0 kB
VmData:      10332 kB
VmStk:         136 kB
VmExe:         992 kB
VmLib:        2104 kB
VmPTE:          76 kB
VmPMD:          12 kB
VmSwap:          0 kB
HugetlbPages:          0 kB
CoreDumping:    0
Threads:        1
SigQ:   0/3067
SigPnd: 0000000000000000
ShdPnd: 0000000000000000
SigBlk: 0000000000010000
SigIgn: 0000000000384004
SigCgt: 000000004b813efb
CapInh: 0000000000000000
CapPrm: 0000000000000000
CapEff: 0000000000000000
CapBnd: ffffffffffffffff
CapAmb: 0000000000000000
NoNewPrivs:     0
Seccomp:        0
Speculation_Store_Bypass:       vulnerable
Cpus_allowed:   00000001
Cpus_allowed_list:      0
Mems_allowed:   1
Mems_allowed_list:      0
voluntary_ctxt_switches:        150
nonvoluntary_ctxt_switches:     545)",
      kRssAnonKb);
  const std::string kPartialProcessStatus = R"(Name:   bash
Umask:  0022
State:  S (sleeping))";
  const std::string kEmptyProcessStatus = "";

  {
    ProcessMemoryUsage process_memory_usage = CreateAndInitializeProcessMemoryUsage();
    ErrorMessageOr<void> updating_result =
        UpdateProcessMemoryUsageFromProcessStatus(kValidProcessStatus, &process_memory_usage);
    EXPECT_FALSE(updating_result.has_error());
    ExpectProcessMemoryUsageEq(process_memory_usage, kMissingInfo, kMissingInfo, kRssAnonKb);
  }

  {
    ProcessMemoryUsage process_memory_usage = CreateAndInitializeProcessMemoryUsage();
    ErrorMessageOr<void> updating_result =
        UpdateProcessMemoryUsageFromProcessStatus(kPartialProcessStatus, &process_memory_usage);
    EXPECT_TRUE(updating_result.has_error());
    ExpectProcessMemoryUsageEq(process_memory_usage);
  }

  {
    ProcessMemoryUsage process_memory_usage = CreateAndInitializeProcessMemoryUsage();
    ErrorMessageOr<void> updating_result =
        UpdateProcessMemoryUsageFromProcessStatus(kEmptyProcessStatus, &process_memory_usage);
    EXPECT_TRUE(updating_result.has_error());
    ExpectProcessMemoryUsageEq(process_memory_usage);
  }
}

TEST(MemoryUtil, GetProcessMemoryCGroupName) {
  const std::string kCGroupName = "user.slice/user-1000.slice";

  const std::string kValidProcessCGroup = absl::Substitute(R"(10:memory:/$0
9:blkio:/user.slice/user-1000.slice
8:net_cls,net_prio:/
7:cpu,cpuacct:/user.slice/user-1000.slice
6:perf_event:/
5:freezer:/
4:cpuset:/
3:pids:/user.slice/user-1000.slice
2:devices:/user.slice/user-1000.slice
1:name=systemd:/user.slice/user-1000.slice/session-3.scope)",
                                                           kCGroupName);

  const std::string kPartialProcessCGroup = R"(3:pids:/user.slice/user-1000.slice
2:devices:/user.slice/user-1000.slice
1:name=systemd:/user.slice/user-1000.slice/session-3.scope)";

  const std::string kEmptyProcessCGroup = "";

  {
    std::string parsing_result = GetProcessMemoryCGroupName(kValidProcessCGroup);
    EXPECT_EQ(parsing_result, kCGroupName);
  }

  {
    std::string parsing_result = GetProcessMemoryCGroupName(kPartialProcessCGroup);
    EXPECT_TRUE(parsing_result.empty());
  }

  {
    std::string parsing_result = GetProcessMemoryCGroupName(kEmptyProcessCGroup);
    EXPECT_TRUE(parsing_result.empty());
  }
}

TEST(MemoryUtils, UpdateCGroupMemoryUsageFromMemoryStat) {
  const int kRssInBytes = 245760;
  const int KMappedFileInBytes = 1234;
  const int kPageFaults = 1425;
  const int kMajorPageFaults = 1;
  const int kUnevictableInBytes = 0;
  const int kInactiveAnonInBytes = 16384;
  const int kActiveAnonInBytes = 253952;
  const int kInactiveFileInBytes = 3678;
  const int kActiveFileInBytes = 12288;

  const std::string kValidCGroupMemoryStatus = absl::Substitute(
      R"(cache 36864
rss $0
rss_huge 0
shmem 0
mapped_file $1
dirty 135168
writeback 0
pgpgin 299
pgpgout 230
pgfault $2
pgmajfault $3
inactive_anon $5
active_anon $6
inactive_file $7
active_file $8
unevictable $4
hierarchical_memory_limit 14817636352
total_cache 36864
total_rss 245760
total_rss_huge 0
total_shmem 0
total_mapped_file 0
total_dirty 135168
total_writeback 0
total_pgpgin 299
total_pgpgout 230
total_pgfault 1425
total_pgmajfault 1
total_inactive_anon 16384
total_active_anon 253952
total_inactive_file 0
total_active_file 12288
total_unevictable 0)",
      kRssInBytes, KMappedFileInBytes, kPageFaults, kMajorPageFaults, kUnevictableInBytes,
      kInactiveAnonInBytes, kActiveAnonInBytes, kInactiveFileInBytes, kActiveFileInBytes);

  const std::string kPartialCGroupMemoryStatus = R"(cache 36864
rss_huge 0)";

  const std::string kEmptyCGroupMemoryStatus = "";

  {
    CGroupMemoryUsage cgroup_memory_usage = CreateAndInitializeCGroupMemoryUsage();
    ErrorMessageOr<void> updating_result =
        UpdateCGroupMemoryUsageFromMemoryStat(kValidCGroupMemoryStatus, &cgroup_memory_usage);
    EXPECT_FALSE(updating_result.has_error());
    ExpectCGroupMemoryUsageEq(cgroup_memory_usage, kMissingInfo, kRssInBytes, KMappedFileInBytes,
                              kPageFaults, kMajorPageFaults, kUnevictableInBytes,
                              kInactiveAnonInBytes, kActiveAnonInBytes, kInactiveFileInBytes,
                              kActiveFileInBytes);
  }

  {
    CGroupMemoryUsage cgroup_memory_usage = CreateAndInitializeCGroupMemoryUsage();
    ErrorMessageOr<void> updating_result =
        UpdateCGroupMemoryUsageFromMemoryStat(kPartialCGroupMemoryStatus, &cgroup_memory_usage);
    EXPECT_FALSE(updating_result.has_error());
    ExpectCGroupMemoryUsageEq(cgroup_memory_usage);
  }

  {
    CGroupMemoryUsage cgroup_memory_usage = CreateAndInitializeCGroupMemoryUsage();
    ErrorMessageOr<void> updating_result =
        UpdateCGroupMemoryUsageFromMemoryStat(kEmptyCGroupMemoryStatus, &cgroup_memory_usage);
    EXPECT_TRUE(updating_result.has_error());
    ExpectCGroupMemoryUsageEq(cgroup_memory_usage);
  }
}

}  // namespace orbit_memory_tracing