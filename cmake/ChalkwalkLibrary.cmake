# ---------------------------------------------------------------------------
# Where the shared libraries come from.
#
# Unset, nothing changes: this repository's submodules are used and a fresh
# clone builds with no extra steps. The submodule stays the source of truth for
# WHICH commit this project wants.
#
# CHALKWALK_SOUNDFONT_DIR -- a cache variable or an environment variable --
# points at a working checkout instead,
# which is what makes a change to a library testable HERE without a commit and
# without a push:
#
#     cmake -B build -DCHALKWALK_SOUNDFONT_DIR=$HOME/Programming/chalkwalk-soundfont
#
# This project then compiles that working tree directly. Edit there, rebuild
# here, run the suite; no round trip through GitHub. That matters most when the
# library is the thing under test: a bumped FluidLite wants Lockstep's Tone
# machine driven against it before the SHA is committed anywhere.
#
# THE SUBMODULE SHA NO LONGER DESCRIBES WHAT YOU BUILT while one of these is
# set, which is the whole cost of it. CI must not use them, and neither should
# anything whose result is meant to be attributable, since a number that cannot
# name the commit that produced it is not a measurement. Use an override to
# iterate; bump the submodule and re-verify before calling anything done.
#
# Same shape as Anvil's CHALKWALK_PHYSICAL_DIR, deliberately: one pattern
# across the ecosystem is worth more than a better one used in one place.
# ---------------------------------------------------------------------------

function(chalkwalk_add_library name submodule_path)
    # Already added by a parent, so use theirs.
    #
    # These libraries nest: a project can pull in one that pulls in another it
    # has already added. A second copy is not a version conflict -- it is a
    # duplicate CMake target name, which fails the configure outright.
    #
    # Whichever project adds it first wins and the rest reuse it, which is the
    # same rule chalkwalk-ninjam applies to its vendored ogg and vorbis. It
    # also means the OUTER project's submodule SHA is the one that describes
    # the build, and the inner one is not consulted at all -- so a nested
    # library does not need its own submodules checked out.
    if(TARGET chalkwalk_${name})
        message(STATUS "chalkwalk-${name}: already provided by a parent project")
        return()
    endif()

    string(TOUPPER "${name}" upper)
    set(var "CHALKWALK_${upper}_DIR")

    if(NOT ${var} AND DEFINED ENV{${var}})
        set(${var} "$ENV{${var}}")
    endif()
    set(${var} "${${var}}" CACHE PATH
        "Working checkout of chalkwalk-${name}; empty means use this repository's own submodule")

    if(${var})
        if(NOT EXISTS "${${var}}/CMakeLists.txt")
            message(FATAL_ERROR
                "${var} is set to '${${var}}' but there is no chalkwalk-${name} "
                "there. Point it at a checkout, or unset it to use the submodule.")
        endif()
        set(root "${${var}}")
        message(STATUS
            "chalkwalk-${name}: OVERRIDE at ${root} "
            "(the submodule SHA does not describe this build)")
    else()
        set(root "${CMAKE_CURRENT_SOURCE_DIR}/${submodule_path}")
        if(NOT EXISTS "${root}/CMakeLists.txt")
            message(FATAL_ERROR
                "No chalkwalk-${name}.\n"
                "  This repository's ${submodule_path} submodule is not checked "
                "out, and ${var} is not set. Either:\n"
                "    git submodule update --init --recursive\n"
                "  or point at a working checkout:\n"
                "    cmake -B build -D${var}=/path/to/chalkwalk-${name}")
        endif()
    endif()

    # Each library's own suite runs inside this project's ctest, so this
    # project verifies its dependencies rather than assuming them. For
    # chalkwalk-soundfont that is load-bearing: its one test is what proves the
    # FluidLite patch is applied in the build we actually made.
    set(CHALKWALK_${upper}_TESTS ON CACHE BOOL "" FORCE)
    add_subdirectory("${root}" "${CMAKE_BINARY_DIR}/libs/${name}")
endfunction()
