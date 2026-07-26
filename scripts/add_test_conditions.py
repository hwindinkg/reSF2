with open('E:/reSF2/tests/CMakeLists.txt', 'r') as f:
    content = f.read()

insert_before = content.find('add_test(NAME test_slot_utils')

new_section = '''
# ConditionAnimation / CCA / findNameInModelSlots test
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

content_before = content[:insert_before]
content_after = content[insert_before:]
with open('E:/reSF2/tests/CMakeLists.txt', 'w') as f:
    f.write(content_before + new_section + content_after)
print('OK')
