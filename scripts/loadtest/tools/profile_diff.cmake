# Compare a capture's stage budget against the baseline's.
#
# usage: cmake -Dprofile_dir=<dir> "-Dstamps=<baseline>;...;<latest>" -P tools/profile_diff.cmake
#
# Reads the tab-separated budgets tools/stage_budget.awk writes, two passes per
# stamp. Nanoseconds are carried as tenths throughout.

if(NOT DEFINED profile_dir OR NOT DEFINED stamps)
	message(FATAL_ERROR "usage: cmake -Dprofile_dir=<dir> \"-Dstamps=<baseline>;...;<latest>\" -P tools/profile_diff.cmake")
endif()

list(LENGTH stamps stamp_count)
if(stamp_count LESS 2)
	message(FATAL_ERROR "need a baseline and at least one later capture, got ${stamp_count} stamp(s)")
endif()

function(budget_path out stamp pass) #{{{1
	set(path "${profile_dir}/${stamp}-${pass}.budget.tsv")
	if(NOT EXISTS "${path}")
		message(FATAL_ERROR "no ${path}: the capture it names is gone, so nothing can be compared against it")
	endif()
	set(${out} "${path}" PARENT_SCOPE)
endfunction()

function(tenths out value) #{{{1
	if(NOT value MATCHES "^([0-9]+)\\.([0-9])$")
		message(FATAL_ERROR "'${value}' is not a one-decimal number")
	endif()
	math(EXPR result "${CMAKE_MATCH_1} * 10 + ${CMAKE_MATCH_2}")
	set(${out} "${result}" PARENT_SCOPE)
endfunction()

function(format_signed out value) #{{{1
	# 26 becomes "+2.6", -26 becomes "-2.6".
	set(sign "+")
	if(value LESS 0)
		set(sign "-")
		math(EXPR value "0 - ${value}")
	endif()
	math(EXPR whole "${value} / 10")
	math(EXPR frac "${value} % 10")
	set(${out} "${sign}${whole}.${frac}" PARENT_SCOPE)
endfunction()

function(format_tenths out value) #{{{1
	math(EXPR whole "${value} / 10")
	math(EXPR frac "${value} % 10")
	set(${out} "${whole}.${frac}" PARENT_SCOPE)
endfunction()

macro(read_budget prefix stamp pass) #{{{1
	# Sets ${prefix}_stages, and per stage ${prefix}_ns_<key>, ${prefix}_scope_<key>
	# and ${prefix}_name_<key>. Sets ${prefix}_total to the whole packet.
	budget_path(_path "${stamp}" "${pass}")
	file(STRINGS "${_path}" _lines)
	set(${prefix}_stages "")
	foreach(_line IN LISTS _lines)
		string(REPLACE "\t" ";" _fields "${_line}")
		list(GET _fields 0 _kind)
		if(_kind STREQUAL "total")
			list(GET _fields 1 ${prefix}_total)
		elseif(_kind STREQUAL "stage")
			list(GET _fields 1 _name)
			list(GET _fields 2 _scope)
			list(GET _fields 5 _ns)
			string(MAKE_C_IDENTIFIER "${_name}" _key)
			tenths(_ns_tenths "${_ns}")
			list(APPEND ${prefix}_stages "${_key}")
			set(${prefix}_name_${_key} "${_name}")
			set(${prefix}_scope_${_key} "${_scope}")
			set(${prefix}_ns_${_key} "${_ns_tenths}")
		else()
			message(FATAL_ERROR "${_path}: unknown row '${_kind}'")
		endif()
	endforeach()
	if(NOT ${prefix}_stages)
		message(FATAL_ERROR "${_path} names no stage")
	endif()
endmacro()

function(absolute out value) #{{{1
	if(value LESS 0)
		math(EXPR value "0 - ${value}")
	endif()
	set(${out} "${value}" PARENT_SCOPE)
endfunction()

list(GET stamps 0 base_stamp)
list(GET stamps -1 new_stamp)

read_budget(base_a "${base_stamp}" 1)
read_budget(base_b "${base_stamp}" 2)
read_budget(new_a "${new_stamp}" 1)
read_budget(new_b "${new_stamp}" 2)

if(NOT base_a_stages STREQUAL new_a_stages)
	message(FATAL_ERROR
		"the two captures name different stages, so tools/stage_budget.awk changed between them; "
		"re-take the baseline"
	)
endif()

message(STATUS "baseline ${base_stamp}")
message(STATUS "this run ${new_stamp}")

set(in_scope_rows "")
set(context_rows "")
foreach(key IN LISTS base_a_stages)
	math(EXPR delta "${new_a_ns_${key}} - ${base_a_ns_${key}}")

	math(EXPR base_floor "${base_b_ns_${key}} - ${base_a_ns_${key}}")
	math(EXPR new_floor "${new_b_ns_${key}} - ${new_a_ns_${key}}")
	absolute(base_floor "${base_floor}")
	absolute(new_floor "${new_floor}")
	set(floor "${base_floor}")
	if(new_floor GREATER floor)
		set(floor "${new_floor}")
	endif()

	format_tenths(base_text "${base_a_ns_${key}}")
	format_tenths(new_text "${new_a_ns_${key}}")
	format_signed(delta_text "${delta}")
	format_tenths(floor_text "${floor}")

	set(row "| ${base_a_name_${key}} | ${base_text} | ${new_text} | ${delta_text} | ${floor_text} |")
	if(base_a_scope_${key} STREQUAL "in_scope")
		list(APPEND in_scope_rows "${row}")
	else()
		list(APPEND context_rows "${row}")
	endif()
endforeach()

# The whole packet is in whole nanoseconds.
math(EXPR total_delta "${new_a_total} - ${base_a_total}")
set(total_text "${total_delta}")
if(total_delta GREATER_EQUAL 0)
	set(total_text "+${total_delta}")
endif()

list(JOIN in_scope_rows "\n" in_scope_text)
list(JOIN context_rows "\n" context_text)
message(
	"\nns/packet, baseline's first pass against this run's\n"
	"\n"
	"| stage | baseline | this run | delta | pair floor |\n"
	"|---|---|---|---|---|\n"
	"${in_scope_text}\n"
	"\ncontext, which neither pal nor the sample moves\n"
	"\n"
	"| stage | baseline | this run | delta | pair floor |\n"
	"|---|---|---|---|---|\n"
	"${context_text}\n"
	"\nthe whole packet went ${base_a_total} -> ${new_a_total} ns, ${total_text}\n"
)

if(stamp_count GREATER 2)
	set(history "")
	foreach(stamp IN LISTS stamps)
		read_budget(entry "${stamp}" 1)
		set(cells "")
		foreach(key IN LISTS entry_stages)
			format_tenths(cell "${entry_ns_${key}}")
			list(APPEND cells "${cell}")
		endforeach()
		list(JOIN cells " | " cell_text)
		list(APPEND history "| ${stamp} | ${entry_total} | ${cell_text} |")
	endforeach()

	set(headings "")
	set(rules "| --- | --- |")
	foreach(key IN LISTS base_a_stages)
		list(APPEND headings "${base_a_name_${key}}")
		string(APPEND rules " --- |")
	endforeach()
	list(JOIN headings " | " heading_text)
	list(JOIN history "\n" history_text)
	message(
		"every capture, ns/packet\n"
		"\n"
		"| capture | total | ${heading_text} |\n"
		"${rules}\n"
		"${history_text}\n"
	)
endif()
