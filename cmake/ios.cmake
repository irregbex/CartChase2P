# Native UIKit host: no desktop SDL, shell-based launcher, or engine changes.
enable_language(OBJC)
list(REMOVE_ITEM DKC1_ENGINE_SOURCES
    "${SNESRECOMP_ROOT}/runner/src/launcher.c"
    "${SNESRECOMP_ROOT}/runner/src/launcher_cache.c"
    "${SNESRECOMP_ROOT}/runner/src/launcher_picker.c")

option(DKC1_IOS_DIAGNOSTICS "Include the existing headless runner for simulator QA" OFF)
add_executable(dkc1_ios MACOSX_BUNDLE
    ${DKC1_ENGINE_SOURCES}
    ${DKC1_SNESRECOMP_GEN_SOURCES}
    ${DKC1_RUNNER_SOURCES}
    runner/headless_host.c
    assets/ios/Assets.xcassets
    runner/ios/ios_main.m
    runner/ios/ios_graphics.m
    runner/macos_graphics.m
    runner/desktop_graphics.c
    runner/desktop_crt.c
    runner/ios/ios_audio_ring.c
    runner/ios/ios_viewport.c
    runner/ios/ios_touch_button.c)
if(DKC1_IOS_DIAGNOSTICS)
    target_sources(dkc1_ios PRIVATE runner/headless_main.c tests/test_macos_graphics.m)
    set_source_files_properties(runner/headless_main.c PROPERTIES
        COMPILE_DEFINITIONS "main=Dkc1IOSHeadlessMain")
    set_source_files_properties(tests/test_macos_graphics.m PROPERTIES
        COMPILE_DEFINITIONS "main=Dkc1IOSGraphicsTestMain"
        COMPILE_OPTIONS "-fno-objc-arc")
    target_compile_definitions(dkc1_ios PRIVATE DKC1_IOS_DIAGNOSTICS=1)
endif()
target_include_directories(dkc1_ios PRIVATE runner recomp
    ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
target_compile_definitions(dkc1_ios PRIVATE
    SNESRECOMP_TRACE=0 SNESRECOMP_REVERSE_DEBUG=0
    SNESRECOMP_EXTERNAL_RAM_ROUTINE_GUARDS=1 SYSTEM_VOLUME_MIXER_AVAILABLE=0
    DKC1_BUILD_COMMIT="${DKC1_BUILD_COMMIT}" DKC1_BUILD_CONFIG="ios")
target_compile_options(dkc1_ios PRIVATE
    $<$<COMPILE_LANGUAGE:OBJC>:-fobjc-arc>
    $<$<COMPILE_LANGUAGE:C>:-Wno-unused-function>)
target_link_libraries(dkc1_ios PRIVATE ${SNESRECOMP_RUNNER_LIBRARIES}
    "-framework UIKit" "-framework Foundation" "-framework CoreGraphics"
    "-framework QuartzCore" "-framework Metal" "-framework AVFoundation"
    "-framework GameController" "-framework UniformTypeIdentifiers")
set_target_properties(dkc1_ios PROPERTIES
    OUTPUT_NAME DKC1Recomp
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/assets/ios/Info.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.flat2vr.dkc1recomp.ios"
    MACOSX_BUNDLE_BUNDLE_NAME DKC1Recomp
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_SUPPORTS_MACCATALYST NO
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME AppIcon
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.flat2vr.dkc1recomp.ios")
set_source_files_properties(assets/ios/Assets.xcassets PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)
set_source_files_properties(runner/macos_graphics.m PROPERTIES
    COMPILE_OPTIONS "-fno-objc-arc")
# Xcode treats .metal resources as compiler inputs. Copy the exact source with
# a text extension so the shared renderer can compile it at runtime on iOS.
configure_file(runner/macos_graphics.metal
    "${CMAKE_CURRENT_BINARY_DIR}/ios_upscaling.txt" COPYONLY)
target_sources(dkc1_ios PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/ios_upscaling.txt")
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/ios_upscaling.txt" PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)

foreach(notice IN ITEMS LICENSE THIRD_PARTY_NOTICES.md snesrecomp/LICENSE
        snesrecomp/THIRD_PARTY_ATTRIBUTION.md)
    # Preserve both projects' identically named LICENSE files.
    string(REPLACE "/" "-" notice_name "${notice}")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${notice}"
        "${CMAKE_CURRENT_BINARY_DIR}/Licenses/${notice_name}" COPYONLY)
    target_sources(dkc1_ios PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Licenses/${notice_name}")
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/Licenses/${notice_name}"
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources/Licenses)
endforeach()
