# core/ — portable static libraries. NO platform-specific code here;
# platform-specific code goes in platform/ (later phase).
add_subdirectory(data)
add_subdirectory(audio)
add_subdirectory(render)
add_subdirectory(scene)
add_subdirectory(app)