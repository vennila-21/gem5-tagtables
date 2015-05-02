/*
 * Copyright (c) 1999-2012 Mark D. Hill and David A. Wood
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "base/intmath.hh"
#include "debug/RubyCache.hh"

#include "debug/RubyCacheTrace.hh"
#include "debug/RubyResourceStalls.hh"
#include "debug/RubyStats.hh"
#include "mem/protocol/AccessPermission.hh"
#include "mem/ruby/structures/CacheMemoryPF.hh"
//#include "mem/ruby/structures/CacheMemoryPFPF.hh"
#include "mem/ruby/system/System.hh"
//#include "mem/ruby/structures/entry-level.hh"


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

static inline int CeilLog2(uint n)
{
  return FloorLog2(n - 1) + 1;
}

int root_width=4;
int _num_chunks = 4;
int pageroot_depth=4;
int expansions =0;
int merges = 0;
int m_num_fields = _num_chunks;
int m_entry_size=uint32_t(pow(2, (CeilLog2((20 + _num_chunks*19)/8)))); //TODO :757 : GET SIZE OF ENTRY
std::list<l3map_entry> l3_mmap;
using namespace std;

ostream&
operator<<(ostream& out, const CacheMemoryPF& obj)
{
    obj.print(out);
    out << flush;
    return out;
}
bool order_blocks(block first, block second) {
  return (first.offset < second.offset);
}

bool is_tbl_ptr(entry *e) {
  return (e->type==LEVEL_PTR); //if "type" of entry is 1 => no, it's not a table ptr
}

void populate_sst_entry(entry *e, list<block> blocks) {
  //int no_blocks=blocks.size();
  //TODO:  make selection of field more intuitive (instead of starting in the first block for everything except those that are at subblock 3f).
  //assert(no_blocks <= int(m_num_fields));
  if(e->type == STATUS_VECTOR) {	//accumulate statistics for merges
    ++merges;
    //re-initialize all status vector entries
    for(int offset = 0; offset < (PAGESIZE/BLOCKSIZE); offset++) {	//initialize status vector entries to invalid
      e->status_vector[offset] = -1;
    }
  }
  e->type = SST_ENTRY;	//it is possible to call this on an entry that used to be a status vector
  list<block>::iterator it;
  uint i;
  for(it=blocks.begin(), i=0; it!=blocks.end(); it++, i++) {
    if((*it).len && ((*it).offset == 0)) {
      e->sst_e.fields[0].presence = 1;
      e->sst_e.fields[0].offset = (*it).offset;
      e->sst_e.fields[0].page_offset = (*it).page_offset;
      e->sst_e.fields[0].len = (*it).len;
      e->sst_e.fields[0].PFentry_ptr = (*it).PFentry_ptr;
    } else if((*it).len && ((*it).offset == ((PAGESIZE/BLOCKSIZE)-1))) {
      e->sst_e.fields[m_num_fields-1].presence = 1;
      e->sst_e.fields[m_num_fields-1].offset = (*it).offset;
      e->sst_e.fields[m_num_fields-1].page_offset = (*it).page_offset;
      e->sst_e.fields[m_num_fields-1].len = (*it).len;
      e->sst_e.fields[m_num_fields-1].PFentry_ptr = (*it).PFentry_ptr;
      while(i < m_num_fields-1) {
	e->sst_e.fields[i].presence = 0;
	if(i > 0) {
	  e->sst_e.fields[i].offset = e->sst_e.fields[i-1].offset + e->sst_e.fields[i-1].len;  //guaranteed to have at least an entry in the first field
	} else {
	  e->sst_e.fields[i].offset = 0;
	}
	e->sst_e.fields[i].len = 0;
	e->sst_e.fields[i].page_offset = -1;
	e->sst_e.fields[i].PFentry_ptr = NULL;
	++i;
      }
      i = m_num_fields; //Prevent overwriting last field later
      break;
    } else {
      if((*it).len) {
	e->sst_e.fields[i].presence = 1;
	e->sst_e.fields[i].offset = (*it).offset;
	e->sst_e.fields[i].page_offset = (*it).page_offset;
	e->sst_e.fields[i].len = (*it).len;
	 e->sst_e.fields[i].PFentry_ptr = (*it).PFentry_ptr;
      } else {
	e->sst_e.fields[i].presence = 0;
	if(i > 0) {
	  e->sst_e.fields[i].offset = e->sst_e.fields[i-1].offset + e->sst_e.fields[i-1].len;  //guaranteed to have at least an entry in the first field
	} else {
	  e->sst_e.fields[i].offset = 0;
	}
	e->sst_e.fields[i].len = 0;
	e->sst_e.fields[i].page_offset = -1;
	 e->sst_e.fields[i].PFentry_ptr = NULL;
      }
    }
  }
  for(; i < m_num_fields; i++) { //populate remaining empty entries
    assert(i > 0);
    e->sst_e.fields[i].presence = 0;
    e->sst_e.fields[i].offset = e->sst_e.fields[i-1].offset + e->sst_e.fields[i-1].len;  //guaranteed to have at least an entry in the first field
    e->sst_e.fields[i].len = 0;
    e->sst_e.fields[i].page_offset = -1;
    e->sst_e.fields[i].PFentry_ptr = NULL;
  }
  if(!e->sst_e.fields[m_num_fields-1].presence) {
    e->sst_e.fields[m_num_fields-1].len = ((PAGESIZE/BLOCKSIZE)-1) - e->sst_e.fields[m_num_fields-1].offset;
    e->sst_e.fields[m_num_fields-1].page_offset = -1;
    e->sst_e.fields[m_num_fields-1].PFentry_ptr = NULL;
  }
}

void CacheMemoryPF::L3_triggered_eviction(int64 evicted) {
  //evict all entries associated with 'evicted' from page table, polb, and translation cache (NOTE:  TC eviction occurs by merely removing a page table entry currently)
  //find map entry with seq_no associated with 'evicted'
  int no_ones = CeilLog2(MAXTRACES+1);
  uint64_t ones = (uint64_t)(pow(2,no_ones))-1;
  int shift_amt = ADDR_LEN - no_ones - CeilLog2(64 /*TODO: replace with variable*/);
  uint64_t clear_mask = ~(ones << shift_amt);
  /*TEST
    cout << "Masking 'evicted' with 0x" << hex << clear_mask << dec << " to get rid of metadata trace mask" << endl;
    //TEST*/
  evicted &= clear_mask;
/*
#if DEBUG_PINNING
  cout << "'evicted' corresponds to seq_no " << evicted << " (hex 0x" << hex << evicted << dec << ")" << endl;
#endif
*/
  uint seq_no = int(evicted);
  DPRINTF(TagTable, "L3_triggered_eviction - seq no :: %d",seq_no);
  DPRINTF(TagTable1, "L3_triggered_eviction - seq no :: %d",seq_no);
  list<l3map_entry>::iterator map_it;
  for(map_it = l3_mmap.begin(); map_it != l3_mmap.end(); map_it++) {
    if(map_it->seq_no == seq_no) break;
  }

  assert(map_it != l3_mmap.end());

  for(uint i = 0; i < map_it->size; i++) {	//evict entry associated with each tag
    assert(i < (BLOCKSIZE/m_entry_size));
    if(!map_it->is_lvl[i]) {	//an SST_ENTRY
      root->evict_entry(map_it->meta_tags[i]);
      //if(pt_polb != nullptr) {
	//DEBUG_MSG("Evicting POLB entry due to L3 triggered eviction");
	//pt_polb->evict(map_it->meta_tags[i] >> CeilLog2(BLOCKSIZE));
      //}
    } else {	//entry was associated with LEVEL_PTR => to prevent possible infinite recursion (l3_mmap evictions of LEVEL_PTR children), just relocate the associate entry
/*
#if DEBUG_PINNING
      cout << "This tag is associated with a LEVEL_PTR => reassigning" << endl;
#endif
  */    auto n_map_it = l3_mmap.end();
      n_map_it--;	//point to last valid entry
      uint idx = (BLOCKSIZE/m_entry_size);  //retain index location of new block (because 'size' can change after 'insert_metadata_block')
      if(n_map_it->size == (BLOCKSIZE/m_entry_size)) {	//need to create a new map entry
	l3map_entry n_map_entry(m_entry_size);
	n_map_entry.seq_no = n_map_it->seq_no + 1;
	assert(n_map_entry.seq_no < std::numeric_limits<uint>::max());
	n_map_entry.size = 0;
	// for(int init = 0; init < (BLOCKSIZE/m_entry_size); init++) {	//initialize 'tags' array
	//   n_map_entry.meta_tags[init] = 0;
	//   n_map_entry.data_tags[init] = 0;
	//   n_map_entry.is_lvl[init] = false;
	// }
	l3_mmap.push_back(n_map_entry);
	++n_map_it;	//should now point to new entry
	n_map_it->size++;
	idx = 0;
/*
#if DEBUG_PINNING
	cout << "Created new entry in L3 map => inserting into L3 ... ";
#endif
*/

	  insert_metadata_block(n_map_entry.seq_no);
	
/*
#if DEBUG_PINNING
	cout << "done" << endl;
#endif*/
      } else {
	idx = n_map_it->size;
	n_map_it->size++;
      }
      assert(idx < (BLOCKSIZE/m_entry_size));
      n_map_it->is_lvl[idx] = true;
      n_map_it->data_tags[idx] = map_it->data_tags[i];
      n_map_it->meta_tags[idx] = (n_map_it->seq_no << CeilLog2(BLOCKSIZE/m_entry_size));
      n_map_it->meta_tags[idx] += idx;	//this is a LEVEL_PTR => using tag that is concatenation of seq_no and array index
      uint64_t new_tag = n_map_it->meta_tags[idx];
      assert(n_map_it->size <= (BLOCKSIZE/m_entry_size));
/*
#if DEBUG_PINNING
      cout << "New tag for this relocated LEVEL_PTR is 0x" << hex << new_tag << " (previously 0x" << map_it->meta_tags[i] << ") " << dec << " in new seq_no " << n_map_it->seq_no << " and index " << idx << endl;
#endif
*/
      //need a unique tag for the L3_mmap (in order to find metadata when subsequently searching through this level pointer)
      // => concatenate map entry's seq no. and this entry's location in the associated array
      //tell root to find the entry on the "map_it->data_tags[i]" path with "meta_tags" and make it "new_tag"
      root->update_tag(map_it->data_tags[i], map_it->meta_tags[i], new_tag);	
    }
  }
  //remove entry from l3_mmap
  l3_mmap.erase(map_it);
}

void CacheMemoryPF::insert_metadata_block(uint seq_no) {	
	  //insert into L3
	  DPRINTF(TagTable1, "insert_metadata_block: ENTRY \n");
	  DPRINTF(TagTable, "insert_metadata_block: ENTRY \n");
  int64 metadata_addr = (uint64_t(MAXTRACES + 1) << (ADDR_LEN-CeilLog2(MAXTRACES+1))) + (seq_no << CeilLog2(64 /*TODO: replace with variable*/));
  DPRINTF(TagTable1, "insert_metadata_block: metadata_addr: %0x \n", metadata_addr);
  DPRINTF(TagTable, "insert_metadata_block: metadata_addr: %0x \n", metadata_addr);
 int evicted=0;
  
  //int64 victim = Probe_PF(Address(metadata_addr), true, evicted); 
  //ul3->Probe(metadata_addr, uint(1), CACHE_BASE::ACCESS_TYPE_LOAD, true, evicted);
  //L3_access_maintenance_cycles += (ul3->get_tag_access_time() + ul3->get_data_access_time());
  if(evicted!=0){
		//L3_triggered_eviction(victim);
}
  DPRINTF(TagTable, "insert_metadata_block: EXIT \n");
}


int64 CacheMemoryPF::Probe_PF(const Address& address, bool metadata, int& evicted)
{
  DPRINTF( TagTable, "Probe_PF ENTRY\n");
  DPRINTF( TagTable1, "Probe_PF address: %ld\n",address.m_address);
  assert(address == line_address(address));
  int64 cacheSet = addressToCacheSet(address);
  DPRINTF( TagTable1, "Probe_PF cacheSet: %ld\n",cacheSet);
  DPRINTF( TagTable, "Probe_PF cacheSet: %ld\n",cacheSet);
  int found = findTagInSet(cacheSet, address);
  DPRINTF( TagTable1, "Probe_PF loc in cacheSet: %d\n",found);
  
  if(found== -1){
    if(m_replacementPolicy_ptr == NULL)
    {
		DPRINTF( TagTable, "Probe_PF m_replacementPolicy_ptr is null\n" );
	}
 
  DPRINTF( TagTable1, "Probe_PF victim way: %d\n",m_replacementPolicy_ptr->getVictim(cacheSet));
  evicted = 1;
  //std::vector<AbstractCacheEntry*> temp = m_cache[cacheSet];
  //DPRINTF("FIRST CHECK DONE ");
  //std::vector<std::vector<AbstractCacheEntry*> > another_temp = m_cache[cacheSet][m_replacementPolicy_ptr->getVictim(cacheSet)];
  //DPRINTF("SECOND CHECK DONE");
  int64 victim = m_cache[cacheSet][m_replacementPolicy_ptr->getVictim(cacheSet)]->m_Address.m_address;
  DPRINTF( TagTable1, "Probe_PF victim: %0x\n",victim);
   DPRINTF( TagTable, "Probe_PF EXIT\n");
  return victim;
  
//TODO need to check if evicted is metadata. if do not set it to address, but 0.
}
return 0;
}

CacheMemoryPF *
RubyCachePFParams::create()
{
    return new CacheMemoryPF(this);
}

CacheMemoryPF::CacheMemoryPF(const Params *p)
    : SimObject(p),
    dataArray(p->dataArrayBanks, p->dataAccessLatency, p->start_index_bit),
    tagArray(p->tagArrayBanks, p->tagAccessLatency, p->start_index_bit)
{
    m_cache_size = p->size;
    m_latency = p->latency;
    m_cache_assoc = p->assoc;
    m_policy = p->replacement_policy;
    m_start_index_bit = p->start_index_bit;
    m_is_instruction_only_cache = p->is_icache;
    m_resource_stalls = p->resourceStalls;
    root =  new entry_level(root_width, 0, _num_chunks, pageroot_depth, NULL);
     
}

void
CacheMemoryPF::init()
{
    m_cache_num_sets = (m_cache_size / m_cache_assoc) /
        RubySystem::getBlockSizeBytes();
    assert(m_cache_num_sets > 1);
    m_cache_num_set_bits = floorLog2(m_cache_num_sets);
    assert(m_cache_num_set_bits > 0);
    DPRINTF(TagTable, " CacheMemoryPF::init()\n");
    DPRINTF(TagTable, " CacheMemoryPF:: m_cache_assoc: %d \n", m_cache_assoc);
     m_replacementPolicy_ptr = new LRUPolicy(m_cache_num_sets, m_cache_assoc);
     
    DPRINTF( TagTable, "CacheMemoryPF::init() m_replacementPolicy_ptr: %0xp\n",m_replacementPolicy_ptr);
    assert(m_replacementPolicy_ptr != NULL);
		
    m_cache.resize(m_cache_num_sets);
    for (int i = 0; i < m_cache_num_sets; i++) {
        m_cache[i].resize(m_cache_assoc);
        for (int j = 0; j < m_cache_assoc; j++) {
            m_cache[i][j] = NULL;
        }
    }
}


Address
CacheMemoryPF::cacheProbe(const Address& address)const
{
    assert(address == line_address(address));
    assert(!cacheAvail(address));

    int64 cacheSet = addressToCacheSet(address);
     return m_cache[cacheSet][m_replacementPolicy_ptr->getVictim(cacheSet)]->
        m_Address;
     
}

CacheMemoryPF::~CacheMemoryPF()
{
    if (m_replacementPolicy_ptr != NULL)
        delete m_replacementPolicy_ptr;
    for (int i = 0; i < m_cache_num_sets; i++) {
        for (int j = 0; j < m_cache_assoc; j++) {
            delete m_cache[i][j];
        }
    }
}

// convert a Address to its location in the cache
int64
CacheMemoryPF::addressToCacheSet(const Address& address) const
{
    assert(address == line_address(address));
    
    return address.bitSelect(m_start_index_bit, m_start_index_bit + m_cache_num_set_bits - 1);
}

// Given a cache index: returns the index of the tag in a set.
// returns -1 if the tag is not found.
int
CacheMemoryPF::findTagInSet(int64 cacheSet, const Address& tag) const
{
    assert(tag == line_address(tag));
    // search the set for the tags
    m5::hash_map<Address, int>::const_iterator it = m_tag_index.find(tag);
    if (it != m_tag_index.end())
        if (m_cache[cacheSet][it->second]->m_Permission !=
            AccessPermission_NotPresent)
            return it->second;
    return -1; // Not found
}

// Given a cache index: returns the index of the tag in a set.
// returns -1 if the tag is not found.
int
CacheMemoryPF::findTagInSetIgnorePermissions(int64 cacheSet,
                                           const Address& tag) const
{
    assert(tag == line_address(tag));
    // search the set for the tags
    m5::hash_map<Address, int>::const_iterator it = m_tag_index.find(tag);
    if (it != m_tag_index.end())
        return it->second;
    return -1; // Not found
}

bool
CacheMemoryPF::tryCacheAccess(const Address& address, RubyRequestType type,
                            DataBlock*& data_ptr)
{
    assert(address == line_address(address));
    DPRINTF(RubyCache, "address: %s\n", address);
    int64 cacheSet = addressToCacheSet(address);
    int loc = findTagInSet(cacheSet, address);
    if (loc != -1) {
        // Do we even have a tag match?
        AbstractCacheEntry* entry = m_cache[cacheSet][loc];
        m_replacementPolicy_ptr->touch(cacheSet, loc, curTick());
        data_ptr = &(entry->getDataBlk());

        if (entry->m_Permission == AccessPermission_Read_Write) {
            return true;
        }
        if ((entry->m_Permission == AccessPermission_Read_Only) &&
            (type == RubyRequestType_LD || type == RubyRequestType_IFETCH)) {
            return true;
        }
        // The line must not be accessible
    }
    data_ptr = NULL;
    return false;
}

bool
CacheMemoryPF::testCacheAccess(const Address& address, RubyRequestType type,
                             DataBlock*& data_ptr)
{
    assert(address == line_address(address));
    DPRINTF(RubyCache, "address: %s\n", address);
    int64 cacheSet = addressToCacheSet(address);
    int loc = findTagInSet(cacheSet, address);

    if (loc != -1) {
        // Do we even have a tag match?
        AbstractCacheEntry* entry = m_cache[cacheSet][loc];
        m_replacementPolicy_ptr->touch(cacheSet, loc, curTick());
        data_ptr = &(entry->getDataBlk());

        return m_cache[cacheSet][loc]->m_Permission !=
            AccessPermission_NotPresent;
    }

    data_ptr = NULL;
    return false;
}

// tests to see if an address is present in the cache
bool
CacheMemoryPF::isTagPresent(const Address& address) const
{
    assert(address == line_address(address));
    int64 cacheSet = addressToCacheSet(address);
    int loc = findTagInSet(cacheSet, address);

    if (loc == -1) {
        // We didn't find the tag
        DPRINTF(RubyCache, "No tag match for address: %s\n", address);
        return false;
    }
    DPRINTF(RubyCache, "address: %s found\n", address);
    return true;
}

// Returns true if there is:
//   a) a tag match on this address or there is
//   b) an unused line in the same cache "way"
bool
CacheMemoryPF::cacheAvail(const Address& address) const
{
    assert(address == line_address(address));

    int64 cacheSet = addressToCacheSet(address);

    for (int i = 0; i < m_cache_assoc; i++) {
        AbstractCacheEntry* entry = m_cache[cacheSet][i];
        if (entry != NULL) {
            if (entry->m_Address == address ||
                entry->m_Permission == AccessPermission_NotPresent) {
                // Already in the cache or we found an empty entry
                return true;
            }
        } else {
            return true;
        }
    }
    return false;
}
AbstractCacheEntry* CacheMemoryPF::check_for_hit(uint64_t  addr, entry *e) {
  
  int subblock = (addr >> FloorLog2(BLOCKSIZE)) & ((PAGESIZE/BLOCKSIZE) - 1);  //subblock within entry that this address represents
  	DPRINTF(TagTable, "check_for_hit :: addr : %0x , subblock : %d\n", addr, subblock);
    if(e->type == SST_ENTRY) {
		DPRINTF(TagTable, "check_for_hit :: SST_ENTRY\n");
      for(uint i = 0; i < m_num_fields; i++) {
	if((subblock >= e->sst_e.fields[i].offset) && (subblock < (e->sst_e.fields[i].offset+e->sst_e.fields[i].len)) && e->sst_e.fields[i].presence) {  //address is in this - present - field
	  DPRINTF(TagTable, "check_for_hit :: PFentry_ptr returned :%0xp\n" ,e->sst_e.fields[i].PFentry_ptr);
	  DPRINTF(TagTable, "check_for_hit :: offset matched:%d, len: %d\n" ,e->sst_e.fields[i].offset,e->sst_e.fields[i].len );
	  DPRINTF(TagTable, "check_for_hit :: index : %d\n",i);
	  DPRINTF(TagTable," check_for_hit: entrypointer : %0xp\n", e);
	  //DPRINTF(TagTable," check_for_hit: entry_1 m_address : %0x\n", e->m_Address.m_address);
	  return e->sst_e.fields[i].PFentry_ptr;
  }
  
}
DPRINTF(TagTable, "check_for_hit :: returning NULL!!");
return NULL;
}
else {
DPRINTF(TagTable, "check_for_hit :: returning null\n, not SST,");
return NULL;
}
}



AbstractCacheEntry*
CacheMemoryPF::allocate(const Address& address, AbstractCacheEntry* entry)
{
    assert(address == line_address(address));
    assert(!isTagPresent(address));
    assert(cacheAvail(address));
    DPRINTF(RubyCache, "address: %s\n", address);

    // Find the first open slot
    int64 cacheSet = addressToCacheSet(address);
    std::vector<AbstractCacheEntry*> &set = m_cache[cacheSet];
    for (int i = 0; i < m_cache_assoc; i++) {
        if (!set[i] || set[i]->m_Permission == AccessPermission_NotPresent) {
            set[i] = entry;  // Init entry
            set[i]->m_Address = address;
            set[i]->m_Permission = AccessPermission_Invalid;
            DPRINTF(RubyCache, "Allocate clearing lock for addr: %x\n",
                    address);
            set[i]->m_locked = -1;
            m_tag_index[address] = i;

           m_replacementPolicy_ptr->touch(cacheSet, i, curTick());

            return entry;
        }
    }
    panic("Allocate didn't find an available entry");
}

AbstractCacheEntry* CacheMemoryPF::lookupPF(const Address& addr){//, int size, ACCESS_TYPE accessType, uint64_t &unified_cycles) {
  int levels_walked = 0;
  uint64_t address = addr.m_address;
  DPRINTF(TagTable, "lookupPF: addr: %ld\n", address);
  //short page_offset = -1;	//page offset for populating POLB entry on miss
	    //check POLB (page offset lookaside buffer)
	     entry *e = search_level(address, NULL,levels_walked);//, unified_cycles);
	     if(!e->valid) {
			return NULL;
		} else {
		return check_for_hit(address, e);
	}

}

// looks an address up in the cache
const AbstractCacheEntry*
CacheMemoryPF::lookupCacheMemory(const Address& address) const
{
    assert(address == line_address(address));
    int64 cacheSet = addressToCacheSet(address);
    int loc = findTagInSet(cacheSet, address);
    if(loc == -1) return NULL;
    return m_cache[cacheSet][loc];
}

entry* CacheMemoryPF::search_level(uint64_t addr, entry_level *lvl, int &levels_walked){//, uint64_t &unified_cycles) {
  DPRINTF(TagTable, "search_level:: ENTRY \n");
  ++levels_walked;
  if(lvl == NULL) {  //searching root
    lvl = root;
  }
  assert(lvl->get_depth() < DEPTH);
  DPRINTF(TagTable, "search_level:: addr: %0x, depth: %d, mask: %d\n", addr,lvl->get_depth(),lvl->get_mask()  );
 
  int index = getindex(addr, lvl->get_depth(), lvl->get_mask());
  
  
  entry *e = lvl->lookup(index);
  if(!e->valid) {  //miss
  DPRINTF(TagTable, "search_level:: EXIT 1\n");
    return e;
  } else {
/*    addr_t metadata_addr = addr_t(pow(2,ADDR_LEN));
    for(auto map_it = l3_mmap.begin(); map_it != l3_mmap.end(); map_it++) {
      for(uint i = 0; i < (BLOCKSIZE/m_entry_size); i++) {
	if(map_it->meta_tags[i] == e->tag) {	//match
	  metadata_addr = (addr_t(MAXTRACES + 1) << (ADDR_LEN-CeilLog2(MAXTRACES+1))) + (map_it->seq_no << CeilLog2(64 ));
	  break;
	}
      }
    }*/
    if(is_tbl_ptr(e)) { 	//walk to next level
/*      if((transcache_cap > 0) && (e->tc_path > 0)) {	//the location of the next level is in the translation cache
	levels_walked--;
	assert(levels_walked >= 0);
	++tc_levels_saved;
      }*/
      e = search_level(addr, e->addr, levels_walked);//, unified_cycles);
    }
/*    if(e->valid && (check_for_hit(addr, e) != -1) && (e->type != LEVEL_PTR)) {
      //Account for hit for this level
      traverse_distribution[lvl->get_depth()]++;
    }*/
  }//valid entry
  DPRINTF(TagTable, "search_level:: EXIT 2\n");
  return e;
  
}

std::list<block> build_entry(std::list<block> present_blocks) {
	DPRINTF(TagTable," Entering build_entry\n");
  std::list<block>::iterator it;
  present_blocks.sort(order_blocks);
  //find merge opportunities
  for(it=present_blocks.begin(); it!=present_blocks.end(); it++) {
    std::list<block>::iterator nit = it;  //nit = next iterator
    ++nit;
    while((nit != present_blocks.end()) && 
	  ((*it).offset + (*it).len == (*nit).offset) && 
	  ((*it).page_offset != -1) && 
	  ((*it).page_offset + (*it).len == (*nit).page_offset)/*make sure page offsets are also contiguous*/) {
      (*it).len += (*nit).len;
      
      if((*it).page_offset == -1) {	//this entry still needs a page_offset assigned => don't lose value of merged page_offset to allow it to be used
	(*it).page_offset = ((*nit).page_offset | (PAGESIZE/BLOCKSIZE));  //flip bit just beyond max as a signal
      }
      present_blocks.erase(nit);
      nit = it;
      ++nit;
    }
  }
DPRINTF(TagTable," Exiting build_entry\n");
  //TODO:  check to see if it makes sense to collapse this entry into the upper level
  return present_blocks;
}

int64 CacheMemoryPF::getMetadataaddr(uint seqno){
	DPRINTF(TagTable, " getMetadataaddr : ENTRY\n");
	
int64 metadata_addr = (uint64_t(MAXTRACES + 1) << (ADDR_LEN-CeilLog2(MAXTRACES+1))) + (seqno << CeilLog2(64 /*TODO: replace with variable*/));
DPRINTF(TagTable, " getMetadataaddr : EXIT , metadata_addr= %ld\n",metadata_addr);
return metadata_addr;
}

AbstractCacheEntry* CacheMemoryPF::AllocatePF(const Address& address, AbstractCacheEntry* entry_1)
{
	allocate(address, entry_1);
	return allocatePF_inner(address, entry_1, NULL);
}

AbstractCacheEntry* CacheMemoryPF::allocatePF_inner(const Address& address, AbstractCacheEntry* entry_1, entry_level *lvl){
	
	 DPRINTF(TagTable, "allocatePF_inner:: ENTRY \n");
	 DPRINTF(TagTable, "allocatePF_inner:: entry_1 : %0xp\n",entry_1);
	 DPRINTF(TagTable," check_for_hit:  m_address : %0x\n", entry_1->m_Address.m_address);
uint64_t addr = address.m_address;
short page_offset = -1;
int index;
int width;

if(lvl == NULL){
lvl = root;
}

DPRINTF(TagTable, "allocatePF_inner addr: %0x \n", addr);
index = getindex(addr, lvl->get_depth(), lvl->get_mask());
width = 4;
entry *e = lvl->lookup(index);
if(!e->valid) {  //no valid data is stored at this location => insert
    e->valid = true; //entry is now valid
    e->type = SST_ENTRY;  //entry is an SST (as opposed to a pointer to another level)
    e->tag = get_tag(addr);
 if(l3_mmap.empty()) {       //this will be the first entry in the map
      l3map_entry n_map_entry(m_entry_size);
      
      n_map_entry.seq_no = 0;
      n_map_entry.size = 0;
      l3_mmap.push_back(n_map_entry);
	insert_metadata_block(n_map_entry.seq_no);
	
DPRINTF(TagTable, "allocatePF_inner:  l3map_entry created\n");
 //entry_1->m_Address =  Address(getMetadataaddr(n_map_entry.seq_no));
}
	auto map_it = l3_mmap.end();
    map_it--;	//point to valid entry
    uint idx = (BLOCKSIZE/m_entry_size);
 if(map_it->size == (BLOCKSIZE/m_entry_size)) {	//need to create a new map entry
      l3map_entry n_map_entry(m_entry_size);
      n_map_entry.seq_no = map_it->seq_no + 1;
      assert(n_map_entry.seq_no < std::numeric_limits<uint>::max());
      n_map_entry.size = 0;
       l3_mmap.push_back(n_map_entry);
      ++map_it;	//should now point to new entry
      map_it->size++;
      idx = 0;
	insert_metadata_block(map_it->seq_no); 
      
	}
else {
      idx = map_it->size;
      map_it->size++;
    }
	map_it->meta_tags[idx] = e->tag;
    map_it->data_tags[idx] = e->tag;
list<block> insert_block;
    block temp;
    temp.offset = ((addr >> FloorLog2(64))) & ((4096/64)-1);
    
    temp.len = 1;
    temp.page_offset = lvl->replace(temp.offset, expansions, merges); //determine if this insertion necessitates a replacement and make it (i.e., 64 blocks already tracked)
	DPRINTF(TagTable," allocatePF_inner: offset calculated for entry : %d\n", temp.page_offset);
    page_offset = temp.page_offset;
    temp.PFentry_ptr = entry_1;
    insert_block.push_front(temp);  //subblock based on address
    insert_block = build_entry(insert_block);
    populate_sst_entry(e, insert_block);
    DPRINTF(TagTable," allocatePF_inner: entryptr : %0xp\n", e);
   DPRINTF(TagTable," allocatePF_inner:  PFentryptr set: %0xp \n",  e->sst_e.fields[0].PFentry_ptr);
     
}
else{ //entry is valid => if not at leaf, make sure current address' tag matches the entry's, else split them at a new level
    if(is_tbl_ptr(e)) {
	DPRINTF(TagTable," AllocatePF : its a tbl ptr, calling allocate PF recursively : %d\n");
      allocatePF_inner(address, entry_1 ,e->addr);
      //Fix up my tc_path if something has been inserted (e.g., an SST_ENTRY was pushed down and a new entry created along with it in the new level)
    } else { 
if(e->tag == get_tag(addr)) {
	page_offset = insert_into_existing(addr, e, lvl, entry_1);
	// TODO get address of exising entry
      } else {	//need to split until a level where they are mapped to different indices (use recursion)
	//determine what - if any is currently known - the new level will have for a page root
	entry_level *npage_root = NULL;	//page_root to pass to new entry_level (if it's level 3)
	if(lvl->get_depth() >= pageroot_depth) {
	  if(lvl->get_depth() == pageroot_depth) {	//pass current level's pointer, else pass current level's page_root
	    npage_root = lvl;
	  } else {
	    npage_root = lvl->get_pageroot();
	  }
	}
	//create new level for both entries
	e->addr = new entry_level(width, lvl->get_depth()+1, m_num_fields, pageroot_depth, npage_root);
	/*DEBUG
	cout << "Copying entry:" << endl;
	print_entry(e);
	//DEBUG*/
	//copy existing :`entry to new level
	//entry *nentry = e->addr->copy_entry(e);
	e->type = LEVEL_PTR;
e->sst_e.prefetch_vector.reset();
	for(uint field = 0; field < m_num_fields; field++) {
	  e->sst_e.fields[field].presence = 0;
	  e->sst_e.fields[field].page_offset = -1;
	  e->sst_e.fields[field].len = 0;
	  e->sst_e.fields[field].offset = -1;
	  e->sst_e.fields[field].PFentry_ptr=NULL;
	}
	for(int offset = 0; offset < (PAGESIZE/BLOCKSIZE); offset++) {	//initialize status vector entries to invalid
	  e->status_vector[offset] = -1;
	}
	//find next empty entry in the L3_mmap to place this LEVEL_PTR entry
	uint64_t new_tag = 0;
	page_offset = 0;
	auto map_it = l3_mmap.end();
	map_it--;	//point to valid entry
	uint idx = (BLOCKSIZE/m_entry_size) + page_offset;  //retain index location of new block (can't key off 'size' because it can change after 'insert_metadata_block')
	if(map_it->size == (BLOCKSIZE/m_entry_size)) {	//need to create a new map entry
	  l3map_entry n_map_entry(m_entry_size);
	  n_map_entry.seq_no = map_it->seq_no + 1;
	  assert(n_map_entry.seq_no < std::numeric_limits<uint>::max());
	  n_map_entry.size = 0;
 l3_mmap.push_back(n_map_entry);
	  ++map_it;	//should now point to new entry
	  map_it->size++;
	  idx = 0;
	  
	    insert_metadata_block(n_map_entry.seq_no); 
       
	  
	} else {
	  idx = map_it->size;
	  map_it->size++;
	}
        map_it->is_lvl[idx] = true;	//indicate that the last entry is a lvl pointer
	map_it->data_tags[idx] = e->tag;  //retain tag associated with actual data so I can walk directly to this entry if necessary (e.g., in L3_triggered_eviction)
	map_it->meta_tags[idx] = (map_it->seq_no << CeilLog2(BLOCKSIZE/m_entry_size));
	map_it->meta_tags[idx] += idx;	//this is a LEVEL_PTR => using tag thats is concatenation of seq_no and array index
	new_tag = map_it->meta_tags[idx];
	//assert(map_it->size <= (BLOCKSIZE/m_entry_size));
//#if DEBUG_PINNING
//	cout << "New tag for this entry-turned-LEVEL_PTR is 0x" << hex << new_tag << dec << " from seq_no " << map_it->seq_no << " and index " << idx << endl;
//#endif
	//need a unique tag for the L3_mmap => concatenate map entry's seq no. and this entry's location in the associated array
//	assert(e->type == LEVEL_PTR);
	e->tag = new_tag;
	insert_metadata_block(map_it->seq_no);
  
       
	

	//descend and attempt to insert the address again
	allocatePF_inner(address, entry_1, e->addr);
      }
    }
  }

  //In case a new path was created below me, I need to verify my TC path value (this should be much less heavy handed than calling fix_tc_paths regularly)
  //if((transcache_cap > 0) && (e->type == LEVEL_PTR)) {
    //int occ = e->addr->get_tc_occupancy();
    //e->tc_path = occ;
  //}

  /*TEST
  if(e->tag == 0x7fe782f2e000) {
    print_entry(e);
  }
  //TEST*/
 DPRINTF(TagTable, "allocatePF_inner:: EXIT \n");
  //assert(page_offset != -1);
  
  return entry_1;
}



uint64_t CacheMemoryPF::getindex(uint64_t addr, int lvl, int mask) {
  /*TEST
  if(lvl > 1) cout << "Address 0x" << hex << addr << dec;
  //TEST*/
  
  uint64_t index = uint64_t(pow(2,ADDR_LEN));
  
  DPRINTF(TagTable, "getindex index_before: %d\n",index);
  assert(lvl < DEPTH);
	
  //shift off block & page offsets
  uint64_t temp_addr = addr >> (FloorLog2(BLOCKSIZE)+FloorLog2((PAGESIZE/BLOCKSIZE)));
  
  //shift off the bits of every level above this
  for(int lvl_depth = 0; lvl_depth < lvl; lvl_depth++) {
    temp_addr >>= 4;
  }
  index = temp_addr & mask;
  DPRINTF(TagTable, "getindex index_after: %d\n",index);
  assert(unsigned(index) != uint64_t(pow(2,ADDR_LEN)));
	DPRINTF(TagTable, "getindex :: addr input : %ld index: %d\n",addr,index);
  return index;
}

//Determine the tag (address bits to uniquely identify leaf entry for this address - i.e., everything but block & page offsets) for a given address
int64_t CacheMemoryPF::get_tag(uint64_t addr) { 
	     //determine tag necessary for this entry
	DPRINTF(TagTable, "get_tag : m_start_index_bit: %d cache_num_set_bits: %0x, m_start_index_bit: %0x \n",   m_cache_num_sets, m_cache_num_set_bits, m_start_index_bit );     
  int64_t tag = addr >> (FloorLog2(BLOCKSIZE) + FloorLog2(PAGESIZE/BLOCKSIZE));
  DPRINTF(TagTable, "get_tag : addr %0x\n", addr);
  DPRINTF(TagTable, "get_tag : tag  %0x\n", tag);
  //FIXME : 757
  //tag <<= (FloorLog2(BLOCKSIZE) + FloorLog2(PAGESIZE/BLOCKSIZE));	//shift 0's back in
  /*TEST
  cout << "tag for entry with address 0x" << hex << addr << " is 0x" << tag << dec << endl;
  //TEST*/
  assert(tag != int64_t(pow(2,ADDR_LEN)));

  return tag;
}

void
CacheMemoryPF::deallocate(const Address& address)
{   DPRINTF(TagTable, "deallocate: address: %0x \n", address.m_address);
    assert(address == line_address(address));
    assert(isTagPresent(address));
    DPRINTF(RubyCache, "address: %s\n", address);
    int64 cacheSet = addressToCacheSet(address);
    int loc = findTagInSet(cacheSet, address);
    if (loc != -1) {
        delete m_cache[cacheSet][loc];
        m_cache[cacheSet][loc] = NULL;
        m_tag_index.erase(address);
    }
}



// looks an address up in the cache
AbstractCacheEntry*
CacheMemoryPF::lookup(const Address& address)
{
    assert(address == line_address(address));
    int64 cacheSet = addressToCacheSet(address);
    int loc = findTagInSet(cacheSet, address);
    if(loc == -1) return NULL;
    return m_cache[cacheSet][loc];
}

// looks an address up in the cache
const AbstractCacheEntry*
CacheMemoryPF::lookup(const Address& address) const
{
    assert(address == line_address(address));
    int64 cacheSet = addressToCacheSet(address);
    int loc = findTagInSet(cacheSet, address);
    if(loc == -1) return NULL;
    return m_cache[cacheSet][loc];
}

// Sets the most recently used bit for a cache block
void
CacheMemoryPF::setMRU(const Address& address)
{
    int64 cacheSet = addressToCacheSet(address);
    int loc = findTagInSet(cacheSet, address);

    if(loc != -1){
       m_replacementPolicy_ptr->touch(cacheSet, loc, curTick()); }
}

void
CacheMemoryPF::recordCacheContents(int cntrl, CacheRecorder* tr) const
{
    uint64 warmedUpBlocks = 0;
    uint64 totalBlocks M5_VAR_USED = (uint64)m_cache_num_sets
                                                  * (uint64)m_cache_assoc;

    for (int i = 0; i < m_cache_num_sets; i++) {
        for (int j = 0; j < m_cache_assoc; j++) {
            if (m_cache[i][j] != NULL) {
                AccessPermission perm = m_cache[i][j]->m_Permission;
                RubyRequestType request_type = RubyRequestType_NULL;
                if (perm == AccessPermission_Read_Only) {
                    if (m_is_instruction_only_cache) {
                        request_type = RubyRequestType_IFETCH;
                    } else {
                        request_type = RubyRequestType_LD;
                    }
                } else if (perm == AccessPermission_Read_Write) {
                    request_type = RubyRequestType_ST;
                }

                if (request_type != RubyRequestType_NULL) {
                    tr->addRecord(cntrl, m_cache[i][j]->m_Address.getAddress(),
                                  0, request_type,
                                  m_replacementPolicy_ptr->getLastAccess(i, j),
                                  m_cache[i][j]->getDataBlk());
                    warmedUpBlocks++;
                }
            }
        }
    }

    DPRINTF(RubyCacheTrace, "%s: %lli blocks of %lli total blocks"
            "recorded %.2f%% \n", name().c_str(), warmedUpBlocks,
            (uint64)m_cache_num_sets * (uint64)m_cache_assoc,
            (float(warmedUpBlocks)/float(totalBlocks))*100.0);
}

void
CacheMemoryPF::print(ostream& out) const
{
    out << "Cache dump: " << name() << endl;
    for (int i = 0; i < m_cache_num_sets; i++) {
        for (int j = 0; j < m_cache_assoc; j++) {
            if (m_cache[i][j] != NULL) {
                out << "  Index: " << i
                    << " way: " << j
                    << " entry: " << *m_cache[i][j] << endl;
            } else {
                out << "  Index: " << i
                    << " way: " << j
                    << " entry: NULL" << endl;
            }
        }
    }
}

void
CacheMemoryPF::printData(ostream& out) const
{
    out << "printData() not supported" << endl;
}

void
CacheMemoryPF::setLocked(const Address& address, int context)
{
    DPRINTF(RubyCache, "Setting Lock for addr: %x to %d\n", address, context);
    assert(address == line_address(address));
    int64 cacheSet = addressToCacheSet(address);
    int loc = findTagInSet(cacheSet, address);
    assert(loc != -1);
    m_cache[cacheSet][loc]->m_locked = context;
}

void
CacheMemoryPF::clearLocked(const Address& address)
{
    DPRINTF(RubyCache, "Clear Lock for addr: %x\n", address);
    assert(address == line_address(address));
    int64 cacheSet = addressToCacheSet(address);
    int loc = findTagInSet(cacheSet, address);
    assert(loc != -1);
    m_cache[cacheSet][loc]->m_locked = -1;
}

bool
CacheMemoryPF::isLocked(const Address& address, int context)
{
    assert(address == line_address(address));
    int64 cacheSet = addressToCacheSet(address);
    int loc = findTagInSet(cacheSet, address);
    assert(loc != -1);
    DPRINTF(RubyCache, "Testing Lock for addr: %llx cur %d con %d\n",
            address, m_cache[cacheSet][loc]->m_locked, context);
    return m_cache[cacheSet][loc]->m_locked == context;
}

void
CacheMemoryPF::regStats()
{
    m_demand_hits
        .name(name() + ".demand_hits")
        .desc("Number of cache demand hits")
        ;

    m_demand_misses
        .name(name() + ".demand_misses")
        .desc("Number of cache demand misses")
        ;

    m_demand_accesses
        .name(name() + ".demand_accesses")
        .desc("Number of cache demand accesses")
        ;

    m_demand_accesses = m_demand_hits + m_demand_misses;

    m_sw_prefetches
        .name(name() + ".total_sw_prefetches")
        .desc("Number of software prefetches")
        .flags(Stats::nozero)
        ;

    m_hw_prefetches
        .name(name() + ".total_hw_prefetches")
        .desc("Number of hardware prefetches")
        .flags(Stats::nozero)
        ;

    m_prefetches
        .name(name() + ".total_prefetches")
        .desc("Number of prefetches")
        .flags(Stats::nozero)
        ;

    m_prefetches = m_sw_prefetches + m_hw_prefetches;

    m_accessModeType
        .init(RubyRequestType_NUM)
        .name(name() + ".access_mode")
        .flags(Stats::pdf | Stats::total)
        ;
    for (int i = 0; i < RubyAccessMode_NUM; i++) {
        m_accessModeType
            .subname(i, RubyAccessMode_to_string(RubyAccessMode(i)))
            .flags(Stats::nozero)
            ;
    }

    numDataArrayReads
        .name(name() + ".num_data_array_reads")
        .desc("number of data array reads")
        .flags(Stats::nozero)
        ;

    numDataArrayWrites
        .name(name() + ".num_data_array_writes")
        .desc("number of data array writes")
        .flags(Stats::nozero)
        ;

    numTagArrayReads
        .name(name() + ".num_tag_array_reads")
        .desc("number of tag array reads")
        .flags(Stats::nozero)
        ;

    numTagArrayWrites
        .name(name() + ".num_tag_array_writes")
        .desc("number of tag array writes")
        .flags(Stats::nozero)
        ;

    numTagArrayStalls
        .name(name() + ".num_tag_array_stalls")
        .desc("number of stalls caused by tag array")
        .flags(Stats::nozero)
        ;

    numDataArrayStalls
        .name(name() + ".num_data_array_stalls")
        .desc("number of stalls caused by data array")
        .flags(Stats::nozero)
        ;
}

void
CacheMemoryPF::recordRequestType(CacheRequestType requestType)
{
    DPRINTF(RubyStats, "Recorded statistic: %s\n",
            CacheRequestType_to_string(requestType));
    switch(requestType) {
    case CacheRequestType_DataArrayRead:
        numDataArrayReads++;
        return;
    case CacheRequestType_DataArrayWrite:
        numDataArrayWrites++;
        return;
    case CacheRequestType_TagArrayRead:
        numTagArrayReads++;
        return;
    case CacheRequestType_TagArrayWrite:
        numTagArrayWrites++;
        return;
    default:
        warn("CacheMemoryPF access_type not found: %s",
             CacheRequestType_to_string(requestType));
    }
}

bool
CacheMemoryPF::checkResourceAvailable(CacheResourceType res, Address addr)
{
    if (!m_resource_stalls) {
        return true;
    }

    if (res == CacheResourceType_TagArray) {
        if (tagArray.tryAccess(addressToCacheSet(addr))) return true;
        else {
            DPRINTF(RubyResourceStalls,
                    "Tag array stall on addr %s in set %d\n",
                    addr, addressToCacheSet(addr));
            numTagArrayStalls++;
            return false;
        }
    } else if (res == CacheResourceType_DataArray) {
        if (dataArray.tryAccess(addressToCacheSet(addr))) return true;
        else {
            DPRINTF(RubyResourceStalls,
                    "Data array stall on addr %s in set %d\n",
                    addr, addressToCacheSet(addr));
            numDataArrayStalls++;
            return false;
        }
    } else {
        assert(false);
        return true;
    }
}

struct c_unique {
  uint32_t current;
  c_unique() { current=0; }
  uint32_t operator()() { return current++; }
} UniqueNumber;

std::bitset<PAGESIZE/BLOCKSIZE> build_presence(std::list<block> present_blocks) {
  /*TEST
  std::cout << "Build Presence" << std::endl;
  //TEST*/
  std::bitset<PAGESIZE/BLOCKSIZE> presence_vector; //initializes all bits to '0'

  for(const auto &chunk : present_blocks) {
    /*TEST
    cout << chunk;
    //TEST*/
    if(chunk.page_offset != short(-1)) { //don't populate presence for block I'm trying to insert
      for(uint32_t pblock = chunk.page_offset; pblock < uint32_t(chunk.page_offset + chunk.len); ++pblock) {
	assert(!presence_vector.test(pblock));  //should not already be set
	presence_vector.set(pblock);
      }
    }
  }
 
  return presence_vector;
}

std::list<block> CacheMemoryPF::build_blocks(entry *e) {
	DPRINTF(TagTable ,"CacheMemoryPF::build_blocks entry\n");
  std::list<block> blocks;
  block temp;
  if((e != NULL) && e->valid) {	//can be invalid if only block was evicted by replace()
    if(e->type == SST_ENTRY) {
		DPRINTF(TagTable ,"CacheMemoryPF::build_blocks if condition \n");
      //determine which subblocks are already present
      for(uint j = 0; j < m_num_fields; j++) {
	if(e->sst_e.fields[j].presence) {
	  temp.offset = e->sst_e.fields[j].offset;
	  temp.page_offset = short(e->sst_e.fields[j].page_offset);
	  temp.len = e->sst_e.fields[j].len;
	  temp.PFentry_ptr= e->sst_e.fields[j].PFentry_ptr;
	  blocks.push_back(temp);
	}
      }
    } else if (e->type == STATUS_VECTOR) {
		DPRINTF(TagTable ,"CacheMemoryPF::build_blocks else if condition Status vector\n");
      for(int i = 0; i < (PAGESIZE/BLOCKSIZE); i++) {
	if(e->status_vector[i] != -1) { //block is present
	  int offset = i;
	  int expected = e->status_vector[i];	//page offsets need to be contiguous to lump into a "block"
	  while((i < (PAGESIZE/BLOCKSIZE)) && (e->status_vector[i] == expected)) {
	    ++i;
	    ++expected;
	  }
	  temp.offset = offset;
	  temp.page_offset = e->status_vector[offset];
	  temp.len = i-offset;
	  
	  blocks.push_back(temp);
	  if((i < (PAGESIZE/BLOCKSIZE)) && (e->status_vector[i] != -1)) {	//break out of 'while' loop due to non-contiguous - but valid - page offset => need to prevent 'for' loop from incrementing me beyond this entry (by decrementing i to make up for the increment it will get)
	    i--;
	  }
	}
      }
    } 
  } 
  return blocks;
}

short CacheMemoryPF::insert_into_existing(uint64_t addr, entry *&e, entry_level *level, AbstractCacheEntry* entry_1) {
DPRINTF(TagTable," insert_into_existing : entering\n");
  short page_offset = -1;	//return value
  int preferred = (PAGESIZE/BLOCKSIZE);		//preferred page offset to either stay contiguous or match subblock with page_offset
  short actual = (PAGESIZE/BLOCKSIZE);		//actual page_offset assigned by page table
  int subblock = ((addr >> FloorLog2(BLOCKSIZE))) & ((PAGESIZE/BLOCKSIZE)-1);  		//subblock within entry that this address represents (page offset of address - i.e., the 6 bits above the block offset)
  assert(subblock <= ((PAGESIZE/BLOCKSIZE)));  //subblock is in valid range

  list<block> present_blocks = build_blocks(e); //summarize blocks currently present

  //add new block
  block temp;
  temp.offset = subblock;
  temp.len = 1;
  temp.page_offset = -1;
  present_blocks.push_back(temp);

  //create ideal list of blocks for the entry (merge new block into existing, etc.)
  list<block> potential_blocks = build_entry(present_blocks);
  /*TEST
  for(const auto &blk : potential_blocks) {
    std::cout << std::hex << blk.offset << ", " << blk.len << ", " << blk.page_offset << std::dec << "; ";
  }
  std::cout << std::endl;
  //TEST*/
  std::bitset<PAGESIZE/BLOCKSIZE> presence_vector = build_presence(present_blocks);
  //loop through list of blocks for one corresponding to addition
  //bool alone = false;	//track whether the block's offset is contiguous or not, if not don't need to convert to status vector if I don't get the preferred address
  for(list<block>::iterator it = potential_blocks.begin(); it != potential_blocks.end(); it++) {
    //if block encompasses addition, request contiguous page_offset
    if((subblock >= (*it).offset) && (subblock < ((*it).offset + (*it).len))) {	//new block is within this chunk
      if((*it).page_offset != -1) {	//new block is contiguous with an existing chunk
	if((*it).page_offset < (PAGESIZE/BLOCKSIZE)) {	//new block is not the front => just get offset from existing (has to be at the end, right?)
	  preferred = (*it).page_offset + (subblock - (*it).offset);
	  assert(preferred == ((*it).page_offset + (*it).len));
	}
	//new block is at the front of this chunk 
	// => unencode the page_offset that was previously associated with the chunk and decrement by 1 to get the preferred offset
	else {
	  preferred = (((PAGESIZE/BLOCKSIZE)-1) & (*it).page_offset) - 1;	//1 less than the value stored with the MSB '1' reset
	  //What about the case where the original chunk was at offset 0?  'preferred' will be negative...
	  if(preferred < 0) {	//fix.  Shift everything over and evict the last block if there's not an empty block available
	    //Just evict the last block for now.  Accomplished through simple reduction of the length field of this block
	    //Are there any gotchas if I've now evicted the only other block in this chunk?
	    (*it).len--;
	    level->update_evictions(1);
	    preferred = 0;
	  }
	  assert(preferred >= 0);
	}
      } else {  //newly inserted block is alone
	if(!presence_vector.test(subblock)) {
	  preferred = subblock;
	} else {  //find available block
	  std::vector<uint32_t> rand_blocks((PAGESIZE/BLOCKSIZE));
	  generate(rand_blocks.begin(), rand_blocks.end(), UniqueNumber);
	  random_shuffle(rand_blocks.begin(), rand_blocks.end());
	  for(const uint32_t tblock : rand_blocks) {
	    if(!presence_vector.test(tblock)) {
	      preferred = tblock;
	      break;
	    }
	  }
	}
	//alone = true;
      }
      assert((preferred >= 0) && (preferred < (PAGESIZE/BLOCKSIZE)));
      actual = level->replace(preferred, expansions, merges);
      // break;
    }
  }

  if(!e->valid || (e->tag == 0)) {	//Entry was actually deleted because the only field in the entry was evicted to make room for this insertion => re-populate the entry with necessary information
    //alone = true;
    e->valid = true;
    e->type = SST_ENTRY;
    e->tag = get_tag(addr);
  }
  //Rebuild present_blocks to reflect potential changes due to replace() call above
  /*DEBUG
  cout << "Building blocks 2" << endl;
  //DEBUG*/
  present_blocks = build_blocks(e);
  //add new block
  temp.offset = subblock;
  temp.len = 1;
  //use "actual" page offset for new block
  temp.page_offset = actual;
  temp.PFentry_ptr = entry_1;
  present_blocks.push_back(temp);

  //assign return value of function to page_offset assigned
  page_offset = actual;

  assert(actual != (PAGESIZE/BLOCKSIZE));
//page offsets are contiguous => create simple entry
    //remake entry based on list of blocks
    present_blocks = build_entry(present_blocks);	//NOTE:  build_entry will not encode a page_offset in this case since none are -1
    if(present_blocks.size() > m_num_fields) { //squeeze into existing entry
      present_blocks = build_blocks(e);
      //add new block
      temp.offset = subblock;
      temp.len = 1;
      //use "actual" page offset for new block
      temp.page_offset = actual;
      present_blocks.push_back(temp);
      present_blocks.sort(order_blocks);
    }
    populate_sst_entry(e, present_blocks);
  /*TEST
  calc_entry_occupancy(e);
  //TEST*/

  assert(page_offset != -1);
  return page_offset;
}
