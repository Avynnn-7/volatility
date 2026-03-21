# CMake generated Testfile for 
# Source directory: C:/Users/stars/OneDrive/Desktop/volatility/vol_arb_claude/vol_arb
# Build directory: C:/Users/stars/OneDrive/Desktop/volatility/vol_arb_claude/vol_arb/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test([=[AllTests]=] "C:/Users/stars/OneDrive/Desktop/volatility/vol_arb_claude/vol_arb/build/Debug/vol_tests.exe")
  set_tests_properties([=[AllTests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/stars/OneDrive/Desktop/volatility/vol_arb_claude/vol_arb/CMakeLists.txt;156;add_test;C:/Users/stars/OneDrive/Desktop/volatility/vol_arb_claude/vol_arb/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test([=[AllTests]=] "C:/Users/stars/OneDrive/Desktop/volatility/vol_arb_claude/vol_arb/build/Release/vol_tests.exe")
  set_tests_properties([=[AllTests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/stars/OneDrive/Desktop/volatility/vol_arb_claude/vol_arb/CMakeLists.txt;156;add_test;C:/Users/stars/OneDrive/Desktop/volatility/vol_arb_claude/vol_arb/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test([=[AllTests]=] "C:/Users/stars/OneDrive/Desktop/volatility/vol_arb_claude/vol_arb/build/MinSizeRel/vol_tests.exe")
  set_tests_properties([=[AllTests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/stars/OneDrive/Desktop/volatility/vol_arb_claude/vol_arb/CMakeLists.txt;156;add_test;C:/Users/stars/OneDrive/Desktop/volatility/vol_arb_claude/vol_arb/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test([=[AllTests]=] "C:/Users/stars/OneDrive/Desktop/volatility/vol_arb_claude/vol_arb/build/RelWithDebInfo/vol_tests.exe")
  set_tests_properties([=[AllTests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/stars/OneDrive/Desktop/volatility/vol_arb_claude/vol_arb/CMakeLists.txt;156;add_test;C:/Users/stars/OneDrive/Desktop/volatility/vol_arb_claude/vol_arb/CMakeLists.txt;0;")
else()
  add_test([=[AllTests]=] NOT_AVAILABLE)
endif()
