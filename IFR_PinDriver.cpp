#include "pin.H"

#include <signal.h>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <string>
#include <string.h>
#include <dlfcn.h>
#include <vector>
#include <set>
#include <assert.h>

#include "IFR_BasicBlock.h"

//KNOB<bool> KnobConcise(KNOB_MODE_WRITEONCE, "pintool",
//			   "concise", "true", "Print output concisely");//default cache is verbose 

enum MemOpType { MemRead = 0, MemWrite = 1 };

INT32 usage()
{
    cerr << "IFRit -- A Sound Data Race Detector";
    cerr << KNOB_BASE::StringKnobSummary();
    cerr << endl;
    return -1;
}

vector<IFR_BasicBlock> findBlocks(RTN rtn){

  /*Takes a PIN RTN object and returns a set containing the 
   *addresses of the instructions that are entry points to basic blocks
   *"Engineering a Compiler pg 439, Figure 9.1 'Finding Leaders'"
   */
  set<ADDRINT> leaders = set<ADDRINT>();
  bool first = true;
  for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins)){

    if( first ){
      first = false;
      leaders.insert( INS_Address(ins) );
    }

    if( INS_IsBranch(ins) ){

      assert( !INS_IsRet(ins) );
      if( !INS_IsIndirectBranchOrCall(ins) ){
      
        leaders.insert(INS_DirectBranchOrCallTargetAddress(ins));
        leaders.insert(INS_NextAddress(ins));

      }/*else{

        Calls and Indirect Branches may go anywhere, so we conservatively assume they jump to the moon

      }*/

    }

  }
  
  std::vector<IFR_BasicBlock> bblist = std::vector<IFR_BasicBlock>();
  IFR_BasicBlock bb = IFR_BasicBlock();   
  for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins)){

    fprintf(stderr,"Processing %p\n",INS_Address(ins));
    bb.add(ins);

    INS next = INS_Next(ins);
    if( INS_Valid(next) ){
      /*Next is a valid instruction.*/

      if( leaders.find(INS_Address(next)) != leaders.end() ){

        /*Next is a block leader -- End the block here*/

        if( INS_IsBranch(ins) ){

          /*Block ends with a branch insn*/

          assert( !INS_IsRet(ins) );
          if( !INS_IsIndirectBranchOrCall(ins) ){

            /*End of block with Direct Branch insns*/        
            bb.setTarget(INS_DirectBranchOrCallTargetAddress(ins));
            if( INS_Category(ins) != XED_CATEGORY_UNCOND_BR ){
              bb.setFallthrough(INS_NextAddress(ins));
            }else{
              bb.setFallthrough(0);
            }

          }

        }else{
          /*Block ends with a non-branch insn*/
          bb.setTarget(0);
          bb.setFallthrough(INS_NextAddress(ins));
        }
        bblist.push_back(bb);
        bb = IFR_BasicBlock();  //ends the block

      }

    }

  }
  bb.setTarget(0);
  bb.setFallthrough(0);
  bblist.push_back(bb);

  return bblist;
   
}

VOID instrumentRoutine(RTN rtn, VOID *v){
 

  RTN_Open(rtn);
  if( !RTN_Valid(rtn) || !IMG_IsMainExecutable( IMG_FindByAddress( RTN_Address(rtn) ) )){
    RTN_Close(rtn);
    return;
  }

  vector<IFR_BasicBlock> bblist = findBlocks(rtn); 

  fprintf(stderr,">>>>>>>>>>>>>>%s<<<<<<<<<<<<<<<\n",RTN_Name(rtn).c_str());
  for( std::vector<IFR_BasicBlock>::iterator i = bblist.begin();
       i != bblist.end();
       i++
     ){

    i->print();
    fprintf(stderr,"-------------------------------------\n");

  }

  RTN_Close(rtn);

}

VOID instrumentImage(IMG img, VOID *v)
{

}

void Read(THREADID tid, ADDRINT addr, ADDRINT inst){
}

void Write(THREADID tid, ADDRINT addr, ADDRINT inst){
}



VOID threadBegin(THREADID threadid, CONTEXT *sp, INT32 flags, VOID *v)
{
  
}
    
VOID threadEnd(THREADID threadid, const CONTEXT *sp, INT32 flags, VOID *v)
{

}

VOID dumpInfo(){
}


VOID Fini(INT32 code, VOID *v)
{
}

BOOL segvHandler(THREADID threadid,INT32 sig,CONTEXT *ctx,BOOL hasHndlr,const EXCEPTION_INFO *pExceptInfo, VOID*v){
  return TRUE;//let the program's handler run too
}

BOOL termHandler(THREADID threadid,INT32 sig,CONTEXT *ctx,BOOL hasHndlr,const EXCEPTION_INFO *pExceptInfo, VOID*v){
  return TRUE;//let the program's handler run too
}


int main(int argc, char *argv[])
{

  PIN_InitSymbols();
  if( PIN_Init(argc,argv) ) {
    return usage();
  }

  RTN_AddInstrumentFunction(instrumentRoutine,0);

  PIN_InterceptSignal(SIGTERM,termHandler,0);
  PIN_InterceptSignal(SIGSEGV,segvHandler,0);

  PIN_AddThreadStartFunction(threadBegin, 0);
  PIN_AddThreadFiniFunction(threadEnd, 0);
  PIN_AddFiniFunction(Fini, 0);
 
  PIN_StartProgram();
  
  return 0;
}
