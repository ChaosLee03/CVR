add_llvm_library( MyPass MODULE BUILDTREE_ONLY
  MyPass.cpp

  DEPENDS
  intrinsics_gen
  PLUGIN_TOOL
  opt
  )
