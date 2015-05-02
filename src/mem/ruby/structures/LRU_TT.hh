 
 #ifndef __MEM_RUBY_SYSTEM_LRU_TT_HH__
 #define __MEM_RUBY_SYSTEM_LRU_TT_HH__
 #include "mem/ruby/structures/AbstractReplacementPolicy.hh"
 #include "debug/TagTable.hh"
 #include "debug/TagTable1.hh"
 #define VERIFY 0
 
 class LRU_TT : public AbstractReplacementPolicy
  {
  private:
    uint64_t *_tags;//[MAX_ASSOCIATIVITY];
    bool *_dirty; //Indicate if data is dirty (i.e., has had a 'store' access) at this location
    bool *_metadata_bitvector;//[MAX_ASSOCIATIVITY];	//bitvector to identify which tags are associated with metadata
    uint _tagsLastIndex;
    uint *_useList;	//list for LRU_TT, 0 is LRU_TT
    uint high_watermark;	//max number of entries that can be page table "meta data"
    int associativity ;
    uint low_watermark;		//min number of entries, below which metadata can't be evicted
    uint _interval_misses;	//number of misses in the current dueling epoch
    uint waterlevel;	//number of meta data entries currently

    void fix_LRU_TT_list(int assigned[]);

  public:
    LRU_TT(int64 num_sets, int64 _associativity);

    ~LRU_TT()
    {
      delete[] _tags;
      delete[] _dirty;
      delete[] _metadata_bitvector;
      delete[] _useList;
    }

    void Initialize(uint _associativity, uint _high_watermark, uint _low_watermark);

    void Flush();

    uint64_t GetTag(uint way) { return _tags[way]; }
    int64 getVictim(int64 set) const;
    void SetAssociativity(uint assoc);
    uint GetAssociativity(uint assoc) { return _tagsLastIndex + 1; }
    int64 Replace(uint64_t tag, bool metadata, uint64_t &evicted);
    void SetWatermarks(uint high_wm, uint low_wm);
    uint GetHighWatermark() { return high_watermark; }
    uint GetLowWatermark() { return low_watermark; }
	void touch(int64 set, int64 way, Tick time);
    void EndDuelEpoch();
    uint GetIntervalMisses() { return _interval_misses; }
    
    bool Find(uint64_t tag, bool metadata);
    int64 getVictim(uint64_t tag, bool metadata, uint64_t &evicted);
    void updateMRU(uint64_t tag);

    void incIntervalMisses() { ++_interval_misses; }
  };
  
inline void LRU_TT::touch(int64 set, int64 index, Tick time)
{
    assert(index >= 0 && index < m_assoc);
    assert(set >= 0 && set < m_num_sets);

    m_last_ref_ptr[set][index] = time;
}


inline int64 LRU_TT::getVictim(int64 set) const
{
    //  assert(m_assoc != 0);
    Tick time, smallest_time;
    int64 smallest_index;

    smallest_index = 0;
    smallest_time = m_last_ref_ptr[set][0];

    for (unsigned i = 0; i < m_assoc; i++) {
        time = m_last_ref_ptr[set][i];
        // assert(m_cache[cacheSet][i].m_Permission !=
        //     AccessPermission_NotPresent);

        if (time < smallest_time) {
            smallest_index = i;
            smallest_time = time;
        }
    }

    //  DEBUG_EXPR(CACHE_COMP, MedPrio, cacheSet);
    //  DEBUG_EXPR(CACHE_COMP, MedPrio, smallest_index);
    //  DEBUG_EXPR(CACHE_COMP, MedPrio, m_cache[cacheSet][smallest_index]);
    //  DEBUG_EXPR(CACHE_COMP, MedPrio, *this);

    return smallest_index;
}


inline LRU_TT::LRU_TT(int64 num_sets, int64 assoc):AbstractReplacementPolicy(num_sets, assoc)
{
	DPRINTF(TagTable, "LRU_TT:: constructor\n");
	DPRINTF(TagTable, "LRU_TT:: assoc: %d\n",assoc );
	associativity = assoc;
	DPRINTF(TagTable, "LRU_TT:: assoc assigned: %d\n",associativity );
	_tagsLastIndex = (assoc - 1);
	waterlevel = 0;
	_interval_misses = 0;
  _tags = new uint64_t[assoc];
  _dirty = new bool[assoc];
  _metadata_bitvector = new bool[assoc];
  _useList = new uint[assoc];
   high_watermark = 10;
   low_watermark = 0;

  for (int index = _tagsLastIndex; index >= 0; index--)
    {
      _tags[index] = uint64_t(0);
      _dirty[index] = false;
      _metadata_bitvector[index] = false;
      _useList[index] = index; //initialize _useList items for implementing LRU_TT
    }
}

inline void LRU_TT::Initialize(uint _associativity, uint _high_watermark, uint _low_watermark)
{
  _tagsLastIndex = _associativity - 1;
  associativity = _associativity;
  high_watermark = _high_watermark;
  low_watermark = _low_watermark;
  _interval_misses = 0;
  waterlevel = 0;
  _tags = new uint64_t[_associativity];
  _dirty = new bool[_associativity];
  _metadata_bitvector = new bool[_associativity];
  _useList = new uint[_associativity];

  for (int index = _tagsLastIndex; index >= 0; index--)
    {
      _tags[index] = uint64_t(0);
      _dirty[index] = false;
      _metadata_bitvector[index] = false;
      _useList[index] = index; //initialize _useList items for implementing LRU_TT
    }
}

inline void LRU_TT::fix_LRU_TT_list(int assigned[])
{
  //function to fix the _useList if I find out that some values are either missing or assigned to multiple indices
  int none = associativity;
  int two = associativity; //store indices that either have none assigned or two assigned (if there's something not either, this won't work => assert)
  for (int index = _tagsLastIndex; index >= 0; index--) {
    assert((assigned[index] >= 0) && (assigned[index] <= 2));
    if((assigned[index] == 0) && (none == (int)associativity)) { //will only assign first instance of 0 to none to allow multiple calls to this fcn to fix all errors
      none = index;
    } else if ((assigned[index] == 2) && (two == (int)associativity)) { //will only assign first instance of 2 to two to allow multiple calls to this fcn to fix all errors
      two = index;
    }
  }
  for (int index = _tagsLastIndex; index >= 0; index--) {
    if((int)_useList[index] == two) {
      _useList[index] = none;
      two = none;  //should ensure that it doesn't hit again (could alternatively make it equal to associativity)
    }
  }
}

inline void LRU_TT::Flush()
{
  assert(associativity); //need to verify associativity is valid before this set really does anything
  for (int index = _tagsLastIndex; index >= 0; index--)
    {
      _tags[index] = uint64_t(0);  //why should this be any different from the constructor?  (it was initially, see ROUND_ROBIN)
      _dirty[index] = false;
      _metadata_bitvector[index] = false;
      _useList[index] = index; //initialize _useList items for implementing LRU_TT
    }
}

inline void LRU_TT::SetAssociativity(uint assoc)
{
  _tagsLastIndex = assoc - 1;
  associativity = assoc;
}

inline void LRU_TT::SetWatermarks(uint high_wm, uint low_wm) {
  high_watermark = high_wm;
  low_watermark = low_wm;
  assert(high_watermark >= low_watermark);
}

inline void LRU_TT::EndDuelEpoch() {
  _interval_misses = 0;
}

inline bool LRU_TT::Find(uint64_t tag, bool metadata)
{
  assert(associativity); //need to verify associativity is valid before this set really does anything
  bool isHit = false;

  for (int index = _tagsLastIndex; index >= 0; index--) {
    if(_tags[index] == tag) {
      isHit = true;
      break;
    }
  }
  
  return isHit;
}

inline void LRU_TT::updateMRU(uint64_t tag) {
  int curr_val;  //current LRU_TT value of data being accessed

  for (int index = _tagsLastIndex; index >= 0; index--) {
    if(_tags[index] == tag) {
      curr_val = _useList[index];        //Retain previous LRU_TT state of block for updating
      _useList[index] = associativity-1; //Update LRU_TT state to MRU
      for(int i = 0; i < (int)associativity; i++) { //Iterate through all other blocks and decrement their LRU_TT state
	// (if it was lower than this block's previous value)
	if((i != index) && ((int)_useList[i] > curr_val)) {
	  _useList[i]--;
	}
      }
      //_dirty[index] |= isWrite;  //if this is a write, the data is now dirty
#if VERIFY
      //	  cout << "Found hit, new list is:" << endl;
      int *assigned = new int[associativity];
      for(int i = 0; i < (int)associativity; i++) {
	assigned[i] = 0;
      }
      for(int i = 0; i < (int)associativity; i++) {
	//	    cout << " ," << _useList[i];
	assigned[_useList[i]]++;
      }
      //	  cout << endl;
      for(int i = 0; i < (int)associativity; i++) {
	//make sure all possible values are assigned once and only once
	if((assigned[i] == 0) || (assigned[i] > 1)) {
	  cout << "LRU_TT list is broken when curr_val is " << curr_val << " and associativity is " << associativity << endl;
	  for(int j = 0; j < (int)associativity; j++) {
	    cout << j << "(" << _useList[j] << "/" << assigned[_useList[j]] << "), ";
	  }
	  cout << endl;
	  // fix_LRU_TT_list(assigned);
	  // cout << "Should be fixed now" << endl;
	  // for(int j = 0; j < (int)associativity; j++) {
	  //   cout << j << "(" << _useList[j] << "), ";
	  // }
	  // cout << endl;
	  //assert(0);
	  //assert(assigned[i]);
	  //assert(assigned[i] < 2);
	}
      }
      delete[] assigned;
#endif
    }
  }
}


inline int64 LRU_TT::Replace(uint64_t tag, bool metadata, uint64_t &evicted) 
{
	DPRINTF(TagTable, "LRU_TT::Replace victim is metadata, assoc: %d \n",m_assoc);
	DPRINTF(TagTable, "LRU_TT::Replace : ENTRY, tag = %ld tag, evicted = %ld\n", tag, evicted);
	
  assert(associativity); //need to verify associativity is valid before this set really does anything
  // assert(waterlevel <= high_watermark);  //can exceed if recently switched from high to low
  uint64_t victim = uint64_t(0);
  uint index = associativity;
  if(!metadata && (waterlevel <= low_watermark)) {	//don't allow replacement of metadata (i.e., only "real" data eligible for replacement)
    //find non-metadata element with the lowest LRU_TT status
     DPRINTF(TagTable, "LRU_TT::Replace : else : don't allow replacement of metadata");
    uint lowestStatus = associativity;
    
    for(int i = 0; i < (int)associativity; i++) {
      if(!_metadata_bitvector[i] && (_useList[i] < lowestStatus)) {
	lowestStatus = _useList[i];
      }
    }
    for(int i = 0; i < (int)associativity; i++) {
      if(_useList[i] == lowestStatus) {	//non-metadata with lowest LRU_TT status => replace
	index = i;
	_useList[i] = associativity-1; //set this as MRU
      } else if(_useList[i] > lowestStatus) {
	_useList[i]--;
      }
    }
  } else if(metadata && (waterlevel == high_watermark)) {	//don't allow insertion of more metadata (i.e., only metadata eligible for replacement)
    //find metadata element with the lowest LRU_TT status
    uint lowestStatus = associativity;
    for(int i = 0; i < (int)associativity; i++) {
      if(_metadata_bitvector[i] && (_useList[i] < lowestStatus)) {
	lowestStatus = _useList[i];
      }
    }
    for(int i = 0; i < (int)associativity; i++) {
      if(_useList[i] == lowestStatus) {	//metadata with lowest LRU_TT status => replace
	index = i;
	_useList[i] = associativity-1; //set this as MRU
      } else if(_useList[i] > lowestStatus) {
	_useList[i]--;
      }
    }
  } else {	//no restrictions
	  DPRINTF(TagTable, "LRU_TT::Replace : else : no restrictions");
	   //DPRINTF(TagTable, "LRU_TT::Replace victim is metadata, assoc: %d \n",associativity);
    for(int i = 0; i < (int)m_assoc; i++) {
		// DPRINTF(TagTable, "LRU_TT::Replace victim is metadata, _tags[index]: %b \n",_useList[i]);
      if(!_useList[i]) {  //LRU_TT status is 0 => replace
	index = i;
	//	  curr_val = _useList[i];
	_useList[i] = associativity-1; //set this as MRU
      } else {
	_useList[i]--;  //decrement all others' LRU_TT status (no need to test for >, since the test would be against 0)
      }
    }
  }
  if(_metadata_bitvector[index]) {
	  DPRINTF(TagTable, "LRU_TT::Replace victim is metadata \n");
	  	//victim is metadata (doesn't matter if the new stuff is or not, need to tell page table)
    evicted = uint64_t(_tags[index]);
    DPRINTF(TagTable, "LRU_TT::Replace victim is metadata, evicted: %0x \n",_tags[index]);
    /*WARN
    printf("WARNING:  metadata entry was evicted (evicted tag is 0x%lX)\n",uint64_t(_tags[index]));
    //WARN*/
    
  }
  assert(index != associativity);

  if(_metadata_bitvector[index] && !metadata) {		//replacing metadata with real data - implies meta data eviction => evict from PT
    waterlevel--;
  } else if(!_metadata_bitvector[index] && metadata) {	//replacing real data with meta data
    waterlevel++;
  }
  // assert(waterlevel <= high_watermark);	//high_watermark);  //can exceed if recently switched from high to low
  victim = _tags[index];  //only return victim if dirty
  /*TEST
  std::cout << "Replacing index " << index << std::endl;
  if(_dirty[index]) {
    std::cout << "Evicting tag 0x" << std::hex << uint64_t(_tags[index]) << std::dec << std::endl;
  }
  //TEST*/
  _tags[index] = tag;
  //_dirty[index] = false;
  _metadata_bitvector[index] = metadata;
DPRINTF(TagTable, "LRU_TT::Replace : EXIT, returning victim:%0x, evicted: %0x \n",victim,evicted);

  return victim;
}
#endif // __MEM_RUBY_SYSTEM_LRU_TT_HH__
