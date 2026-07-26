with open('E:/reSF2/tests/CMakeLists.txt', 'r') as f:
    content = f.read()

insert_before = content.find('add_test(NAME test_name_utils')
if insert_before < 0:
    print('ERROR: cannot find insertion point')
    exit(1)

new_section = '''
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
'''

content_before = content[:insert_before]
content_after = content[insert_before:]
with open('E:/reSF2/tests/CMakeLists.txt', 'w') as f:
    f.write(content_before + new_section + content_after)
print('OK')
