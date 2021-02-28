set(sources
	regcomp.c
	regerror.c
	regexec.c
	regex.h
	tre-ast.c
	tre-ast.h
	tre-compile.c
	tre-compile.h
	tre-config.h
	tre-filter.c
	tre-filter.h
	tre.h
	tre-internal.h
	tre-match-approx.c
	tre-match-backtrack.c
	tre-match-parallel.c
	tre-match-utils.h
	tre-mem.c
	tre-mem.h
	tre-parse.c
	tre-parse.h
	tre-stack.c
	tre-stack.h
	xmalloc.c
	xmalloc.h
	)

add_library(tre-regex OBJECT ${sources})
set(COMPILE_FLAGS -Wno-unused-value)
if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(COMPILE_FLAGS "${COMPILE_FLAGS} -Wno-maybe-uninitialized")
endif()
target_compile_options(tre-regex PRIVATE ${COMPILE_FLAGS})
target_include_directories(tre-regex PRIVATE ${PROJECT_SOURCE_DIR}/src)
