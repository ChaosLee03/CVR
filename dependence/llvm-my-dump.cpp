#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <queue>

#include <cassert>
#include <cstdio>

#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

#if LLVM_VERSION_MAJOR >= 4
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#else
#include <llvm/Bitcode/ReaderWriter.h>
#endif

#include "dg/PointerAnalysis/PointerAnalysisFI.h"
#include "dg/PointerAnalysis/PointerAnalysisFS.h"
#include "dg/PointerAnalysis/PointerAnalysisFSInv.h"
#include "dg/llvm/DataDependence/DataDependence.h"
#include "dg/llvm/LLVMDG2Dot.h"
#include "dg/llvm/LLVMDependenceGraph.h"
#include "dg/llvm/LLVMDependenceGraphBuilder.h"
#include "dg/llvm/LLVMSlicer.h"
#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"

#include "dg/tools/llvm-slicer-opts.h"
#include "dg/tools/llvm-slicer-utils.h"

#include "dg/util/TimeMeasure.h"

using namespace dg;
using namespace dg::debug;
using llvm::errs;
using namespace std;
using namespace llvm;


llvm::cl::opt<bool> enable_debug(
        "dbg", llvm::cl::desc("Enable debugging messages (default=false)."),
        llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> bb_only(
        "bb-only",
        llvm::cl::desc("Only dump basic blocks of dependence graph to dot"
                       " (default=false)."),
        llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> mark_only(
        "mark",
        llvm::cl::desc("Only mark nodes that are going to be in the slice"
                       " (default=false)."),
        llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<std::string>
        dump_func_only("func", llvm::cl::desc("Only dump a given function."),
                       llvm::cl::value_desc("string"), llvm::cl::init(""),
                       llvm::cl::cat(SlicingOpts));
llvm::cl::opt<std::string> MyPassStringOpt("Varname", llvm::cl::desc("接受变量名"), llvm::cl::value_desc("string"), llvm::cl::init(""));
        

// TODO: This machinery can be replaced with llvm::cl::callback setting
// the desired flags directly when we drop support for LLVM 9 and older.
enum PrintingOpts {
    call,
    cfgall,
    postdom,
    no_cfg,
    no_control,
    no_data,
    no_use
};

llvm::cl::list<PrintingOpts> print_opts(
        llvm::cl::desc("Dot printer options:"),
        llvm::cl::values(
                clEnumVal(call, "Print calls (default=false)."),
                clEnumVal(cfgall,
                          "Print full control flow graph (default=false)."),
                clEnumVal(postdom,
                          "Print post dominator tree (default=false)."),
                clEnumValN(no_cfg, "no-cfg",
                           "Do not print control flow graph (default=false)."),
                clEnumValN(
                        no_control, "no-control",
                        "Do not print control dependencies (default=false)."),
                clEnumValN(no_data, "no-data",
                           "Do not print data dependencies (default=false)."),
                clEnumValN(no_use, "no-use",
                           "Do not print uses (default=false).")
#if LLVM_VERSION_MAJOR < 4
                        ,
                nullptr
#endif
                ),
        llvm::cl::cat(SlicingOpts));

vector<Value*> vec;
set<Value*> myset;
vector<Value*> convec;
set<Value*> myconset;
vector<Value*> controldefvec;
set<Value*> mycondefset;
void findDef(LLVMDependenceGraph *dg, LLVMNode *node) {
    queue<LLVMNode*> nodeque;
    nodeque.push(node);
    int n = 1;
    while (!nodeque.empty()) {
        int length = nodeque.size();
        for (int i = 0; i < length; i++) {
            LLVMNode *top = nodeque.front();
            nodeque.pop();
            if (isa<GlobalVariable>(top->getValue())) {
                if (myset.find(top->getValue()) == myset.end() && n != 2) {
                    outs() << "n = " << n << "\n";
                    outs() << top->getValue()->getName() << "\n";
                    myset.insert(top->getValue());
                    vec.push_back(top->getValue());
                }
            }
            if (auto *allcoinst = dyn_cast<AllocaInst>(top->getValue())) {
                if (myset.find(top->getValue()) == myset.end() && n != 2) {
                    myset.insert(top->getValue());
                    vec.push_back(top->getValue());
                }
            }
            n++;
            for (auto black = top->user_begin(); black != top->user_end(); black++) {
//                outs() << *black << "\n";
//                outs() << black << "\n";
                nodeque.push(*black);
            }
            for (auto gray = top->rev_data_begin(); gray != top->rev_data_end(); gray++) {
                nodeque.push(*gray);
            }
        }
    }
}
void findDef2(LLVMDependenceGraph *dg, LLVMNode *node) {
    queue<LLVMNode*> nodeque;
    nodeque.push(node);
    int n = 1;
    while (!nodeque.empty()) {
        int length = nodeque.size();
        for (int i = 0; i < length; i++) {
            LLVMNode *top = nodeque.front();
            nodeque.pop();
            if (isa<GlobalVariable>(top->getValue())) {
                if (mycondefset.find(top->getValue()) == mycondefset.end() && n != 1) {
                    mycondefset.insert(top->getValue());
                    controldefvec.push_back(top->getValue());
                }
            }
            if (auto *allcoinst = dyn_cast<AllocaInst>(top->getValue())) {
                if (mycondefset.find(top->getValue()) == mycondefset.end() && n != 1) {
                    mycondefset.insert(top->getValue());
                    controldefvec.push_back(top->getValue());
                }
            }
            n++;
            for (auto black = top->user_begin(); black != top->user_end(); black++) {
//                outs() << *black << "\n";
//                outs() << black << "\n";
                nodeque.push(*black);
            }
            for (auto gray = top->rev_data_begin(); gray != top->rev_data_end(); gray++) {
                nodeque.push(*gray);
            }
        }
    }
}
void findcontrol(LLVMDependenceGraph *dg, LLVMNode *node) {
    queue<LLVMNode*> nodeque;
    nodeque.push(node);
    int n = 1;
    while (!nodeque.empty()) {
        int length = nodeque.size();
        for (int i = 0; i < length; i++) {
            LLVMNode *top = nodeque.front();
            nodeque.pop();
            if (myconset.find(top->getValue()) == myconset.end() && n != 1) {
                convec.push_back(top->getValue());
                findDef2(dg, top);
                myconset.insert(top->getValue());
            }
            n++;
            auto bblock = top->getBBlock()->revControlDependence();
            for (auto b = bblock.begin(); b != bblock.end(); b++) {
                auto nodes = (*b)->getNodes();
                for (auto it = nodes.begin(); it != nodes.end(); it++) {
                    if (isa<BranchInst>(*(*it)->getValue())) {
                        nodeque.push(*it);
                    }
                }
            }
        }
    }
}

int main(int argc, char *argv[]) {
    setupStackTraceOnError(argc, argv);
    SlicerOptions options = parseSlicerOptions(argc, argv);
    uint32_t opts = PRINT_CFG | PRINT_DD | PRINT_CD | PRINT_USE | PRINT_ID;
    for (auto opt : print_opts) {
        switch (opt) {
        case no_control:
            opts &= ~PRINT_CD;
            break;
        case no_use:
            opts &= ~PRINT_USE;
            break;
        case no_data:
            opts &= ~PRINT_DD;
            break;
        case no_cfg:
            opts &= ~PRINT_CFG;
            break;
        case call:
            opts |= PRINT_CALL;
            break;
        case postdom:
            opts |= PRINT_POSTDOM;
            break;
        case cfgall:
            opts |= PRINT_CFG | PRINT_REV_CFG;
            break;
        }
    }

    if (enable_debug) {
        DBG_ENABLE();
    }

    llvm::LLVMContext context;
    std::unique_ptr<llvm::Module> M =
            parseModule("llvm-dg-dump", context, options);
    if (!M)
        return 1;
    
    
    llvmdg::LLVMDependenceGraphBuilder builder(M.get(), options.dgOptions);
    auto dg = builder.build();

    
    std::string lookingforname = MyPassStringOpt.getValue();
//    llvm::outs() << "输出参数：" << lookingforname << "\n";

    auto mygraph = dg.get();
    

    for(auto &p : *dg) {
        if (p.first) {
            llvm::Value *value = p.first;
            if (auto *Instrinst = dyn_cast<llvm::Instruction>(value)) {
                for (int i = 0, e = Instrinst->getNumOperands(); i != e; i++) {
                    llvm::Value *op = Instrinst->getOperand(i);
                    if (op->hasName() && op->getName().str() == lookingforname) {
                        findDef(mygraph, p.second);
                        findcontrol(mygraph, p.second);
//                        llvm::outs() << *p.first << "数据依赖边,黑边: " << ( p.second->getUserDependenciesNum()) << "\n";//黑边
//                        for (auto a = p.second->user_begin(); a != p.second->user_end(); a++) {
//                            llvm::outs() << *((*a)->getValue()) << "\n";
//
//
//                        }
//                        llvm::outs() << *p.first << "数据依赖边,灰边: " << (p.second->getRevDataDependenciesNum()) << "\n";//灰边
//                        for (auto a = p.second->rev_data_begin(); a != p.second->rev_data_end(); a++) {
//                            llvm::outs() << *((*a)->getValue()) << "\n";
//                        }
                    }
                }
            }
        }
    }
    
    outs() << lookingforname << "数据依赖于: ";
    for (auto a : vec) {
        if (a->hasName()) {
            outs() << a->getName() << " ";
        }
        else {
            outs() << *a << " ";
        }
    }
    //if, else, switch, while, for
    outs() << "\n" << lookingforname << "控制依赖于: ";
    for (auto a : controldefvec) {

        if (a->hasName()) {
            outs() << a->getName().str() << " ";
        }
        else {
            outs() << *a << " ";
        }
    }
    return 0;
}
