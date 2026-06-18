##============================================================================
##  Copyright (c) Kitware, Inc.
##  All rights reserved.
##  See LICENSE.txt for details.
##
##  This software is distributed WITHOUT ANY WARRANTY; without even
##  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
##  PURPOSE.  See the above copyright notice for more information.
##============================================================================

cmake_minimum_required(VERSION 3.0 FATAL_ERROR)

set(version 1.13.2)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  cmake_host_system_information(RESULT host_arch QUERY OS_PLATFORM)
  if(host_arch STREQUAL "aarch64")
    set(sha256sum fd2cacc8050a7f12a16a2e48f9e06fca5c14fc4c2bee2babb67b58be17a607fc)
    set(platform linux-aarch64)
  elseif(host_arch STREQUAL "x86_64")
    set(sha256sum 5749cbc4e668273514150a80e387a957f933c6ed3f5f11e03fb30955e2bbead6)
    set(platform linux)
  else()
    message(FATAL_ERROR "Unrecognized platform ${host_arch}")
  endif()
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
  set(sha256sum 21915277db59756bfc61f6f281c1f5e3897760b63776fd3d360f77dd7364137f)
  set(platform mac)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
  set(sha256sum 07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65)
  set(platform win)
else()
  message(FATAL_ERROR "Unrecognized platform ${CMAKE_HOST_SYSTEM_NAME}")
endif()

set(tarball "ninja-${platform}.zip")

file(DOWNLOAD
  "https://github.com/ninja-build/ninja/releases/download/v${version}/${tarball}" .gitlab/${tarball}
  EXPECTED_HASH SHA256=${sha256sum}
  SHOW_PROGRESS
  )

execute_process(
  COMMAND ${CMAKE_COMMAND} -E tar xf ${tarball}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/.gitlab
  RESULT_VARIABLE extract_results
  )

if(extract_results)
  message(FATAL_ERROR "Extracting `${tarball}` failed: ${extract_results}.")
endif()
