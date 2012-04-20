#include <pin.H>

enum MemOpType { MemRead = 0, MemWrite = 1, MemBoth = 2 };
class IFR_MemoryRef{

public:
 
  IFR_MemoryRef(); 
  IFR_MemoryRef(REG,ADDRDELTA,REG,UINT32,MemOpType); 
  REG base;
  ADDRDELTA displacement;
  REG index;
  UINT32 scale;
  MemOpType type;

};
