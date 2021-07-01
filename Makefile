cc=clang-cl
mode=release
name=prog.scr
flags=-GS- -W4 -permissive- -Xclang -std=c11
libs=d3d11.lib dxgi.lib dxguid.lib d3dcompiler.lib user32.lib kernel32.lib Gdi32.lib shell32.lib Shcore.lib
link_flags=-subsystem:windows -entry:entry -nodefaultlib -out:$(name) $(libs)

ifeq ($(mode), release)
flags+=-DRELEASE_BUILD
flags+=-O2
else
flags+=-Od -Zi
link_flag+=-debug
endif

all: shaders main.c
	@$(cc) $(flags) main.c -link $(link_flags)
	@mt.exe -nologo -manifest main.exe.manifest -outputresource:"$(name)";#1

shaders: pixel_shader.h vertex_shader.h
pixel_shader.h vertex_shader.h: shaders.hlsl
ifeq ($(mode), release)
	@fxc -O3 -Fh pixel_shader.h -T ps_5_0 -E ps_main -nologo shaders.hlsl
	@fxc -O3 -Fh vertex_shader.h -T vs_5_0 -E vs_main -nologo shaders.hlsl
endif
