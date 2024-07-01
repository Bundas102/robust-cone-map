add_falcor_executable(Parallax)

target_sources(Parallax PRIVATE
	Parallax.cpp
	Parallax.h
	
	ComputeProgramWrapper.cpp
	ComputeProgramWrapper.h

    ParallaxPixelDebug/ParallaxPixelDebug.h
    ParallaxPixelDebug/ParallaxPixelDebug.cpp
    ParallaxPixelDebug/ParallaxPixelDebug.slangh
    ParallaxPixelDebug/ParallaxDebug.ps.slang

	Conemap.cs.slang
	FindIntersection.slang
	MaxMip2d.cs.slang
	Minmax.cs.slang
    MaxMips.cs.slang
	Parallax.ps.slang
	Parallax.vs.slang
	ProceduralHeightmap.cs.slang
	QuickConemap.cs.slang
	Refinement.slang
	TextureCopy.cs.slang
    IntersectBilinearPatch.slang

    3rdparty/implot/implot.h
    3rdparty/implot/implot.cpp
    3rdparty/implot/implot_internal.h
    3rdparty/implot/implot_items.cpp
)

target_compile_definitions(Parallax PRIVATE $<$<PLATFORM_ID:Windows>:IMGUI_API=__declspec\(dllimport\)> )
set_target_properties(Parallax PROPERTIES VS_GLOBAL_VcpkgEnabled "false")
target_include_directories(Parallax PRIVATE 3rdparty/implot)

target_copy_shaders(Parallax Samples/Parallax)

add_custom_command(TARGET Parallax PRE_BUILD 
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data ${FALCOR_OUTPUT_DIRECTORY}/data )

target_source_group(Parallax "Samples")
