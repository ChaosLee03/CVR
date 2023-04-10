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

std::set<std::string> store;

class MyASTVisitor : public RecursiveASTVisitor<MyASTVisitor> {
public:
    ASTContext&ACT;
    MyASTVisitor(ASTContext &A) : ACT(A) {

    }
    
    bool VisitVarDecl(VarDecl *VD) {
        SourceManager &SM = ACT.getSourceManager();
        if (VD->hasGlobalStorage() && VD->isFileVarDecl() && !SM.isInSystemHeader(VD->getLocation())) { //去掉在头文件里定义的全局变量
            store.insert(VD->getNameAsString());
            llvm::outs() << VD->getNameAsString() << "\n";
        }
        return true;
    }
};
class MyASTConsumer : public ASTConsumer {
public:
    void HandleTranslationUnit(ASTContext &Ctx) override {
        MyASTVisitor MV(Ctx);
        MV.TraverseDecl(Ctx.getTranslationUnitDecl());
        writefile();
        system("python3 /Users/mypc/Downloads/pythonProject2/NAMEPluginpost.py");
    }
    void writefile() {
        std::ofstream outfile("/Users/mypc/Downloads/PS/NAMEPluginout.txt");
        if (outfile.is_open()) {
            for (auto &a : store) {
                outfile << a << "\n";
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
