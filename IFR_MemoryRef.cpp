#include "IFR_MemoryRef.h"

IFR_MemoryRef::IFR_MemoryRef(){

}


IFR_MemoryRef::IFR_MemoryRef(REG b, ADDRDELTA d, REG i, UINT32 s, MemOpType t){
  base = b;
  displacement = d;
  index = i;
  scale = s;
  type = t;
}
