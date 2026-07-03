# The test binary links the static library and drives it via the public header
# (pulled in transitively from the library's include/ directory).

add_executable(test_dotenv test_dotenv.c)
target_link_libraries(test_dotenv PRIVATE dotenv_static)
target_compile_options(test_dotenv PRIVATE -Wall -Wextra)

add_test(NAME test_dotenv COMMAND test_dotenv)
