# Checks the repository layout rules stated in README.md.
#
# The rules exist because the tree drifted: fifteen build trees, hundreds of
# untracked records inside them, and documentation that referenced a build tree
# which no longer existed. A rule with no mechanism is a wish, so each rule is
# expressed as a check that names the offending path.
#
# Every rule is classed enforce or report.
#
#   enforce  the rule already holds, so a violation is a regression and this
#            test fails.
#   report   README states the rule but the tree does not satisfy it yet. The
#            findings are listed and the test still passes, so the list itself
#            is the migration queue. Promote a rule to enforce in this file once
#            its findings reach zero; the intended end state is all enforced.
#
# Registered by CMakeLists.txt through joggle_script_test, which supplies
# SOURCE, so this runs with the rest of the suite: ctest -R layout.

if(NOT DEFINED SOURCE)
  message(FATAL_ERROR "layout.cmake requires -DSOURCE=<repository root>")
endif()

set(enforce_failures 0)

function(layout_report name kind)
  set(findings ${ARGN})
  list(LENGTH findings count)
  if(count EQUAL 0)
    message("  [          ok] ${name}")
    return()
  endif()
  if(kind STREQUAL "enforce")
    message("  [        FAIL] ${name}")
    foreach(finding IN LISTS findings)
      message("                 ${finding}")
    endforeach()
    math(EXPR bumped "${enforce_failures} + 1")
    set(enforce_failures "${bumped}" PARENT_SCOPE)
  else()
    message("  [report (${count})] ${name}")
    foreach(finding IN LISTS findings)
      message("                 ${finding}")
    endforeach()
  endif()
endfunction()

# Build trees that README.md gives a role to, taken from the first column of its
# tables. Only a declared tree is legitimate; the check below reports any other
# build* directory on disk.
file(READ "${SOURCE}/README.md" readme_text)
string(REPLACE "\n" ";" readme_lines "${readme_text}")
set(declared "")
foreach(line IN LISTS readme_lines)
  if(NOT line MATCHES "^\\|")
    continue()
  endif()
  string(REPLACE "|" ";" cells "${line}")
  list(GET cells 1 cell)
  string(REPLACE "`" "" cell "${cell}")
  string(STRIP "${cell}" cell)
  string(REGEX REPLACE "/$" "" cell "${cell}")
  if(cell MATCHES "^build")
    list(APPEND declared "${cell}")
  endif()
endforeach()
list(REMOVE_DUPLICATES declared)

# No generated tree content may be tracked.
execute_process(
  COMMAND git ls-files
  WORKING_DIRECTORY "${SOURCE}"
  OUTPUT_VARIABLE tracked
  ERROR_QUIET
  RESULT_VARIABLE git_result
)
set(tracked_under_build "")
if(git_result EQUAL 0)
  string(REPLACE "\n" ";" tracked_lines "${tracked}")
  foreach(path IN LISTS tracked_lines)
    if(path MATCHES "^build")
      list(APPEND tracked_under_build "${path}")
    endif()
  endforeach()
endif()

# Every build tree on disk must have a declared role.
file(GLOB build_entries RELATIVE "${SOURCE}" "${SOURCE}/build*")
set(undeclared_build "")
foreach(entry IN LISTS build_entries)
  if(NOT IS_DIRECTORY "${SOURCE}/${entry}")
    continue()
  endif()
  list(FIND declared "${entry}" index)
  if(index EQUAL -1)
    list(APPEND undeclared_build "${entry}")
  endif()
endforeach()

# A paper script must not default its output into the development build tree.
# Records and study output are the only legitimate default roots.
#
# Findings name the file but not the line. Splitting a file into a CMake list
# drops empty elements, so counting through such a list reports a line number
# that is short by the number of blank lines above the match; a wrong line
# number is worse than none, and the pattern is greppable.
file(GLOB_RECURSE paper_scripts "${SOURCE}/paper/*.py")
set(script_output_root "")
foreach(script IN LISTS paper_scripts)
  file(RELATIVE_PATH rel "${SOURCE}" "${script}")
  file(READ "${script}" script_text)
  if(script_text MATCHES
     "default=Path\\(\"build/|default=\"build/|default=Path\\('build/|default='build/")
    list(APPEND script_output_root
         "${rel} defaults an output argument to build/..., not build-study/")
  endif()
endforeach()

# Documented repository paths must exist. Bare src/ and docs/ are excluded
# because LLVM, ONNX-MLIR, and TVM use the same two names and the baseline
# records quote their trees on purpose.
set(path_prefixes
    "build" "paper/" "test/" "modules/" "extensions/" "include/"
    "tool/" "submission/")
set(docs "${SOURCE}/README.md")
file(GLOB found "${SOURCE}/docs/*.md")
list(APPEND docs ${found})
file(GLOB found "${SOURCE}/paper/*.md")
list(APPEND docs ${found})
file(GLOB_RECURSE found "${SOURCE}/paper/*.md")
list(APPEND docs ${found})
list(APPEND docs ${found})
file(GLOB found "${SOURCE}/extensions/*/README.md")
list(APPEND docs ${found})
list(REMOVE_DUPLICATES docs)

set(doc_path_missing "")
foreach(doc IN LISTS docs)
  if(NOT EXISTS "${doc}")
    continue()
  endif()
  file(RELATIVE_PATH rel "${SOURCE}" "${doc}")
  # A dated log quotes paths as they stood when written; it records history
  # rather than instructing a reader, so stale paths in it are not defects.
  if(rel STREQUAL "paper/plan.md")
    continue()
  endif()
  get_filename_component(doc_dir "${doc}" DIRECTORY)
  set(bases "${SOURCE}")
  set(current "${doc_dir}")
  while(NOT current STREQUAL "${SOURCE}" AND NOT current STREQUAL "/"
        AND NOT current STREQUAL "")
    list(APPEND bases "${current}")
    get_filename_component(parent "${current}" DIRECTORY)
    if(parent STREQUAL "${current}")
      break()
    endif()
    set(current "${parent}")
  endwhile()
  file(READ "${doc}" doc_text)
  string(REPLACE "\n" ";" doc_lines "${doc_text}")
  set(number 0)
  foreach(line IN LISTS doc_lines)
    math(EXPR number "${number} + 1")
    string(REGEX MATCHALL "`[^`]+`" tokens "${line}")
    foreach(raw IN LISTS tokens)
      string(REPLACE "`" "" token "${raw}")
      string(STRIP "${token}" token)
      if(NOT token MATCHES "/")
        continue()
      endif()
      if(token MATCHES "[*<>{} ]")
        continue()
      endif()
      set(prefixed FALSE)
      foreach(prefix IN LISTS path_prefixes)
        if(token MATCHES "^${prefix}")
          set(prefixed TRUE)
        endif()
      endforeach()
      if(NOT prefixed)
        continue()
      endif()
      string(REGEX REPLACE "#.*$" "" token "${token}")
      string(REGEX REPLACE ":[0-9]+(-[0-9]+)?$" "" token "${token}")
      string(REGEX REPLACE "/$" "" token "${token}")
      if(token STREQUAL "")
        continue()
      endif()
      string(REPLACE "/" ";" parts "${token}")
      list(GET parts 0 root_component)
      if(root_component MATCHES "^build")
        # Content under a generated tree is absent until something builds it,
        # so only the tree itself must be declared. undeclared-build-tree owns
        # that rule, and this one stops at the root.
        if(NOT IS_DIRECTORY "${SOURCE}/${root_component}")
          list(FIND declared "${root_component}" index)
          if(index EQUAL -1)
            list(APPEND doc_path_missing "${rel}:${number}: ${token}")
          endif()
        endif()
        continue()
      endif()
      set(found FALSE)
      foreach(base IN LISTS bases)
        if(EXISTS "${base}/${token}")
          set(found TRUE)
        endif()
      endforeach()
      if(NOT found)
        list(APPEND doc_path_missing "${rel}:${number}: ${token}")
      endif()
    endforeach()
  endforeach()
endforeach()

# Each study directory must carry its own README.
set(study_home "")
foreach(base "paper/experiments" "paper/studies")
  if(NOT IS_DIRECTORY "${SOURCE}/${base}")
    continue()
  endif()
  file(GLOB children RELATIVE "${SOURCE}" "${SOURCE}/${base}/*")
  foreach(child IN LISTS children)
    if(IS_DIRECTORY "${SOURCE}/${child}"
       AND NOT EXISTS "${SOURCE}/${child}/README.md")
      list(APPEND study_home "${child}/README.md")
    endif()
  endforeach()
endforeach()

# Metrics rather than pass/fail rules. They are reported because the tree grew
# faster than it was organized, and they are what the reorganization has to move.
file(GLOB flat_scripts "${SOURCE}/paper/*.py")
file(GLOB flat_docs "${SOURCE}/paper/*.md")
list(LENGTH flat_scripts script_count)
list(LENGTH flat_docs doc_count)
set(flat_paper "")
if(script_count GREATER 0 OR doc_count GREATER 0)
  # Judged acceptable rather than outstanding. paper/README.md indexes every
  # script with its output, the layout has been stable for the whole study, and
  # relocating the scripts would break the recorded reproduction commands that
  # name them. The count stays visible so a later change of mind is measurable,
  # and the disposition is stated so the number is not read as a defect.
  set(flat_paper
      "${script_count} measurement scripts and ${doc_count} study documents are flat in paper/ (accepted: indexed in paper/README.md)")
endif()

# A "records inside the development tree" metric was removed as redundant. The
# rule it proxied for is that a study script must not write into build/, and
# script-output-root checks that directly. As a metric it could not separate a
# violation from legitimate content, because a CTest working directory lives in
# build/ by design and writes exactly the kind of json and csv it counted.

# Markdown link targets must resolve, and they resolve relative to the linking
# file rather than to the repository root. That is a different rule from the
# backticked paths above, and it is the one that breaks when a document moves
# into a subdirectory: every `../` in it silently gains a level.
file(GLOB_RECURSE markdown "${SOURCE}/docs/*.md" "${SOURCE}/paper/*.md")
file(GLOB more_markdown
     "${SOURCE}/README.md" "${SOURCE}/extensions/*/README.md")
list(APPEND markdown ${more_markdown})
list(REMOVE_DUPLICATES markdown)

set(doc_link_missing "")
foreach(doc IN LISTS markdown)
  if(NOT EXISTS "${doc}")
    continue()
  endif()
  file(RELATIVE_PATH rel "${SOURCE}" "${doc}")
  get_filename_component(doc_dir "${doc}" DIRECTORY)
  file(READ "${doc}" rest)
  # Walked with string(FIND) rather than extracted with a regex. CMake returns
  # matches as a semicolon list, and a semicolon inside the document merges
  # fragments, which reported link targets nobody wrote; the walker cannot.
  while(TRUE)
    string(FIND "${rest}" "](" open)
    if(open EQUAL -1)
      break()
    endif()
    math(EXPR after "${open} + 2")
    string(SUBSTRING "${rest}" ${after} -1 tail)
    string(FIND "${tail}" ")" close)
    if(close EQUAL -1)
      break()
    endif()
    string(SUBSTRING "${tail}" 0 ${close} target)
    math(EXPR resume "${after} + ${close} + 1")
    string(SUBSTRING "${rest}" ${resume} -1 rest)

    if(target MATCHES "://" OR target MATCHES "^#" OR target MATCHES "^mailto:")
      continue()
    endif()
    string(REGEX REPLACE "#.*$" "" target "${target}")
    if(target STREQUAL "")
      continue()
    endif()
    if(NOT EXISTS "${doc_dir}/${target}")
      list(APPEND doc_link_missing "${rel} -> ${target}")
    endif()
  endwhile()
endforeach()

message("repository layout")
layout_report("tracked-under-build" enforce ${tracked_under_build})
layout_report("undeclared-build-tree" report ${undeclared_build})
layout_report("script-output-root" report ${script_output_root})
layout_report("doc-path-missing" report ${doc_path_missing})
layout_report("doc-link-missing" report ${doc_link_missing})
layout_report("study-home" report ${study_home})
layout_report("flat-paper-scripts" report ${flat_paper})

if(enforce_failures GREATER 0)
  message(FATAL_ERROR "${enforce_failures} layout rule(s) failed")
endif()
message("report-class findings above are the migration queue")
