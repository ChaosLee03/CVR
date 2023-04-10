set(LLVM_LINK_COMPONENTS
  support
)

add_clang_executable(Iotool
  Iotool.cpp
  )
target_link_libraries(Iotool
  PRIVATE
  clangAST
  clangASTMatchers
  clangBasic
  clangFrontend
  clangSerialization
  clangTooling
  )