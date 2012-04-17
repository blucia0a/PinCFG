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
#include <algorithm>
#include <ext/hash_map>
#include <assert.h>

#include "IFR_BasicBlock.h"

using __gnu_cxx::hash_map;

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

void findBlocks(RTN rtn, vector<IFR_BasicBlock> &bblist, hash_map<ADDRINT, IFR_BasicBlock> &blocks){

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

  fprintf(stderr,"Computing blocks...\n"); 
  int ii = 0;

  IFR_BasicBlock bb = IFR_BasicBlock();   
  for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins)){

    fprintf(stderr," %d", ++ii); 
    bb.add(ins);

    INS next = INS_Next(ins);
    if(   (INS_Valid(next) &&  leaders.find(INS_Address(next)) != leaders.end()) || !INS_Valid(next) ){

      /*Next is a block leader or end of routine -- End the block here*/

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

      blocks.insert( std::pair<ADDRINT,IFR_BasicBlock>(bb.getEntryAddr(), IFR_BasicBlock(bb)) ); 
      bblist.push_back(IFR_BasicBlock(bb));
      bb.clear();

    }

  }
  fprintf(stderr,"Done!\n");

  return;
   
}

void computePredecessors(RTN rtn, vector<IFR_BasicBlock> bblist, hash_map<ADDRINT, set<ADDRINT> > &pred){

  fprintf(stderr,"Computing predecessors...\n");
  
  pred[ bblist.begin()->getEntryAddr() ] = set<ADDRINT>(); 
  for( vector<IFR_BasicBlock>::iterator i = bblist.begin(); i != bblist.end(); i++){

    if( pred.find( i->getTarget() ) == pred.end() ){
      pred[ i->getTarget() ] = set<ADDRINT>();
    }    
    
    if( pred.find( i->getFallthrough() ) == pred.end() ){
      pred[ i->getFallthrough() ] = set<ADDRINT>();
    }    

    pred[ i->getTarget() ].insert( i->getEntryAddr() );
    pred[ i->getFallthrough() ].insert( i->getEntryAddr() );

  }
  fprintf(stderr,"Done Computing predecessors\n");
  return;

}

void computeDominators(RTN rtn, vector<IFR_BasicBlock> bblist, hash_map<ADDRINT, set<ADDRINT> > pred, hash_map<ADDRINT, set<ADDRINT> > &dom){


  fprintf(stderr,"First setting allnodes to all nodes \n");
  set<ADDRINT> allNodes = set<ADDRINT>(); 
  for( vector<IFR_BasicBlock>::iterator i = bblist.begin(); i != bblist.end(); i++){
    allNodes.insert( i->getEntryAddr() ); 
  }
  fprintf(stderr,"Done setting allnodes to all nodes \n");

  fprintf(stderr,"Dom set of entry node is the empty set\n");
  /*Set the dominator set of the entry node to be empty*/
  dom.insert(std::pair<ADDRINT,set<ADDRINT> >(bblist.begin()->getEntryAddr(), set<ADDRINT>()));
  dom[ bblist.begin()->getEntryAddr() ].insert( bblist.begin()->getEntryAddr() );

  fprintf(stderr,"Done: Dom set of entry node is the empty set\n");
  
  /*Set the dominator set for all other nodes to be all nodes*/
  fprintf(stderr,"Initializing dom sets to full...\n");
  bool first = true;
  for( vector<IFR_BasicBlock>::iterator i = bblist.begin(); i != bblist.end(); i++){

    if( first ){
      /*Skip the first block*/
      first = false;
      continue;
    }

    dom.insert(std::pair<ADDRINT,set<ADDRINT> >(i->getEntryAddr(), set<ADDRINT>()));
    dom[ i->getEntryAddr() ].insert( allNodes.begin(), allNodes.end() ); 
     
  }
  fprintf(stderr,"Done Initializing dom sets to full\n");

  /*Fixed-Point Part: Iteratively remove elems from each nodes dom set until
   *                  no set changes.
   */
  int dbg_i = 0;
  bool anyChange;
  do{

    anyChange = false;
    dbg_i++;

    bool first = true;
    fprintf(stderr,"Dataflow Eq. Iteration %d\n",dbg_i);
    for( vector<IFR_BasicBlock>::iterator i = bblist.begin(); i != bblist.end(); i++){

      if( first ){
        /*Skip the first block*/
        first = false;
        continue;
      }
      
      /*Intersection over all p \in pred(n)( dom(p) ) ... */
      fprintf(stderr,"Searching predecessors...\n");
      set<ADDRINT> intDoms = set<ADDRINT>();
      bool firstPred = true;
      for(set<ADDRINT>::iterator p = pred[ i->getEntryAddr() ].begin();
          p != pred[ i->getEntryAddr() ].end();
          p++
         ){

        /*p is a predecessor of i*/
        if( firstPred ){

          /*If it is the first predecessor, intDoms is empty, so initialize it with p's doms*/
          firstPred = false;
          intDoms.insert( dom[ *p ].begin(), dom[ *p ].end() );

        }else{

          /*If p is non-empty, we've seen a predecessor, so we only want the common elems in intDoms and dom(p)*/
          set<ADDRINT> intersection = set<ADDRINT>(); 
          std::set_intersection( intDoms.begin(), intDoms.end(), 
                                 dom[ *p ].begin(), dom[ *p ].end(), 
                                 std::inserter( intersection, intersection.begin() ) ); 

          intDoms.clear();
          intDoms.insert(intersection.begin(), intersection.end());
        

        }

      } 
      fprintf(stderr,"Done Searching predecessors...\n");

      /*... Unioned with n itself */
      intDoms.insert( i->getEntryAddr() );

      
      if( intDoms.size() != dom[ i->getEntryAddr() ].size() ){

        anyChange = true;

      }

      dom[ i->getEntryAddr() ].clear(); 
      dom[ i->getEntryAddr() ].insert( intDoms.begin(), intDoms.end() );

    }


  }while(anyChange);

}

VOID instrumentRoutine(RTN rtn, VOID *v){
 

  RTN_Open(rtn);
  if( !RTN_Valid(rtn) || !IMG_IsMainExecutable( IMG_FindByAddress( RTN_Address(rtn) ) )){
    RTN_Close(rtn);
    return;
  }

  vector<IFR_BasicBlock> bblist = vector<IFR_BasicBlock>(); 
  hash_map<ADDRINT, IFR_BasicBlock> blocks = hash_map<ADDRINT, IFR_BasicBlock>();
  findBlocks(rtn,bblist,blocks); 
  
  hash_map<ADDRINT, set<ADDRINT> > pred = hash_map<ADDRINT, set<ADDRINT> >();
  computePredecessors(rtn,bblist,pred);
  for( vector<IFR_BasicBlock>::iterator i = bblist.begin(); i != bblist.end(); i++){
    fprintf(stderr,"Predecessors to %p:\n\t",i->getEntryAddr());
    for( set<ADDRINT>::iterator pi = pred[ i->getEntryAddr() ].begin();
         pi != pred[ i->getEntryAddr() ].end(); pi++ ){
         fprintf(stderr,"%p ",*pi);
    }
    fprintf(stderr,"\n");
  }

  fprintf(stderr,"Computing dominators!\n");
  hash_map<ADDRINT, set<ADDRINT> > dominators = hash_map<ADDRINT, set<ADDRINT> >();
  computeDominators(rtn, bblist, pred, dominators);
  for( vector<IFR_BasicBlock>::iterator i = bblist.begin(); i != bblist.end(); i++){
    fprintf(stderr,"Dominators of %p:\n\t",i->getEntryAddr());
    for( set<ADDRINT>::iterator di = dominators[ i->getEntryAddr() ].begin();
         di != dominators[ i->getEntryAddr() ].end(); di++ ){
         fprintf(stderr,"%p ",*di);
    }
    fprintf(stderr,"\n");
  }
  

  fprintf(stderr,"Done Computing Dominators!\n");

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
