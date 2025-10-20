# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "CMakeFiles\\dldl-lhsj_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\dldl-lhsj_autogen.dir\\ParseCache.txt"
  "dldl-lhsj_autogen"
  )
endif()
