#include "IFR_BasicBlock.h"

IFR_BasicBlock::IFR_BasicBlock(){
  
}
  
void IFR_BasicBlock::add(INS ins){
  insns.push_back(ins); 
}


ADDRINT IFR_BasicBlock::getEntryAddr(){
  return INS_Address(*(insns.begin()));
}

void IFR_BasicBlock::setTarget(ADDRINT targ){
  target = targ;
}


void IFR_BasicBlock::setFallthrough(ADDRINT ft){
  fallthrough = ft;
}

void IFR_BasicBlock::setIsReturn(bool is){
  isReturn = is;
}

void IFR_BasicBlock::print(){

  fprintf(stderr,"E:%p T:%p F:%p\n",getEntryAddr(),target,fallthrough);
  for( std::vector<INS>::iterator i = insns.begin();
       i != insns.end();
       i++
     ){
    INS ins = *i;
    fprintf(stderr,"%p %s %s\n",INS_Address(ins),CATEGORY_StringShort(INS_Category(ins)).c_str(),INS_Disassemble(ins).c_str());
  }

}


