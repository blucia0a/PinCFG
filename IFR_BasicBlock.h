#include <vector>
#include <pin.H>
class IFR_BasicBlock{

  std::vector<INS> insns;
  ADDRINT target;
  ADDRINT fallthrough;
  bool isReturn;

public:
  IFR_BasicBlock();

  void add(INS ins);
  
  ADDRINT getEntryAddr();
  void setTarget(ADDRINT targ);
  void setFallthrough(ADDRINT ft);
  void setIsReturn(bool is);

  void print();

};
