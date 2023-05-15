#include <cassert>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>
#include <map>

#include "llvm/IR/Operator.h"
#include "llvm/IR/Instructions.h"
#include "dg/tools/llvm-slicer-opts.h"
#include "dg/tools/llvm-slicer-utils.h"
#include "dg/tools/llvm-slicer.h"

#if LLVM_VERSION_MAJOR >= 4
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#else
#include <llvm/Bitcode/ReaderWriter.h>
#endif

#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Pass.h>
#include <llvm/Analysis/LoopIterator.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Analysis/LoopPass.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Transforms/Scalar/IndVarSimplify.h>
#include "dg/llvm/SystemDependenceGraph/SDG2Dot.h"
#include "dg/util/debug.h"

#include "json.h"

//using namespace dg;

using namespace llvm;
using namespace std;

llvm::cl::opt<bool> enable_debug(
        "dbg", llvm::cl::desc("Enable debugging messages (default=false)."),
        llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> dump_bb_only(
        "dump-bb-only",
        llvm::cl::desc("Only dump basic blocks of dependence graph to dot"
                       " (default=false)."),
        llvm::cl::init(false), llvm::cl::cat(SlicingOpts));
llvm::cl::opt<string> whichtochoose("use", cl::init(""), cl::desc("decide which part to be called"));
//存储依赖关系
//一个变量依赖于哪些变量
unordered_map<dg::sdg::DGBBlock*, vector<GlobalValue*>>Control;
map<string, vector<string>> Convar;//一个全局变量控制依赖于哪些变量
unordered_map<dg::sdg::DGNode*, vector<GlobalValue*>>Defuse;
unordered_map<dg::sdg::DGNode*, vector<GlobalValue*>>DataRAW;
//void DebugValue(dg::sdg::DGNode* dg_node)
//{
//    std::ostringstream ostr;
//    llvm::raw_os_ostream ro(ostr);
//    ostr.str("");
//    if(sdg->getValue(dg_node))
//        ro<<*(sdg->getValue(dg_node));
//    ro.flush();
//    std::cout<<ostr.str()<<std::endl;
//}
static std::string DebugValue(const llvm::Value* value)
{
    if(!value)
        return "";
    std::ostringstream ostr;
    llvm::raw_os_ostream ro(ostr);
    ostr.str("");
    value->print(ro);
    //ro<<*(value);
    ro.flush();
    //std::cout<<ostr.str()<<std::endl;
    return ostr.str();
}


void Debug(const Instruction *i) {
    outs() << *i << "\n";
}
void DebugValue(const llvmdg::SystemDependenceGraph &s, dg::sdg::DGNode * node) {
    auto v = s.getValue(node);
    if (!v) {
        outs() << "No Value!\n";
        return ;
    }
    outs() << *v << "\n";
}
void controldep(llvmdg::SystemDependenceGraph &sdg, dg::sdg::DGNode* dgNode, vector<GlobalValue*> &globals, int nodeid, int times) {
    if (times == 100)
        return;
    //检索这个节点有没有被检查过

    auto it = Control.find(dgNode->getBBlock());
    if (it != Control.end()) {
        for (auto global : it->second) {
            globals.push_back(global);
        }
    }
    outs() << "this node " << *sdg.getValue(dgNode) << "\n";
    for (auto *condep : dgNode->getBBlock()->control_deps()) {
        if (condep == dgNode)
            continue;
        if (auto conblock = dg::sdg::DGBBlock::get(condep)) {
            if (auto value = sdg.getValue(conblock->back())) {
                Value *brvalue = nullptr;
                if (auto brinst = dyn_cast<BranchInst>(value)) {
                    outs() << *brinst << "\n";
                    if (!brinst->isConditional())
                        continue;
                    brvalue = brinst->getCondition();
                    outs() << brvalue->getName() << "\n";
                }
            }

        }
    }

}

void datarawdep(llvmdg::SystemDependenceGraph &sdg, dg::sdg::DGNode* dgNode, vector<GlobalValue*> &globals, int nodeid, int times) {
    if (times == 100)
        return;
    if (nodeid == -1) {
        if (auto global = dyn_cast<GlobalValue>(sdg.getValue(dgNode)))
        globals.push_back(global);
        return;
    }
    outs() << "this node "; DebugValue(sdg, dgNode);  outs() << dgNode->getID() << "\n";
    for (auto dgdep = dgNode->memdep_begin(); dgdep != dgNode->memdep_end(); dgdep++) {
        outs() << *sdg.getValue(*dgdep) << " " << (*dgdep)->getID() << "\n";
        datarawdep(sdg, dg::sdg::DGNode::get(*dgdep), globals, (*dgdep)->getID(), times + 1);
    }
    for (auto dgdefuse = dgNode->users_begin(); dgdefuse != dgNode->users_end(); dgdefuse++) {
        if (sdg.getValue(*dgdefuse)) {
            outs() << *sdg.getValue(*dgdefuse) << " " << (*dgdefuse)->getID() << "\n";
            datarawdep(sdg, dg::sdg::DGNode::get(*dgdefuse), globals, (*dgdefuse)->getID(), times + 1);
        }

    }
}
//变量类型:常规变量，数组，结构体，指针
//数组有可能下标为变量、指针、表达式
//结构体有可能还有个结构体成员
//先收集所有全局变量的信息，
class GlobalVarInfo{
    //描述位置和读写, DILocation可以记录行号和列号，string记录读还是写，读为'R'，写为'W',
    //这里应该使用pair进行记录，而不是map
    pair<DILocation*, string> linesRW;
    //描述该变量，因为GlobalVariable不能被继承，所以用作成员变量
    GlobalVariable *GV;
    //名称
    string name;
    //判断是否是普通变量，普通变量使用相似命名分析，结构体和数组使用兄弟元素分析
    //flag = 0表示是普通变量
    //flag = 1表示数组或结构体
    int flag;
    //它控制依赖的变量
//    set<GlobalVarInfo*> dependeceon;
    vector<tuple<string, int, int>> dependenceC;
    //它数据依赖的变量
    vector<tuple<string, int, int>> dependenceD;
    //依赖于它的变量
//    set<GlobalVarInfo*> dependeceby;
    //它所属于的函数
    string belongto;
    //它所属的指令
    Instruction *I;
    //它所属的DGNode节点
    dg::sdg::DGNode *dgn;
    //最近一次写它的节点
    dg::sdg::DGNode *lastWdgn;
    //与它名字相似的变量
    vector<pair<string, int>> closename;
    //这次访问的ID
    unsigned int ID;
  public:
    //构造函数
    GlobalVarInfo(GlobalVariable *GV) : GV(GV) {dgn = nullptr, lastWdgn = nullptr;}
    //获取位置和读写
    pair<DILocation*, string> getRWLoc() {return linesRW;}
    //添加位置和读写的映射
    void addlines(DILocation *loc, string rw) {linesRW.first = loc, linesRW.second = rw;}
    //获取GlobalVariable
    GlobalVariable* getVariable() {return GV;}
    //设置名称
    void setname(string n) {name = n;}
    //获取名称
    string getName() {return name;}
    //设置标志
    void setflag(int f) {flag = f;}
    //获取标志
    int getflag() {return flag;}
    //设置这个位置的这个变量所属于的函数的名称
    void setbelongto(string name) {belongto = name;}
    //获取它所属的函数
    string getbelongto() {return belongto;}
    //获取它所属的指令
    Instruction* getbelongtoi() {return I;}
    //设置它的指令
    void setInst(Instruction *i) {I = i;}
    //获取它所属的节点
    dg::sdg::DGNode *getDGNode() {return dgn;}
    //设置它所属的节点
    void setDGNode(dg::sdg::DGNode *D) {dgn = D;}
    //设置上一次写它的节点
    void setLastWNode(dg::sdg::DGNode *D) {lastWdgn = D;}
    //获取上一次写它的节点
    dg::sdg::DGNode *getLastWNode() {return lastWdgn;}
    //添加它控制依赖的变量
    void adddependencevarC(tuple<string, int, int> dep) {dependenceC.push_back(dep);}
    //获得它控制依赖的变量
    vector<tuple<string, int, int>> getDepenceonC() {return dependenceC;}
    //添加它数据依赖的变量
    void adddependencevarD(tuple<string, int, int> dep) {dependenceD.push_back(dep);}
    //获得它数据依赖的变量
    vector<tuple<string, int, int>> getDepenceonD() {return dependenceD;}
    //添加名字相似的变量
    void addclosename(pair<string, int> name) {closename.push_back(name);}
    //获得名字相似的变量
    vector<pair<string, int>> getclosename() {return closename;}
    //设置它的ID
    void setID(unsigned int id) {ID = id;}
    //获取它的ID
    unsigned getID() {return ID;}
    bool operator<(const GlobalVarInfo& other) const{
        int thisline = linesRW.first->getLine();
        int thatline = other.linesRW.first->getLine();
        if (thisline != thatline) {
            return thisline < thatline;
        } else {
            if (name != other.name)
                return name < other.name;
            else {
                return linesRW.second < other.linesRW.second;
            }
        }
    }
    bool operator==(const GlobalVarInfo& other) const {
        if (linesRW.first->getLine() == other.linesRW.first->getLine() && name == other.name && linesRW.second == other.linesRW.second)
            return true;
        return false;
    }
};
//存储所有的全局变量
//放到全局位置，减少作为参数的次数
vector<GlobalVarInfo*> globalvarlist;
//LoopPass分析Loop
struct MyLoopPass : public LoopPass {
  public:
    static char ID;
    MyLoopPass() : LoopPass(ID) {}
    bool runOnLoop(Loop *L, LPPassManager &LPM) override {
        outs() << L->getName() << "\n";
        auto LoopBBs = L->getBlocks();
        bool hasarray = false;
        map<GlobalVarInfo*, Instruction*> golist;//存放这个loop里访问的所有全局数组与其对应的指令
        PHINode *IndVar = L->getCanonicalInductionVariable();
        if (!IndVar) {
            outs() << "it's null\n";
        }
        else {
            outs() << IndVar->getName().str() << "\n";
        }
        for (auto &BB : LoopBBs) {
            for (auto &I : *BB) {
                if (auto *GEPO = dyn_cast<GEPOperator>(&I)) {

                }
                int num = I.getNumOperands();
                outs() << I <<"\n";
                for (int i = 0; i < num; i++) {
                    Value *v = I.getOperand(i);
                    outs() << *v << "\n";
                    if (GlobalVariable *GV = dyn_cast<GlobalVariable>(v)) {
                        if (auto *ptrType = dyn_cast<PointerType>(v->getType())) {
                            if (auto *arrayType = dyn_cast<ArrayType>(ptrType->getElementType())) {
                                hasarray = true;
                                GlobalVarInfo *GVI = new GlobalVarInfo(GV);
                                GVI->setname(GV->getName().str());
                                outs() << GVI->getName() << "\n";
                                golist.insert({GVI, &I});
                            }
                        }
                    }
                }
            }
        }
        if (hasarray) {
            for (auto &m : golist) {
                Type *gtype = m.first->getVariable()->getType()->getElementType();
                int subnums = 0;
                if (ArrayType * AT = dyn_cast<ArrayType>(gtype)) {
                    subnums++;
                    while (ArrayType *innertype = dyn_cast<ArrayType>(AT->getElementType())) {
                        subnums++;
                        if (ArrayType *eletype = dyn_cast<ArrayType>(AT->getElementType())) {
                            AT = eletype;
                        }
                    }
                }
                outs() << subnums << " \n";
//                Instruction *I = m.second;
//                if (auto *GEP = dyn_cast<GEPOperator>(I)) {
//                    int nums = GEP->getNumOperands();
//                    outs() << nums << "\n";
//                    int subnums = nums - 1;
//                }
            }
        }
        return false;
    }
};
char MyLoopPass::ID = 0;

//指令对应的操作数
//一个指令可以涉及到多个操作数，那就是CallInst指令
map<Instruction*, set<GlobalVarInfo*>> InstrtoGlobl;
//记录每个变量的上一个写操作
map<string, dg::sdg::DGNode*> GlobaltoLastW;
vector<pair<Instruction*, GlobalVarInfo>> insttoGlobal;
//存放每个函数里有哪些全局变量
map<string, vector<GlobalVarInfo*>> funhasvar;
//存放距离近的变量
map<GlobalVarInfo*, set<GlobalVarInfo*>> Globalclose;
//存放分类好的兄弟元素
map<string, set<string>> brothers;//第一个string存放这个数组/结构体的名字，比如b[4], b[3]第一个string为b, set里存放b[3], b[4]
void similarnameAnalysis();
bool similarnameAnalysis(string &a, string &b);
void brotherAnalysis();
void controldep(llvmdg::SystemDependenceGraph &sdg, GlobalVarInfo *g);
void memdep(llvmdg::SystemDependenceGraph &sdg, GlobalVarInfo *g);
void loopanalysis(std::unique_ptr<llvm::Module> M);
void findvarclose(int distance);
unsigned int visitid = 1;
int main(int argc, char *argv[]) {

    setupStackTraceOnError(argc, argv);
    SlicerOptions options = parseSlicerOptions(argc, argv);
    llvm::LLVMContext context;
    std::unique_ptr<llvm::Module> M =
            parseModule("llvm-sdg-dump", context, options);
    if (!M)
        return 1;

    if (!M->getFunction(options.dgOptions.entryFunction)) {
        llvm::errs() << "The entry function not found: "
                     << options.dgOptions.entryFunction << "\n";
        return 1;
    }

    if (enable_debug) {
        DBG_ENABLE();
    }
    //收集一下loop的信息
//    std::unique_ptr<llvm::legacy::FunctionPassManager> GlobalLFPM;
//    GlobalLFPM = std::make_unique<llvm::legacy::FunctionPassManager>(M.get());
//    GlobalLFPM->add(new MyLoopPass());
//    for (auto &F : *M) {
//        outs() << F.getName() << "\n";
//        GlobalLFPM->run(F);
//    }

    //先搜集全部的全局变量使用情况
    for (auto &G : M->globals()) {
        for (auto &U : G.uses()) {
            if (auto *I = dyn_cast<Instruction>(U.getUser())) {
                bool isgepo = false;
                if (isa<GEPOperator>(I))
                    isgepo = true;
                if (!isgepo) {
                    if (auto &L = I->getDebugLoc()) {
                        // 如果变量是个常规变量或指针
                        string rw;//记录读或写
                        if (isa<StoreInst>(I)) {
                            rw = 'w';
                        }
                        if (isa<LoadInst>(I)) {
                            rw = 'r';
                        }
                        if (isa<CallInst>(I)) {
                            rw = 'r';
                        }
                        GlobalVarInfo *g = new GlobalVarInfo(&G);
                        g->setname(G.getName().str());
                        g->setflag(0);//普通变量是0
                        DILocation *loc = L;
                        g->addlines(loc, rw);
                        g->setInst(I);
                        g->setID(visitid++);
                        if (InstrtoGlobl.find(I) != InstrtoGlobl.end()) {
                            InstrtoGlobl[I].insert(g);
                        }
                        else {
                            set<GlobalVarInfo*> s;
                            s.insert(g);
                            InstrtoGlobl.insert(make_pair(I, s));
                        }
                        string funname = I->getFunction()->getName().str();
                        if (funname != "") {
                            g->setbelongto(funname);
                            if (funhasvar.find(funname) != funhasvar.end()) {
                                funhasvar[funname].push_back(g);
                            }
                            else {
                                vector<GlobalVarInfo*> vec;
                                vec.push_back(g);
                                funhasvar.insert(make_pair(funname, vec));
                            }
                        }
                        globalvarlist.push_back(g);
                    }
                }
            }
            if (auto *GEPOp = dyn_cast<GEPOperator>(U.getUser())) {
                // 直接判断下标是不是变量，是直接跳过
                int num = GEPOp->getNumOperands();
                if (num >= 3) {
                    Value *offset = GEPOp->getOperand(2);
                    auto *f = dyn_cast<ConstantInt>(offset);
                    if (f == nullptr) {
                        //下标不是一个立即数
                        if (auto *sextinst = dyn_cast<SExtInst>(offset)) {
                            //如果这是个伸长指令
                            offset = sextinst->getOperand(0);
//                            errs() << *offset << "\n";
                        }
                        if (isa<ConstantExpr>(offset)) {
                            outs() << "yes\n";
                        }
                        //offset是下标
                        //如何处理？
                        Instruction *I;
                        string rw;
                        for (auto &U2 : GEPOp->uses()) {
                            if (I = dyn_cast<Instruction>(U2.getUser())) {
                                if (auto *store = dyn_cast<StoreInst>(I)) {
                                    rw += 'W';
                                    break;
                                }
                                if (auto *load = dyn_cast<LoadInst>(I)) {
                                    rw += 'R';
                                    break;
                                }
                            }
                        }
                        GlobalVarInfo *g = new GlobalVarInfo(&G);
                        g->setID(visitid++);
                        g->setname(G.getName().str());
                        g->setInst(I);
                        g->setflag(1);
                        DILocation *DL = I->getDebugLoc();
                        g->addlines(DL, rw);
                        if (InstrtoGlobl.find(I) != InstrtoGlobl.end()) {
                            InstrtoGlobl[I].insert(g);
                        }
                        else {
                            set<GlobalVarInfo*> s;
                            s.insert(g);
                            InstrtoGlobl.insert(make_pair(I, s));
                        }
                        string funname = I->getFunction()->getName().str();
                        if (funname != "") {
                            g->setbelongto(funname);
                            if (funhasvar.find(funname) != funhasvar.end()) {
                                funhasvar[funname].push_back(g);
                            }
                            else {
                                vector<GlobalVarInfo*> vec;
                                vec.push_back(g);
                                funhasvar.insert(make_pair(funname, vec));
                            }
                        }
                        globalvarlist.push_back(g);
                        outs() << *GEPOp << "\n";
                        outs() << "下标是变量！\n";

                    }
                    else {
                        int t = 0;
                        string rw;
                        Instruction *I;
                        for (auto &U2 : GEPOp->uses()) {
                            if (I = dyn_cast<Instruction>(U2.getUser())) {
                                // 如果指令是 StoreInst，那么就是写，否则是读
                                if (auto *Store = dyn_cast<StoreInst>(I)) {
                                    //写结构体/数组
                                    rw += 'w';
                                    break;
                                }
                                if (auto *Load = dyn_cast<LoadInst>(I)) {
                                    //读结构体/数组
                                    rw += 'r';
                                    break;
                                }
                            }
                        }
                        //判断是数组还是结构体
                        if (auto *PT = dyn_cast<PointerType>(G.getType())) {
                            string fullname;
                            if (auto *ST = dyn_cast<StructType>(PT->getElementType())) {
                                //是结构体
                                string structname = G.getName().str();
                                //成员的名称
                                string fieldname;
                                int level = num - 2; //，剩下的是偏移的层数
                                auto mn = dyn_cast<MDNode>(G.getMetadata("dbg")->getOperand(0));
                                if (auto *mn2 = dyn_cast<MDNode>(mn->getOperand(3))) {
                                    if (auto *mn3 = dyn_cast<MDNode>(mn2->getOperand(4))) {
                                        function<void(int, MDNode*)> f = [&] (int depth, MDNode* mn0) { //处理结构体嵌套
                                            if (depth == level)
                                                return;
                                            unsigned idx = cast<ConstantInt>(GEPOp->getOperand(2 + depth))->getZExtValue();
                                            if (auto *mn4 = dyn_cast<MDNode>(mn0->getOperand(idx))) {
                                                if (MDString *mds = dyn_cast<MDString>(mn4->getOperand(2))) {
                                                    fieldname = fieldname + "." + mds->getString().str();
                                                }
                                                if (depth + 1 == level)
                                                    return;
                                                if (auto *mn5 = dyn_cast<MDNode>(mn4->getOperand(3))) {
                                                    if (auto *mn6 = dyn_cast<MDNode>(mn5->getOperand(4))) {
                                                        f(depth + 1, mn6);
                                                    }
                                                }
                                            }
                                        };
                                        f(0, mn3);
                                    }
                                }
                                fullname = structname + fieldname;
                            }
                            if (auto *AT = dyn_cast<ArrayType>(PT->getElementType())) {
                                // 是数组
                                string arrayVarName = G.getName().str();
                                fullname = arrayVarName;
                                if (auto *self = GEPOp->getOperand(0)) {
                                    Type *ElementTypeOfAT =
                                            AT->getElementType();
                                    // 设置一个标记变量，0代表普通数组，1代表结构体数组,2代表二维数组
                                    int ff = 0;
                                    // plus代表名字上补上的部分，eg a[3][4]中的[4], b[4].a中的.a
                                    string plusname = "";
                                    if (StructType *ST = dyn_cast<StructType>(ElementTypeOfAT)) {
                                        // 进到这里说明是结构体数组
                                        ff = 1;
                                        int level = num - 2;
                                        auto mn = dyn_cast<MDNode>(G.getMetadata("dbg")->getOperand(0));
                                        if (auto *mn2 = dyn_cast<MDNode>(mn->getOperand(3))) {
                                            if (auto *mn3 = dyn_cast<MDNode>(mn2->getOperand(3))) {
                                                if (auto *mn3dot5 = dyn_cast<MDNode>(mn3->getOperand(4))) {
                                                    function<void(int, MDNode*)> f = [&] (int depth, MDNode* mn0) { //处理结构体嵌套
                                                        if (depth == level)
                                                            return;
                                                        unsigned idx = cast<ConstantInt>(GEPOp->getOperand(2 + depth))->getZExtValue();
                                                        if (auto *mn4 = dyn_cast<MDNode>(mn0->getOperand(idx))) {
                                                            if (MDString *mds = dyn_cast<MDString>(mn4->getOperand(2))) {
                                                                plusname = plusname + "." + mds->getString().str();
                                                            }
                                                            if (depth + 1 == level)
                                                                return;
                                                            if (auto *mn5 = dyn_cast<MDNode>(mn4->getOperand(3))) {
                                                                if (auto *mn6 = dyn_cast<MDNode>(mn5->getOperand(4))) {
                                                                    f(depth + 1, mn6);
                                                                }
                                                            }
                                                        }
                                                    };
                                                    f(1, mn3dot5);
                                                }

                                            }
                                        }
                                    }
                                    if (auto *Idx = GEPOp->getOperand(2)) {
                                        //获取第一个维度的下标
                                        if (auto *CIdx = dyn_cast<ConstantInt>(Idx)) {
                                            unsigned idx = CIdx->getZExtValue();
                                            fullname += "[" + to_string(idx) + "]";
                                        }
                                        else {
                                            //下标不是一个ConstantInt，这种情况其实已经跳过了
                                            fullname += "[i]";
                                        }
                                    }
                                    //判断操作数的类型，是不是高维数组
                                    Value *ope = GEPOp->getOperand(0);
//                                    outs() << *ope << "\n";
                                    int dimension = 1;//记录数组的维度
                                    if (auto *pointertype = dyn_cast<PointerType>(ope->getType())) {
                                        if (auto *arraytype = dyn_cast<ArrayType>(pointertype->getElementType())) {
                                            while (auto *nexttype = dyn_cast<ArrayType>(arraytype->getElementType())) {
                                                dimension++;
                                                arraytype = nexttype;
                                            }
                                        }
                                    }
//                                    outs() << "是" << dimension << "维数组\n";
                                    for (int i = 3; i < GEPOp->getNumOperands() && dimension > 1; i++, dimension--) {
                                        //获取其他维度下标
                                        if (auto *Idx = GEPOp->getOperand(i)) {
                                            if (auto *CIdx = dyn_cast<ConstantInt>(Idx)) {
                                                unsigned idx = CIdx->getZExtValue();
                                                fullname += "[" + to_string(idx) + "]";
                                            }
                                        }
                                    }
                                    fullname += plusname;
                                }
                            }
                            GlobalVarInfo *g = new GlobalVarInfo(&G);
                            DILocation *loc = I->getDebugLoc();
                            g->setname(fullname);
                            g->addlines(loc, rw);
                            g->setflag(1);
                            g->setInst(I);
                            g->setID(visitid++);
                            set<GlobalVarInfo*> s;
                            s.insert(g);
                            InstrtoGlobl.insert(make_pair(I,s));
                            if (I->getFunction()->hasName()) {
                                string funname = I->getFunction()->getName().str();
                                g->setbelongto(funname);
                                if (funhasvar.count(funname)) {
                                    funhasvar[funname].push_back(g);
                                }
                                else {
                                    vector<GlobalVarInfo*> vec;
                                    vec.push_back(g);
                                    funhasvar.insert(make_pair(funname, vec));
                                }
                            }
                            globalvarlist.push_back(g);
                        }
                    }
                }
            }
        }
    }
    sort(globalvarlist.begin(), globalvarlist.end(), [] (GlobalVarInfo *a, GlobalVarInfo *b) {
        return *a < *b;
    });
    DGLLVMPointerAnalysis PTA(M.get(), options.dgOptions.PTAOptions);
    PTA.run();
    LLVMDataDependenceAnalysis DDA(M.get(), &PTA, options.dgOptions.DDAOptions);
    DDA.run();
    LLVMControlDependenceAnalysis CDA(M.get(), options.dgOptions.CDAOptions);
    // CDA runs on-demand

    llvmdg::SystemDependenceGraph sdg(M.get(), &PTA, &DDA, &CDA);

    for (auto *dg : sdg.getSDG()) {//找到每个变量属于哪一个node
        for (auto *node : dg->getNodes()) {
            if (Value *v = sdg.getValue(node))  {
                if (auto *inst = dyn_cast<Instruction>(v)) {
                    if (auto *Winst = dyn_cast<StoreInst>(inst)) {
                        auto it = InstrtoGlobl.find(inst);
                        if (it != InstrtoGlobl.end()) {
                            for (auto &g : it->second) {
                                g->setLastWNode(node);
                                g->setDGNode(node);
                                GlobaltoLastW.insert(make_pair(g->getName(), node));
                            }
                        }
                    }
                    else {
                        auto it = InstrtoGlobl.find(inst);
                        if (it != InstrtoGlobl.end()) {
                            for (auto &g : it->second) {
                                g->setDGNode(node);
                                if (GlobaltoLastW.find(g->getName()) != GlobaltoLastW.end())
                                    g->setLastWNode(GlobaltoLastW[g->getName()]);
                            }
                        }
                    }

                }
            }
        }
    }
    string wheretogo = whichtochoose.getValue();
    if (wheretogo == "name") {
        outs() << "name\n";
        similarnameAnalysis();
        return 0;
    }
    else if (wheretogo == "print") {

        int count = 0;
        for (auto g : globalvarlist) {
            outs() << "global variable: " << g->getName() << " belongs to " << g->getbelongto() << " located in " << g->getRWLoc().first->getLine() << "\n";
            outs() << ++count << "\n";//843
        }
    }
    else if (wheretogo.find("distance") != string::npos) {
        int distance = 10;
        if (wheretogo.size() > 8) {
            string stringdis = wheretogo.substr(8);
            distance = std::stoi(stringdis);
            outs() << distance << "\n";
        }
        findvarclose(distance);
//        for (auto &p : Globalclose) {
//            outs() << p.first->getName() << " in line " << p.first->getRWLoc().first->getLine() << "\n";
//            for (auto &g : p.second) {
//                outs() << g->getName() << " in line " << g->getRWLoc().first->getLine() << "\n";
//            }
//            outs() << "end of " << p.first->getName() << "  *************\n";
//        }
        return 0;
    }
    else if (wheretogo == "IO") {
        cout << "IO";
        return 0;
    }
    else if (wheretogo == "brother") {
        cout << "brother\n";
        brotherAnalysis();
        return 0;
    }
    else if (wheretogo == "dependence") {
        outs() << "dependence*******\n";
        for (auto *g : globalvarlist) {
            outs() << "control*********\n";
            controldep(sdg, g);
            outs() << "memory***********\n";
            memdep(sdg, g);
        }
        for (auto *g : globalvarlist) {
            outs() << "Dependence START--------:\n" << "   " << g->getName() << " line" << g->getRWLoc().first->getLine() << "\n";
            for (auto &s : g->getDepenceonC()) {
                outs() << "     controlled by " << get<0>(s)<< " in lines " << get<1>(s) << "\n";
            }
            for (auto &s : g->getDepenceonD()) {
                outs() << "     data dependence with " << get<0>(s) << "in lines " << get<1>(s) << "\n";
            }
            outs() << "Dependence END--------\n";
        }
//        for (auto *dg : sdg.getSDG()) {
//            outs() << dg->getName() << "\n";
//            for (auto *node : dg->getNodes()) {
//                if(sdg.getValue(node) == nullptr)
//                    continue;
//                if (auto nodevalue = dyn_cast<Instruction>(sdg.getValue(node))) {
//                    if (auto storeinst = dyn_cast<StoreInst>(nodevalue)) {
//                        //取出store指令的第二个操作数
//                        auto op = dyn_cast<GlobalVariable>(storeinst->getOperand(1));
//                        if (!op) {
//                            continue;
//                        }
//                        queue<dg::sdg::DGNode*> que;
//                        set<dg::sdg::DGNode*> visited;
//                        que.push(node);
//                        visited.insert(node);
//                        int count = 0;
//                        while (!que.empty()) {
//                            int num = que.size();
//                            for (int i = 0; i < num; i++) {
//                                dg::sdg::DGNode *dgn = que.front();
//                                que.pop();
//                                if (count == 0) {
//                                    for (auto dgnuse = dgn->users_begin(); dgnuse != dgn->users_end(); dgnuse++) {
//
//                                    }
//                                    for (auto dgnn = dgn->memdep_begin(); dgnn != dgn->memdep_end(); dgnn++) {
//                                        outs() << "进入\n";
//                                    }
//                                    for (auto dgnn : dgn->rev_memdep()) {
//                                        if (dgnn = *dgn->rev_memdep_begin()) {
//                                            outs() << *sdg.getValue(dgn) << " " << dgn->getID() << "\n";
//                                        }
//                                        outs() << *sdg.getValue(dgnn)  << "\n";
//                                    }
//                                    for (auto dgnuse : dgn->users()) {
//                                        DebugValue(sdg.getValue(dgnuse));
//                                    }
//                                    for (auto indgn : dgn->memdep()) {
//
//                                    }
//                                    count++;
//                                }
//                                else {
//                                    for (auto indgn : dgn->rev_memdep()) {
//                                        outs() << 5;
//                                    }
//                                    count++;
//                                }
//                            }
//                        }
//                    }
//                }
//            }
//        }
        return 0;
    }
    else if (wheretogo.find("save") != string::npos) {
        int distance = 10;
        if (wheretogo.size() > 4) {
            string stringdis = wheretogo.substr(4);
            distance = std::stoi(stringdis);
        }
        for (auto *g : globalvarlist) {
            controldep(sdg, g);
            memdep(sdg, g);
        }
        findvarclose(distance);
        similarnameAnalysis();
        if (globalvarlist.size() > 0) {
            errs() << "Yes\n";
        }
        ofstream file("/Users/mypc/Desktop/myoutput/output.csv");
        if (file.is_open()) {
            for (auto &g : globalvarlist) {
                file << g->getID() << ",";
                file << g->getName() << ",";
                file << g->getRWLoc().second << ",";
                file << g->getRWLoc().first->getLine() << ",";
                file << "\n";
            }
            file.close();
        } else {
            errs() << "not opened!\n";
        }
        int count = 0;
        for (auto &g : funhasvar) {
            string filename = "/Users/mypc/Desktop/myoutput/output_" + g.first + ".csv";
            ofstream fileid(filename);
            if (fileid.is_open()) {
                //id-代码距离-数据依赖距离-控制依赖距离-名称相似度
                for (auto &fg : g.second) {
                    int theline = fg->getRWLoc().first->getLine();
                    string thename = fg->getName();
                    fileid << fg->getID() << ",";
                    fileid << thename << ",";
                    fileid << fg->getRWLoc().second << ",";
                    fileid << theline << ",";
                    for (auto &nextfg : g.second) {
                        string towrite = "";
                        if (nextfg == fg) {
                            towrite = "/";
                            fileid << towrite << ",";
                            continue;
                        }
                        towrite += to_string(nextfg->getID());
                        towrite += "-";
                        int lines = nextfg->getRWLoc().first->getLine();
                        int wline = abs(lines - theline);
                        towrite += to_string(wline);
                        towrite += "-";
                        int datadistance = 1000000;
                        auto dtemps = fg->getDepenceonD();
                        for (auto &dtemp : dtemps) {
                            if (get<0>(dtemp) == nextfg->getName() && get<1>(dtemp) == lines) {
                                datadistance = get<2>(dtemp);
                            }
                        }
//                        auto temps = nextfg->getDepenceonD();
//                        for (auto &temp : temps) {
//                            if (get<0>(temp) == thename && get<1>(temp) == theline) {
//                                datadistance = get<2>(temp);
//                            }
//                        }
                        towrite += to_string(datadistance);
                        towrite += "-";
                        string thisname = nextfg->getName();
                        int controldistance = 1000000;
                        auto ctemps = fg->getDepenceonC();
                        for (auto ctemp : ctemps) {
                            if (get<0>(ctemp) == nextfg->getName() && get<1>(ctemp) == lines) {
                                controldistance = get<2>(ctemp);
                            }
                        }
//                        auto contemps = nextfg->getDepenceonC();
//                        for (auto contemp : contemps) {
//                            if (get<0>(contemp) == thename && get<1>(contemp) == theline) {
//                                controldistance = get<2>(contemp);
//                            }
//                        }
                        towrite += to_string(controldistance);
                        towrite += "-";
                        if (similarnameAnalysis(thename, thisname)) {
                            towrite += "1";
                        }
                        else {
                            towrite += "0";
                        }
                        fileid << towrite << ",";
                    }
                    fileid << "\n";
                }
            }
            else {
                errs() << "Failed to open file!\n";
            }
        }
        Json::Value root;
        Json::FastWriter writer;
        Json::Value eachelement;
        for (auto *g : globalvarlist) {
            eachelement["name"] = g->getName();
            eachelement["line"] = g->getRWLoc().first->getLine();
            Json::Value depD(Json::arrayValue), depC(Json::arrayValue);
            Json::Value line, name;
            for (auto &dc : g->getDepenceonD()) {
                line["line"] = get<1>(dc);
                name["name"] = get<0>(dc);
                Json::Value temp(Json::arrayValue);
                temp.append(line);
                temp.append(name);
                depD.append(temp);
            }
            eachelement["datadependence"] = depD;
            for (auto &dd : g->getDepenceonC()) {
                line["line"] = get<1>(dd);
                name["name"] = get<0>(dd);
                Json::Value temp(Json::arrayValue);
                temp.append(line);
                temp.append(name);
                depC.append(temp);
            }
            eachelement["controldependence"] = depC;
            Json::Value disstore(Json::arrayValue);
            for (auto &dis : Globalclose[g]) {
                line["line"] = dis->getRWLoc().first->getLine();
                name["name"] = dis->getName();
                Json::Value temp(Json::arrayValue);
                temp.append(line);
                temp.append(name);
                disstore.append(temp);
            }
            eachelement["shortdistance"] = disstore;
            Json::Value namestore(Json::arrayValue);
            for (auto &na : g->getclosename()) {
                line["line"] = na.second;
                name["name"] = na.first;
                Json::Value temp(Json::arrayValue);
                temp.append(line);
                temp.append(name);
                namestore.append(temp);
            }
            eachelement["similarname"] = namestore;
            root.append(eachelement);
        }
        string json_file = writer.write(root);
        outs() << json_file << "\n";
        std::ofstream out_file("/Users/mypc/Desktop/output.json");
        out_file << json_file;
        out_file.close();

    }
    else {
        errs() << "请输入正确的参数!\n";
    }

    return 0;
}
bool similarnameAnalysis(string &a, string &b) {
    //这个函数的作用：分析两个变量名a, b有没有相同的前缀
    size_t alen = a.size();
    size_t blen = b.size();
    size_t minlen;
    if (alen > blen) {
        if (a.find(b) != string::npos) {
            return true;
        }
        minlen = blen;
        size_t i = 0;
        for (i = 0; i < minlen; i++) {
            if (a[i] != b[i])
                break;
        }
        if (i == 0)
            return false;
        string prefix = a.substr(0, i);
        char lastchar = prefix.back();
        if (lastchar == '[' || lastchar == '.')
            return true;
        double per = i * 1.0 / minlen;
        if (lastchar == '_' && per > 0.2)
            return true;

    }
    else if (alen < blen) {
        if (b.find(a) != string::npos) {
            return true;
        }
        minlen = alen;
        size_t i = 0;
        for (i = 0; i < minlen; i++) {
            if (a[i] != b[i])
                break;
        }
        if (i == 0)
            return false;
        string prefix = a.substr(0, i);
        char lastchar = prefix.back();
        if (lastchar == '[' || lastchar == '.')
            return true;
        double per = i * 1.0 / alen;
        if (lastchar == '_' && per > 0.5)
            return true;
    }
    else {
        if (a == b) {
            return true;
        }
        minlen = alen;
        size_t i = 0;
        for (i = 0; i < minlen; i++) {
            if (a[i] != b[i])
                break;
        }
        if (i == 0)
            return false;
        string prefix = a.substr(0, i);
        char lastchar = prefix.back();
        if (lastchar == '[' || lastchar == '.')
            return true;
        double per = i * 1.0 / blen;
        if (lastchar == '_' && per > 0.5)
            return true;
        char nextchar = a[i + 1];
        if (std::isalpha(lastchar) && std::isalpha(nextchar) && (isupper(lastchar) && islower(nextchar) || isupper(nextchar) && islower(lastchar)))
            return true;
    }
    return false;
}
void similarnameAnalysis() {
    for (auto &funvar : funhasvar) {
        auto vars = funvar.second;
        int nums = funvar.second.size();
        for (int i = 0; i < nums; i++) {
            auto *g = vars[i];
            for (int j = 0; j < nums; j++) {
                if (j == i)
                    continue;
                auto *gl = vars[j];
                string gname = g->getName();
                string glname = gl->getName();
                if (gname != glname && similarnameAnalysis(gname, glname)) {
                    g->addclosename({glname, gl->getRWLoc().first->getLine()});
                }
            }
        }
    }
    for (auto &g : globalvarlist) {
        outs() << "similar name START-----------------\n";
        outs() << "   " << g->getName() << " " << g->getRWLoc().first->getLine() << " is similar with\n";
        for (auto &p : g->getclosename()) {
            outs() << "     " << p.first << " " << " in line : " << p.second << "\n";
        }
        outs() << "similar name END-------------------\n";
    }
//    //使用收集到的变量进行相似命名分析
//    //把所有常规变量名放入v1中
//    vector<string> namelist;
//    set<string> nameset;
//    for (auto *va : globalvarlist) {
//        if (va->getflag() == 1) {
//            string name = va->getName();
//            if (nameset.find(name) == nameset.end()) {
//                namelist.push_back(name);
//                nameset.insert(name);
//            }
//
//        }
//    }
//    //不调用system("python3 /Users/mypc/Downloads/pythonProject2/NAMEPluginpost.py");
//    //遍历两遍这个list，第一遍先发现所有可能的前缀，第二遍根据前缀分组
//    set<string> substrings;//存放所有可能的前缀
//    for (auto &s : namelist) {
//        //            errs() << s << "\n";
//        if (s.find('_') != string::npos) {
//            //如果有"_"，就把每个"_"左侧的全部字符串作为可能的前缀
//            size_t pos = s.find('_');
//            // 处理第一个子字符串
//            substrings.insert(s.substr(0, pos));
//            // 查找下划线位置并提取子字符串
//            while ((pos = s.find('_', pos + 1)) != string::npos) {
//                substrings.insert(s.substr(0, pos));
//            }
//        }
//        // 查找第一个大写字母出现的位置
//        size_t uppos = s.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
//        // 查找第一个小写字母出现的位置
//        size_t lowpos = s.find_first_of("abcdefghijklmnopqrstuvwxyz");
//
//        int countup = 0;//统计大写字母数量
//        int countlower = 0;//统计小写字母数量
//        for (size_t i = 0; i < s.size(); i++) {
//            if (isupper(s[i])) {
//                countup++;
//            }
//            else if (islower(s[i])) {
//                countlower++;
//            }
//        }
//        if (countlower > countup) {
//            //大写字母没出现过就跳过
//            if (uppos != string::npos) {
//                if (lowpos != string::npos) {
//                    //有大写字母而且也有小写字母
//                    substrings.insert(s.substr(0, uppos));
//                }
//            }
//        }
//        else {
//            if (uppos != string::npos && lowpos != string::npos) {
//                substrings.insert(s.substr(0, lowpos));
//            }
//        }
//
//        //查找第一个数字出现的地方
//        size_t numpos = s.find_first_of("0123456789");
//        if (numpos != string::npos) {
//            substrings.insert(s.substr(0, numpos));
//        }
//    }
//    //前缀到变量名的映射
//    map<string, set<string>> pretoname;
//    //变量名到前缀的映射
//    map<string, set<string>> nametopre;
//    //变量名到变量名的映射
//    map<string, set<string>> nametoname;
//    //as asF asB
//    for (auto &s : namelist) {
//        for (auto &pre : substrings) {
//            if (s.find(pre) == 0) {
//                //如果变量以这个前缀开头
//                pretoname[pre].insert(s);
//                nametopre[s].insert(pre);
//            }
//        }
//    }
//
//    for (auto &n : nametopre) {
//        for (auto &m : n.second) {
//            for (auto &b : pretoname[m]) {
//                if (b != n.first) {
//                    nametoname[n.first].insert(b);
//                }
//            }
//        }
//    }
//    for (auto &p : nametoname) {
//        outs().flush();
//        outs() << "与变量名" << p.first << "相似的名字有: ";
//        for (auto &q : p.second) {
//            outs() << q << " ";
//        }
//        outs() << "\n";
//    }
}
void brotherAnalysis() {
    for (auto *va : globalvarlist) {
        if (va->getflag() != 0) {
            //它不是一个普通变量，而是一个数组或这是结构体
            string name = va->getName();
            size_t index1 = name.find_first_of('[');//'['在这个变量中的位置
            size_t index2 = name.find_first_of('.');//'.'在这个变量中的位置
            size_t left = 0;//截取的位置，把b[4]截成b
            if (index1 != string::npos) {
                if (index2 != string::npos) {
                    left = min(index1, index2);
                }
                else {
                    left = index1;
                }
            }
            else {
                left = index2;
            }
            string basename = name.substr(0, left);
            if (brothers.count(basename) != 0) {
                brothers[basename].insert(name);
            }
            else {
                set<string> namelist;
                namelist.insert(name);
                brothers.insert(make_pair(basename, namelist));
            }
        }
    }
    for (auto &b : brothers) {
        outs() << b.first << " ";
        for (auto &bo : b.second) {
            outs() << bo << " ";
        }
        outs() << "\n";
    }
}
void loopanalysis(std::unique_ptr<llvm::Module> M) {

}
void controldep(llvmdg::SystemDependenceGraph &sdg, GlobalVarInfo *g) {
    dg::sdg::DGNode *node = g->getDGNode();
    if (!node)
        return;
    map<dg::sdg::DGNode*, int> visited;//visited[node] = 1是访问过的
    outs() << g->getName() << " " << g->getRWLoc().first->getLine() << ": " << "\n";
    set<dg::sdg::DGNode*> tofindcomesfrom;
    function<void(dg::sdg::DGNode *dg_node)> f = [&] (dg::sdg::DGNode *dg_node) {
        if (visited[dg_node] == 1)
            return;
        visited[dg_node] = 1;
        auto *dg_block = dg_node->getBBlock();
        //遍历它依赖于哪些节点
        for (auto *controldepnode : dg_block->control_deps()) {
            if (controldepnode == dg_node) {
                continue;
                //  与自己相同就跳过
            }
            auto thisid = dg_node->getID();
            auto thatid = dg::sdg::DGBBlock::get(controldepnode)->back()->getID();
            if (thatid > thisid)
                continue;
//            outs() << dg_node->getID() << "\n";
//            outs() << dg::sdg::DGBBlock::get(controldepnode)->back()->getID() << "\n";
            if (auto *conblock = dg::sdg::DGBBlock::get(controldepnode)) {
                if (auto *value = sdg.getValue(conblock->back())) {
                    if (auto *inst = dyn_cast<Instruction>(value)) {
                        if (auto *brinst = dyn_cast<BranchInst>(inst)) {
                            if (!brinst->isConditional())
                                continue;
                            Value *curnodevalue = sdg.getValue(dg_node);
                            Instruction *curnodeinst = dyn_cast<Instruction>(curnodevalue);
                            BasicBlock *block = nullptr;
                            if (curnodeinst->getParent() == brinst->getSuccessor(0)) {
                                block = brinst->getSuccessor(0);
                            }
                            else if (curnodeinst->getParent() == brinst->getSuccessor(1)) {
                                block = brinst->getSuccessor(1);
                            }
                            string labelname0;
                            if (block && block->hasName()) {
                                labelname0 = block->getName().str();
                                if (auto it = labelname0.find(".end")) {
                                    if (it != std::string::npos) {
                                        //通往end分支，已经不属于这个控制范围了
                                        return;
                                    }
                                }
                            }
                            auto *v = brinst->getOperand(0);
//                            outs() << *v << "\n";
                            tofindcomesfrom.insert(conblock->back());
                        }
                        else if (auto *switchinst = dyn_cast<SwitchInst>(inst)) {
                            Value *v = switchinst->getCondition();
//                            outs() << *v << "\n";
                            tofindcomesfrom.insert(conblock->back());

                        }
                    }

                }
                f(conblock->back());
            }

        }
    };
    f(node);
    visited.clear();
    function<void(dg::sdg::DGNode * dn_node1, dg::sdg::DGNode *dg_node2, int depth)> datacomesfrom;
    datacomesfrom = [&](dg::sdg::DGNode *dg_node1, dg::sdg::DGNode *dg_node2, int depth) {
        if (visited.find(dg_node1) != visited.end()) {
            return;
        }
        visited[dg_node1] = 1;
        int thisid = dg_node1->getID();
        int thatid = 0;
        for (auto *usedep : dg_node1->users()) {
            if (auto *v = sdg.getValue(usedep)) {
                thatid = usedep->getID();
                if (thatid > thisid)
                    continue;
                datacomesfrom(dg::sdg::DGNode::get(usedep), dg_node1, depth);
            }
        }
        for (auto *memdep : dg_node1->memdep()) {
            if (auto *v = sdg.getValue(memdep)) {
                thatid = memdep->getID();
                if (thatid > thisid)
                    continue;
                datacomesfrom(dg::sdg::DGNode::get(memdep), dg_node1, depth);
            }
        }
        if (Value *v = sdg.getValue(dg_node1)) {
            if (Instruction *inst = dyn_cast<Instruction>(v)) {
                if (inst && isa<LoadInst>(inst)) {
                    LoadInst *LI = dyn_cast<LoadInst>(inst);
                    Value *from = LI->getOperand(0);
                    if (from && isa<GlobalValue>(from)) {
                        if (from->hasName()) {
                            auto Loc = LI->getDebugLoc();
                            auto line = Loc->getLine();
//                            outs() << from->getName() << " from "
//                                   << inst->getDebugLoc()->getLine()
//                                   << "\n";
                            g->adddependencevarC(make_tuple(from->getName().str(), inst->getDebugLoc()->getLine(), depth));
                        } else
//                            outs() << *from << "\n";
                        ;
                    }

                } else if (inst && isa<AllocaInst>(inst)) {
                    AllocaInst *AI = dyn_cast<AllocaInst>(inst);
                    Value *from = dyn_cast<Value>(AI);
                    if (from->hasName()) {
                        Value *last = sdg.getValue(dg_node2);
                        int line = 0;
                        if (Instruction *lastinst = dyn_cast<Instruction>(last)) {
                            line = lastinst->getDebugLoc()->getLine();
                        }
//                        outs() << from->getName() << " from " << line << "\n";
                        g->adddependencevarC(make_tuple(from->getName().str(), line, depth));
                    } else {
//                        outs() << *from << "\n";
;
                    }

                }
                else if(inst && isa<GEPOperator>(inst)) {
                    GEPOperator *gepo = dyn_cast<GEPOperator>(inst);
                    Value *from = gepo->getOperand(0);
                    if (isa<GlobalValue>(from)) {
                        if (from->hasName()) {
                            auto line = inst->getDebugLoc()->getLine();
                            //不从globalist里找是因为有可能是带变量下标的数组，目前没有收集
//                            outs() << from->getName() << "is from " << line << "\n";
                            g->adddependencevarC(make_tuple(from->getName().str(), line, depth));
                        }

                    }

                }
                else if (inst && isa<CallInst>(inst)) {
                    CallInst *Cinst = dyn_cast<CallInst>(inst);
                    int nums = Cinst->getNumArgOperands();
                    for (int i = 0; i < nums; i++) {
                        Value *from = Cinst->getArgOperand(i);
                        if (from->hasName()) {
                            auto line = Cinst->getDebugLoc()->getLine();
//                            outs() << from->getName() << " is from " << line << "\n";
                            g->adddependencevarC(make_tuple(from->getName().str(), line, depth));
                        }
                    }
                }
            }
        }
    };
    int depth = tofindcomesfrom.size();
    for (auto *dgn : tofindcomesfrom) {
        datacomesfrom(dgn, nullptr, depth--);
    }
}
void memdep(llvmdg::SystemDependenceGraph &sdg, GlobalVarInfo *g) {
    dg::sdg::DGNode *node = g->getDGNode();
    if (!node)
        return ;
    map<dg::sdg::DGNode*, int> visited;//visited[node] = 1是访问过的
    outs() << g->getName() << " " << g->getRWLoc().first->getLine() << ": " << "\n";
    function<void(dg::sdg::DGNode *dg_node, dg::sdg::DGNode *dg_node_pre, int depth)> f = [&] (dg::sdg::DGNode *dg_node, dg::sdg::DGNode *dg_node_pre, int depth) {
        if (!dg_node)
            return;
        if (visited.find(dg_node) != visited.end()) {
            return;
        }
        visited[dg_node] = 1;
        if (dg_node == dg_node_pre)
            return;
        Value *vnode = sdg.getValue(dg_node);
        if (!vnode)
            return;
        Instruction *I = dyn_cast<Instruction> (vnode);
        outs() << "Now we are dealing with the Instruction " << *I << "\n";
        if (auto *GEPO = dyn_cast<GEPOperator>(I)) {
            //如果是数组，首先获得它的地址，最后才向地址里写入
            Value *v = dyn_cast<Value>(I);
            outs() << *v << "     1111\n";
            queue<dg::sdg::DGNode*> que;
            que.push(dg_node);
            dg::sdg::DGNode *lastnode;
            while (!que.empty()) {
                int num = que.size();
                for (int i = 0; i < num; i++) {
                    auto top = que.front();
                    que.pop();
                    for (auto *Use : top->uses()) {
                        if (auto *v = sdg.getValue(Use)) {
                            dg::sdg::DGNode *next = dg::sdg::DGNode::get(Use);
                            que.push(next);
                            lastnode = next;
                        }
                    }
                }
            }
            outs() << *sdg.getValue(lastnode) << "     22222\n";
            for (auto *pre : lastnode->users()) {
                Value *p = sdg.getValue(pre);
                if (!p) {
                    return;
                }
                if (Instruction *I = dyn_cast<Instruction>(p)) {
                    outs() << *I << "    33333\n";

                    if (I && isa<LoadInst>(I)) {
                        Value *op = I->getOperand(0);
                        outs() << *op << "    444444\n";
                        if (op && isa<GlobalValue>(op)) {
                            outs() << op->getName() << " from " << I->getDebugLoc()->getLine() << "\n";
                        }
                        else {
                            string name = op->getName().str();
                            if (auto it = name.find(".addr")) {
                                if (it != name.size()) {
                                    name = name.substr(0, it);
                                }
                            }
                            outs() << name << "    55555\n";
                            auto nodeid = lastnode->getID();
                            auto beforenode = dg::sdg::DGNode::get(pre);
                            unsigned id = 0;
                            dg::sdg::DGNode *closestnode;
                            for (auto closest : beforenode->memdep()) {
                                auto *node = dg::sdg::DGNode::get(closest);
                                unsigned int thisid = closestnode->getID();
                                if (id < thisid && thisid < nodeid) {
                                    id = thisid;
                                    closestnode = node;
                                }
                            }
                            Value *value = sdg.getValue(closestnode);
                            if (value && value->hasName())
                                outs() << value->getName().str() << "     4444444\n";
                                ;
                        }
                    }
                    else if (I && isa<AllocaInst>(I)) {

                    }
                }
            }

            return;
        }

        int thisid = dg_node->getID();
        int thatid = 0;
        for (auto *usedep : dg_node->users()) {
            if (auto *v = sdg.getValue(usedep)) {
                outs() << *v << "\n";
                Instruction *instruction = dyn_cast<Instruction>(v);
                outs() << *instruction << "\n";
                thatid = usedep->getID();
                if (thatid > thisid)
                    continue;
                f(dg::sdg::DGNode::get(usedep), dg_node, depth + 1);
            }
        }
        for (auto *memdep : dg_node->memdep()) {
            if (auto *v = sdg.getValue(memdep)) {
                outs() << *v << "\n";
                Instruction *instruction = dyn_cast<Instruction>(v);
                outs() << *instruction << "\n";
                thatid = memdep->getID();
                if (thatid > thisid)
                    continue;
                f(dg::sdg::DGNode::get(memdep), dg_node, depth + 1);
            }
        }

        if (Value *v = sdg.getValue(dg_node)) {
            if (Instruction *inst = dyn_cast<Instruction>(v)) {
                if (isa<LoadInst>(inst)) {
                    LoadInst *LI = dyn_cast<LoadInst>(inst);
                    Value *from = LI->getOperand(0);
                    if (from && isa<GlobalValue>(from)) {//找到一个全局变量之后，还不能停下，因为有a = b, c = a的情况
                        if (depth == 0) {
                            f(g->getLastWNode(), nullptr, depth + 1);
                        }
                        else if (depth != 0 && from->hasName()) {
                            outs() << from->getName() << " from "
                                   << inst->getDebugLoc()->getLine() << "\n";
                            g->adddependencevarD(make_tuple(from->getName().str(), inst->getDebugLoc()->getLine(), depth));
                            GlobalVarInfo *g2 = *InstrtoGlobl[inst].begin(); //Load指令，只有一个全局变量
                            f(g2->getLastWNode(), nullptr, depth + 1);
                        } else
                            ;
                    }

                } else if (isa<AllocaInst>(inst)) {
                    AllocaInst *AI = dyn_cast<AllocaInst>(inst);
                    Value *from = dyn_cast<Value>(AI);
                    if (from->hasName()) {
                        Value *last = sdg.getValue(dg_node_pre);
                        int line = 0;
                        if (Instruction *lastinst = dyn_cast<Instruction>(last)) {
                            line = lastinst->getDebugLoc()->getLine();
                        }
                        outs() << from->getName() << " is from " << line << "\n";
                        g->adddependencevarD(make_tuple(from->getName().str(), line, depth));
                    } else {
                        ;
                    }

                }
                else if (isa<CallInst>(inst) && depth != 0) {
                    CallInst *Cinst = dyn_cast<CallInst>(inst);
                    int nums = Cinst->getNumArgOperands();
                    for (int i = 0; i < nums; i++) {
                        Value *from = Cinst->getArgOperand(i);
                        if (from && from->hasName()) {
                            outs() << from->getName() << " is from " << Cinst->getDebugLoc()->getLine() << "\n";
                            g->adddependencevarD(make_tuple(from->getName().str(), Cinst->getDebugLoc()->getLine(), depth));
                        }
                        else {
                            ;
                        }
                    }
                }
            }
        }
    };
    f(node, nullptr, 0);
}
void findvarclose(int distance) {

    for (auto &p : funhasvar) {
        string curfunname = p.first;
        sort(p.second.begin(), p.second.end(), [] (GlobalVarInfo *a, GlobalVarInfo *b) {
            return *a < *b;
        });
        for (int i = 0; i < p.second.size(); i++) {
            GlobalVarInfo *gl = p.second[i];
            auto baseline = gl->getRWLoc().first->getLine();
            set<GlobalVarInfo*> s;
            Globalclose.insert({gl, s});
            for (int j = 0; j < p.second.size(); j++) {
                GlobalVarInfo *glo = p.second[j];
                if (gl == glo) {
                    continue;
                }
                auto line = glo->getRWLoc().first->getLine();
                if (line > baseline + distance) {
                    break;
                }
                if (line >= baseline - distance) {
                    Globalclose[gl].insert(glo);
                }
            }
        }
    }

    for (auto *g : globalvarlist) {
        outs() << g->getName() << " is close to :\n";
        for (auto *e : Globalclose[g]) {
            outs() << "     " << e->getName() << " in line " << e->getRWLoc().first->getLine() << "\n";
        }
    }
}
