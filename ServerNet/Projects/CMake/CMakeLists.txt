cmake_minimum_required(VERSION 2.8)
project(SERVERNET)


set(ServerNetIncludeDir ${ServerNetRoot}/Headers PARENT_SCOPE)


file(GLOB Sources ${ServerNetRoot}/Sources/*.cpp)

list(REMOVE_ITEM Sources ${ServerNetRoot}/Sources/SslStub.cpp
  ${ServerNetRoot}/Sources/XWinNetAddr.cpp
  ${ServerNetRoot}/Sources/XWinSocket.cpp)


add_library(ServerNet SHARED ${Sources})


include_directories(${IcuIncludeDir}
  ${ServerNetRoot}/Headers
  ${XBoxRoot}
  ${OpenSSLIncludeDir})


target_link_libraries(ServerNet Kernel KernelIPC Xml crypto ssl)
