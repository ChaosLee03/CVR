set(LLVM_LINK_COMPONENTS
  support
)

add_clang_executable(Shortdistancetool
  Shortdistancetool.cpp
  )
target_link_libraries(Shortdistancetool
  PRIVATE
  clangAST
  clangASTMatchers
  clangBasic
  clangFrontend
  clangSerialization
  clangTooling
  )