//NOTE:  use test() (and presumably set() & clear(), etc) method(s) of bitset to protect against out of bounds accesses unlike '[]' operator
#include <assert.h>
#include <time.h>
#include <stdlib.h>
#include <iomanip>
#include <algorithm> //for random_shuffle
#include "entry-level.hh"

using namespace std;

#define VERIFY_TC 0
#define DEBUG_LEVEL 0
#define DEBUG 1
#define addr_t uint64_t
#define TEST 0
typedef addr_t uint64_t;
#define DEBUG_MSG(msg) \
  do { if(TEST) std::cout << msg << std::endl; } while(0)

#define WARN(comp, msg) \
  do { if(comp) std::cout << "WARN: " << msg << std::endl; } while(0)
namespace {

/*!
 *  @brief Computes floor(log2(n))
 *  Works by finding position of MSB set.
 *  @returns -1 if n == 0.
 */
static inline int FloorLog2(uint n)
{
  int p = 0;

  if (n == 0) return -1;

  if (n & 0xffff0000) { p += 16; n >>= 16; }
  if (n & 0x0000ff00) { p +=  8; n >>=  8; }
  if (n & 0x000000f0) { p +=  4; n >>=  4; }
  if (n & 0x0000000c) { p +=  2; n >>=  2; }
  if (n & 0x00000002) { p +=  1; }

  return p;
}

}

entry_level::entry_level(int w, int d, uint32_t _num_chunks, int proot_depth, entry_level *p_root) :
  m_num_fields(_num_chunks),
  width(4),
  pageroot_depth(proot_depth),
  mask(int(pow(2,w)) - 1),
  depth(d),
  occupancy(0),
  moves(0),
  page_root(p_root),
  evictions(0),
  l3_evictions(0)
{
  int no_entries = int(pow(2,width));
  array = new entry * [no_entries]; //array is an array of entries of size 2^(number of bits used to index it)
  for(int i = 0; i < no_entries; i++) { //initialize all entries invalid
    array[i] = NULL;
  }
  presence_vector.reset();
  for(int i = 0; i < (PAGESIZE/BLOCKSIZE); i++) {
    offsets_present[i] = 0;
  }
  //initialize seed for random victim replacement
  srand(0);
}

entry_level::~entry_level() {
  for(int i = 0; i < int(pow(2,width)); i++) {
    if((array[i] != NULL) && (array[i]->type == LEVEL_PTR)) {
      delete array[i]->addr;
    }
    delete array[i];
    array[i] = NULL;
  }
  delete [] array;
}

short entry_level::replace(int preferred, int &expansions, int &merges) {
  short victim = -1;
  if(depth < pageroot_depth) {		//Above the PageRoot
    victim = preferred;  //relies on caller to have verified 'preferred' is available
  } else if (depth == pageroot_depth) {	//The PageRoot
    assert(occupancy <= (PAGESIZE/BLOCKSIZE));
    assert(preferred < (PAGESIZE/BLOCKSIZE));
    if(presence_vector.test(preferred)) {	//preferred offset is not available
      victim = get_victim();
      //evict the victim (if it's assigned)
      if(presence_vector.test(victim)) {
	evict_block(victim, expansions, merges);
      }
    } else {				//preferred offset is available
#if DEBUG
      cout << "Using preferred page_offset of 0x" << hex << preferred << dec << endl;
#endif
      assert(!presence_vector.test(preferred));
      victim = short(preferred);
    }
    presence_vector.set(int(victim));
#if DEBUG
    cout << "Set presence for 0x" << hex << victim << dec << " for page root " << this << endl;
#endif
    occupancy = int(presence_vector.count());
    assert(occupancy <= (PAGESIZE/BLOCKSIZE));
#if DEBUG
    cout << "Returning " << ((int(victim)==preferred)?"":"non-") << "preferred victim 0x" << hex << victim << " (preferred was " << preferred << dec << ") from replace function (occupancy now " << occupancy << " for page root " << this << ")" << endl;
    cout << "Set presence for 0x" << hex << int(victim) << dec << " for page root " << this << endl;
#endif
  } else {	//below the PageRoot
    victim = page_root->replace(preferred, expansions, merges);
  }

  assert(victim != -1);
  return victim;
}

namespace {
// class generator:
struct c_unique {
  uint32_t current;
  c_unique() { current=0; }
  uint32_t operator()() { return current++; }
} UniqueNumber;
}

short entry_level::get_victim() {
  assert(depth == pageroot_depth);
  short next_r = (PAGESIZE/BLOCKSIZE);
  if(presence_vector.count() < (PAGESIZE/BLOCKSIZE)) {	//at least one page_offset is available 
    // => find one randomly to assign to next_r (looking through in order can lead to problems)
    std::vector<uint32_t> victims((PAGESIZE/BLOCKSIZE));
    generate(victims.begin(), victims.end(), UniqueNumber);
    random_shuffle(victims.begin(), victims.end());
    for(const uint32_t test_victim : victims) {
      if(!presence_vector.test(test_victim)) {
	next_r = test_victim;
#if DEBUG_LEVEL
	cout << "Found victim 0x" << hex << next_r << dec << " unassigned in presence vector" << endl;
#endif
	break;
      }
    }
  } else {	//choose real victim (i.e., kick something out)
    //TODO:  implement functionality for different replacement algorithms, currently random
    next_r = rand() % (PAGESIZE/BLOCKSIZE);
#if DEBUG_LEVEL
    cout << "Chose random victim 0x" << hex << next_r << dec << endl;
#endif
  }
  assert(next_r < (PAGESIZE/BLOCKSIZE));
  return next_r;
}

bool block_ordering(block first, block second) {
  return (first.offset < second.offset);
}

list<block> entry_level::build_entry(list<block> present_blocks) {
  list<block>::iterator it;
#if DEBUG
  if(present_blocks.size() > 0) {
    cout << "Before insertion, blocks are:" << endl;
    for(it=present_blocks.begin(); it!=present_blocks.end(); it++) {
      cout << hex << (*it).offset << " - " << (*it).offset + (*it).len - 1 << "\t(po: " << (*it).page_offset << ")" << dec << endl;
    }
    cout << "Inserting " << hex << present_blocks.back().offset << " - " << present_blocks.back().offset + present_blocks.back().len - 1 << dec << endl;
  }
#endif

  present_blocks.sort(block_ordering);

#if DEBUG
  if(present_blocks.size() > 0) {
    cout << "After insertion, blocks are:" << endl;
    for(it=present_blocks.begin(); it!=present_blocks.end(); it++) {
      cout << hex << (*it).offset << " - " << (*it).offset + (*it).len - 1 << "\t(po: " << (*it).page_offset << ")" << dec << endl;
    }
  }
#endif

  //find merge opportunities
  for(it = present_blocks.begin(); it != present_blocks.end(); it++) {
    list<block>::iterator nit = it;  //nit = next iterator
    nit++;
    while((nit != present_blocks.end()) && ((*it).offset + (*it).len == (*nit).offset) && ((*it).page_offset + (*it).len == (*nit).page_offset)/*make sure page offsets are also contiguous*/) {
      (*it).len += (*nit).len;
      present_blocks.erase(nit);
      nit = it;
      nit++;
    }
  }
#if DEBUG
  if(present_blocks.size() > 0) {
    cout << "After merge:" << endl;
    for(it=present_blocks.begin(); it!=present_blocks.end(); it++) {
      cout << hex << (*it).offset << " - " << (*it).offset + (*it).len - 1 << "\t(po: " << (*it).page_offset << ")" << dec << endl;
    }
  } else {
    cout << "No blocks to order/merge" << endl;
  }
#endif

  //TODO:  check to see if it makes sense to collapse this entry into the upper level
  return present_blocks;
}

void entry_level::evict_block(short victim, int &expansions, int &merges) {
  if((depth == pageroot_depth) && !presence_vector.test(victim)) { return; }
#if DEBUG
  if(depth == pageroot_depth) {
    cout << "Evicting block 0x" << hex << victim << " from page root " << this << dec << endl;
  }
#endif
  addr_t victim_address = -1;	//address to send to POLB for eviction
  //find and invalidate victim
  for(int i = 0; i < int(pow(2,width)); i++) {
    if((array[i] != NULL) && (array[i]->valid)) {
      if(array[i]->type == SST_ENTRY) {
	for(uint j = 0; j < m_num_fields; j++) {
	  if((array[i]->sst_e.fields[j].page_offset != -1) && 
	     (array[i]->sst_e.fields[j].page_offset <= victim) && 
	     ((array[i]->sst_e.fields[j].page_offset + array[i]->sst_e.fields[j].len > victim))) {  //evict (NOTE:  assumes contiguous assignment - i.e., entries with consecutive addresses are consecutive in the page)
#if DEBUG
	    cout << "I have found the victim in field " << j << " of entry:" << endl;
	    print_entry(i, cout, 0);
#endif
	    assert(array[i]->valid);
	    list<block> present_blocks;
	    if(array[i]->sst_e.fields[j].len == 1) {  //can just invalidate
	      victim_address = array[i]->tag + array[i]->sst_e.fields[j].offset;
	      array[i]->sst_e.fields[j].presence = 0;
	      array[i]->sst_e.fields[j].page_offset = -1;
	      array[i]->sst_e.fields[j].len = 0;
	      array[i]->sst_e.fields[j].PFentry_ptr = NULL;
	      //fix the rest of the fields
	    } else { //need to split block => might create STATUS_VECTOR
	      //split field into two blocks
	      block temp;
	      int l = array[i]->sst_e.fields[j].page_offset;
	      if(l != victim) {  //bottom of range isn't the victim
		l = victim;
		victim_address = array[i]->tag + array[i]->sst_e.fields[j].offset + (l - array[i]->sst_e.fields[j].page_offset);
		temp.offset = array[i]->sst_e.fields[j].offset;
		temp.len = l - array[i]->sst_e.fields[j].page_offset;
		temp.page_offset = array[i]->sst_e.fields[j].page_offset;
		temp.PFentry_ptr = array[i]->sst_e.fields[j].PFentry_ptr;
		present_blocks.push_back(temp);
	      } else {  //bottom of range *is* the victim
		victim_address = array[i]->tag + array[i]->sst_e.fields[j].offset;	//victim's address uses this field's base offset
	      }
	      l++;
	      if(l < array[i]->sst_e.fields[j].page_offset + array[i]->sst_e.fields[j].len) {
		//there will exist a second chunk after eviction (i.e., victim was not the top of the chunk)
		l = array[i]->sst_e.fields[j].page_offset + array[i]->sst_e.fields[j].len;
		temp.offset = array[i]->sst_e.fields[j].offset + (victim - array[i]->sst_e.fields[j].page_offset) + 1;
		temp.len = l - victim - 1;
		temp.page_offset = victim + 1;
		temp.PFentry_ptr = array[i]->sst_e.fields[j].PFentry_ptr;
		present_blocks.push_back(temp);
	      }
	      //set to invalid
	      array[i]->sst_e.fields[j].presence = 0;
	      array[i]->sst_e.fields[j].page_offset = -1;
	    }

	    //walk through each field and add to block list
	    block temp2;
	    for(uint k = 0; k < m_num_fields; k++) {
	      if(array[i]->sst_e.fields[k].presence) {
		temp2.offset = array[i]->sst_e.fields[k].offset;
		temp2.len = array[i]->sst_e.fields[k].len;
		temp2.page_offset = array[i]->sst_e.fields[k].page_offset;
		temp2.PFentry_ptr = array[i]->sst_e.fields[k].PFentry_ptr;
		present_blocks.push_back(temp2);
	      }
	    }
	    present_blocks = build_entry(present_blocks);
	    populate_entry(i, present_blocks, expansions, merges);
	    //TEST
	    cout << "Victim's address is " << hex << victim_address << " (tag:  0x" << array[i]->tag << ")" << dec << endl;
	    //TEST
	    //if(pt_polb != NULL) pt_polb->evict(victim_address);
	    evictions++;
#if DEBUG
	    cout << "Evicted 0x" << hex << victim << dec << " from SST entry:" << endl;
	    print_entry(i, cout, 0);
#endif
	    break;
	  }
	}
      } else if (array[i]->type == STATUS_VECTOR) {
	for(int j = 0; j < (PAGESIZE/BLOCKSIZE); j++) {
	  if(array[i]->status_vector[j] == victim) {
#if DEBUG
	    cout << "Found victim in status vector:" << endl;
	    print_entry(i, cout, 0);
#endif
	    victim_address = array[i]->tag + j;
	    array[i]->status_vector[j] = -1;
	    //attempt to collapse this to an SST_ENTRY or delete it altogether if it's empty
	    bool empty = true;
	    list<block> present_blocks;
	    for(int j = 0; j < (PAGESIZE/BLOCKSIZE); j++) {
	      if(array[i]->status_vector[j] != -1) {		//block is present
		empty = false;
		int offset = j;
		short expected = array[i]->status_vector[j];	//what is the next contiguous page offset to expect?
		do {
		  j++;
		  expected++;
		} while((j < (PAGESIZE/BLOCKSIZE)) && (array[i]->status_vector[j] == expected));
		block temp;
		temp.offset = offset;
		temp.page_offset = array[i]->status_vector[offset];
		temp.len = j-offset;
		present_blocks.push_back(temp);
		j--;	//make sure I look at this entry again even though it's not contiguous
	      }
	    }
	    if(present_blocks.size() < m_num_fields) {  //Create SST ENTRY if few enough blocks
	      present_blocks = build_entry(present_blocks);	//Necessary?
	      populate_entry(i, present_blocks, expansions, merges);
	    }
	    if(empty) {
	      array[i] = NULL;
	      //TODO:  look at collapsing other entries up if this now means there's only one left
	    }
#if DEBUG
	    cout << "Entry is now" << endl;
	    print_entry(i, cout, 0);
#endif
	    /*TEST
	    cout << "Victim's address is " << hex << victim_address << dec << endl;
	    //TEST*/
	    //if(pt_polb != NULL) pt_polb->evict(victim_address);
	    evictions++;
#if DEBUG
	    cout << "Evicted 0x" << hex << victim << dec << " from status vector" << endl;
	    print_entry(i, cout, 0);
#endif
	    break;
	  }
	}
      } else if(array[i]->type == LEVEL_PTR) {
	//follow pointer to find the victim
	array[i]->addr->evict_block(victim, expansions, merges);
	//TODO:  check to see if this LEVEL_PTR could be collapsed up to an SST_ENTRY (i.e., it now only has a single entry below it because of this eviction)
      } else {
	cout << "This entry is an invalid type" << endl;
	assert(0);
      }
    }
  }
}

//TODO:  should this function be pulled up into where it's called (i.e., since I think there's only one place where it's called, put the code there instead of creating a function)
list<block> entry_level::fix_entry(int i) {
  list<block> present_blocks;
  assert(array[i] != NULL);
  if(array[i]->type == SST_ENTRY) { //currently, this is the only possibility
    for(uint j = 0; j < m_num_fields; j++) {
      if(array[i]->sst_e.fields[j].presence) {
	block temp;
	temp.offset = array[i]->sst_e.fields[j].offset;
	temp.len = array[i]->sst_e.fields[j].len;
	temp.page_offset = array[i]->sst_e.fields[j].page_offset;
	present_blocks.push_back(temp);
      }
    }
    //TODO:  there should not be any merge opportunities when evicting, right?
  } else {
    cout << "This function was called on the wrong entry type" << endl;
    assert(0);
  }
  return present_blocks;
}

void entry_level::populate_entry(int index, list<block> blocks, int &expansions, int &merges) {
#if DEBUG
  cout << "Rebuilding entry" << endl;
  print_entry(index, cout, 0);
#endif
  if(blocks.size() == 0) {	//empty entry => make invalid
    //if(array[index]->tc_path > 0) pt->dec_tc(array[index]->tag);	//deleting this will affect (i.e., decrement) tc_path values of paths above me
    array[index]->type = ENTRY_TYPE_NUM;
    array[index]->valid = false;
    array[index]->addr = NULL;
    for(uint field = 0; field < m_num_fields; field++) {
	array[index]->sst_e.fields[field].presence = 0;
	array[index]->sst_e.fields[field].offset = -1;
	array[index]->sst_e.fields[field].page_offset = -1;
	array[index]->sst_e.fields[field].len = 0;
    }
    array[index]->tc_path = 0;
    for(int j = 0; j < PAGESIZE/BLOCKSIZE; j++) {	//initialize status vector entries to invalid
      array[index]->status_vector[j] = -1;
    }
    /*DEBUG
    printf("Deleting %p\n", array[index]);
    //DEBUG*/
    //TODO:  look at collapsing other entries up if this now means there's only one left
  } else if(blocks.size() <= m_num_fields) {	//Create SST entry
    assert(array[index] != NULL);
    if(array[index]->type != SST_ENTRY) {
      array[index]->type = SST_ENTRY;
      merges++;
      //re-initialize all status vector entries
      for(int offset = 0; offset < (PAGESIZE/BLOCKSIZE); offset++) {	//initialize status vector entries to invalid
	array[index]->status_vector[offset] = -1;
      }
    }
    list<block>::iterator it;
    uint i;
    for(it=blocks.begin(), i=0; it!=blocks.end(); it++, i++) {
      if((*it).len && ((*it).offset == 0)) {
	array[index]->sst_e.fields[0].presence = 1;
	array[index]->sst_e.fields[0].offset = (*it).offset;
	array[index]->sst_e.fields[0].page_offset = (*it).page_offset;
	array[index]->sst_e.fields[0].len = (*it).len;
      } else {
	if((*it).len) {
	  array[index]->sst_e.fields[i].presence = 1;
	  array[index]->sst_e.fields[i].offset = (*it).offset;
	  array[index]->sst_e.fields[i].page_offset = (*it).page_offset;
	  array[index]->sst_e.fields[i].len = (*it).len;
	} else {
	  array[index]->sst_e.fields[i].presence = 0;
	  if(i > 0) {
	    array[index]->sst_e.fields[i].offset = array[index]->sst_e.fields[i-1].offset + array[index]->sst_e.fields[i-1].len;  //guaranteed to have at least an entry in the first field
	  } else {
	    array[index]->sst_e.fields[i].offset = 0;
	  }
	  array[index]->sst_e.fields[i].len = 0;
	  array[index]->sst_e.fields[i].page_offset = -1;
	}
      }
    }
    for(; i < m_num_fields; i++) { //populate remaining empty entries
      assert(i > 0);
      array[index]->sst_e.fields[i].presence = 0;
      array[index]->sst_e.fields[i].offset = array[index]->sst_e.fields[i-1].offset + array[index]->sst_e.fields[i-1].len;  //guaranteed to have at least an entry in the first field
      array[index]->sst_e.fields[i].len = 0;
      array[index]->sst_e.fields[i].page_offset = -1;
    }
    if(!array[index]->sst_e.fields[m_num_fields-1].presence) {
      array[index]->sst_e.fields[m_num_fields-1].len = ((PAGESIZE/BLOCKSIZE)-1) - array[index]->sst_e.fields[m_num_fields-1].offset;
      array[index]->sst_e.fields[m_num_fields-1].page_offset = -1;
    }
  } else {	//Too many blocks => erase the shortest blocks (should only require 1 deletion, but this recurses in case that's not true)
#if DEBUG_LEVEL
    cout << "Too many blocks => finding and deleting the shortest" << endl;
#endif
    //TODO:  make selection of which fields are retained more intelligent
    int shortest = (PAGESIZE/BLOCKSIZE);
    list<block>::iterator id = blocks.end();
    for(list<block>::iterator it = blocks.begin(); it != blocks.end(); it++) {
      if((*it).len < shortest) {
	shortest = (*it).len;
	id = it;
      }
    }
#if DEBUG_LEVEL
    cout << "Shortest is 0x" << hex << shortest << " at " << (*id).offset << " - " << (*id).offset + (*id).len - 1 << "\t(po: " << (*id).page_offset << ")" << dec << endl;
#endif
    assert(shortest < (PAGESIZE/BLOCKSIZE));
    assert(id != blocks.end());
    //update presence vector for evicted blocks
    if(depth >= pageroot_depth) {
      pair<short, short> evicted;
      evicted.first = (*id).page_offset;			//first block evicted
      evicted.second = (*id).page_offset + (*id).len - 1;	//last block evicted
      clear_presence(evicted);
    }
    evictions += shortest;
    blocks.erase(id);
    populate_entry(index, blocks, expansions, merges);
  }
#if DEBUG
  cout << "Done" << endl;
  print_entry(index, cout, 0);
#endif
}

void entry_level::set_presence(pair<short,short> changes) {
  if(depth == pageroot_depth) {
    //TODO:  assert that changes.second < (PAGESIZE/BLOCKSIZE)?
    for(int i = changes.first; i <= changes.second; i++) {
      if(i < (PAGESIZE/BLOCKSIZE)) {
	presence_vector.set(i);
#if DEBUG
	cout << "Set presence for 0x" << hex << i << dec << " for page root " << this << endl;
#endif
      }
    }
    occupancy = int(presence_vector.count());
  } else if (depth > pageroot_depth) {
    page_root->set_presence(changes);
  }
}

void entry_level::clear_presence(pair<short, short> changes) {
  if(depth == pageroot_depth) {
    assert(depth == pageroot_depth);
    for(int i = changes.first; i <= changes.second; i++) {
      if(i < (PAGESIZE/BLOCKSIZE)) {
	assert(presence_vector.test(i));	//offset is already assigned
	presence_vector.reset(i);
#if DEBUG
	cout << "Cleared presence for 0x" << hex << i << dec << " for page root " << this << " 1" << endl;
#endif
      }
    }
    occupancy = int(presence_vector.count());
  } else if (depth > pageroot_depth) {
    page_root->clear_presence(changes);
  }
}

entry * entry_level::lookup(int index) {
	DPRINTF(TagTable, "entry_level::lookup : ENTRY\n");
  if(array[index] == NULL) {
    array[index] = new entry(m_num_fields);
    
    array[index]->type = ENTRY_TYPE_NUM;
    array[index]->valid = false;
    array[index]->addr = NULL;
    // array[index]->sst_e.is_match = false;
    array[index]->sst_e.prefetch_vector.reset();
    for(uint field = 0; field < m_num_fields; field++) {
      array[index]->sst_e.fields[field].presence = 0;
      array[index]->sst_e.fields[field].page_offset = -1;
      array[index]->sst_e.fields[field].len = 0;
      array[index]->sst_e.fields[field].offset = -1;
    }
    for(int offset = 0; offset < (PAGESIZE/BLOCKSIZE); offset++) {	//initialize status vector entries to invalid
      array[index]->status_vector[offset] = -1;
    }
    array[index]->tag = 0;
    array[index]->tc_path = 0;
  }
  DPRINTF(TagTable, "entry_level::lookup : EXIT\n");
  return array[index];
  
}
int entry_level::getindex(uint64_t addr, int lvl, int mask) {
  /*TEST
  if(lvl > 1) cout << "Address 0x" << hex << addr << dec;
  //TEST*/
  int index = uint64_t(pow(2,ADDR_LEN));
  assert(lvl < DEPTH);
	
  //shift off block & page offsets
  uint64_t temp_addr = addr >> (FloorLog2(BLOCKSIZE)+FloorLog2((PAGESIZE/BLOCKSIZE)));
  //shift off the bits of every level above this
  for(int lvl_depth = 0; lvl_depth < lvl; lvl_depth++) {
    temp_addr >>= 4;
  }
  index = temp_addr & mask;
  assert(unsigned(index) != addr_t(pow(2,ADDR_LEN)));

  return index;
}
entry *entry_level::copy_entry(entry *e) {
  DEBUG_MSG("Pushing entry " << std::endl << *e << std::endl << " down");
  int index = getindex(e->tag, depth, mask);
  assert(array[index] == NULL);	//this function is only be called right after the level is created => all entries must be NULL
  array[index] = new entry(m_num_fields);
  array[index]->valid = e->valid;
  array[index]->type = e->type;
  if(e->type == SST_ENTRY) {
    assert(e->tc_path <= 1);
  }
  array[index]->tc_path = e->tc_path;
  array[index]->addr = NULL;		//the entry passed is currently pointing to *this* level => don't want that
  // array[index]->sst_e.is_match = e->sst_e.is_match;
  array[index]->sst_e.prefetch_vector.reset();	//TODO: why does this get reset?  shouldn't it be copied from e?
  for(uint field = 0; field < m_num_fields; field++) {
    array[index]->sst_e.fields[field].presence = e->sst_e.fields[field].presence;
    array[index]->sst_e.fields[field].page_offset = e->sst_e.fields[field].page_offset;
    array[index]->sst_e.fields[field].len = e->sst_e.fields[field].len;
    array[index]->sst_e.fields[field].offset = e->sst_e.fields[field].offset;
  }
  for(int offset = 0; offset < (PAGESIZE/BLOCKSIZE); offset++) {	//initialize status vector entries
    array[index]->status_vector[offset] = e->status_vector[offset];
  }
  array[index]->tag = e->tag;

  //need to update page root's presence vector
  if(depth > pageroot_depth) {
    page_root->bulk_update_presence();
  } else if(depth == pageroot_depth) {
    bulk_update_presence();
  }

  return array[index];
}

//update presence vector (and occupancy) to reflect newly inserted entry (should be only entry present)
void entry_level::bulk_update_presence() {
  DEBUG_MSG("Performing bulk presence update");
  assert(depth == pageroot_depth);
  //create and initialize array to store offsets
  std::vector<int> offsets((PAGESIZE/BLOCKSIZE), 0);

  accumulate_page_offsets(offsets);

  for(int offset = 0; offset < (PAGESIZE/BLOCKSIZE); offset++) {
    assert((offsets[offset] == 0) || (offsets[offset] == 1));
    if(offsets[offset] == 1) {
      presence_vector.set(offset);
#if DEBUG
      cout << "Set presence for 0x" << hex << offset << dec << " for page root " << this << endl;
#endif
    } else {
      assert(offsets[offset] == 0);
      presence_vector.reset(offset);
#if DEBUG
	cout << "Cleared presence for 0x" << hex << offset << dec << " for page root " << this << " 2" << endl;
#endif
    }
  }

  occupancy = presence_vector.count();
}

void entry_level::accumulate_page_offsets(std::vector<int> &offset_array) {
  /*TEST
  std::cout << this << " Accumulating offsets at depth " << depth << ":  ";
  //TEST*/
  assert(depth >= pageroot_depth);
  for(int index = 0; index < int(pow(2,width)); index++) {
    if((array[index] != NULL) && (array[index]->valid)) {
      if(array[index]->type == LEVEL_PTR) {
	array[index]->addr->accumulate_page_offsets(offset_array);
      } else if(array[index]->type == SST_ENTRY) {
	for(const auto &field : array[index]->sst_e.fields) {
	  if(field.presence) {
#if DEBUG
	    if(field.page_offset == -1) {
	      cout << "Page offset for valid field is -1:" << endl;
	      print_entry(index, cout, 0);
	    }
#endif
	    assert(field.page_offset != -1);
	    for(int populate = field.page_offset; populate < (field.page_offset + field.len); ++populate) {
	      assert(offset_array[populate] == 0);
	      offset_array[populate]++;
	      /*TEST
	      std::cout << std::hex << populate << std::dec << ", ";
	      //TEST*/
	    }
	  }
	}
      } else {
	WARN(1, "Invalid entry type at index " << index << ":  " << array[index]->type);
	assert(0);
      }
    }
  }
  /*TEST
  std::cout << std::endl;
  //TEST*/
}

uint entry_level::get_evictions() {
  uint evicts = evictions;
  for(int i = 0; i < int(pow(2,width)); i++) {
    if((array[i] != NULL) && (array[i]->type == LEVEL_PTR)) {
      evicts += array[i]->addr->get_evictions();
    }
  }
  return evicts;
}

uint entry_level::get_l3_evictions() {
  uint evicts = l3_evictions;
  for(int i = 0; i < int(pow(2,width)); i++) {
    if((array[i] != NULL) && (array[i]->type == LEVEL_PTR)) {
      evicts += array[i]->addr->get_l3_evictions();
    }
  }
  return evicts;
}

void entry_level::print_level() const {
  print_level(std::cout);
}

std::ostream & entry_level::print_level(std::ostream & out) const {
  //Indent width of a level * the depth of this level
  int level_width = 95;
  int indent = depth * level_width;

  //Print name of level
  out << "Level " << depth << ":" << endl;

  //Loop through all entries
  for(int index = 0; index < int(pow(2, width)); index++) {
    if((array[index] != NULL) && array[index]->valid) {
      //if it's a pointer to another level, call that level's print_level
      if(array[index]->type == LEVEL_PTR) {
	out << setfill(' ') << setw(indent) << "";
	out << "0x" << hex << index << dec;
	out << setfill('-') << setw(level_width-2) << ">(" << array[index]->tc_path << ")";
  	array[index]->addr->print_level(out);
	out << endl;
      //if it's an SST entry or vector, print it (via print_entry)
      } else if((array[index]->type == SST_ENTRY) || (array[index]->type == STATUS_VECTOR)) {
  	out << setfill(' ') << setw(indent) << "*";
  	print_entry(index, out, indent);
      } else {
  	cout << "Unknown type (" << array[index]->type << ") in print_level call" << endl;
  	assert(0);
      }
    }
  }
  out << endl;
  if(depth == 0)
    out << "* - denotes valid - non level pointer - entry" << endl;
  return out;
}

std::ostream & operator<< (std::ostream & out, const entry_level & e_lvl) {
  return e_lvl.print_level(out);
}

//Return number of active SST entries
int entry_level::get_active_entries() {
  int active = 0;
  for(uint index = 0; index < uint(pow(2,width)); index++) {
    if((array[index] != NULL) && (array[index]->type != ENTRY_TYPE_NUM)) {
      if(array[index]->type == LEVEL_PTR) {
	active += array[index]->addr->get_active_entries();
      } else if(array[index]->type == SST_ENTRY) {
	active += array[index]->valid;
      } else {
	cout << "Unknown type (" << array[index]->type << ")" << endl;
	assert(0);
      }
    }
  }
  /*TEST
  cout << "I have " << active << " active entries" << endl;
  //TEST*/
  return active;
}



int entry_level::get_sst_tracked(int &entries, int &lvlptrs) {
  int tracked = 0;
  for(int index = 0; index < int(pow(2,width)); index++) {
    if(array[index] != NULL) {
      if(array[index]->type == LEVEL_PTR) {
	lvlptrs++;
	tracked += array[index]->addr->get_sst_tracked(entries, lvlptrs);
      } else if((array[index]->valid) && (array[index]->type == SST_ENTRY)) {
	entries++;
	for(uint field = 0; field < m_num_fields; field++) {
	  if(array[index]->sst_e.fields[field].presence) {	//tracked blocks => accumulate len
	    tracked += array[index]->sst_e.fields[field].len;
	  }
	}
      }
    }
  }
  /*TEST
  cout << "I have " << tracked << " SST tracked entries" << endl;
  //TEST*/
  return tracked;
}

int entry_level::get_vector_tracked(int &entries) {
  int tracked = 0;
  for(int index = 0; index < int(pow(2,width)); index++) {
    if(array[index] != NULL) {
      if(array[index]->type == LEVEL_PTR) {
	tracked += array[index]->addr->get_vector_tracked(entries);
      } else if((array[index]->valid) && (array[index]->type == STATUS_VECTOR)) {
	entries++;
	for(int block = 0; block < (PAGESIZE/BLOCKSIZE); block++) {
	  if(array[index]->status_vector[block] != -1) {	//block is present
	    tracked++;
	  }
	}
      }
    }
  }
  /*TEST
  cout << "I have " << tracked << " status vector tracked entries" << endl;
  //TEST*/
  return tracked;
}

// Verify presence vector matches actual offsets assigned -AND-
// Ensure that no assigned page offset is invalid (i.e., greater than (PAGESIZE/BLOCKSIZE))
void entry_level::verify_page_offsets(int offset_array[]) {
  // switch(depth) {
  // case 0 :
  // case 1 :	//root or lvl 1 => just call all LEVEL_PTRS
  if(depth < pageroot_depth) {
#if DEBUG
    //    cout << "Entry Level 0x" << hex << this << dec << " at depth " << depth << " initiating verification of page offsets" << endl;
#endif
    for(int index = 0; index < int(pow(2,width)); index++) {
      if(array[index] != NULL) {
	if(array[index]->type == LEVEL_PTR) {
	  array[index]->addr->verify_page_offsets(offset_array);
	} else {
	  if(array[index]->type == SST_ENTRY) {
	    for(uint field = 0; field < m_num_fields; field++) {
	      if(array[index]->sst_e.fields[field].presence) {
		assert((array[index]->sst_e.fields[field].page_offset + array[index]->sst_e.fields[field].len) <= (PAGESIZE/BLOCKSIZE));
	      }
	    }
	  }
	}
      }
    }
  // case 2 :	//page root => accumulate status from all leaves below you
  } else if (depth >= pageroot_depth) {
    if(depth == pageroot_depth) {	//initialize tracking array to pass to lower levels
      for(int i = 0; i < (PAGESIZE/BLOCKSIZE); i++) {
	assert((offset_array[i] == 0) || (offset_array[i] == 1));
	offset_array[i]=0;
      }
    }
    //NOTE:  this case will - and should - fall through to the next since there's no 'break'
    // case 3 :	//leaf node => accumulate your status and pass up
    for(int index = 0; index < int(pow(2,width)); index++) {
      if(array[index] != NULL) {
	if(array[index]->type == LEVEL_PTR) {
	  array[index]->addr->verify_page_offsets(offset_array);
	} else if(array[index]->type == SST_ENTRY) {
	  for(uint field = 0; field < m_num_fields; field++) {
	    if(array[index]->sst_e.fields[field].presence) {
#if DEBUG
	      if(array[index]->sst_e.fields[field].page_offset == -1) {
		cout << "Page offset for valid field is -1:" << endl;
		print_entry(index, cout, 0);
	      }
#endif
	      assert(array[index]->sst_e.fields[field].page_offset != -1);
	      assert((array[index]->sst_e.fields[field].page_offset + array[index]->sst_e.fields[field].len) <= (PAGESIZE/BLOCKSIZE));
	      for(int populate = array[index]->sst_e.fields[field].page_offset; populate < array[index]->sst_e.fields[field].page_offset+array[index]->sst_e.fields[field].len; populate++) {
		offset_array[populate]++;
	      }
	    }
	  }
	} else if(array[index]->type == STATUS_VECTOR) {
	  for(int page = 0; page < (PAGESIZE/BLOCKSIZE); page++) {
	    if(array[index]->status_vector[page] != -1) {
	      offset_array[array[index]->status_vector[page]]++;
	    }
	  }
	} else {	//this shouldn't happen
	  cout << "Unknown entry type (" << array[index]->type << ")" << endl;
	  assert(0);
	}
      }
    }
  }

  //have page root verify
  if(depth == pageroot_depth) {
    for(int i = 0; i < (PAGESIZE/BLOCKSIZE); i++) {
      if(offset_array[i] > 1) {
   	cout << "Page offset at 0x" << hex << i << " has more than one block assigned to it (" << dec << offset_array[i] << " assigned) for page root " << this << endl;
	cout << "presence_vector is " << presence_vector.test(i) << " for the page" << endl;
	for(int index = 0; index < int(pow(2,width)); index++) {
	  if(array[index] != NULL) {
	    if(array[index]->type == LEVEL_PTR) {
	      array[index]->addr->find_offender(i);	//find entries that have duplicate page offsets
	    } else if(array[index]->type == SST_ENTRY) {
	      for(uint field = 0; field < m_num_fields; field++) {
		if((array[index]->sst_e.fields[field].presence) && (i >= array[index]->sst_e.fields[field].page_offset) && (i < (array[index]->sst_e.fields[field].page_offset+array[index]->sst_e.fields[field].len))) {
		  cout << "Offender found at entry (in field " << field << ")" << endl;
		  print_entry(index, cout, 0);
		  cout << "(at index " << index << " of leaf " <<  this << "'s array)" << endl;
		}
	      }
	      //	      assert(0);
	    }
	  }
	}
	assert(0);
      }
      //Verify presence vector while we're at it
      if(presence_vector.test(i)) {
   	if(offset_array[i] != 1) {
	  cout << "offset_array says page 0x" << hex << i << " is *not* assigned (indicates " << offset_array[i] << ") when the presence vector says it *is* at page root " << dec << this << endl;
	}
	assert(offset_array[i] == 1);
      } else {
   	if(offset_array[i] != 0) {
	  cout << "offset_array says page 0x" << hex << i << " *is* assigned (indicates " << offset_array[i] << ") when the presence vector says it is *not* at page root " << dec << this << endl;
	}
	assert(offset_array[i] == 0);
      }
    }
  }
}

void entry_level::print_entry(int index, int indent) const {
  print_entry(index, std::cout, indent);
}

ostream & entry_level::print_entry(int index, ostream & out, int indent) const {
  int entry_width = 97;
  if((array[index] != NULL) && (array[index]->type == SST_ENTRY)) {
    out << setfill(' ') << setw(5) << "" << setfill('-') << setw(entry_width) << "" << endl << setfill(' ') << setw(indent) << "0x" << hex << index << ":";
    for(uint i = 0; i < m_num_fields; i++) {
      out << " | o:" << array[index]->sst_e.fields[i].offset << " (po: " << array[index]->sst_e.fields[i].page_offset << ") p:" << array[index]->sst_e.fields[i].presence << " l:" << array[index]->sst_e.fields[i].len;
    }
    out << "|(" << array[index]->tc_path << ") tag 0x" << array[index]->tag << dec << endl;
    out << setfill(' ') << setw(5) << "" << setfill(' ') << setw(indent) << "" << setfill('-') << setw(entry_width) << "" << endl;
  } else if ((array[index] != NULL) && (array[index]->type == STATUS_VECTOR)) {
    out << "0x" << hex << index << ": " << "";
    for(int i = 0; i < (PAGESIZE/BLOCKSIZE); i++) {
      out << array[index]->status_vector[i] << ", ";
    }
    out << dec << endl;
  } else if((array[index] != NULL) && (array[index]->type == ENTRY_TYPE_NUM)) {
    out << "Entry apparently re-initialized (entry_level print_entry)" << endl;
  } else if(array[index] == NULL) {
    out << "Entry was deleted => not printing (entry_level print_entry)" << endl;
  } else {
    out << "I don't know what type this is (" << array[index]->type << " at entry " << array[index] << ") (entry_level print_entry)" << endl;
    exit(0);
  }
  return out;
}

void entry_level::find_offender(int pageoff) {
  for(int index = 0; index < int(pow(2,width)); index++) {
    if(array[index] != NULL) {
      if(array[index]->type == SST_ENTRY) {
	assert(depth == (DEPTH-1));
	for(uint field = 0; field < m_num_fields; field++) {
	  if((array[index]->sst_e.fields[field].presence) && (pageoff >= array[index]->sst_e.fields[field].page_offset) && (pageoff < (array[index]->sst_e.fields[field].page_offset+array[index]->sst_e.fields[field].len))) {
	    cout << "Offender found at entry (in field " << field << ")" << endl;
	    print_entry(index, cout, 0);
	    cout << "(at index " << index << " of leaf " <<  this << "'s array)" << endl;
	  }
	}
      } else if(array[index]->type == STATUS_VECTOR) {
	assert(depth == (DEPTH-1));
	for(int page = 0; page < (PAGESIZE/BLOCKSIZE); page++) {
	  if(array[index]->status_vector[page] == pageoff) {
	    cout << "Offender found at entry (in offset " << page << ")" << endl;
	    print_entry(index, cout, 0);
	    cout << "(at index " << index << " of leaf " <<  this << "'s array)" << endl;
	  }
	}
      } else if(array[index]->type == LEVEL_PTR) {
	assert(depth < (DEPTH-1));
	array[index]->addr->find_offender(pageoff);
      } else {
	cout << "Not a valid entry type" << endl;
	assert(0);
      }
    }
  }
}

void entry_level::get_size(int size[], int lvl_count[]) {
  //increment count for how many levels are at my depth
  lvl_count[depth]++;

  //determine number of active entries I have
  for(int index = 0; index < int(pow(2,width)); index++) {
    if((array[index] != NULL) && (array[index]->valid)){
      size[depth]++;
      if(array[index]->type == LEVEL_PTR) {
	array[index]->addr->get_size(size, lvl_count);
      }
    }
  }
}

bool entry_level::negotiate_new_assignment(pair<short, short> changes, int &expansions, int &merges) {
  if(depth == pageroot_depth) {		//only the page root has presence vector
    //make sure that these new assignments work.  If not, evict the conflicters (resulting in complete eviction of a field if it can't stay contiguous)
    for(int i = changes.first; i <= changes.second; i++) {
      if(i < (PAGESIZE/BLOCKSIZE)) {
	if(presence_vector.test(i)) {	//offset is already assigned
	  /*TEST
	  cout << "Conflict for page offset " << hex << i << dec << " => replacing" << endl;
	  //TEST*/
	  evict_block(i, expansions, merges);
	} else {				//offset not assigned
	  presence_vector.set(i);
#if DEBUG
	  cout << "Set presence for 0x" << hex << i << dec << " for page root " << this << endl;
#endif
	}
      }
    }
    occupancy = int(presence_vector.count());
  } else if(depth > pageroot_depth) {	//call your page root
    page_root->negotiate_new_assignment(changes, expansions, merges);
  } //else {	//new assignment has to be valid }

  return true;
}

int entry_level::get_tc_occupancy() {
  int occupancy = 0;
  for(int index = 0; index < int(pow(2,width)); index++) {
    if((array[index] != NULL) && (array[index]->valid)){
      occupancy += array[index]->tc_path;
      /*DEBUG
      if((array[index]->type == SST_ENTRY) && (array[index]->tc_path > 1)) cout << "Failed check (4): tc_path = 0x" << hex << array[index]->tc_path << dec << endl;
      //DEBUG*/
    }
  }
  return occupancy;
}

void entry_level::print_tc_status() {
  bool first = ((depth>0)?true:false);
  if(depth == 0) cout << "Translation Cache Occupancy = " << get_tc_occupancy() << endl;
  for(int index = 0; index < int(pow(2,width)); index++) {
    if((array[index] != NULL) && (array[index]->valid) && (array[index]->tc_path > 0)){
      if(!first) {
	for(int indent = 0; indent < depth; indent++) {
	  cout << "\t";
	}
      } else {
	cout << "\t";
	first = false;
      }
      cout << hex << index << dec << "(" << array[index]->tc_path << ")";
      if(array[index]->type == LEVEL_PTR) array[index]->addr->print_tc_status(/*array[index]->tc_path*/);
      else if(array[index]->type == SST_ENTRY) cout << endl;
    }
  }
}

//Find an entry (currently random) to evict a path from
// 1.  Create random number between 1 and the number of paths from this level
// 2.  Walk through entries from the beginning, accumulating the number of paths encompassed by them, until I reach the random number's value
// 3.  Decrement that entry's tc_path and descend (if it's a LEVEL_PTR) to perform the same sequence on it
// In the end, I should have eliminated 1 - and only 1 - path from the translation cache
void entry_level::evict_tc_entry() {
  /*TEST
  if(depth==0) {
    cout << "Before eviction, TransCache is:" << endl;
    print_tc_status();
  }
  //TEST*/
  int occupancy = get_tc_occupancy();
  assert(occupancy > 0);
  int victim = (rand() % occupancy) + 1;
  int acc = 0;
  //accumulate tc_path values until the entry containing the random number is found
  for(int index = 0; index < int(pow(2,width)); index++) {
    if((array[index] != NULL) && (array[index]->valid)) {
      acc += array[index]->tc_path;
      /*DEBUG
      if((array[index]->type == SST_ENTRY) && (array[index]->tc_path > 1)) cout << "Failed check (5): tc_path = 0x" << hex << array[index]->tc_path << dec << endl;
      //DEBUG*/
      if(acc >= victim) {
	//if not at leaf, recurse
	if(array[index]->type == LEVEL_PTR) {
	  array[index]->addr->evict_tc_entry();
	}
	array[index]->tc_path--;	//signify one less path from this tree is valid (NOTE:  this "tree" could just be a leaf - i.e., SST_ENTRY)
	/*DEBUG
	if((array[index]->type == SST_ENTRY) && (array[index]->tc_path > 1)) cout << "Failed check (6): tc_path = 0x" << hex << array[index]->tc_path << dec << endl;
	if(array[index]->tc_path < 0) cout << "Decremented tc path count below zero: 0x" << hex << array[index]->tc_path << dec << endl;
	//DEBUG*/
	break;
      }
    }
  }

#if VERIFY_TC
  //Have root verify that the number of entries has decreased by 1 (and is within the capacity)
  if(depth==0) {
    assert(occupancy == (get_tc_occupancy() + 1));
  }
#endif
  /*TEST
  if(depth==0) {
    cout << "After eviction, TransCache is:" << endl;
    print_tc_status();
  }
  //TEST*/
}

bool entry_level::verify_tc_status(int check) {
  bool leaves = true;	//correctness of my leaves
  int verify = 0;	//accumulate number of paths in this subtree
  for(int index = 0; index < int(pow(2,width)); index++) {
    if((array[index] != NULL) && (array[index]->valid) && (array[index]->tc_path > 0)){
      verify += array[index]->tc_path;
      /*DEBUG
      if((array[index]->type == SST_ENTRY) && (array[index]->tc_path > 1)) cout << "Failed check (7): tc_path = 0x" << hex << array[index]->tc_path << dec << endl;
      //DEBUG*/
      if(array[index]->type == LEVEL_PTR) leaves &= array[index]->addr->verify_tc_status(array[index]->tc_path);
    }
  }
  if(verify != check) {
    cout << endl << "Level " << depth << " table found " << verify << " paths when it expected " << check << endl;
    print_level(cout);
  }
  return(leaves & (verify == check));
}

//Decrement tc_path values on all entries along 'tag's path
void entry_level::dec_tc_paths(addr_t tag) {
  int index = getindex(tag, depth, mask);	//find the associated entry in my array
  if((array[index] != NULL) && array[index]->valid) {
    array[index]->tc_path--;
    assert(array[index]->tc_path >= 0);	//should never call this when tc_path is 0 to begin with
    if(array[index]->type == LEVEL_PTR) {
      array[index]->addr->dec_tc_paths(tag);
    }
  }
}

//General TC path fixing (depth-first search and fix of all paths at this subtree)
int entry_level::fix_tc_paths() {
  int my_paths = 0;
  for(int index = 0; index < int(pow(2,width)); index++) {
    if((array[index] != NULL) && (array[index]->valid) && (array[index]->tc_path > 0)) {
      if(array[index]->type == LEVEL_PTR) {
	int child_paths = array[index]->addr->fix_tc_paths();
	my_paths += child_paths;
	if(array[index]->tc_path != child_paths) {
#if DEBUG
	  print_level(cout);
#endif
	}
	array[index]->tc_path = child_paths;
      } else if(array[index]->type == SST_ENTRY) {
	//DEBUG
	if(array[index]->tc_path > 1) {
	  cout << "Failed check (8): tc_path = 0x" << hex << array[index]->tc_path << dec << endl;
	  array[index]->tc_path = 1;
	}
	//DEBUG*/
	my_paths += array[index]->tc_path;
      } else {
	cout << "Unexpected entry type ... killing" << endl;
	assert(0);
      }
    }
  }

  return my_paths;
}

bool entry_level::evict_entry(addr_t tag) {
  bool evicted = false;		//was an entry actually found to evict?
  int index = getindex(tag, depth, mask);
  if((array[index] == NULL) || (!array[index]->valid)) return false;	//has been evicted already
  if(array[index]->type == LEVEL_PTR) {
    assert(array[index]->tag != tag);
    evicted |= array[index]->addr->evict_entry(tag);
    //need to update page root's presence vector
    if(depth == pageroot_depth) bulk_update_presence();
  } else if (array[index]->type == SST_ENTRY) {
    if(array[index]->tag == tag) {
      //Make sure this isn't an empty entry (indicates that it is being inserted now => don't evict)
      //Can occur if an entry is evicted but remains in a metadata block (since I don't evict
      //metadata on a Tag Table eviction) and a subsequent access re-initializes the entry and 
      //causes this old metadata entry to be evicted in response
      bool actually_populated = false;
      for(uint field = 0; field < m_num_fields; field++) {
	actually_populated |= array[index]->sst_e.fields[field].presence;
      }
      if(actually_populated) {
	evicted = true;
	/*TEST
	cout << "Found SST_ENTRY matching tag 0x" << hex << tag << dec << endl;
	//TEST*/
	int covered_blocks = 0;
	for(uint field = 0; field < m_num_fields; field++) {
	  if(array[index]->sst_e.fields[field].presence) {
	    covered_blocks += array[index]->sst_e.fields[field].len;
	  }
	}
	l3_evictions += covered_blocks;/*number of blocks covered by this entry*/
	/*TEST
	cout << covered_blocks << " blocks evicted due to L3 eviction of entry: " << endl;
	pt->print_entry(array[index]);
	//TEST*/
	//if(array[index]->tc_path > 0) pt->dec_tc(array[index]->tag);	//deleting this will affect (i.e., decrement) tc_path values of paths above me
	array[index]->type = ENTRY_TYPE_NUM;
	array[index]->valid = false;
	array[index]->addr = NULL;
	for(uint field = 0; field < m_num_fields; field++) {
	  array[index]->sst_e.fields[field].presence = 0;
	  array[index]->sst_e.fields[field].offset = -1;
	  array[index]->sst_e.fields[field].page_offset = -1;
	  array[index]->sst_e.fields[field].len = 0;
	}
	array[index]->tc_path = 0;
	for(int j = 0; j < PAGESIZE/BLOCKSIZE; j++) {	//initialize status vector entries to invalid
	  array[index]->status_vector[j] = -1;
	}
      }
    } else {	//entry has been replaced already
      return false;
    }
  }

  //evictions will likely cause TC paths to be incorrect (hopefully this function is rare enough not to be too big of a penalty to fix paths in this way)
  //if((depth == 0) && evicted) pt->fix_tc();	//call page table version (instead of directly) because it has a check for the transcache even being instantiated (i.e., transcache_cap > 0)

  return evicted;
}

// Update tag associated with entry from 'old_tag' to 'new_tag'
// Called when LEVEL_PTR metadata is moved from one L3 block to another (due to eviction)
// 'path_tag' is the data necessary to determine the walk to find the level pointer
//  necessary because this method gets called a lot and is very slow otherwise (~6 ms per call per gprof)
void entry_level::update_tag(addr_t path_tag, addr_t old_tag, addr_t new_tag) {
  int index = getindex(path_tag, depth, mask);	//find the associated entry in my array
  assert((array[index] != NULL) && (array[index]->valid));
  assert(array[index]->type == LEVEL_PTR);
  if(array[index]->tag == old_tag) {
    /*TEST
    cout << "Found tag 0x" << hex << old_tag << " at LEVEL_PTR (0x" << array[index]->tag << "), changing to 0x";
    //TEST*/
    array[index]->tag = new_tag;
    /*TEST
    cout << "0x" << hex << array[index]->tag << dec << endl;
    //TEST*/
  } else {  //just a LEVEL_PTR on the path to the necessary LEVEL_PTR
    array[index]->addr->update_tag(path_tag, old_tag, new_tag);
  }
}

//Update stats for a block that was evicted by the pagetable (never called)
void entry_level::mark_evicted(int row_offset) {
  evictions++;
  if(depth == pageroot_depth) {
    assert(presence_vector.test(row_offset));
    presence_vector.reset(row_offset);
#if DEBUG
    cout << "Cleared presence for 0x" << hex << row_offset << dec << " for page root " << this << " 3" << endl;
#endif
  } else if(depth > pageroot_depth) {
    page_root->clear_presence(make_pair(row_offset, row_offset));
  }
}

//Rearrange blocks in row to make entry associated with 'addr' have < m_num_fields fields ('offset' is the row_offset assigned to 'addr' => do *not* overwrite that block with a prefetch)
int entry_level::prefetch(addr_t addr, short offset, bool _evict) {
  //identify least gap between chunks in entry associated with 'addr'
  int index = getindex(addr, depth, mask);	//identify entry
  /*TEST
  cout << "Prefetching changes this entry:" << endl;
  pt->print_entry(array[index]);
  //TEST*/
  int small_gap = (PAGESIZE/BLOCKSIZE);	//initialize the minimum gap to be the maximum - unachievable - possible
  uint gap_field = m_num_fields;		//index of field associated with bottom of smallest gap
  pair<short, short> gap_range;		//range of blocks that make up the gap (i.e., are *not* currently tracked by this entry)
  //initialize gap_range to invalid amount to catch situations where I can't prefetch to reduce the number of entries
  gap_range.first = 0;
  gap_range.second = -1;
  int shortest_chunk = (PAGESIZE/BLOCKSIZE);	//to track shortest field in case I need to revert to an eviction
  int shortest_field = m_num_fields;			//index of field that corresponds to the smallest chunk
  //iterate through fields
  for(uint field = 0; field < m_num_fields; field++) {
    if(array[index]->sst_e.fields[field].presence) {
      int curr_gap = array[index]->sst_e.fields[field+1].page_offset - (array[index]->sst_e.fields[field].page_offset + array[index]->sst_e.fields[field].len);	//gap between current field and next
      if(!_evict && (array[index]->sst_e.fields[field].offset + array[index]->sst_e.fields[field].len + curr_gap) == array[index]->sst_e.fields[field+1].offset) {	//ensure chunks *could* be contigous with a prefetch
	if((curr_gap > 0) && (curr_gap < small_gap)) {	//only allow positive gaps (0 gaps *shouldn't* be possible if I reach this point)
	  short gap_bot = array[index]->sst_e.fields[field].page_offset + array[index]->sst_e.fields[field].len;
	  short gap_top = array[index]->sst_e.fields[field+1].page_offset - 1;
	  //ensure this gap does not include 'offset'
	  if(!((gap_bot <= offset) && (gap_top >= offset))) {	//'offset' is not sandwiched between the top and bottom of the gap
	    small_gap = curr_gap;
	    gap_field = field;
	    gap_range.first = gap_bot;
	    gap_range.second = gap_top;
	  }
	}
      }
      if(array[index]->sst_e.fields[field].len < shortest_chunk) {
      	shortest_chunk = array[index]->sst_e.fields[field].len;
      	shortest_field = field;
      }
    }
  }

  int evict_field = m_num_fields;
  if(_evict || ((gap_range.second == -1) && (gap_range.first == 0))) {	//no valid gap found => just evict LRU chunk
    //evict_field = (shortest_chunk == 0) ? shortest_field : array[index]->sst_e.field_lru.back();
    /*TEST
    if(array[index]->sst_e.fields[evict_field].len > shortest_chunk) {
      cout << "via evicting " << array[index]->sst_e.fields[evict_field].len << "-entry field " << hex << evict_field << " (shortest field is " << shortest_field << dec << " at " << array[index]->sst_e.fields[shortest_field].len << ", shortest_chunk = " << shortest_chunk << ")" << endl;
    }
    //TEST*/
    pair<short,short> evict_range = make_pair(array[index]->sst_e.fields[evict_field].page_offset, array[index]->sst_e.fields[evict_field].page_offset + array[index]->sst_e.fields[evict_field].len - 1);
    //find existing blocks within the gap (potentially in other entries) and evict them
    if(depth > pageroot_depth) {
      page_root->find_and_evict(evict_range);	//tell page root to walk its branches and evict any blocks within specified range
    } else if(depth == pageroot_depth) {	//already at page root
      find_and_evict(evict_range);
    } else {					//above pageroot => only evict from current entry
      assert(array[index]->type == SST_ENTRY);

      //invalidate shortest field
      evictions += array[index]->sst_e.fields[shortest_field].len;
      array[index]->sst_e.fields[shortest_field].presence = 0;
      array[index]->sst_e.fields[shortest_field].page_offset = -1;
      array[index]->sst_e.fields[shortest_field].len = 0;

      //shift the rest of the fields
      //walk through each field and add to block list
      list<block> present_blocks;
      block temp;
      for(sst_field &field : array[index]->sst_e.fields) {
	if(field.presence) {
	  temp.offset = field.offset;
	  temp.len = field.len;
	  temp.page_offset = field.page_offset;
	  present_blocks.push_back(temp);
	}
      }
      present_blocks = build_entry(present_blocks);
      int junk;
      populate_entry(index, present_blocks, junk, junk);
    }
    //remove evicted blocks from presence_vector
    clear_presence(evict_range);
  } else {
    /*TEST
    cout << "via prefetching " << hex << gap_range.first << " through " << gap_range.second << dec << endl;
    //TEST*/
    assert(small_gap != (PAGESIZE/BLOCKSIZE));
    assert(gap_field != m_num_fields);
    assert(gap_range.second >= gap_range.first);
    //find existing blocks within the gap (potentially in other entries) and evict them
    if(depth > pageroot_depth) {
      page_root->find_and_evict(gap_range);	//tell page root to walk its branches and evict any blocks within specified range
    } else if(depth == pageroot_depth) {	//already at page root
      find_and_evict(gap_range);
    } else {					//above pageroot => only evict from current entry
      assert(array[index]->type == SST_ENTRY);

      //invalidate shortest field
      evictions += array[index]->sst_e.fields[shortest_field].len;
      array[index]->sst_e.fields[shortest_field].presence = 0;
      array[index]->sst_e.fields[shortest_field].page_offset = -1;
      array[index]->sst_e.fields[shortest_field].len = 0;

      //shift the rest of the fields
      //walk through each field and add to block list
      list<block> present_blocks;
      block temp;
      for(sst_field &field : array[index]->sst_e.fields) {
	if(field.presence) {
	  temp.offset = field.offset;
	  temp.len = field.len;
	  temp.page_offset = field.page_offset;
	  present_blocks.push_back(temp);
	}
      }
      present_blocks = build_entry(present_blocks);
      int junk;
      populate_entry(index, present_blocks, junk, junk);
    }

    //Eviction above (in find_and_evict call) can lead to fields shifting (if a victim causes elimination of one of the lower-order fields in this entry)
    // => need to ensure 'gap_field' is correct
    for(uint field = 0; field < m_num_fields; field++) {
      if(array[index]->sst_e.fields[field].presence && (array[index]->sst_e.fields[field].page_offset + array[index]->sst_e.fields[field].len == gap_range.first)) {
	/*TEST
	if(field != gap_field) cout << "Have to change gap_field from " << gap_field << " to " << field << endl;
	//TEST*/
	gap_field = field;
      }
    }
    //extend base field to include prefetched blocks and the adjacent field
    array[index]->sst_e.fields[gap_field].len = (array[index]->sst_e.fields[gap_field+1].page_offset + array[index]->sst_e.fields[gap_field+1].len - array[index]->sst_e.fields[gap_field].page_offset);
    evict_field = gap_field + 1;

    //make sure all blocks are considered present & set the prefetch vector
    set_presence(gap_range);
    for(short block = gap_range.first; block <= gap_range.second; block++) {
      array[index]->sst_e.prefetch_vector.set(block);
    }
    //Shift remaining fields down - NOTE:  this occurs within the evict_block call when a whole field is evicted (i.e., no prefetching is possible)
    assert(evict_field < int(m_num_fields));
    for(uint field = evict_field; field < m_num_fields-1; field++) {	//shift others
      array[index]->sst_e.fields[field].presence = array[index]->sst_e.fields[field+1].presence;
      array[index]->sst_e.fields[field].page_offset = array[index]->sst_e.fields[field+1].page_offset;
      array[index]->sst_e.fields[field].len = array[index]->sst_e.fields[field+1].len;
      array[index]->sst_e.fields[field].offset = array[index]->sst_e.fields[field+1].offset;
    }
  }

  //Set final field to empty
  array[index]->sst_e.fields[m_num_fields-1].presence = 0;
  array[index]->sst_e.fields[m_num_fields-1].page_offset = -1;
  array[index]->sst_e.fields[m_num_fields-1].len = 0;
  array[index]->sst_e.fields[m_num_fields-1].offset = -1;

  /*TEST
  cout << " to this entry:" << endl;
  pt->print_entry(array[index]);
  //TEST*/

  /*DEBUG
  if(depth == pageroot_depth) print_presence_vector();
  else if (depth > pageroot_depth) page_root->print_presence_vector();
  //DEBUG*/

  return (gap_range.second - gap_range.first + 1);	//NOTE: if no valid gap is found, this will evaluate to 0 (-1 + 0 + 1)
}

// Determine if a block with specified address is tracked in the row and evict if it is
void entry_level::find_and_evict(pair<short, short> range) {
  for(short block = range.first; block <= range.second; block++) {
    int junk;
    assert(depth >= pageroot_depth);
    if(depth == pageroot_depth) {
      /*DEBUG
      cout << "Will attempt to find and evict offsets 0x" << hex << range.first << " through 0x" << range.second << dec << " in page root " << this << ":" << endl;
      print_level(cout);
      //DEBUG*/
      evict_block(block, junk, junk);
    } else if(depth > pageroot_depth) {
      /*DEBUG
      cout << "Will attempt to find and evict offsets 0x" << hex << range.first << " through 0x" << range.second << dec << " in page root " << page_root << ":" << endl;
      page_root->print_level(cout);
      //DEBUG*/
      page_root->evict_block(block, junk, junk);
    }
  }
}

void entry_level::print_presence_vector() {
  if(depth > pageroot_depth) page_root->print_presence_vector();
  else if(depth == pageroot_depth) {
    cout << "Presence Vector for " << this << " is:" << endl;
    for(int i = 0; i < (PAGESIZE/BLOCKSIZE); i++) {
      cout << hex << i << dec << ":" << presence_vector.test(i) << ", ";
    }
    cout << endl;
  } else cout << "I am above the page root => don't utilize a presence vector" << endl;
}


uint32_t entry_level::getDistance(addr_t addr) {
  uint index = getindex(addr, depth, mask);
  int page_offset = (addr >> FloorLog2(BLOCKSIZE)) & ((PAGESIZE/BLOCKSIZE)-1);
  uint min_distance = (PAGESIZE / BLOCKSIZE);

  //Walk to entry that addr is associated with
  if((array[index] != NULL) && array[index]->valid) {
    if(array[index]->type == LEVEL_PTR) {
      min_distance = array[index]->addr->getDistance(addr);
    } else if(array[index]->type == SST_ENTRY) {
      assert(array[index]->tag == get_tag(addr));
      for(uint32_t field_num = 0; field_num < m_num_fields; ++field_num) {
	if(array[index]->sst_e.fields[field_num].presence) { //valid chunk
	  uint distance = (PAGESIZE / BLOCKSIZE);
	  if(page_offset < array[index]->sst_e.fields[field_num].page_offset) {
	    distance = array[index]->sst_e.fields[field_num].page_offset - page_offset;
	  } else if(page_offset > (array[index]->sst_e.fields[field_num].page_offset + array[index]->sst_e.fields[field_num].len)) {
	    distance = page_offset - (array[index]->sst_e.fields[field_num].page_offset + array[index]->sst_e.fields[field_num].len);
	  } else if((array[index]->sst_e.fields[field_num].len > 1) && 
		    ((page_offset == array[index]->sst_e.fields[field_num].page_offset) || 
		     (page_offset == (array[index]->sst_e.fields[field_num].page_offset + 
				      array[index]->sst_e.fields[field_num].len)))) {
	    //block was accommodated by extending an existing chunk 
	    //=> don't bother looking anymore, return distance of max (don't want to prefetch)
	    return (PAGESIZE / BLOCKSIZE);
	  }
	  if(distance < min_distance) {
	    min_distance = distance;
	  }
	}
      }
    }
  }

  return min_distance;
}

uint32_t entry_level::getSSTEntries() {
  int tracked = 0;
  for(int index = 0; index < int(pow(2,width)); index++) {
    if((array[index] != NULL) && (array[index]->valid)) {
      if(array[index]->type == LEVEL_PTR) {
	tracked += array[index]->addr->getSSTEntries();
      } else if(array[index]->type == SST_ENTRY) {
	++tracked;
      } else {
	cout << "Unknown type (" << array[index]->type << ")" << endl;
	assert(0);
      }
    }
  }

  return tracked;
}

uint32_t entry_level::getLvlPtrs() {
  int tracked = 0;
  for(int index = 0; index < int(pow(2,width)); index++) {
    if((array[index] != NULL) && (array[index]->valid)) {
      if(array[index]->type == LEVEL_PTR) {
	++tracked;
	tracked += array[index]->addr->getLvlPtrs();
      }
    }
  }

  return tracked;
}

uint32_t entry_level::getChunks() {
  int tracked = 0;
  for(int index = 0; index < int(pow(2,width)); index++) {
    if((array[index] != NULL) && (array[index]->valid)) {
      if(array[index]->type == LEVEL_PTR) {
	tracked += array[index]->addr->getChunks();
      } else if(array[index]->type == SST_ENTRY) {
	for(const auto &field : array[index]->sst_e.fields) {
	  if(field.presence) { ++tracked; }
	}
      } else {
	cout << "Unknown type (" << array[index]->type << ")" << endl;
	assert(0);
      }
    }
  }

  return tracked;
}

void print_assigned(const std::vector<addr_t> assigned) {
  addr_t prev_addr = 0;
  uint32_t length = 0;
  for(const auto &block : assigned) {
    if(block != addr_t(-1)) {
      if(block == (prev_addr + (length << FloorLog2(BLOCKSIZE)))) {
	++length;
      } else {
	if(prev_addr != 0) {
	  std::cout << "0x" << std::hex << prev_addr << " - 0x" << (prev_addr + ((length - 1) << FloorLog2(BLOCKSIZE))) << std::dec << ", ";
	}
	prev_addr = block;
	length = 1;
      }
    }
  }
  std::cout << std::endl;
}

//1.  All pageroots and above create vector of addresses existing at each row offset
//2.  Sort vector
//3.  Descend to each entry associated with vector index, in turn
//    a.  assign next available chunk to it and increment length for every entry in the vector that is contiguous with it
//    b.  repeat descent and assignment for first non-contiguous address
void entry_level::defragment() {
  /*TEST
  if(depth == 0) {
    print_level(cout);
  }
  //TEST*/
  //initialize vector with number of invalid addresses equal to the number of blocks in a row
  std::vector<addr_t> assigned((PAGESIZE/BLOCKSIZE), addr_t(-1));

  if(depth < pageroot_depth) {  //each SST_ENTRY fully describes row
    for(int index = 0; index < int(pow(2,width)); index++) {
      if((array[index] != NULL) && (array[index]->valid)) {
	if(array[index]->type == LEVEL_PTR) {
	  array[index]->addr->defragment();
	} else if(array[index]->type == SST_ENTRY) {
	  /*TEST
	  std::cout << "Defragging SST_ENTRY above the page root from " << std::endl;
	  print_entry(index, cout, 0);
	  //TEST*/
	  for(auto &field : array[index]->sst_e.fields) {
	    if(field.presence) {
	      for(uint32_t page_offset = field.page_offset; page_offset < uint32_t(field.page_offset + field.len); ++page_offset) {
		assigned.at(page_offset) = array[index]->tag + ((field.offset + (page_offset - field.page_offset)) << FloorLog2(BLOCKSIZE));
		DEBUG_MSG("Page offset " << std::hex << page_offset << " is assigned to tag 0x" << array[index]->tag << ", with base offset 0x" << field.offset << ", and location in chunk 0x" << (page_offset - field.page_offset) << ", leading to address 0x" << assigned.at(page_offset) << std::dec);
	      }
	    }
	    //clear field (will be repopulated later)
	    field.presence = false;
	    field.page_offset = -1;
	    field.len = 0;
	    field.offset = -1;
	  }
	  //assigned fully populated for row => sort
	  std::sort(assigned.begin(), assigned.end());

	  //re-populate field from sorted vector
	  uint32_t curr_field = 0;
	  for(uint32_t page_off = 0; page_off < assigned.size(); ) {
	    if(assigned.at(page_off) != uint32_t(-1)) {
	      populate_sst_entry(index, curr_field, page_off, assigned);
	    } else {
	      ++page_off;
	    }
	  }
	  /*TEST
	  std::cout << "  ...to" << std::endl;
	  print_entry(index, cout, 0);
	  //TEST*/

	  //Clear 'assigned' for later SST_ENTRYs
	  assigned.assign((PAGESIZE/BLOCKSIZE), addr_t(-1));
	}
      }
    }
  } else if(depth == pageroot_depth) {  //each LEVEL_PTR & SST_ENTRY contributes to vector
    for(int index = 0; index < int(pow(2,width)); index++) {
      if((array[index] != NULL) && (array[index]->valid)) {
	if(array[index]->type == LEVEL_PTR) {
	  array[index]->addr->add_to_vector(assigned);
	} else if(array[index]->type == SST_ENTRY) {
	  for(auto &field : array[index]->sst_e.fields) {
	    if(field.presence) {
	      for(uint32_t page_offset = field.page_offset; page_offset < uint32_t(field.page_offset + field.len); ++page_offset) {
		assigned.at(page_offset) = array[index]->tag + ((field.offset + (page_offset - field.page_offset)) << FloorLog2(BLOCKSIZE));
		DEBUG_MSG("Page offset " << std::hex << page_offset << " is assigned to tag 0x" << array[index]->tag << ", with base offset 0x" << field.offset << ", and location in chunk 0x" << (page_offset - field.page_offset) << ", leading to address 0x" << assigned.at(page_offset) << std::dec);
	      }
	    }
	    //clear field (will be repopulated later)
	    field.presence = false;
	    field.page_offset = -1;
	    field.len = 0;
	    field.offset = -1;
	  }
	}
      }
    }
    //assigned fully populated for row => sort
    std::sort(assigned.begin(), assigned.end());

    //descend and populate all existing entries from sorted vector
    //foreach address in the vector, traverse to leaf entry and populate
    uint32_t curr_field = 0;
    addr_t prev_tag = addr_t(-1);
    for(uint32_t page_off = 0; page_off < assigned.size(); ) { //inc of page_off happens in if-elseif-else
      assert(curr_field < m_num_fields);
      if(assigned.at(page_off) != addr_t(-1)) {
	uint32_t path_index = getindex(assigned.at(page_off), depth, mask);
	addr_t addr_tag = get_tag(assigned.at(page_off));
	//reset curr_field everytime change tag
	if(addr_tag != prev_tag) {
	  prev_tag = addr_tag;
	  curr_field = 0;
	}
	assert(array[path_index] != nullptr);
	assert(array[path_index]->valid);
	if(array[path_index]->type == LEVEL_PTR) {
	  array[path_index]->addr->populate_level(curr_field, page_off, assigned);
	  //NOTE: do *not* increment 'page_off', handled in function
	} else if(array[path_index]->type == SST_ENTRY) {
	  populate_sst_entry(path_index, curr_field, page_off, assigned);
	  //NOTE: do *not* increment 'page_off', handled in function
	} else {
	  ++page_off;
	}
      } else {
	++page_off;
      }
    }
    bulk_update_presence();
  } else {  //should this ever happen?
    assert(0);
  }

  /*TEST
  if(depth == 0) {
    print_level(cout);
  }
  //TEST*/
}

//Populate vector - passed by reference - with all page offsets assigned in entries rooted at this
void entry_level::add_to_vector(std::vector<addr_t> &assigned) {
  assert(depth > pageroot_depth);
  for(int index = 0; index < int(pow(2,width)); index++) {
    if((array[index] != NULL) && (array[index]->valid)) {
      if(array[index]->type == LEVEL_PTR) {
	array[index]->addr->add_to_vector(assigned);
      } else if(array[index]->type == SST_ENTRY) {
	for(auto &field : array[index]->sst_e.fields) {
	  if(field.presence) {
	    for(uint32_t page_offset = field.page_offset; page_offset < uint32_t(field.page_offset + field.len); ++page_offset) {
	      assigned.at(page_offset) = array[index]->tag + ((field.offset + (page_offset - field.page_offset)) << FloorLog2(BLOCKSIZE));
	      DEBUG_MSG("Page offset " << std::hex << page_offset << " is assigned to tag 0x" << array[index]->tag << ", with base offset 0x" << field.offset << ", and location in chunk 0x" << (page_offset - field.page_offset) << ", leading to address 0x" << assigned.at(page_offset) << std::dec);
	    }
	    //clear field (will be repopulated later)
	    field.presence = false;
	    field.page_offset = -1;
	    field.len = 0;
	    field.offset = -1;
	  }
	}
      }
    }
  }
}

uint64_t entry_level::get_tag(uint64_t addr) {      //determine tag necessary for this entry
  uint64_t tag = addr >> (FloorLog2(BLOCKSIZE) + FloorLog2(PAGESIZE/BLOCKSIZE));
  tag <<= (FloorLog2(BLOCKSIZE) + FloorLog2(PAGESIZE/BLOCKSIZE));	//shift 0's back in
  /*TEST
  cout << "tag for entry with address 0x" << hex << addr << " is 0x" << tag << dec << endl;
  //TEST*/
  assert(tag != addr_t(pow(2,ADDR_LEN)));

  return tag;
}
//TODO:  verify do-while works
void entry_level::populate_sst_entry(uint32_t index, uint32_t &curr_field, uint32_t &page_off, std::vector<addr_t> assigned) {
  addr_t block_addr = assigned.at(page_off);
  do {
    if(block_addr != addr_t(-1)) {
      uint32_t block_offset = (block_addr >> FloorLog2(BLOCKSIZE)) & ((PAGESIZE/BLOCKSIZE) - 1);
      //if block is contiguous with currently building field, increment length
      if(block_offset == uint32_t(array[index]->sst_e.fields[curr_field].offset + array[index]->sst_e.fields[curr_field].len)) {
	++array[index]->sst_e.fields[curr_field].len;
      }
      //else begin new field
      else {
	//as long as this isn't the first entry (i.e., first field is invalid), increment to next field
	if(array[index]->sst_e.fields[curr_field].presence) { ++curr_field; }
	assert(curr_field < m_num_fields);
	assert(!array[index]->sst_e.fields[curr_field].presence);
	assert(get_tag(block_addr) == array[index]->tag);
	array[index]->sst_e.fields[curr_field].presence = true;
	array[index]->sst_e.fields[curr_field].len = 1;
	array[index]->sst_e.fields[curr_field].offset = block_offset;
	array[index]->sst_e.fields[curr_field].page_offset = page_off;
      }
    }
    ++page_off;
  } while((page_off < (PAGESIZE/BLOCKSIZE)) && (assigned.at(page_off) == ++block_addr));
}

void entry_level::populate_level(uint32_t &curr_field, uint32_t &page_off, std::vector<addr_t> assigned) {
  uint32_t path_index = getindex(assigned.at(page_off), depth, mask);
  assert(array[path_index] != nullptr);
  assert(array[path_index]->valid);
  if(array[path_index]->type == LEVEL_PTR) {
    array[path_index]->addr->populate_level(curr_field, page_off, assigned);
  } else if(array[path_index]->type == SST_ENTRY) {
    populate_sst_entry(path_index, curr_field, page_off, assigned);
  }
}
