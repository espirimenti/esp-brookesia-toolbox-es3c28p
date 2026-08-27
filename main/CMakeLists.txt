idf_component_register(SRCS "main.cpp" INCLUDE_DIRS "." REQUIRES es3c28p_bsp)
target_compile_options(${COMPONENT_LIB} PUBLIC -Wno-missing-field-initializers)