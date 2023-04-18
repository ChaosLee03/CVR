#include "llvm/Pass.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/MemoryDependenceAnalysis.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Debug.h"
#include <set>
#include <map>

using namespace llvm;
using namespace std;

/*
 TODO: 目前需要处理二维数组，结构体数组，结构体里有数组，结构体里有结构体
 */

namespace {

//变量类型:常规变量，数组，结构体，指针
//数组有可能下标为变量、指针、表达式
//结构体有可能还有个结构体成员
//先收集所有全局变量的信息，
class GlobalVarInfo{
    //描述位置和读写, DILocation可以记录行号和列号，string记录读还是写，读为'R'，写为'W'
    map<DILocation*, string> linesRW;
    //描述该变量，因为GlobalVariable不能被继承，所以用作成员变量
    GlobalVariable *GV;
    //如果是数组或结构体的话，自定义一个更精确的名称覆盖GV中的名称，
    string name;
    //判断是否是普通变量，普通变量使用相似命名分析，结构体和数组使用兄弟元素分析
    //flag = 1表示是普通变量
    int flag;
public:
    //构造函数
    GlobalVarInfo(GlobalVariable *GV) : GV(GV) {}
    //获取位置和读写
    map<DILocation*, string> getRWLoc() {return linesRW;}
    //添加一条位置和读写的映射
    void addlines(DILocation *loc, string rw) {linesRW[loc] = rw;}
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
};
//相似命名分析
void similarnameAnalysis(vector<GlobalVarInfo*> v);
//依赖分析
void dependenceAnalysis();
//兄弟元素分析
void brotherAnalysis(vector<GlobalVarInfo*> v);
//访问距离分析
void distanceAnalysis(vector<GlobalVarInfo*> v);
//连续IO分析
void IOAnalysis(vector<GlobalVarInfo*> v);
//遍历所有指令，寻找全局变量
//定义一个 ModulePass
struct Mypass : public ModulePass {
    static char ID;
    Mypass() : ModulePass(ID) {}
    //存储所有的全局变量
    vector<GlobalVarInfo*> globalvarlist;
    // 实现 runOnModule 方法
    bool runOnModule(Module &M) override {
        //遍历所有全局变量
        for (auto &G : M.globals()) {
            errs() << "global variable name: " << G.getName() << "\n";
            errs() << "global variable type: " << *G.getType() << "\n";
            //获取其位置和读写
            //遍历使用这个变量的所有指令
            for (auto &U : G.uses()) {
                if (auto *I = dyn_cast<Instruction>(U.getUser())) {
                    //如果变量是个常规变量或指针
                    if (auto &L = I->getDebugLoc()) {//这里的&L是引用
                        // 记录下指令位置和读写类型
                        // 这里需要根据具体情况进行分析，如何判断读写类型
                        string rw;
                        if (isa<StoreInst>(I)) {
                            rw = 'W';
                        }
                        else if (isa<LoadInst>(I)) {
                            rw = 'R';
                        }
                        //创建VarInfo对象
                        GlobalVarInfo *g = new GlobalVarInfo(&G);
                        DILocation *loc = L;
                        g->addlines(loc, rw);
                        //放入globalvarlist中
                        globalvarlist.push_back(g);
                    }
                }
                if (auto *GEPOp = dyn_cast<GEPOperator>(U.getUser())) {
                    //如果变量是个结构体/数组
                    //先获取getelmentptr所在的语句是load还是store
                    for (auto &U2 : GEPOp->uses()) {
                        string rw;
                        Instruction *I;
                        if (I = dyn_cast<Instruction>(U2.getUser())) {
                        // 如果指令是 StoreInst，那么就是写，否则是读
                            if (auto *Store = dyn_cast<StoreInst>(I)) {
                                //写结构体/数组
                                rw += 'W';
                            }
                            if (auto *Load = dyn_cast<LoadInst>(I)) {
                                //读结构体/数组
                                rw += 'R';
                            }
                        }
                        //判断是数组还是结构体，两者需要区别开来
                        if (auto *PT = dyn_cast<PointerType>(G.getType())) {
                            string fullName;
                            if (auto *ST = dyn_cast<StructType>(PT->getElementType())) {
                                //处理结构体
                                string structVarName = G.getName().str();
                                unsigned fieldIdx = cast<ConstantInt>(GEPOp->getOperand(2))->getZExtValue();
                                errs() << "偏移为" << fieldIdx << "\n";
                                // 获取结构体成员名称
                                std::string fieldName;
                                //FIXME: 结构体里面是结构体或是数组还没有考虑到
                                //NOTICE: 结构体里的变量在IR上没有名称，必须在调试信息里找
                                
                                //遍历metadata node,找结构体里的普通变量的名称
                                //这个遍历方法是试出来的,在metadata node里一个个找，无任何遍历方法(如DFS, BFS)，不太清楚为什么getOperand()里面的数字为什么是这样
                                //对于结构体里有个成员变量是数组的情况，该方法无法获取下标,只能获取数组名
                                //对于结构体里有结构体的情况，该方法无法获得访问的成员结构体里的成员，只能获得成员结构体名
                                if (auto * mn = dyn_cast<MDNode>(G.getMetadata("dbg")->getOperand(0))) {
                                    if (auto *mn2 = dyn_cast<MDNode>(mn->getOperand(3))) {
                                        if (auto *mn3 = dyn_cast<MDNode>(mn2->getOperand(4))) {
                                            if (auto *mn4 = dyn_cast<MDNode>(mn3->getOperand(fieldIdx))) {
                                                if (MDString *mds = dyn_cast<MDString>(mn4->getOperand(2))) {
                                                    fieldName = mds->getString().str();
                                                }
                                            }
                                        }
                                    }
                                }
                                
                                fullName = structVarName + "." + fieldName;
                                errs() << fullName << "\n";
                            }
                            if (auto *AT = dyn_cast<ArrayType>(PT->getElementType())) {
                                //处理数组
                                string arrayVarName = G.getName().str();
                                fullName = arrayVarName;
                                //FIXME: 数组内容是结构体或二维数组还没有考虑到
                                //数组里是普通变量的情况
                                if (auto *self = GEPOp->getOperand(0)) {
                                    //获得指向结构体的指针
                                    if (auto *ptr = dyn_cast<PointerType>(self->getType())) {
                                        if (auto *strptr = dyn_cast<StructType>(ptr->getElementType())) {
                                            //如果是结构体，即结构体数组的情况
                                            
                                        }
                                        else if (auto *arrptr = dyn_cast<ArrayType>(ptr->getElementType())) {
                                            //如果是数组，即二维数组的情况
                                            
                                        }
                                        else {
                                            //如果不是数组也不是结构体
                                            if (auto *Idx = GEPOp->getOperand(2)) {
                                                if (auto *CIdx = dyn_cast<ConstantInt>(Idx)) {
                                                    unsigned idx = CIdx->getZExtValue();
                                                    fullName += "[" + to_string(idx) + "]";
                                                }
                                                else {
                                                    /*FIXME: 这里是数组下标不是常量的情况，在源代码中，它可能是变量，可能是表达式
                                                      FIXME: 现在先这样
                                                     */
                                                    fullName += "[i]";
                                                }
                                            }
                                        }
                                    }
                                }
                                
                                
                            }
                            GlobalVarInfo *g = new GlobalVarInfo(&G);
                            DILocation *loc = I->getDebugLoc();
                            g->setname(fullName);
                            g->addlines(loc, rw);
                            g->setflag(0);//0表示数组或结构体
                            globalvarlist.push_back(g);
                        }

                    }
                }
            }
        }
        //遍历globalvarlist
        //FIXME: 现在列号还是有问题，不能准确定位
        for (auto g : globalvarlist) {
            if (g->getflag() == 1)
                errs() << "变量名： " << g->getVariable()->getName() << "\n";
            else {
                errs() << "变量名： " << g->getName() << "\n";
            }
            errs() << "变量类型: " << *g->getVariable()->getType() << "\n";
            for (auto p : g->getRWLoc()) {
                errs() << "所在行号" << p.first->getLine() << " 所在列号" << p.first->getColumn() << "\n";
                errs() << "读写类型: " << p.second << "\n";
            }
        }
        similarnameAnalysis(globalvarlist);
        return false;
    }
    void similarnameAnalysis(vector<GlobalVarInfo*> v) {
        //使用收集到的变量进行相似命名分析
        //把所有常规变量名放入v1中
        vector<string> namelist;
        for (auto * va : v) {
            if (va->getflag() == 1) {
                namelist.push_back(va->getVariable()->getName().str());
            }
        }
        //调用system("python3 /Users/mypc/Downloads/pythonProject2/NAMEPluginpost.py");
    }
    
    void brotherAnalysis(vector<GlobalVarInfo*> v) {
        
    }
    
    void distanceAnalysis(vector<GlobalVarInfo*> v) {
        
    }
    
    void IOAnalysis(vector<GlobalVarInfo*> v) {
        
    }
};
}
char Mypass::ID = 0;
static RegisterPass<Mypass> X ("Mypass", "This is for generating  the CFG");
