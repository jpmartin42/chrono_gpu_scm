cmake_minimum_required(VERSION 3.18)

project(chrono_gpu_scm)

# Finding  the chrono location
LIST(APPEND CMAKE_PREFIX_PATH "/home/chrono/chrono/build/cmake/")

set(Chrono_DIR "/home/chrono/chrono/build/cmake/")

set(CHRONO_DATA_DIR "/home/chrono/chrono/data")

find_package(Chrono
             COMPONENTS Irrlicht
             OPTIONAL_COMPONENTS PardisoMKL
             CONFIG)

find_package(ament_cmake REQUIRED)
find_package(orocos_kdl REQUIRED)


#--------------------------------------------------------------
# Return now if Chrono or a required component was not found.
#--------------------------------------------------------------
if (NOT Chrono_FOUND)
  message("Could not find Chrono or one of its required modules")
  return()
endif()

set(CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD ${CHRONO_CXX_STANDARD})

include_directories(${CHRONO_INCLUDE_DIRS})

#--------------------------------------------------------------
# Tweaks to disable some warnings with MSVC
#--------------------------------------------------------------
if(MSVC)
    add_definitions("-D_CRT_SECURE_NO_DEPRECATE")  # avoids deprecation warnings
    add_definitions("-D_SCL_SECURE_NO_DEPRECATE")  # avoids deprecation warnings
    add_definitions( "-DNOMINMAX" )                # do not use MSVC's min/max macros
endif()

add_executable(my_demo src/my_example.cpp)

#--------------------------------------------------------------
# Set properties for the executable target
#--------------------------------------------------------------

# Here, we define a macro CHRONO_DATA_DIR which will contain the
# path to the Chrono data directory, either in its source tree
# (if using a build version of Chrono), or in its install tree
# (if using an installed version of Chrono).
target_compile_definitions(my_demo PRIVATE "CHRONO_DATA_DIR=\"${CHRONO_DATA_DIR}\"") 

if(MSVC)
    set_target_properties(my_demo PROPERTIES MSVC_RUNTIME_LIBRARY ${CHRONO_MSVC_RUNTIME_LIBRARY})
endif()

#--------------------------------------------------------------
# Link to Chrono targets for the requested modules
#--------------------------------------------------------------

target_link_libraries(my_demo PRIVATE ${CHRONO_TARGETS})


ament_package()