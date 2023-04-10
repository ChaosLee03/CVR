set(LLVM_LINK_COMPONENTS
  support
)

add_clang_executable(Brothertool
  Brothertool.cpp
  )
target_link_libraries(Brothertool
  PRIVATE
  clangAST
  clangASTMatchers
  clangBasic
  clangFrontend
  clangSerialization
  clangTooling
  )