#include <cmath>
#include <cstdlib>
#include <iostream>
#include <list>
#include <bitset>
#include <map>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <assert.h>
#include <cmath>
#include "mem/ruby/common/Address.hh"
#include "mem/ruby/system/System.hh"
#include "mem/ruby/slicc_interface/AbstractCacheEntry.hh"
#include "debug/TagTable.hh"
#include "debug/TagTable1.hh"
//#include "CacheMemoryPF.hh"
#include "tagtable_stats.hpp"


#define BLOCKSIZE 64
#define PAGESIZE 4096
#define ADDR_LEN 48
#define DEPTH 4
#define MAXTRACES 8

extern uint64_t CACHESIZE;// (64*MEGA)
extern uint64_t BLOCKS;// (CACHESIZE/BLOCKSIZE)	//number of blocks in cache
extern uint64_t PAGES;// (CACHESIZE/PAGESIZE)	//number of pages in cache




//#include <boost/circular_buffer.hpp>
//extern uint64_t Address;

 // Maximum Number of trace files that can be specified

//block to be used for insertion and sorting ("block" may be a misnomer since it actually represents a contiguous set of cache blocks)
struct block { //contiguous block of data
  int offset;
  int len;
  short page_offset;
  AbstractCacheEntry* PFentry_ptr;
  friend std::ostream& operator<< (std::ostream & out, const block &blk) {
    out << "Block offset = 0x" << std::hex << blk.offset << std::dec << ", length = " << blk.len 
	<< ", page_offset = 0x" << std::hex << blk.page_offset << std::dec << std::endl;
    return out;
  }
};

bool block_ordering(block first, block second);
void print_assigned(const std::vector<uint64_t> assigned);
typedef struct l3map_entry_t {
  uint seq_no;  //indicates offset - from page table's base - of this particular entry-block
  uint size;    //indicates number of entries/tags it currently maps
  std::vector<uint64_t> meta_tags;//[(BLOCKSIZE/ENTRYSIZE)];      //tags for finding metadata in L3 (matches a LEVEL_PTR entry's tag)
  std::vector<uint64_t> data_tags;//[(BLOCKSIZE/ENTRYSIZE)];      //tags for path on pagetable associated with metadata block (equal to meta_tags in SST_ENTRYs)
  std::vector<bool> is_lvl;//[(BLOCKSIZE/ENTRYSIZE)];   //indicate which entries are associated with lvl_ptrs (makes eviction faster since I can directly use the tag for everything else, i.e., don't have search every entry to find it)

  l3map_entry_t(uint32_t _s) :
    meta_tags(_s, 0),
    data_tags(_s, 0),
    is_lvl(_s, false)
  { }
} l3map_entry;




struct sst_field {
  bool presence;		//NOT NECESSARY, len == 0 IMPLIES NOT PRESENT... 1 bit:  are the blocks in this range present? (or is this an exception, huh?  can't be an exception, this is covered by the sst_entry itself, not the field, right?)
  short page_offset;		//log(PAGESIZE/BLOCKSIZE) bits:  subblock w/in page associated with data corresponding to "offset" address (TODO:  implies all other subblocks covered by this entry are contiguous in page)
  short len;			//log(PAGESIZE/BLOCKSIZE) bits:  length beyond this range that presence is valid
  short offset;			//log(PAGESIZE/BLOCKSIZE) bits
  AbstractCacheEntry* PFentry_ptr;
};

//Sorted Segment Table (SST) entry (size = 1 + FIELDS * (1 + 3*log(PAGESIZE/BLOCKSIZE)))
struct sst_entry {
  // bool is_match;			//1 bit:  is presence a match or an exception? (i.e., do the chunks encode the present blocks or the non-present blocks?  Tracking gaps isn't implemented => should always be true)
  std::vector<sst_field> fields;	//FIELDS * (1 + 3*log(PAGESIZE/BLOCKSIZE)) bits
  std::bitset<(PAGESIZE/BLOCKSIZE)> prefetch_vector;	//0 bits (not actually present in physical implementation - just a stat):  flag blocks that were brought in speculatively (due to negotiation to prevent an entry outgrowing # of FIELDS)
//  boost::circular_buffer<uint32_t> field_lru;		//LRU of fields for eviction

  sst_entry(uint _num_fields) :
    fields(_num_fields)
     { 
      //initialize LRU
      for(uint32_t f = 0; f < _num_fields; ++f) {
//	field_lru.push_front(f);
      }
    }
};

typedef enum {
  SST_ENTRY,            /* Entry of FIELDS fields defining chunks of space */
  LEVEL_PTR,            /* Pointer to next lowest level in the table */
  STATUS_VECTOR,        /* Vector defining block-level status */
  ENTRY_TYPE_NUM
} ENTRY_TYPE;


struct entry;

class entry_level {
public:
  entry_level( int w, int d, uint32_t _num_chunks, int proot_depth, entry_level *p_root);
  ~entry_level();
  entry* lookup(int index);
  int get_depth(){return depth;}
  int get_mask(){return mask;}
  entry_level* get_pageroot(){return page_root;}
  uint get_evictions();	//Accumulates and returns evictions of all children levels
  void update_evictions(int removed) { evictions += removed; }
  uint get_l3_evictions();
  short replace(int preferred, int &expansions, int &merges);
  void evict_block(short victim, int &expansions, int &merges);
  bool evict_entry(uint64_t tag);				//evict SST entry associated with a tag
  void update_tag(uint64_t path_tag, uint64_t old_tag, uint64_t new_tag);	//reassign tag from old to new (needed to handle LEVEL_PTRs found in 'evict_entry')
  friend std::ostream& operator<< (std::ostream & out, const entry_level & e_lvl);
  std::ostream & print_level(std::ostream & out) const;
  void print_level() const;
  int get_active_entries();	//walk structure and determine number of valid entries (either status vector or sst entries)
  //Break down how blocks are tracked for space efficiency determination
  int get_sst_tracked(int &entries, int &lvlptrs);
  int get_vector_tracked(int &entries);
  void verify_page_offsets(int offset_array[]);	//function to verify occupancy & presence vector periodically
  entry *copy_entry(entry *e);
  void get_size(int size[], int lvl_count[]);
  int getindex(uint64_t addr, int lvl, int mask);
  void defragment();	//Defragment entries (i.e., re-build all entries to maximize contiguous blocks
  uint64_t get_tag(uint64_t addr);
  //Translation Cache Functions
  int get_tc_occupancy();		//get number of paths present in the translation cache
  void print_tc_status(/*int check*/);	//print the current Translation Cache contents
  void evict_tc_entry();		//find and evict a translation cache entry to keep within capacity
  bool verify_tc_status(int check);	//Checks that a given TC subtree has the correct number of paths
  int fix_tc_paths();			//fix tc_paths for every entry in the level based on its children
  void dec_tc_paths(uint64_t tag);	//Targeted fixing of Translation Cache paths, only the entries that can be on the path of 'tag' are affected

  //Kill the status vector
  bool negotiate_new_assignment(std::pair<short, short> changes, int &expansions, int &merges);
  void clear_presence(std::pair<short, short>);		//clear presence bits for evicted blocks
  void set_presence(std::pair<short, short>);		//set presence bits for prefetched blocks
  void mark_evicted(int row_offset);			//Mark specified row offset as being evicted (triggered by pagetable)
  int prefetch(uint64_t addr, short offset, bool _evict);	//Rearrange blocks in row to make entry associated with 'addr' have <= FIELDS fields
							// return number of blocks prefetched (evict instead of prefetch if _evict)
  void find_and_evict(std::pair<short, short> range);	//find blocks with row offsets given in 'range' and evict them
  void print_presence_vector();

  //Prediction functions
  uint32_t getDistance(uint64_t addr);		//Determine minimum distance this block is from an existing chunk (*NOT* its own chunk however)
  void getChunkSizeStats(tagtable_stats &chunk_size_stats);	//Create histogram of chunk sizes for all existing valid entries

  //Unit Test Interfaces
  uint32_t getSSTEntries();
  uint32_t getLvlPtrs();
  uint32_t getChunks();

private:
 
  uint m_num_fields;
  int width;			//number of bits used to index array
  int pageroot_depth;	//depth at which page roots reside (i.e., roots of subtrees beneath which all blocks are on the same page (row) in the cache) - passed in by page table on construction
  int mask;			//mask used to determine index for this level
  entry **array;		//array of entries
  int depth;			//depth in page table where this level resides
  int occupancy;		//number of blocks captured by last level table (important for ensuring <= 64 entries mapped per page)
  int offsets_present[(PAGESIZE/BLOCKSIZE)];	//array to verify occupancy & presence vector periodically
  //  short next_replace;		//victim for next replacement (if page is full)
  std::bitset<PAGESIZE/BLOCKSIZE> presence_vector;	//offsets already taken
  int moves;			//moves necessitated to keep SST entries contiguous (will need to replace with function later)
  entry_level *page_root;	//entry level that forms the root of the blocks that fit on a single page
  std::list<block> fix_entry(int i);
  void populate_entry(int index, std::list<block> blocks, int &expansions, int &merges);
  //  int update_next_replace();
  short get_victim();
  void print_entry(int index, int indent) const;
  std::ostream & print_entry(int index, std::ostream & out, int indent) const;
  void find_offender(int page);
  std::list<block> build_entry(std::list<block> present_blocks);
  void bulk_update_presence();
  void accumulate_page_offsets(std::vector<int> &offset_array);

  //Statistics
  uint evictions;
  uint l3_evictions;	//evictions caused by evicting data from the L3

  //Defragment functions
  void add_to_vector(std::vector<uint64_t> &assigned);
  void populate_sst_entry(uint32_t index, uint32_t &curr_field, uint32_t &page_off, std::vector<uint64_t> assigned);
  void populate_level(uint32_t &curr_field, uint32_t &page_off, std::vector<uint64_t> assigned);
};





struct entry {
  bool valid;		//1 bit:  is this entry valid?
  int tc_path;		//log(#tcEntries) bits:  number of translation paths this entry is on
  ENTRY_TYPE type;	//1 bit (SST or PTR):  what type of entry is this?
  //below, either or (i.e., length is max(length(addr,sst_e))
  entry_level *addr;	//# bits to access next level of table in L3
  sst_entry sst_e;	//FIELDS * (1 + 3*log(PAGESIZE/BLOCKSIZE)))
  short status_vector[(PAGESIZE/BLOCKSIZE)];  //not used:  status vector stores either -1 (not present) or the block's offset in the page
  uint64_t tag;		//(ADDR_LEN - block offset - page offset - bits to identify row) bits:  address bits needed to disambiguate this entry (when at leaf this isn't necessary, only when entry exists at a higher level)

  entry(uint _num_fields) :
  sst_e(_num_fields)
  { }

  friend std::ostream& operator<< (std::ostream & out, const entry &entry) {
    int entry_width = 97;
    assert(entry.type == SST_ENTRY);
    out << std::setfill(' ') << std::setw(5) << "" << std::setfill('-') << std::setw(entry_width) << "" << std::endl << std::hex;
      for(const auto &fld : entry.sst_e.fields) {
	out << " | o:" << fld.offset << " (po: " << fld.page_offset << ") p:" << fld.presence << " l:" << fld.len;
      }
      out << "|(" << entry.tc_path << ") tag 0x" << std::hex << entry.tag << std::dec << std::endl;
      out << std::setfill(' ') << std::setw(5) << "" << std::setfill(' ') << std::setw(0) << "" << std::setfill('-') << std::setw(entry_width) << "" << std::endl;
    return out;
  }
};



//int m_entry_size=uint32_t(pow(2, (CeilLog2((20 + _num_chunks*19)/8))));



