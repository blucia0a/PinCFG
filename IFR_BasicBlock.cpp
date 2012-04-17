#include "IFR_BasicBlock.h"

IFR_BasicBlock::IFR_BasicBlock(){
  
  insns = vector<INS>();

}

IFR_BasicBlock::IFR_BasicBlock(const IFR_BasicBlock& other){

  insns = vector<INS>();
  insns.assign( other.insns.begin(), other.insns.end() ); 
  target = other.target;
  fallthrough = other.fallthrough;
  isReturn = other.isReturn;

}

IFR_BasicBlock IFR_BasicBlock::operator=(const IFR_BasicBlock& other){

  insns.assign( other.insns.begin(), other.insns.end() ); 
  target = other.target;
  fallthrough = other.fallthrough;
  isReturn = other.isReturn;

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

ADDRINT IFR_BasicBlock::getTarget(){
  return target;
}


void IFR_BasicBlock::setFallthrough(ADDRINT ft){
  fallthrough = ft;
}

ADDRINT IFR_BasicBlock::getFallthrough(){
  return fallthrough;
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

void IFR_BasicBlock::clear(){

  fallthrough = target = 0;
  isReturn = false;
  insns.clear();

}


