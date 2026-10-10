# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/ponchik/Documents/projects/CPP/voxel-game/build_c/_deps/wgpu-linux-x86_64-release-src")
  file(MAKE_DIRECTORY "/home/ponchik/Documents/projects/CPP/voxel-game/build_c/_deps/wgpu-linux-x86_64-release-src")
endif()
file(MAKE_DIRECTORY
  "/home/ponchik/Documents/projects/CPP/voxel-game/build_c/_deps/wgpu-linux-x86_64-release-build"
  "/home/ponchik/Documents/projects/CPP/voxel-game/build_c/_deps/wgpu-linux-x86_64-release-subbuild/wgpu-linux-x86_64-release-populate-prefix"
  "/home/ponchik/Documents/projects/CPP/voxel-game/build_c/_deps/wgpu-linux-x86_64-release-subbuild/wgpu-linux-x86_64-release-populate-prefix/tmp"
  "/home/ponchik/Documents/projects/CPP/voxel-game/build_c/_deps/wgpu-linux-x86_64-release-subbuild/wgpu-linux-x86_64-release-populate-prefix/src/wgpu-linux-x86_64-release-populate-stamp"
  "/home/ponchik/Documents/projects/CPP/voxel-game/build_c/_deps/wgpu-linux-x86_64-release-subbuild/wgpu-linux-x86_64-release-populate-prefix/src"
  "/home/ponchik/Documents/projects/CPP/voxel-game/build_c/_deps/wgpu-linux-x86_64-release-subbuild/wgpu-linux-x86_64-release-populate-prefix/src/wgpu-linux-x86_64-release-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/ponchik/Documents/projects/CPP/voxel-game/build_c/_deps/wgpu-linux-x86_64-release-subbuild/wgpu-linux-x86_64-release-populate-prefix/src/wgpu-linux-x86_64-release-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/ponchik/Documents/projects/CPP/voxel-game/build_c/_deps/wgpu-linux-x86_64-release-subbuild/wgpu-linux-x86_64-release-populate-prefix/src/wgpu-linux-x86_64-release-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
