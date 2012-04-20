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

KNOB<bool> KnobPred(KNOB_MODE_WRITEONCE, "pintool", "pred", "false", "Print block predecessors");
KNOB<bool> KnobDom(KNOB_MODE_WRITEONCE, "pintool", "dom", "false", "Print block dominators");
KNOB<bool> KnobIDom(KNOB_MODE_WRITEONCE, "pintool", "idom", "false", "Print block immediate dominators");
KNOB<bool> KnobDF(KNOB_MODE_WRITEONCE, "pintool", "df", "false", "Print block dominance frontiers");
KNOB<bool> KnobSSA(KNOB_MODE_WRITEONCE, "pintool", "ssa", "false", "Print ssa transformation");
KNOB<bool> KnobBlocks(KNOB_MODE_WRITEONCE, "pintool", "blocks", "false", "Print disassembled code blocks ");

enum MemOpType { MemRead = 0, MemWrite = 1 };

INT32 usage()
{
    cerr << "IFRit -- A Sound Data Race Detector";
    cerr << KNOB_BASE::StringKnobSummary();
    cerr << endl;
    return -1;
}

void findBlocks(RTN rtn, 
                vector<IFR_BasicBlock> &bblist, 
                hash_map<ADDRINT, IFR_BasicBlock> &blocks){

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

  int ii = 0;

  IFR_BasicBlock bb = IFR_BasicBlock();   
  for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins)){

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

  return;
   
}

void computePredecessors(RTN rtn, 
                         vector<IFR_BasicBlock> &bblist, 
                         hash_map<ADDRINT, set<ADDRINT> > &pred){

  
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
  return;

}

void computeDominators(RTN rtn, 
                       vector<IFR_BasicBlock> &bblist, 
                       hash_map<ADDRINT, set<ADDRINT> > &pred, 
                       hash_map<ADDRINT, set<ADDRINT> > &dom){


  set<ADDRINT> allNodes = set<ADDRINT>(); 
  for( vector<IFR_BasicBlock>::iterator i = bblist.begin(); i != bblist.end(); i++){
    allNodes.insert( i->getEntryAddr() ); 
  }

  /*Set the dominator set of the entry node to be empty*/
  dom.insert(std::pair<ADDRINT,set<ADDRINT> >(bblist.begin()->getEntryAddr(), set<ADDRINT>()));
  dom[ bblist.begin()->getEntryAddr() ].insert( bblist.begin()->getEntryAddr() );

  
  /*Set the dominator set for all other nodes to be all nodes*/
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

  /*Fixed-Point Part: Iteratively remove elems from each nodes dom set until
   *                  no set changes.
   */
  int dbg_i = 0;
  bool anyChange;
  do{

    anyChange = false;
    dbg_i++;

    bool first = true;
    for( vector<IFR_BasicBlock>::iterator i = bblist.begin(); i != bblist.end(); i++){

      if( first ){
        /*Skip the first block*/
        first = false;
        continue;
      }
      
      /*Intersection over all p \in pred(n)( dom(p) ) ... */
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

void computeIDoms(vector<IFR_BasicBlock> &bblist, 
                  hash_map<ADDRINT, set<ADDRINT> > &dom, 
                  hash_map<ADDRINT, ADDRINT> &idom){

  for( vector<IFR_BasicBlock>::iterator bi = bblist.begin();
       bi != bblist.end();
       bi++
     ){

    for( set<ADDRINT>::iterator di = dom[bi->getEntryAddr()].begin();
         di != dom[bi->getEntryAddr()].end();
         di++
       ){
  
      if( *di == bi->getEntryAddr() ){ continue; }  //only strict dominators are considered
      bool iDom = true; 
      for( set<ADDRINT>::iterator odi = dom[bi->getEntryAddr()].begin();
           odi != dom[bi->getEntryAddr()].end();
           odi++
         ){

        if( *odi == bi->getEntryAddr() ){ continue; }  //bi-> is the one whose idom we're looking for
        if( *odi == *di ){ continue; }  //only strict dominators are considered

        /*If any other dominator in bi's dominator set is dominated by di, then
         *di can't be bi's iDom.
         */
        if( dom[ *odi ].find( *di ) != dom[ *odi ].end()){
          iDom = false;
        }

      }

      if( iDom ){
        assert( idom.find(bi->getEntryAddr()) == idom.end() );
        idom.insert( std::pair<ADDRINT, ADDRINT>(bi->getEntryAddr(), *di) );
      } 

    }

    if( idom.find(bi->getEntryAddr()) == idom.end() ){
      /*an immediate dominator of 0 means this node has no immediate dominator*/
      idom.insert( std::pair<ADDRINT, ADDRINT>(bi->getEntryAddr(), 0) );
    }

  }

}


bool dominates(ADDRINT b1, ADDRINT b2, hash_map<ADDRINT, set<ADDRINT> > &dom){
  /*True if b2 is in b1's dominator set; AKA true if b2 dominates b1.*/
  return dom[ b1 ].find( b2 ) != dom[ b1 ].end();
}

bool strictlyDominates(ADDRINT b1, ADDRINT b2, hash_map<ADDRINT, set<ADDRINT> > &dom){
  /*True if b2 is in b1's dominator set; AKA true if b2 dominates b1.*/
  return ((dom[ b1 ].find( b2 ) != dom[ b1 ].end()) && (b1 != b2));
}

bool immediatelyDominates(ADDRINT b1, ADDRINT b2, hash_map<ADDRINT, ADDRINT> &idom){
  /*Returns true if b2 is b1's immediate dominator*/
  return idom[ b1 ] == b2;
}


void computeDominanceFrontiers( vector<IFR_BasicBlock> &bblist, 
                                hash_map<ADDRINT, set<ADDRINT> > &pred, 
                                hash_map<ADDRINT, set<ADDRINT> > &dom, 
                                hash_map<ADDRINT, ADDRINT> &idom ,
                                hash_map<ADDRINT, set<ADDRINT> > &df ){


  for( vector<IFR_BasicBlock>::iterator i = bblist.begin();
       i != bblist.end();
       i++ ){

    //for each block 
   
    ADDRINT runner = 0; 
    if( pred[ i->getEntryAddr() ].size() >= 2 ){
      
      for( set<ADDRINT>::iterator pi = pred[ i->getEntryAddr() ].begin();
           pi != pred[ i->getEntryAddr() ].end();
           pi++
         ){

        runner = idom[ *pi ]; 
        while( runner != idom[ i->getEntryAddr() ] && runner != 0/*entry node*/ ){
          df[ runner ].insert( i->getEntryAddr() );// = df[runner]; /*TODO: df[ runner ] <-- DF[runner] U {i}*/
          runner = idom[ runner ];
        }

      }
 
    } 

  }

}

void printMemOp(INS i, UINT32 op){

  //assumes operand op to instruction i is a memory operation 
  cerr << "M[ ";
  if( INS_OperandIsImmediate( i, op ) ){
    cerr << "Immediate...";
  }

  REG r;
  if( (r = INS_OperandMemoryBaseReg( i, op )) != REG_INVALID() ){
    cerr << "r" << r; 
  }

  ADDRDELTA d = INS_OperandMemoryDisplacement( i, op );
  cerr << " + " << d;

  REG ind;
  if( (ind = INS_OperandMemoryIndexReg( i, op )) != REG_INVALID() ){

    UINT32 s;
    s = INS_OperandMemoryScale( i, op );
    cerr << " + r" << ind << "*" << s;

  }

  cerr << " ]" << endl;

}

void computeSSA(vector<IFR_BasicBlock> &bblist){

  for( vector<IFR_BasicBlock>::iterator i = bblist.begin();
       i != bblist.end();
       i++ ){

    for( vector<INS>::iterator ins_i = i->insns.begin();
         ins_i != i->insns.end();
         ins_i++ ){

      int op;
      for( op = 0; op < INS_OperandCount(*ins_i); op++ ){
      
        if( INS_OperandIsReg(*ins_i, op) ){

          if( INS_OperandReadAndWritten(*ins_i, op) ){

            cerr << "R/W Reg " << INS_OperandReg(*ins_i, op) << endl;

          }else{

            if( INS_OperandRead(*ins_i, op) ){

              cerr << "R Reg " << INS_OperandReg(*ins_i, op) << endl;

            }

            if( INS_OperandWritten(*ins_i, op) ){

              cerr << "W Reg " << INS_OperandReg(*ins_i, op) << endl;

            }

         }

        }else if( INS_OperandIsMemory(*ins_i, op) ){

          if( INS_OperandRead(*ins_i, op) && INS_OperandWritten(*ins_i, op) ){
  
            cerr << "R/W "; //INS_OperandReg(*ins_i, mop) << endl;
            printMemOp(*ins_i, op);
  
          }else{
  
            if( INS_OperandRead(*ins_i, op) ){
  
              cerr << "R "; //INS_OperandReg(*ins_i, op) << endl;
              printMemOp(*ins_i, op);
  
            }
  
            if( INS_OperandWritten(*ins_i, op) ){
  
              cerr << "W "; //INS_OperandReg(*ins_i, op) << endl;
              printMemOp(*ins_i, op);
  
            }
  
          }

        }

      }      
      cerr << "(" << INS_Disassemble(*ins_i) << ")" << endl;
  
    }

  }

}


VOID instrumentRoutine(RTN rtn, VOID *v){
 

  RTN_Open(rtn);
  if( !RTN_Valid(rtn) || !IMG_IsMainExecutable( IMG_FindByAddress( RTN_Address(rtn) ) )){
    RTN_Close(rtn);
    return;
  }

  fprintf(stderr,">>>>>>>>>>>>>>%s<<<<<<<<<<<<<<<\n",RTN_Name(rtn).c_str());

  vector<IFR_BasicBlock> bblist = vector<IFR_BasicBlock>(); 
  hash_map<ADDRINT, IFR_BasicBlock> blocks = hash_map<ADDRINT, IFR_BasicBlock>();
  findBlocks(rtn,bblist,blocks); 
  
  hash_map<ADDRINT, set<ADDRINT> > pred = hash_map<ADDRINT, set<ADDRINT> >();
  computePredecessors(rtn,bblist,pred);

  if( KnobPred.Value() == true ){
    for( vector<IFR_BasicBlock>::iterator i = bblist.begin(); i != bblist.end(); i++){
      fprintf(stderr,"Predecessors to %p:\n\t",i->getEntryAddr());
      for( set<ADDRINT>::iterator pi = pred[ i->getEntryAddr() ].begin();
           pi != pred[ i->getEntryAddr() ].end(); pi++ ){
           fprintf(stderr,"%p ",*pi);
      }
      fprintf(stderr,"\n");
    }
  }

  hash_map<ADDRINT, set<ADDRINT> > dom = hash_map<ADDRINT, set<ADDRINT> >();
  computeDominators(rtn, bblist, pred, dom);
  
  if( KnobDom.Value() == true ){
    for( vector<IFR_BasicBlock>::iterator i = bblist.begin(); i != bblist.end(); i++){
      fprintf(stderr,"Dominators of %p:\n\t",i->getEntryAddr());
      for( set<ADDRINT>::iterator di = dom[ i->getEntryAddr() ].begin();
           di != dom[ i->getEntryAddr() ].end(); di++ ){
           fprintf(stderr,"%p ",*di);
      }
      fprintf(stderr,"\n");
    }
  }
  
  hash_map<ADDRINT, ADDRINT > idom = hash_map<ADDRINT, ADDRINT >();
  computeIDoms(bblist, dom, idom);

  if( KnobIDom.Value() == true ){
    for( vector<IFR_BasicBlock>::iterator i = bblist.begin(); i != bblist.end(); i++){
      fprintf(stderr,"IDom of %p: %p\n",i->getEntryAddr(), idom[i->getEntryAddr()]);
    }
  }

  hash_map<ADDRINT, set<ADDRINT> > df = hash_map<ADDRINT, set<ADDRINT> >();
  computeDominanceFrontiers(bblist, pred, dom, idom, df );
  if( KnobDF.Value() == true ){
    for( vector<IFR_BasicBlock>::iterator i = bblist.begin(); i != bblist.end(); i++){
      fprintf(stderr,"DF of %p:\n\t",i->getEntryAddr());
      for( set<ADDRINT>::iterator di = df[ i->getEntryAddr() ].begin();
           di != df[ i->getEntryAddr() ].end(); di++ ){
           fprintf(stderr,"%p ",*di);
      }
      fprintf(stderr,"\n");
    }
    fprintf(stderr,"\n");
  }

  if( KnobSSA.Value() == true ){
    computeSSA(bblist); 
  }

  if( KnobBlocks.Value() == true ){
    for( std::vector<IFR_BasicBlock>::iterator i = bblist.begin();
         i != bblist.end();
         i++
       ){
  
      i->print();
      fprintf(stderr,"-------------------------------------\n");
  
    }
  }

  RTN_Close(rtn);

}

VOID instrumentImage(IMG img, VOID *v)
{

}

void Read(THREADID tid, ADDRINT addr, ADDRINT inst)
{

}

void Write(THREADID tid, ADDRINT addr, ADDRINT inst)
{

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
