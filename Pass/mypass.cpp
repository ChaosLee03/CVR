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
#include "llvm/Analysis/DependenceGraphBuilder.h"
#include <set>
#include <map>
#include <fstream>
#include "llvm/Support/JSON.h"
//#include "json/json.h"
//JSON.h貌似不能编辑json，只能解析
//看一下怎么装jsoncpp

using namespace llvm;
using namespace std;

/*
 
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
    bool operator<(const GlobalVarInfo& other) const{
        auto it = linesRW.begin();
        auto itt = other.linesRW.begin();
        int thisline = it->first->getLine();
        int thatline = itt->first->getLine();
        if (thisline != thatline) {
            return thisline < thatline;
        } else {
            return name < other.name;
        }
    }
};
//有个问题，从json读入的变量无法转为GlobalVarInfo的形式
class MyVar {
public:
    std::string name;
    unsigned loc;
    int id;
    MyVar() {}
    MyVar(std::string s, unsigned l, int i) : name(s), loc(l) , id(i) {}
    bool operator<(const MyVar& other) const{
        if (loc != other.loc) {
            return loc < other.loc;
        } else {
            return name < other.name;
        }
    }
};
//读取json文件,获取共享访问点
void readjsonfile();
//将收集到的变量存放起来
void writeglobalvar();
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
                        g->setname(G.getName().str());
                        DILocation *loc = L;
                        g->addlines(loc, rw);
                        //放入globalvarlist中
                        //FIXME: 如果是num++这种，会把num放入两次，这两次的行号、列号、名称全部都相同,需要设置一个set，去掉完全相同的情况
                        globalvarlist.push_back(g);
                    }
                }
                if (auto *GEPOp = dyn_cast<GEPOperator>(U.getUser())) {
                    //直接判断下标是不是变量，是直接跳过
                    if (GEPOp->getNumOperands() <= 3) {
                        
                        if (auto *f = dyn_cast<ConstantInt>(GEPOp->getOperand(2))) {
                            
                        }
                        else {
                            errs() << "跳过了\n";
                            continue;
                        }
                        
                    }
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
                            }
                            if (auto *AT = dyn_cast<ArrayType>(PT->getElementType())) {
                                //处理数组
                                string arrayVarName = G.getName().str();
                                fullName = arrayVarName;
                                
                                if (auto *self = GEPOp->getOperand(0)) {
                                    Type *ElementTypeOfAT = AT->getElementType();
                                    errs() << *ElementTypeOfAT << " ";
                                    //设置一个标记变量，0代表普通数组，1代表结构体数组,2代表二维数组
                                    int ff = 0;
                                    //plus代表名字上补上的部分，eg a[3][4]中的[4], b[4].a中的.a
                                    string plusname = "";
                                    if (StructType *ST = dyn_cast<StructType>(ElementTypeOfAT)) {
                                        //进到这里说明是结构体数组
                                        // TODO: 处理结构体数组
                                        ff = 1;
//                                        errs() << "is a struct\n";
                                        //获得结构体内的偏移量
                                        /*
                                         for (int i = 0; i < 12; i++) {
                                             b[i].f = i;
                                         }这样的也处理不了,偏移量的获取是先获取下标再偏移的，而下标是常量的时候是直接获得下标的,getElementptr的参数数量是不一样的
                                         */
//                                        if (GEPOp->getNumOperands() <= 3) {
//                                            continue;
//                                        }直接在auto *GEPOp = dyn_cast<GEPOperator>(U.getUser()后判断了
                                        unsigned fieldIdx = cast<ConstantInt>(GEPOp->getOperand(3))->getZExtValue();
//                                        errs() << fieldIdx << " = fieldIdx\n";
                                        if (auto *mn = dyn_cast<MDNode>(G.getMetadata("dbg")->getOperand(0))) {
                                            mn->getOperand(3)->print(errs());
                                            //4 -> elements 3 -> type 2 -> file 1 -> name
//                                            errs() << "\n";
                                            if (auto *mn2 = dyn_cast<MDNode>(mn->getOperand(3))) {
                                                mn2->getOperand(3)->print(errs());
//                                                errs() << "\n";
                                                if (auto *mn3 = dyn_cast<MDNode>(mn2->getOperand(3))) {
                                                    mn3->getOperand(4)->print(errs());
//                                                    errs() << "\n";
                                                    if (auto *mn4 = dyn_cast<MDNode>(mn3->getOperand(4))) {
                                                        if (auto *mn5 = dyn_cast<MDNode>(mn4->getOperand(fieldIdx))) {
                                                            if (MDString *mds = dyn_cast<MDString>(mn5->getOperand(2))) {
//                                                                errs() << mds->getString().str();
                                                                plusname = "." + mds->getString().str();
                                                            }
                                                        }
                                                    }
                                                }
                                            }
//                                            errs() << "\n";
                                        }
                                    }
                                    else if (ArrayType *AT2 = dyn_cast<ArrayType>(ElementTypeOfAT)) {
                                        // TODO: 处理二维数组
                                        // 只能处理下标是常量的二维数组，下标为变量比如for (int i = 0; i < 5; i++) b[i][0] = i 会出错，
                                        // 因为for循环里不是直接在b上取地址，而是先计算出一个变量，再getelementptr这个变量的偏移
                                        // 与下标为常量时的getelementptr操作不同
                                        // 遇到这种情况就跳过
                                        ff = 2;
//                                        if (GEPOp->getNumOperands() <= 3) {
//                                            continue;
//                                        }在auto *GEPOp = dyn_cast<GEPOperator>(U.getUser()后跳过
                                        if (auto *sub2 = GEPOp->getOperand(3)) {
                                            if (auto *CIdx = dyn_cast<ConstantInt>(sub2)) {
                                                unsigned idx = CIdx->getZExtValue();
                                                plusname = "[" + to_string(idx) + "]";
                                            }
                                            else {//实际基本执行不到
                                                plusname = "[unknown]";
                                            }
                                        }
                                    }
                                    if (auto *Idx = GEPOp->getOperand(2)) {
                                        //这里的Idx是数组第一个维度的下标
                                        if (auto *CIdx = dyn_cast<ConstantInt>(Idx)) {
                                            unsigned idx = CIdx->getZExtValue();
                                            fullName += "[" + to_string(idx) + "]";
                                        }
                                        else {
                                            //下标不是一个ConstantInt，这种情况其实已经跳过了
                                            fullName += "[i]";
                                        }
                                    }
                                    fullName += plusname;
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
//        readjsonfile();
        similarnameAnalysis(globalvarlist);
        return false;
    }
    void readjsonfile() {
        //读json格式
        //获取共享访问点
        //把收集到的变量和从json读入的变量汇总
    }
    void writeglobalvar() {
        //写json格式
        //{
        //    "global_var": [
        //     {
        //       "name": XXX,
        //       "line": XXX,
        //       "RW": R/W
        //     }...
        //    ]
        //}
        
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
    void dependenceAnalysis() {
        
    }
    
    void IOAnalysis(vector<GlobalVarInfo*> v) {
        
    }
};
}
char Mypass::ID = 0;
static RegisterPass<Mypass> X ("Mypass", "Mypass");//第一个参数代表在命令行里加上 -Mypass 可以调用本pass，第二个代表描述本pass的信息
