set(LLVM_LINK_COMPONENTS
  support
)

add_clang_executable(Nametool
  Nametool.cpp
  )
target_link_libraries(Nametool
  PRIVATE
  clangAST
  clangASTMatchers
  clangBasic
  clangFrontend
  clangSerialization
  clangTooling
  )