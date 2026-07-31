## List available targets

# Targets come from build.ninja and descriptions from each action's own '##'
# header, so neither is restated here. A helper included by the actions carries a
# plain '#' instead and is therefore not a target.

file(READ "${CMAKE_CURRENT_LIST_DIR}/../build.ninja" text)
string(REPLACE ";" "\\;" text "${text}")
string(REPLACE "\n" ";" lines "${text}")

set(target "")
set(entries "")
set(width 0)
foreach(line IN LISTS lines)
	if(line MATCHES "^build ([^ :]+): (action|relay)$")
		set(target "${CMAKE_MATCH_1}")
		set(rule "${CMAKE_MATCH_2}")
	elseif(target AND line MATCHES "^  name = (.+)$")
		set(script "${CMAKE_MATCH_1}")
		if(rule STREQUAL "relay")
			set(script "${script}-relay")
		endif()
		file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/${script}.cmake" header LIMIT_COUNT 1)
		string(REGEX REPLACE "^## " "" header "${header}")
		list(APPEND entries "${target}\t${header}")
		string(LENGTH "${target}" length)
		if(length GREATER width)
			set(width "${length}")
		endif()
		set(target "")
	endif()
endforeach()

math(EXPR width "${width} + 2")
foreach(entry IN LISTS entries)
	string(REPLACE "\t" ";" entry "${entry}")
	list(GET entry 0 target)
	list(GET entry 1 header)
	string(LENGTH "${target}" length)
	math(EXPR padding "${width} - ${length}")
	string(REPEAT " " ${padding} padding)
	message("  ${target}${padding}${header}")
endforeach()
