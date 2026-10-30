# Respect explicit Qt locations; otherwise use the local dependency or NineSlice's copy.
if(WIN32 AND NOT Qt6_DIR AND NOT CMAKE_PREFIX_PATH AND NOT DEFINED ENV{CMAKE_PREFIX_PATH})
  foreach(QT_CANDIDATE IN ITEMS
      "${CMAKE_SOURCE_DIR}/.deps/qt/${KANTAN_QT_VERSION}/msvc2022_64"
      "${CMAKE_SOURCE_DIR}/../KantanNineSlice/.deps/qt/${KANTAN_QT_VERSION}/msvc2022_64")
    if(EXISTS "${QT_CANDIDATE}/lib/cmake/Qt6/Qt6Config.cmake")
      get_filename_component(QT_CANDIDATE "${QT_CANDIDATE}" ABSOLUTE)
      list(PREPEND CMAKE_PREFIX_PATH "${QT_CANDIDATE}")
      break()
    endif()
  endforeach()
endif()
