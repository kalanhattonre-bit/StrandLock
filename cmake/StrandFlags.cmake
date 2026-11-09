# Compiler flags for StrandCore (PLUGIN-PLAN 1.6): the C++ must do the prototype's arithmetic in the
# prototype's order, so floating point is precise and never contracted into FMAs. Never add /fp:fast,
# -ffast-math or /arch:AVX2 to StrandCore.

# Floating-point rules, also used for the test runner so the expected values it computes follow them.
function(strand_fp_flags target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /fp:precise)
    else()
        target_compile_options(${target} PRIVATE -ffp-contract=off -fno-fast-math)
    endif()
endfunction()

# Everything StrandCore is compiled with: the floating-point rules and its warnings.
function(strand_core_flags target)
    strand_fp_flags(${target})
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wsign-conversion -Wcast-align
                                                 -Wuninitialized -Wunused-parameter -Wredundant-decls)
    endif()
endfunction()

# STRANDLOCK_SANITIZE (the Linux job's second configure): AddressSanitizer and UBSan, so a voice that reads
# a freed snapshot is a crash, not a wrong sample. The flags go on StrandCore and on the runner's own source
# files, not on the JUCE module sources the runner also compiles. UBSan stops at its first report.
set(STRANDLOCK_SANITIZER_COMPILE_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g)
set(STRANDLOCK_SANITIZER_LINK_FLAGS -fsanitize=address,undefined)

function(strand_sanitize_target target)
    target_compile_options(${target} PRIVATE ${STRANDLOCK_SANITIZER_COMPILE_FLAGS})
    target_compile_definitions(${target} PRIVATE STRANDLOCK_SANITIZE=1)
endfunction()

function(strand_sanitize_sources)
    set_property(SOURCE ${ARGN} APPEND PROPERTY COMPILE_OPTIONS ${STRANDLOCK_SANITIZER_COMPILE_FLAGS})
endfunction()
