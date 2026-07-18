add_library(resf2_format STATIC
    xml_doc.cpp
    list_parser.cpp
    location_parser.cpp
    stage_parser.cpp
    json_atlas.cpp
)

target_include_directories(resf2_format PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
