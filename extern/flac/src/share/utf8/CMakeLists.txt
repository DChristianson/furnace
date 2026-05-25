add_library(utf8 STATIC
    charset.c
    iconvert.c
    utf8.c)

target_link_libraries(utf8 PUBLIC grabbag)

if(NOT APPLE)
    target_link_libraries(utf8 PUBLIC $<TARGET_NAME_IF_EXISTS:Iconv::Iconv>)
endif()
