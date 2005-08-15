from m5 import *
from BaseMem import BaseMem

class Prefetch(Enum): vals = ['none', 'tagged', 'stride', 'ghb']

class BaseCache(BaseMem):
    type = 'BaseCache'
    adaptive_compression = Param.Bool(False,
        "Use an adaptive compression scheme")
    assoc = Param.Int("associativity")
    block_size = Param.Int("block size in bytes")
    compressed_bus = Param.Bool(False,
        "This cache connects to a compressed memory")
    compression_latency = Param.Latency('0ns',
        "Latency in cycles of compression algorithm")
    do_copy = Param.Bool(False, "perform fast copies in the cache")
    hash_delay = Param.Int(1, "time in cycles of hash access")
    in_bus = Param.Bus(NULL, "incoming bus object")
    lifo = Param.Bool(False,
        "whether this NIC partition should use LIFO repl. policy")
    max_miss_count = Param.Counter(0,
        "number of misses to handle before calling exit")
    mem_trace = Param.MemTraceWriter(NULL,
                                     "memory trace writer to record accesses")
    mshrs = Param.Int("number of MSHRs (max outstanding requests)")
    out_bus = Param.Bus("outgoing bus object")
    prioritizeRequests = Param.Bool(False,
        "always service demand misses first")
    protocol = Param.CoherenceProtocol(NULL, "coherence protocol to use")
    repl = Param.Repl(NULL, "replacement policy")
    size = Param.MemorySize("capacity in bytes")
    split = Param.Bool(False, "whether or not this cache is split")
    split_size = Param.Int(0,
        "How many ways of the cache belong to CPU/LRU partition")
    store_compressed = Param.Bool(False,
        "Store compressed data in the cache")
    subblock_size = Param.Int(0,
        "Size of subblock in IIC used for compression")
    tgts_per_mshr = Param.Int("max number of accesses per MSHR")
    trace_addr = Param.Addr(0, "address to trace")
    two_queue = Param.Bool(False,
        "whether the lifo should have two queue replacement")
    write_buffers = Param.Int(8, "number of write buffers")
    prefetch_miss = Param.Bool(False,
         "wheter you are using the hardware prefetcher from Miss stream")
    prefetch_access = Param.Bool(False,
         "wheter you are using the hardware prefetcher from Access stream")
    prefetcher_size = Param.Int(100,
         "Number of entries in the harware prefetch queue")
    prefetch_past_page = Param.Bool(False,
         "Allow prefetches to cross virtual page boundaries")
    prefetch_serial_squash = Param.Bool(False,
         "Squash prefetches with a later time on a subsequent miss")
    prefetch_degree = Param.Int(1,
         "Degree of the prefetch depth")
    prefetch_latency = Param.Tick(10,
         "Latency of the prefetcher")
    prefetch_policy = Param.Prefetch('none',
         "Type of prefetcher to use")
    prefetch_cache_check_push = Param.Bool(True,
         "Check if in cash on push or pop of prefetch queue")
    prefetch_use_cpu_id = Param.Bool(True,
         "Use the CPU ID to seperate calculations of prefetches")
    prefetch_data_accesses_only = Param.Bool(False,
         "Only prefetch on data not on instruction accesses")
