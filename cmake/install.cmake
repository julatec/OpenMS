if (WIN32)
	set(OPENMS_LIB_INSTALL_PATH "bin")
else()	## Linux & MacOS
	## variables we need to distinguish 32Bit/64Bit versions
	set(OPENMS_LIB_INSTALL_PATH "lib")
	if (OPENMS_64BIT_ARCHITECTURE)
		set(OPENMS_LIB_INSTALL_PATH "lib64")
	endif()
endif()
	
## CPack installation and packaging procedures
install(TARGETS OpenMS 
  LIBRARY DESTINATION ${OPENMS_LIB_INSTALL_PATH}
  ARCHIVE DESTINATION ${OPENMS_LIB_INSTALL_PATH}
	RUNTIME DESTINATION ${OPENMS_LIB_INSTALL_PATH}
  COMPONENT library)

## install utils
foreach(util ${UTILS_executables})
  install(TARGETS ${util}
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION bin
    COMPONENT applications)
endforeach()

## install TOPP Tools
foreach(TOPP_exe ${TOPP_executables})
  INSTALL(TARGETS ${TOPP_exe} 
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION bin
    COMPONENT applications)
endforeach()

## install share
INSTALL(DIRECTORY share/
  DESTINATION share
  COMPONENT share
  REGEX ".svn" EXCLUDE)

## install the documentation and the tutorials
install(FILES     ${PROJECT_BINARY_DIR}/doc/index.html      DESTINATION share/OpenMS/doc COMPONENT doc)
install(DIRECTORY ${PROJECT_BINARY_DIR}/doc/html            DESTINATION share/OpenMS/doc COMPONENT doc REGEX ".svn" EXCLUDE)
install(FILES ${PROJECT_BINARY_DIR}/doc/OpenMS_tutorial.pdf DESTINATION share/OpenMS/doc COMPONENT doc)
install(FILES ${PROJECT_BINARY_DIR}/doc/TOPP_tutorial.pdf   DESTINATION share/OpenMS/doc COMPONENT doc)

