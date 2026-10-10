# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/ponchik/Documents/projects/CPP/voxel-game/build_c/_deps/webgpu-distribution-src")
  file(MAKE_DIRECTORY "/home/ponchik/Documents/projects/CPP/voxel-game/build_c/_deps/webgpu-distribution-src")
endif()
file(MAKE_DIRECTORY
  "/home/ponchik/Documents/projects/CPP/voxel-game/build_c/_deps/webgpu-distribution-build"
  "/home/ponchik/Documents/projects/CPP/voxel-game/build_c/_deps/webgpu-distribution-subbuild/webgpu-distribution-populate-prefix"
  "/home/ponchik/Documents/projects/CPP/voxel-game/build_c/_deps/webgpu-distribution-subbuild/webgpu-distribution-populate-prefix/tmp"
  "/home/ponchik/Documents/projects/CPP/voxel-game/build_c/_deps/webgpu-distribution-subbuild/webgpu-distribution-populate-prefix/src/webgpu-distribution-populate-stamp"
  "/home/ponchik/Documents/projects/CPP/voxel-game/build_c/_deps/webgpu-distribution-subbuild/webgpu-distribution-populate-prefix/src"
  "/home/ponchik/Documents/projects/CPP/voxel-game/build_c/_deps/webgpu-distribution-subbuild/webgpu-distribution-populate-prefix/src/webgpu-distribution-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/ponchik/Documents/projects/CPP/voxel-game/build_c/_deps/webgpu-distribution-subbuild/webgpu-distribution-populate-prefix/src/webgpu-distribution-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/ponchik/Documents/projects/CPP/voxel-game/build_c/_deps/webgpu-distribution-subbuild/webgpu-distribution-populate-prefix/src/webgpu-distribution-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
