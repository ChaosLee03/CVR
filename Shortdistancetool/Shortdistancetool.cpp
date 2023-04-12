#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/Refactoring/RecursiveSymbolVisitor.h"
#include "clang/Sema/Sema.h"
#include "llvm/Support/raw_ostream.h"
#include "clang/AST/Stmt.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/DenseMap.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>

using namespace clang::tooling;
using namespace clang;

// Apply a custom category to all command-line options so that they are the
// only ones displayed.
static llvm::cl::OptionCategory MyToolCategory("my-tool options");

// CommonOptionsParser declares HelpMessage with a description of the common
// command-line options related to the compilation database and input files.
// It's nice to have this help message in all tools.
static llvm::cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

// A help message for this specific tool can be added afterwards.
static llvm::cl::extrahelp MoreHelp("\nMore help text...\n");

const std::string VarStrToIdFileName = "id_to_str.txt";
int maxId = 1;
std::map<std::string, int> varsStrToId;
std::map<int, std::string> idToVarsStr;
std::set<std::string> visitedvarStr;
std::set<std::string> visitedFuncStr;
std::string CollectedVarInfoFileName = "collected_vars.txt";
std::string CollectedVarInfoFileNameId = "collected_varsid.txt";
std::string inDataMiningFileName = "Miningin";
std::string MiningResult = "MiningResult.txt";
std::string MiningResultStr = "Str" + MiningResult;
int distance = 7;
int halfdistance = distance / 2;
int mysup = 2;
int togethernum = 0; //总项数，代表了输入数据挖掘的所有项的数量 SUP%就是想要的数量/togethernum

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
std::set<MyVar> FuncVars;

void FilesClear() {
    std::ofstream of1("/Users/mypc/Downloads/PS/" + CollectedVarInfoFileName, std::ios_base::out);
    std::ofstream of2("/Users/mypc/Downloads/PS/" + CollectedVarInfoFileNameId, std::ios_base::out);
    std::ofstream of3("/Users/mypc/Downloads/PS/" + MiningResultStr);
    of1.close();
    of2.close();
    of3.close();
    //清空文件里的内容
}
void WriteTovarsStrToId(std::string name) {
    if (visitedvarStr.find(name) == visitedvarStr.end()) {
        idToVarsStr[maxId] = name;
        varsStrToId[name] = maxId++;
        visitedvarStr.insert(name);
    }
    //将每个变量进行编号，存入varsStrToId
}


void WriteSTR_IdToFile() {
    std::ofstream outfile("/Users/mypc/Downloads/PS/" + VarStrToIdFileName, std::ios::trunc);
    std::vector<std::pair<std::string, int>> v(varsStrToId.begin(), varsStrToId.end());
    std::sort(v.begin(), v.end(), [](const std::pair<std::string, int> &p1, const std::pair<std::string, int> &p2){
        return p1.second < p2.second;
    });
    if (outfile.is_open()) {
        for (auto a : v) {
            outfile << a.first << " " << a.second << std::endl;
        }
        outfile.close();
    }//把变量名映射id写入文件,便于检查
}

void WriteCollectedVarToFile(std::vector<std::set<std::string>> v, std::vector<std::set<int>> vid) {
    std::ofstream outfile("/Users/mypc/Downloads/PS/" + CollectedVarInfoFileName, std::ios::app);
    if (!outfile.is_open()) {
        llvm::outs() << "写文件" << CollectedVarInfoFileName << "失败！\n";
        return;
    }
    for (const auto &a : v) {
        for (const auto &b : a) {
            outfile << b << " ";
        }
        outfile << std::endl;
    }
    outfile.close();
    std::ofstream outfile2("/Users/mypc/Downloads/PS/" + CollectedVarInfoFileNameId, std::ios::app);
    if (!outfile2.is_open()) {
        llvm::outs() << "写文件" << CollectedVarInfoFileNameId << "失败！\n";
        return;
    }
    llvm::outs() << "写文件" << CollectedVarInfoFileNameId << "！\n";
    for (const auto &a : vid) {
        for (const auto &b : a) {
            outfile2 << b << " ";
        }
        outfile2 << std::endl;
        togethernum++;
    }
    outfile2.close();
    
}

void TransResultIdToStr() {
    std::ifstream infile("/Users/mypc/Downloads/PS/" + MiningResult);
    if (!infile.is_open()) {
        return;
    }
    std::ofstream outfile("/Users/mypc/Downloads/PS/" + MiningResultStr);
    if (!outfile.is_open()) {
        return;
    }
    std::string inputline;
    while (std::getline(infile, inputline)) {
        std::istringstream iss(inputline);
        std::string token;
        std::vector<std::string> tokens;
        int flag = 0;
        while (iss >> token) {
            if (token == "==>") {
                tokens.push_back(token);
                tokens.push_back(" ");
                continue;
            }
            if (token == "#SUP:") {
                flag = 1;
            }
            if (flag == 1) {
                tokens.push_back(token);
                tokens.push_back(" ");
            }
            else {
                tokens.push_back(idToVarsStr[std::stoi(token)]);
                tokens.push_back(" ");
            }
        }
        for (const auto &a : tokens) {
            outfile << a;
        }
        outfile << std::endl;
    }
    infile.close();
    outfile.close();
}

void Resultsort() {
    std::ifstream infile("/Users/mypc/Downloads/PS/" + MiningResultStr);
    std::vector<std::string> lines;
    std::string line;
    while(getline(infile, line)) {
        lines.push_back(line);
    }
    sort(lines.begin(), lines.end(),[] (const std::string &a, const std::string &b){
        int posa = a.find("CONF: ");
        int posb = b.find("CONF: ");
        double confa = std::stod(a.substr(posa + 6));
        double confb = std::stod(b.substr(posb + 6));
        return confa > confb;
    });
    std::ofstream outfile("/Users/mypc/Downloads/PS/" + MiningResultStr);
    for (const auto & l : lines) {
        outfile << l << std::endl;
    }
}

void removeresultitems() {
    std::ifstream infile("/Users/mypc/Downloads/PS/" + MiningResultStr);
    std::set<std::set<std::string>> store;
    std::string line;
    std::vector<std::string> mid;
    int linenums = 0;
    std::set<int> removelinenum;
    while(getline(infile, line)) {
        linenums++;
        mid.push_back(line);
        std::istringstream sstr(line);//用空格分割字符串
        std::string word;
        std::set<std::string> sto;
        while(sstr >> word) {
            if (word == "#SUP:")
                break;
            if (word == "==>")
                continue;
            sto.insert(word);
        }
        if (store.find(sto) != store.end()) {
            removelinenum.insert(linenums);
        }
        else {
            store.insert(sto);
        }
    }
    for (const auto &a : store) {
        for (const auto &b : a) {
            llvm::outs() << b << " ";
        }
        llvm::outs() << "\n";
    }
//    for (auto a : removelinenum) {    //显示删除了哪些行
//        llvm::outs() << a << " ";
//    }
//    llvm::outs() << "\n";


    
    std::ofstream outfile("/Users/mypc/Downloads/PS/" + MiningResultStr);
    if (!outfile.is_open()) {
        llvm::outs() << "未能写文件\n";
        return;
    }
    int num = 1;
    for (const auto &a : mid) {
        
        if (removelinenum.find(num) != removelinenum.end()) {
            num++;
            continue;
        }
        num++;
        std::istringstream sstr(a);
        std::string word;
        int sup = 0;
        int supflag = 0;
        double conf = 0;
        int conflag = 0;
        std::vector<std::string> towrite;
        outfile << "Variables: (";
        while(sstr >> word) {
            if (word == "#SUP:") {
                supflag = 1;
                continue;
            }
            if (supflag == 1) {
                supflag = 0;
                sup = std::stoi(word);
                continue;
            }
            if (word == "#CONF:") {
                conflag = 1;
                continue;
            }
            if (conflag == 1) {
                conflag = 0;
                conf = std::stod(word);
                continue;
            }
            towrite.push_back(word);
        }
        for (int i = 0; i < towrite.size(); i++) {
            if (towrite[i] == "==>") {
                continue;
            }
                
            outfile << towrite[i];
            if (i != towrite.size() - 1) {
                outfile << " ";
            }
        }
        outfile << ") SUP: " << sup << " CONF: " << conf << std::endl;

    }
    
}

void CallMining(double Sup, double Confi) {
    std::string cmd = "java -jar /Users/mypc/CLionProjects/try/spmf-1.7.jar run Closed_association_rules ";
    cmd = cmd + "/Users/mypc/Downloads/PS/" + CollectedVarInfoFileNameId;
    cmd = cmd + " /Users/mypc/Downloads/PS/" + MiningResult;
    cmd = cmd + " " + std::to_string(Sup / togethernum) + " " + std::to_string(Confi);
    llvm::outs() << cmd << "\n";
    llvm::outs() << "Sup = " << Sup << " togethernum = " << togethernum << "\n";
    system(cmd.c_str());
}

void WriteToFile() {
    WriteSTR_IdToFile();
}
void WriteToMining() {
    std::set<std::string> check;
    std::vector<MyVar> vec;
//    std::map<MyVar, std::set<MyVar>> nearvar;
    std::set<std::string> nearvar;
    std::vector<std::set<std::string>> resultvar;
    std::set<int> nearvarid;
    std::vector<std::set<int>> resultvarid;
    for (auto it = FuncVars.begin(); it != FuncVars.end(); it++) {
        vec.push_back(*it);
    }
//    llvm::outs() << "WriteToMining begins!\n";
//    for (auto a : vec) {
//        llvm::outs() << a.name << " " << a.id << "\n";
//    }
    int n = vec.size();
    if (n == 0) {
        return;
    }
    unsigned startline = vec[0].loc;
    unsigned endline = vec[n - 1].loc;
    llvm::outs() << "startline = " << startline << " endline = " << endline << "\n";
    for (unsigned curline = startline; curline <= endline; curline++) {
        nearvar.clear();
        nearvarid.clear();
        for (int i = 0; i < n; i++) {
            if (vec[i].loc > curline + halfdistance)
                break;
            if (vec[i].loc >= curline - halfdistance) {
                nearvar.insert(vec[i].name);
                nearvarid.insert(vec[i].id);
            }
        }
        if (nearvar.size()) {
            resultvar.push_back(nearvar);
            resultvarid.push_back(nearvarid);
        }
            
    }

    WriteCollectedVarToFile(resultvar, resultvarid);

}


class MyASTVisitor : public RecursiveASTVisitor<MyASTVisitor> {
public:
    ASTContext &Context;
    MyASTVisitor(ASTContext &A) : Context(A) {
        
    }
    SourceManager &SM = Context.getSourceManager();
    
    std::set<std::string> accessedVarNames;
    
    bool VisitFunctionDecl(FunctionDecl *decl) {
        if (!decl->isDefined())
            return true;//跳过不在文件里定义的函数
        if (visitedFuncStr.find(decl->getNameAsString()) != visitedFuncStr.end()) {
            return true;//如果函数已经出现在访问表里就跳过
        }
        visitedFuncStr.insert(decl->getNameAsString());//加入到函数访问表里
        
        
//        SourceLocation loc = decl->getLocation();
//        FileID fileid = SM.getFileID(loc);
//        unsigned filePos = SM.getFileOffset(loc);
//        unsigned linenum = Context.getSourceManager().getLineNumber(fileid, filePos);
//        llvm::outs() << decl->getNameAsString() << ": " << linenum << "\n";
        
        // Clear the set of accessed variable names.
        accessedVarNames.clear();
        FuncVars.clear();
        
        // 遍历函数里的每条语句
        Stmt *body = decl->getBody();
        if (body) {
          TraverseStmt(body);
        }

        // Print the accessed variable names.
//        llvm::outs() << "Variables accessed in function " << decl->getName() << ":\n";
        for (const auto &name : accessedVarNames) {
//            llvm::outs() << "  " << name << "\n";
            WriteTovarsStrToId(name);
        }
        
//        for(const auto &a : FuncVars) {
//            llvm::outs() << a.name << " " << a.loc << "\n";
//        }
        llvm::outs() << "Function: " << decl->getNameAsString() << "\n";
        WriteToMining();
        
        return true;
    }

    bool VisitMemberExpr(MemberExpr* ME) {
        auto QTExpr = ME->getBase();
        if (isa<ImplicitCastExpr>(QTExpr)) {
          return true;
        }//跳过使用结构体进行隐式转换的节点
        const auto *declRefexp = dyn_cast<DeclRefExpr>(QTExpr);
        if (!declRefexp) {
            return true;
        }//避免declRefexp为空造成的崩溃
        const auto *varDecl = dyn_cast<VarDecl>(declRefexp->getDecl()); //导致stdlib.h有问题
        if (!varDecl->hasGlobalStorage()) {
            return true;
        }//跳过不是全局变量的节点
        QualType QT = ME->getBase()->getType();
        const RecordType* RT = QT.getTypePtr()->getAsStructureType();
        if (RT) {
            const FieldDecl *FD = dyn_cast<FieldDecl>(ME->getMemberDecl());
            if (FD) {
                std::string s1 = varDecl->getNameAsString();
                std::string s2 = FD->getNameAsString();
                s1 += ".";
                s1 += s2;
                WriteTovarsStrToId(s1);
                int sid = varsStrToId[s1];
                SourceLocation loc = declRefexp->getLocation();
                FileID id = SM.getFileID(loc);
                unsigned pos = SM.getFileOffset(loc);
                unsigned num = SM.getLineNumber(id, pos);
                FuncVars.insert({s1, num, sid});
                accessedVarNames.insert(s1);
            }
        }
        return true;
    }
    
    bool VisitArraySubscriptExpr(ArraySubscriptExpr *ASE) {
        
        return true;
    }
    
    bool VisitDeclRefExpr(DeclRefExpr *expr) {
      if (auto varDecl = dyn_cast<VarDecl>(expr->getDecl())) {
          if (!varDecl->hasGlobalStorage())
              return true;
          std::string s1 = "";
          if (varDecl->getType()->isArrayType()) {
              return true;
          }
          else if (varDecl->getType()->isRecordType()) {
              return true;
          }
          else {
              s1 = varDecl->getNameAsString();
              WriteTovarsStrToId(s1);
              int sid = varsStrToId[s1];
              SourceLocation loc = expr->getLocation();
              FileID fileid = SM.getFileID(loc);
              unsigned filePos = SM.getFileOffset(loc);
              unsigned linenum = Context.getSourceManager().getLineNumber(fileid, filePos);
              FuncVars.insert({s1, linenum, sid});
//              llvm::outs() << s1 << "    " << linenum << "\n";
          }
          
          accessedVarNames.insert(s1);
      }

      return true;
    }
    
};
class MyASTConsumer : public ASTConsumer {
public:
    int funcnum = 0;
    virtual void HandleTranslationUnit(ASTContext &Ctx) override {
        MyASTVisitor MV(Ctx);
        FilesClear();
        MV.TraverseDecl(Ctx.getTranslationUnitDecl());
        WriteToFile();
        CallMining(mysup * halfdistance, 0.8); //Sup的设置:
        TransResultIdToStr();
        Resultsort();
        removeresultitems();
//        for (auto &a : varsStrToId) {
//            llvm::outs() << a.first << ": " << a.second << "\n";
//        }
        return ;
    }
};
class MyFrontedAction : public ASTFrontendAction {
public:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef InFile) override {
        return std::make_unique<MyASTConsumer>();
    }
    
};
int main(int argc, const char **argv) {
  CommonOptionsParser OptionsParser(argc, argv, MyToolCategory);
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());
  return Tool.run(newFrontendActionFactory<MyFrontedAction>().get());
}
