#include <vector>
#include <pin.H>
class IFR_BasicBlock{

  std::vector<INS> insns;
  ADDRINT target;
  ADDRINT fallthrough;
  bool isReturn;

public:

  IFR_BasicBlock();

  IFR_BasicBlock(const IFR_BasicBlock&); //copy constructor
  IFR_BasicBlock operator=(const IFR_BasicBlock&); //to handle explicit assignment

  void add(INS ins);
  
  ADDRINT getEntryAddr();
  void setTarget(ADDRINT targ);
  void setFallthrough(ADDRINT ft);
  ADDRINT getTarget();
  ADDRINT getFallthrough();
  void setIsReturn(bool is);

  void print();

  void clear();

};
