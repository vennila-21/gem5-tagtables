
#include "AbstractCacheEntry.hh"
class CacheEntryPF : public AbstractCacheEntry
{

 public:
    CacheEntryPF();
    
    bool valid;
    bool type;
    struct chunk {
	uint16 page_offset : 6;
	uint16 length : 6;
	uint16 row_offset: 6;
	bool dirty;
	chunk(){
	page_offset = 0;
	length = 0;
	row_offset = 0;
	dirty= false;
}
	
};
    chunk chunks[4];
    
  


    
 };
