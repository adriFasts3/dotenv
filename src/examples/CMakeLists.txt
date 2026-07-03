# Example programs link the static library and use the public header
# (pulled in transitively from the library's include/ directory).

foreach(name env_to_kvs)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE dotenv_static)
    target_compile_options(${name} PRIVATE -Wall -Wextra)
endforeach()
