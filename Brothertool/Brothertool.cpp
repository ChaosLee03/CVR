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

std::map<std::string, std::set<std::string>> VartoStruct;
std::map<std::string, std::set<std::string>> VartoUnion;
std::map<std::string, std::set<std::string>> VartoArray;

class MyASTVisitor : public RecursiveASTVisitor<MyASTVisitor> {
public:
    ASTContext &ACT;
    MyASTVisitor(ASTContext &A) : ACT(A) {

    }

    bool VisitMemberExpr(MemberExpr* ME) {
        auto QTExpr = ME->getBase();
        if (isa<ImplicitCastExpr>(QTExpr)) {
          return true;
        }//跳过使用结构体进行隐式转换的节点
        const auto *declRefexp = dyn_cast<DeclRefExpr>(QTExpr);
        if (!declRefexp) {
//            llvm::outs() << isa<CallExpr>(QTExpr);
            return true;
        }//避免declRefexp为空造成的崩溃
        const auto *varDecl = dyn_cast<VarDecl>(declRefexp->getDecl()); //导致stdlib.h有问题
        if (!varDecl->hasGlobalStorage()) {
            return true;
        }//跳过不是全局变量的节点

        auto* var = dyn_cast_or_null<VarDecl>(ME->getMemberDecl());
        if (var) {
            if (!var->hasGlobalStorage()) {
                return true;
            }
        }

//        llvm::outs() << "VisitMemberExpr :\n";
//        SourceManager &SM = ACT.getSourceManager();
//        SourceRange SR = ME->getSourceRange();
//        std::string sourceText = clang::Lexer::getSourceText(CharSourceRange::getTokenRange(SR), SM, LangOptions(), 0).str();
//        llvm::errs() << sourceText << "\n";
//
//
//        llvm::outs() << varDecl->getNameAsString() << " is variable name\n";
//        llvm::outs() << ME->getMemberDecl()->getNameAsString() << " is flied name\n";

        QualType QT = ME->getBase()->getType();
        const RecordType* RT = QT.getTypePtr()->getAsStructureType();
        if (RT) {
            const FieldDecl *FD = dyn_cast<FieldDecl>(ME->getMemberDecl());
            if (FD) {
                std::string s1 = varDecl->getNameAsString();
                std::string s2 = FD->getNameAsString();

                std::pair<std::string, std::set<std::string>> temp;
                std::set<std::string> tempset{};
                temp = std::make_pair(s1, tempset);
                if (VartoStruct.find(s1) == VartoStruct.end()) {
                    temp.second.insert(s2);
                    VartoStruct.insert(temp);
                }
                else {
                    auto &target = VartoStruct[s1];
                    target.insert(s2);
                }
//                    VartoStruct.insert(temp);
            }
        }// 获取结构体变量到结构体成员的映射

        RT = QT.getTypePtr()->getAsUnionType();
        if (RT) {
            const FieldDecl *FD = dyn_cast<FieldDecl>(ME->getMemberDecl());
            if (FD) {
                std::string s1 = varDecl->getNameAsString();
                std::string s2 = FD->getNameAsString();

                std::pair<std::string, std::set<std::string>> temp;
                std::set<std::string> tempset{};
                temp = std::make_pair(s1, tempset);
                if (VartoUnion.find(s1) == VartoUnion.end()) {
                    temp.second.insert(s2);
                    VartoUnion.insert(temp);
                }
                else {
                    auto &target = VartoUnion[s1];
                    target.insert(s2);
                }
            }
        }// 获取联合体变量到联合体成员的映射

        return true;
    }

    bool VisitArraySubscriptExpr(ArraySubscriptExpr *ASE) {
         Expr *base = ASE->getBase();
         Expr *index = ASE->getIdx();
         std::string s1, s2;
         const Expr *castE = dyn_cast<Expr>(base);
         if (castE) {
             castE = castE->IgnoreImpCasts(); // 忽略所有隐式转换
             if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(castE)) {
                 const VarDecl *vdl = dyn_cast<VarDecl>(DRE->getDecl());
                 if (!vdl->hasGlobalStorage())
                     return true;// 跳过局部定义的数组变量
                 s1 = DRE->getNameInfo().getAsString();
             }

             if (const MemberExpr *ME = dyn_cast<MemberExpr>(castE)) { // 处理结构体中的数组
                 auto QTExpr = ME->getBase();
                 auto *declRefexp = dyn_cast<DeclRefExpr>(QTExpr);
                 auto *varDecl = dyn_cast<VarDecl>(declRefexp->getDecl());
                 if (!varDecl->hasGlobalStorage()) { // 跳过局部定义的结构体内的数组
                     return true;
                 }
                 s1 += varDecl->getNameAsString();
                 const auto* QT = ME->getMemberDecl();
                 s1 += "::";
                 s1 += QT->getNameAsString();
             }
         }

        const Expr *subE = dyn_cast<Expr>(index);
        if (subE) {
            subE = subE->IgnoreImpCasts(); // 忽略所有隐式转换
            llvm::APSInt value;
            if (subE->isIntegerConstantExpr(value, ACT)) {
                s2 = std::to_string(value.getExtValue());
            }
        }

        std::pair<std::string, std::set<std::string>> temp;
        std::set<std::string> tempset{};
        temp = std::make_pair(s1, tempset);
        if (VartoArray.find(s1) == VartoArray.end()) {
            temp.second.insert(s2);
            VartoArray.insert(temp);

        }
        else {
            auto &target = VartoArray[s1];
            target.insert(s2);
        }
        return true;
    }

};
class MyASTConsumer : public ASTConsumer {
public:
    void HandleTranslationUnit(ASTContext &Ctx) override {
        MyASTVisitor MV(Ctx);
        MV.TraverseDecl(Ctx.getTranslationUnitDecl());
        llvm::outs() << "*******Struct Mapping: \n";
        for (const auto &Pair : VartoStruct) {
            llvm::outs() << Pair.first << ": ";
            for (auto &Str: Pair.second) {
                llvm::outs() << Str << " ";
            }
            llvm::outs() << "\n";
        }
        llvm::outs() << "*******Union Mapping: \n";
        for (const auto &Pair : VartoUnion) {
            llvm::outs() << Pair.first << ": ";
            for (auto &Str: Pair.second) {
                llvm::outs() << Str << " ";
            }
            llvm::outs() << "\n";
        }
        llvm::outs() << "*******Array Mapping: \n";
        for (const auto &Pair : VartoArray) {
            llvm::outs() << Pair.first << " : ";
            for (auto &Str: Pair.second) {
                llvm::outs() << Str << " ";
            }
            llvm::outs() << "\n";
        }
        WriteFile();
    }

    void WriteFile() {
        std::ofstream outfile("/Users/mypc/Downloads/PS/CJPluginout.txt");
        llvm::outs() << "进入写文件函数\n";
        if (outfile.is_open()) {
            if (VartoStruct.size()) {
                outfile << "同属一个结构体：\n";
                for (const auto &a : VartoStruct) {
                    for (const auto &b : a.second) {
                        outfile << a.first << "::" << b << " ";
                    }
                    outfile << "\n";
                }
            }
            if (VartoUnion.size()) {
                outfile << "同属一个联合体：\n";
                for (const auto &a : VartoUnion) {
                    for (const auto &b : a.second) {
                        outfile << a.first << "::" << b << " ";
                    }
                    outfile << "\n";
                }
            }
            if (VartoArray.size()) {
                outfile << "同属一个数组：\n";
                for (const auto &a : VartoArray) {
                    for (const auto &b : a.second) {
                        outfile << a.first << "[" << b << "]" << " ";
                    }
                    outfile << "\n";
                }
            }
        }
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
