# Restore our changes that were lost by git checkout
import os

# 1. Fix CMakeLists.txt - remove port section
with open('E:/reSF2/CMakeLists.txt', 'r') as f:
    content = f.read()

old_port = '''# --- Port demo (modular engine integration) ------------------------------
if(RESF2_BUILD_RUNTIME)
    add_executable(resf2_port main_port.cpp)
    target_link_libraries(resf2_port PRIVATE
        resf2_fight
        resf2_format
        resf2_platform
        resf2_renderer
        resf2_core
        resf2_reverse
        resf2_warnings
        ZLIB::ZLIB
        glfw
    )
    target_compile_features(resf2_port PRIVATE cxx_std_23)
    set_target_properties(resf2_port PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${RESF2_RUNTIME_OUTPUT_DIR}"
    )
endif()
'''

if old_port in content:
    content = content.replace(old_port, '# Port removed\n')
    with open('E:/reSF2/CMakeLists.txt', 'w') as f:
        f.write(content)
    print('CMakeLists.txt: port removed')
else:
    print('CMakeLists.txt: port section not found')

# 2. Fix reverse/CMakeLists.txt - add name_utils.cpp, conditions.cpp
with open('E:/reSF2/engine/reverse/CMakeLists.txt', 'r') as f:
    content = f.read()

old_reverse = '''    dz_decoder.cpp
)'''

new_reverse = '''    dz_decoder.cpp
    name_utils.cpp
    conditions.cpp
)'''

if old_reverse in content:
    content = content.replace(old_reverse, new_reverse)
    with open('E:/reSF2/engine/reverse/CMakeLists.txt', 'w') as f:
        f.write(content)
    print('reverse/CMakeLists.txt: added name_utils.cpp, conditions.cpp')
else:
    print('reverse/CMakeLists.txt: section not found')
    # Try without trailing newline
    idx = content.find('dz_decoder.cpp\n)')
    if idx >= 0:
        content = content[:idx] + 'dz_decoder.cpp\n    name_utils.cpp\n    conditions.cpp\n)' + content[idx+len('dz_decoder.cpp\n)'):]
        with open('E:/reSF2/engine/reverse/CMakeLists.txt', 'w') as f:
            f.write(content)
        print('reverse/CMakeLists.txt: fixed (alt method)')

# 3. Fix tests/CMakeLists.txt - add our test sections
with open('E:/reSF2/tests/CMakeLists.txt', 'r') as f:
    content = f.read()

# Check if they already exist
if 'test_name_utils' not in content:
    insert_point = content.find('add_test(NAME test_json_atlas')
    if insert_point >= 0:
        new_tests = '''
# NameRange / stringEqualWithRange test
add_executable(test_name_utils
    test_name_utils.cpp
)
target_link_libraries(test_name_utils PRIVATE
    resf2_reverse
    resf2_warnings
)
target_compile_features(test_name_utils PRIVATE cxx_std_23)
add_test(NAME test_name_utils COMMAND test_name_utils WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# AnimSlot / findMatchingSlotInList test
add_executable(test_slot_utils
    test_slot_utils.cpp
)
target_link_libraries(test_slot_utils PRIVATE
    resf2_reverse
    resf2_warnings
)
target_compile_features(test_slot_utils PRIVATE cxx_std_23)
add_test(NAME test_slot_utils COMMAND test_slot_utils WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# ConditionAnimation / CCA test
add_executable(test_conditions
    test_conditions.cpp
)
target_link_libraries(test_conditions PRIVATE
    resf2_reverse
    resf2_warnings
)
target_compile_features(test_conditions PRIVATE cxx_std_23)
add_test(NAME test_conditions COMMAND test_conditions WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
'''
        content = content[:insert_point] + new_tests + content[insert_point:]
        with open('E:/reSF2/tests/CMakeLists.txt', 'w') as f:
            f.write(content)
        print('tests/CMakeLists.txt: added test sections')
else:
    print('tests/CMakeLists.txt: already has test_name_utils')

# 4. Verify all our files exist
our_files = [
    'engine/reverse/name_utils.hpp',
    'engine/reverse/name_utils.cpp',
    'engine/reverse/conditions.hpp',
    'engine/reverse/conditions.cpp',
    'engine/reverse/slot_utils.hpp',
    'tests/test_name_utils.cpp',
    'tests/test_slot_utils.cpp',
    'tests/test_conditions.cpp',
]
for f in our_files:
    if os.path.exists('E:/reSF2/' + f):
        print('OK: ' + f)
    else:
        print('MISSING: ' + f)

print('\\nDone')
