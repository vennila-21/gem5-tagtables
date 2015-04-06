include "mem/ruby/slicc_interface/AbstractCacheEntryPF.hh"
AbstractCacheEntryPF::AbstractCacheEntryPF()
{
    m_Permission = AccessPermission_NotPresent;
    m_Address.setAddress(0);
    m_locked = -1;
    valid = false;
    type = true; // true indicates application and false indicates metadata
    
}



