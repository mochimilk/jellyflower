# Shared build helpers. Included from the top-level CMakeLists.txt, so paths and
# output folders stay those of the top-level project.

# jellyflower_add_test(<name> [SOURCES <src>...] [LIBS <lib>...] [DEFINES <def>...]
#                      [DEPENDS <target>...] [LABELS <label>...] [ARGS <arg>...]
#                      [CXX20] [XAML] [RESOURCES] [MANUAL])
#
# Builds tests/<name>.cpp plus SOURCES into the executable <name>.
#   CXX20      compile as C++20 (tests that include controller code); C++17 otherwise
#   XAML       add the manifest that lets the test host XAML Islands
#   RESOURCES  add assets/ and the generated version header to the include path
#   MANUAL     build only; don't register with CTest. Use it for tests that need
#              arguments or change the live system (they're run by hand).
#   LABELS     CTest labels: unit (pure logic), ci (CI runs it), desktop (opens windows)
function(jellyflower_add_test name)
  cmake_parse_arguments(PARSE_ARGV 1 test "CXX20;XAML;RESOURCES;MANUAL" ""
    "SOURCES;LIBS;DEFINES;DEPENDS;LABELS;ARGS")
  set(sources tests/${name}.cpp ${test_SOURCES})
  if(test_XAML)
    list(APPEND sources tests/ClockLayout.manifest)
  endif()
  add_executable(${name} ${sources})
  if(test_CXX20)
    target_compile_features(${name} PRIVATE cxx_std_20)
  else()
    target_compile_features(${name} PRIVATE cxx_std_17)
  endif()
  target_compile_definitions(${name} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00 WINVER=0x0A00 ${test_DEFINES})
  target_compile_options(${name} PRIVATE /W4 /permissive- /EHsc)
  if(test_RESOURCES)
    target_include_directories(${name} PRIVATE assets "${CMAKE_CURRENT_BINARY_DIR}")
  endif()
  if(test_LIBS)
    target_link_libraries(${name} PRIVATE ${test_LIBS})
  endif()
  if(test_DEPENDS)
    add_dependencies(${name} ${test_DEPENDS})
  endif()
  if(NOT test_MANUAL)
    add_test(NAME ${name} COMMAND ${name} ${test_ARGS})
    if(test_LABELS)
      set_tests_properties(${name} PROPERTIES LABELS "${test_LABELS}")
    endif()
  endif()
endfunction()
