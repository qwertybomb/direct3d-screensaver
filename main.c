#include <stdint.h>
#include <stdbool.h>

#pragma warning(push, 0)
#define UNICODE
#include <Windows.h>
#include <shellscalingapi.h>
#undef UNICODE

#include <dxgi.h>
#include <d3d11_4.h>
#include <d3dcompiler.h>
#pragma warning(pop)

#ifdef RELEASE_BUILD
static
#include "pixel_shader.h"

static
#include "vertex_shader.h"
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define REAL_MSVC
#endif

#ifdef REAL_MSVC
#pragma function(memset)
#endif
void *memset(void *dest, int c, size_t count)
{
    char *bytes = (char *)dest;
    while (count-- != 0)
    {
        *bytes++ = (char)c;
    }
    return dest;
}

static int iabs(int const value) { return value < 0 ? -value : value; }

extern int _fltused;
int _fltused;

#define WAKE_THRESHOLD 4
#define BLACK_WINDOW_CLASS L"black_window_class"

// force our struct's size to be a multiple of 16 bytes
#pragma pack(push, 16)
typedef __declspec(align(16)) struct
{
    float aspect_ratio;
    float timer;
    float pixel_width;
} ShaderConstants;
#pragma pack(pop)

typedef enum
{
    PREVIEW_MODE,
    DIALOG_MODE,
    FULLSCREEN_MODE,
    NOTHING_MODE,
    WINDOW_MODE,
} ModeType;

typedef struct
{
    HWND window_handle;
    ID3D11Device *device;
    ID3D11DeviceContext *device_context;
    
    IDXGISwapChain1 *swap_chain;
    
    ID3D11RenderTargetView *frame_buffer_view;
    
    ID3D11VertexShader *vertex_shader;
    ID3D11PixelShader *pixel_shader;
    
    ID3D11Buffer *constant_buffer;

    int width;
    int height;
} State;

__declspec(dllexport) unsigned long NvOptimusEnablement = 0x0000001;

static void state_handle_resize(State *const this,
                                int const new_width,
                                int const new_height)
{
    if ((new_width    ==          0 || new_height   ==  0) ||
        (this->width  ==  new_width && this->height == new_height))
    {
        this->width = new_width;
        this->height = new_height;
        return; 
    }
    
    this->width = new_width;
    this->height = new_height;
    
     // unbind render target
     this->device_context->lpVtbl->OMSetRenderTargets(this->device_context, 0,
                                                      &(ID3D11RenderTargetView *) {NULL}, NULL);
     
     // release frame_buffer view
     this->frame_buffer_view->lpVtbl->Release(this->frame_buffer_view);
     
     // let things finish
     this->device_context->lpVtbl->Flush(this->device_context);
     
     this->swap_chain->lpVtbl->ResizeBuffers(this->swap_chain, 0,
                                             (int unsigned) this->width,
                                             (int unsigned) this->height,
                                             DXGI_FORMAT_UNKNOWN, 0);

     ID3D11Texture2D *window_buffer;
     this->swap_chain->lpVtbl->GetBuffer(this->swap_chain, 0,
                                         &IID_ID3D11Texture2D,
                                         (void **) &window_buffer);
     
     this->device->lpVtbl->CreateRenderTargetView(this->device,
                                                  (ID3D11Resource *)window_buffer,
                                                  NULL, &this->frame_buffer_view);

     // release the buffer
     window_buffer->lpVtbl->Release(window_buffer);

     this->device_context->lpVtbl->OMSetRenderTargets(this->device_context, 1,
                                                      &this->frame_buffer_view, NULL);

         
    // get the size of the portion of the window that we can draw to
    this->device_context->lpVtbl->RSSetViewports(this->device_context, 1,
                                                 &(D3D11_VIEWPORT) {
                                                     .Width = (float)this->width,
                                                     .Height = (float)this->height,
                                                     .MinDepth = 0.0f,
                                                     .MaxDepth = 1.0f,
                                                 });
}

static LRESULT __stdcall WindowProc(HWND const window_handle, UINT const message,
                                    WPARAM const wParam, LPARAM const lParam)
{
    switch (message)
    {
        case WM_SYSCHAR:
        case WM_SYSKEYDOWN:
        {
            // check for alt-f4
            // if the 29-th bit is set the alt is key was pressed
            if (((lParam >> 29) & 0x1) != 0 && wParam == VK_F4)
            {
                PostQuitMessage(0);
            }
            break;
        }
        
        case WM_QUIT:
        case WM_CLOSE:
        case WM_DESTROY:
        {
            PostQuitMessage(0);
            break;
        }
        
        default:
        {
            return DefWindowProcW(window_handle, message, wParam, lParam);
        }
    }
    
    return 0;
}


static LRESULT __stdcall ChildWindowProc(HWND const hWnd, UINT const message,
                                         WPARAM const wParam, LPARAM const lParam)
{
    switch (message)
    {        
        case WM_QUIT:
        case WM_CLOSE:
        case WM_DESTROY:
        {
            PostQuitMessage(0);
            break;
        }
        
        default:
        {
            return DefWindowProcW(hWnd, message, wParam, lParam);
        }
    }

    return 0;
}

static LRESULT __stdcall ScreenSaverProc(HWND const hWnd, UINT const message,
                                         WPARAM const wParam, LPARAM const lParam)
{
    static POINT old_mouse_pos;
    static bool is_first_time = true;
    
    switch (message)
    {   
        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        {
            PostQuitMessage(0);
            break;
        }

        case WM_MOUSEMOVE:
        {
            if (is_first_time)
            {
                GetCursorPos(&old_mouse_pos);
                is_first_time = false;
                break;
            }
            
            POINT current_mouse_pos;
            GetCursorPos(&current_mouse_pos);

            POINT const mouse_movement = {
                iabs(current_mouse_pos.x - old_mouse_pos.x),
                iabs(current_mouse_pos.y - old_mouse_pos.y)               
            };

            // only quit if the mouse movement has gone above a threshold
            if (mouse_movement.x + mouse_movement.y > WAKE_THRESHOLD)
            {
                PostQuitMessage(0);
                old_mouse_pos = current_mouse_pos;
            }

            break;
        }

        case WM_SETCURSOR:
        {
            // hide the cursor
            SetCursor(NULL);
            return TRUE;
        }


        case WM_QUIT:
        case WM_CLOSE:
        case WM_DESTROY:
        {
            PostQuitMessage(0);
            break;
        }

        default:
        {
            return DefWindowProcW(hWnd, message, wParam, lParam);
        }
    }
    
    return 0;
}

static void state_create_window(State *const this,
                                int const width,
                                int const height,
                                ModeType const mode,
                                uint32_t const hwnd_param)
{
    HMODULE const instance_handle = GetModuleHandleW(NULL);
    
    WNDCLASSW window_class = {
        .style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC,
        .hInstance = instance_handle,
        .lpszClassName = L"window_class",
        .hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH),
    };

    DWORD window_style = 0;
    HWND window_handle = NULL;
    WNDPROC window_proc = &ScreenSaverProc;
    wchar_t const *window_title = NULL;
    DWORD window_extra_style = WS_EX_NOREDIRECTIONBITMAP;
   
    switch (mode)
    {
        case WINDOW_MODE:
        {   
            this->width = width;
            this->height = height;

            window_proc = &WindowProc;
            window_title = L"normal window";
            window_style = WS_OVERLAPPEDWINDOW;
            window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
            break;
        }

        case FULLSCREEN_MODE:
        {   
            this->width = GetSystemMetrics(SM_CXSCREEN);
            this->height = GetSystemMetrics(SM_CYSCREEN);

            window_title = L"fullscreen window";
            window_style = WS_POPUP | WS_VISIBLE;
            window_extra_style |= WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
            break;
        }

        case PREVIEW_MODE:
        {
            window_handle = (HWND)(uintptr_t)hwnd_param;

            RECT rect;
            GetClientRect(window_handle, &rect);
            
            this->width = rect.right - rect.left;
            this->height = rect.bottom - rect.top;

            window_proc = &ChildWindowProc;
            window_title = L"preview window";
            window_style = WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN;
            window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);            
            break;
        }

        default: break;
    }

    window_class.lpfnWndProc = window_proc;
    
    RegisterClassW(&window_class);
    this->window_handle = CreateWindowExW(window_extra_style,
                                          window_class.lpszClassName,
                                          window_title, window_style,
                                          0, 0, this->width, this->height,
                                          window_handle, NULL,
                                          instance_handle, NULL);

    ShowWindow(this->window_handle, SW_SHOWDEFAULT);
}

static void state_setup_d3d(State *const this, bool is_windowed)
{
    D3D_FEATURE_LEVEL const feature_levels[] = {D3D_FEATURE_LEVEL_11_1};
    
    UINT creation_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        
#ifndef RELEASE_BUILD
    creation_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    
    D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE,
                      NULL, creation_flags, feature_levels,
                      sizeof(feature_levels) / sizeof(*feature_levels),
                      D3D11_SDK_VERSION, &this->device,
                      NULL, &this->device_context);
    
    IDXGIFactory2 *dxgi_factory;
    {
        IDXGIDevice *dxgi_device;
        this->device->lpVtbl->QueryInterface(this->device,
                                             &IID_IDXGIDevice,
                                             (void **)&dxgi_device);
        
        IDXGIAdapter *dxgi_adapter;
        dxgi_device->lpVtbl->GetAdapter(dxgi_device, &dxgi_adapter);
        
        dxgi_adapter->lpVtbl->GetParent(dxgi_adapter,
                                        &IID_IDXGIFactory,
                                        (void **)&dxgi_factory);

        dxgi_adapter->lpVtbl->Release(dxgi_adapter);
        dxgi_device->lpVtbl->Release(dxgi_device);
    }
    
    dxgi_factory->lpVtbl->CreateSwapChainForHwnd(dxgi_factory, (IUnknown *) this->device,
                                                 this->window_handle,
                                                 &(DXGI_SWAP_CHAIN_DESC1) {
                                                     .Width = 0, // use window width
                                                     .Height = 0, // use window height
                                                     .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                                                     .Stereo = FALSE,
                                                     .SampleDesc.Count = 1,
                                                     .SampleDesc.Quality = 0,
                                                     .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
                                                     .BufferCount = 2,
                                                     .Scaling = DXGI_SCALING_NONE,
                                                     .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
                                                     .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED
                                                 }, NULL, NULL, &this->swap_chain);

    dxgi_factory->lpVtbl->Release(dxgi_factory);
    
    if (!is_windowed)
    {
        this->swap_chain->lpVtbl->SetFullscreenState(this->swap_chain, false, NULL);
    }
    
    ID3D11Texture2D *frame_buffer;
    this->swap_chain->lpVtbl->GetBuffer(this->swap_chain, 0,
                                        &IID_ID3D11Texture2D,
                                        (void**)&frame_buffer);
    
    this->device->lpVtbl->CreateRenderTargetView(this->device,
                                                 (ID3D11Resource*)frame_buffer,
                                                 NULL, &this->frame_buffer_view);

    frame_buffer->lpVtbl->Release(frame_buffer);

#ifndef RELEASE_BUILD
    HRESULT result;
    ID3DBlob *error_blob;
    ID3DBlob *vertex_shader_blob;
    
    result = D3DCompileFromFile(L"shaders.hlsl", NULL,
                                D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                "vs_main", "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0,
                                &vertex_shader_blob, &error_blob);
    
    if (FAILED(result))
    {
        MessageBoxA(this->window_handle,
                    error_blob->lpVtbl->GetBufferPointer(error_blob), "error:", MB_OK);
        ExitProcess(1);
    }
#endif

#ifndef RELEASE_BUILD
    void *const vertex_shader_blob_buffer_pointer =
        vertex_shader_blob->lpVtbl->GetBufferPointer(vertex_shader_blob);
    
    size_t const vertex_shader_blob_buffer_size =
        vertex_shader_blob->lpVtbl->GetBufferSize(vertex_shader_blob);
    
    this->device->lpVtbl->CreateVertexShader(this->device,
                                             vertex_shader_blob_buffer_pointer,
                                             vertex_shader_blob_buffer_size,
                                             NULL, &this->vertex_shader);
#else
    this->device->lpVtbl->CreateVertexShader(this->device,
                                             g_vs_main, sizeof g_vs_main,
                                             NULL, &this->vertex_shader);
#endif
        
#ifndef RELEASE_BUILD
    ID3DBlob *pixel_shader_blob;
    result =  D3DCompileFromFile(L"shaders.hlsl", NULL,
                                 D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                 "ps_main", "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0,
                                 &pixel_shader_blob, &error_blob);

    if (FAILED(result))
    {
        MessageBoxA(this->window_handle,
                    error_blob->lpVtbl->GetBufferPointer(error_blob), "error:", MB_OK);
        ExitProcess(1);
    }
    
    this->device->lpVtbl->CreatePixelShader(this->device,
                                            pixel_shader_blob->lpVtbl->GetBufferPointer(pixel_shader_blob),
                                            pixel_shader_blob->lpVtbl->GetBufferSize(pixel_shader_blob),
                                            NULL, &this->pixel_shader);
#else
    this->device->lpVtbl->CreatePixelShader(this->device,
                                            g_ps_main, sizeof g_ps_main,
                                            NULL, &this->pixel_shader);
#endif
 
#ifndef RELEASE_BUILD
    // release shader blobs
    pixel_shader_blob->lpVtbl->Release(pixel_shader_blob);
    vertex_shader_blob->lpVtbl->Release(vertex_shader_blob);
#endif
    
    this->device->lpVtbl->CreateBuffer(this->device,
                                       &(D3D11_BUFFER_DESC) {
                                           .ByteWidth = (int unsigned) sizeof(ShaderConstants),
                                           .Usage = D3D11_USAGE_DYNAMIC,
                                           .BindFlags  = D3D11_BIND_CONSTANT_BUFFER,
                                           .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE
                                       }, NULL, &this->constant_buffer);
         
    // get the size of the portion of the window that we can draw to
    this->device_context->lpVtbl->RSSetViewports(this->device_context, 1,
                                                 &(D3D11_VIEWPORT) {
                                                     .Width = (float)this->width,
                                                     .Height = (float)this->height,
                                                     .MinDepth = 0.0f,
                                                     .MaxDepth = 1.0f,
                                                 });
}

static void state_draw(State *const this)
{       
    this->device_context->lpVtbl->OMSetRenderTargets(this->device_context, 1,
                                                     &this->frame_buffer_view, NULL);
    
    // clear background color to black
    this->device_context->lpVtbl->ClearRenderTargetView(this->device_context,
                                                        this->frame_buffer_view,
                                                        (float[4]) {[3] = 1.0f});

    
    this->device_context->lpVtbl->IASetPrimitiveTopology(this->device_context,
                                                         D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    
    this->device_context->lpVtbl->VSSetShader(this->device_context, this->vertex_shader, NULL, 0);
    this->device_context->lpVtbl->PSSetShader(this->device_context, this->pixel_shader, NULL, 0);
    
    this->device_context->lpVtbl->PSSetConstantBuffers(this->device_context, 0,
                                                       1, &this->constant_buffer);
    
    // draw the shaders and swap the front/back buffer
    this->device_context->lpVtbl->Draw(this->device_context, 4, 0);
    this->swap_chain->lpVtbl->Present(this->swap_chain, 1, 0);
}

#ifndef RELEASE_BUILD
static bool compare_file_times(FILETIME const a, FILETIME const b)
{
    return
        a.dwLowDateTime == b.dwLowDateTime &&
        a.dwHighDateTime == b.dwHighDateTime;
}

static void state_reload_shader(State *const this,
                                WIN32_FILE_ATTRIBUTE_DATA *const old_file_attribute_data)
{
    WIN32_FILE_ATTRIBUTE_DATA new_file_attribute_data;
    GetFileAttributesExW(L"shaders.hlsl",
                         GetFileExInfoStandard,
                         &new_file_attribute_data);

    // only reload the shader if the shader has been written to 
    if (compare_file_times(new_file_attribute_data.ftLastWriteTime,
                           old_file_attribute_data->ftLastWriteTime))
    {
        return;
    }


    ID3DBlob *error_blob = NULL;
    ID3DBlob *pixel_shader_blob = NULL;

    for(;;)
    {
        HRESULT result = D3DCompileFromFile(L"shaders.hlsl", NULL,
                                            D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                            "ps_main", "ps_5_0",
                                            D3DCOMPILE_ENABLE_STRICTNESS, 0,
                                            &pixel_shader_blob, &error_blob);
        // if the file is in use try again
        if ((result & 0x0000FFFF) == ERROR_SHARING_VIOLATION)
        {
            continue;
        }
        else if (FAILED(result))
        {
            MessageBoxA(this->window_handle,
                        error_blob == NULL ?
                        "" : error_blob->lpVtbl->GetBufferPointer(error_blob),
                        "error:", MB_OK);
            
            error_blob->lpVtbl->Release(error_blob);
        
            goto leave;
        }

        break;
    }
        
    this->pixel_shader->lpVtbl->Release(this->pixel_shader);
    this->device->lpVtbl->CreatePixelShader(this->device,
                                            pixel_shader_blob->lpVtbl->GetBufferPointer(pixel_shader_blob),
                                            pixel_shader_blob->lpVtbl->GetBufferSize(pixel_shader_blob),
                                            NULL, &this->pixel_shader);

    pixel_shader_blob->lpVtbl->Release(pixel_shader_blob);

leave:
    old_file_attribute_data->ftLastWriteTime = new_file_attribute_data.ftLastWriteTime;
}
#endif

static DWORD __stdcall render_thread(void *const context)
{
    State *const state = context;

    LARGE_INTEGER performance_frequency;
    QueryPerformanceFrequency(&performance_frequency);

    LARGE_INTEGER start_counter;
    QueryPerformanceCounter(&start_counter);

    WIN32_FILE_ATTRIBUTE_DATA old_file_attribute_data;
    GetFileAttributesExW(L"shaders.hlsl",
                         GetFileExInfoStandard,
                         &old_file_attribute_data);

    for(;;)
    {
        LARGE_INTEGER current_counter;
        QueryPerformanceCounter(&current_counter);
        
        int64_t const counter_duration =
            current_counter.QuadPart - start_counter.QuadPart; 
        
        float const current_time =
            (float)((double)counter_duration / (double)performance_frequency.QuadPart);

        {
            RECT rect;
            GetClientRect(state->window_handle, &rect);

            state_handle_resize(state,
                                rect.right - rect.left,
                                rect.bottom - rect.top);
        }
        
        // update shader constants
        {
            
            D3D11_MAPPED_SUBRESOURCE mapped_subresource;
            state->device_context->lpVtbl->Map(state->device_context,
                                               (ID3D11Resource *)state->constant_buffer, 0,
                                               D3D11_MAP_WRITE_DISCARD, 0, &mapped_subresource);
            
            ShaderConstants *const shader_constants = mapped_subresource.pData;
            
            shader_constants->aspect_ratio = (float)state->height / (float)state->width;
            shader_constants->timer = current_time;
            shader_constants->pixel_width = 1.0f / (float)state->height;
            
            state->device_context->lpVtbl->Unmap(state->device_context,
                                                 (ID3D11Resource *)
                                                 state->constant_buffer, 0);
        }
        
        state_draw(state);

#ifndef RELEASE_BUILD
        state_reload_shader(state, &old_file_attribute_data);
#endif
    }
}

static uint32_t parse_u32(wchar_t const *string)
{
    uint32_t result = 0;
    while (*string != L\0')
    {
        result *= 10;
        result += *string - L'0';
        ++string;
    }

    return result;
}

void entry(void);
void entry(void)
{
    int argc;
    wchar_t **const argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    if (argc < 2) return;

    uint32_t argument_param = 0;
    ModeType mode = NOTHING_MODE;
    for (int i = 1; i < argc; ++i)
    {
        if (argv[i][0] != L'/' && argv[i][0] != L'-') continue;

        wchar_t const *const argument = argv[i] + 1;
        switch(*argument)
        {
            case L'S':
            case L's':
            {
                if (argument[1] == L'\0')
                {
                    mode = FULLSCREEN_MODE;
                }

                break;
            }

            case L'C':
            case L'c':
            {
                mode = DIALOG_MODE;
                break;
            }

            case L'p':
            case L'P':
            {
                if (argument[1] != L'\0')
                {
                    argument_param = parse_u32(argument + 1);
                }
                else if (i + 1  < argc)
                {
                    argument_param = parse_u32(argv[i + 1]);
                }
                else
                {
                    break;
                }

                mode = PREVIEW_MODE;
                break;
            }

            case L'w':
            case L'W':
            {
                mode = WINDOW_MODE;
                break;
            }
        }
    }

    if (mode == NOTHING_MODE || mode == DIALOG_MODE)
    {
        return;
    }

    if (mode == FULLSCREEN_MODE)
    {
        RegisterClassW(&(WNDCLASS)
                       {
                           .lpszClassName = BLACK_WINDOW_CLASS,
                           .lpfnWndProc = &ScreenSaverProc,
                           .hInstance = GetModuleHandleW(NULL),
                           .hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH),
                       });

        ShowWindow(CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                                   BLACK_WINDOW_CLASS, L"",
                                   WS_POPUP | WS_VISIBLE,
                                   GetSystemMetrics(SM_XVIRTUALSCREEN),
                                   GetSystemMetrics(SM_YVIRTUALSCREEN),
                                   GetSystemMetrics(SM_CXVIRTUALSCREEN),
                                   GetSystemMetrics(SM_CYVIRTUALSCREEN),
                                   NULL, NULL, GetModuleHandleW(NULL), NULL), SW_SHOW);
 
    }

    State state = {0}; 
    state_create_window(&state, 900, 600, mode, argument_param);
    state_setup_d3d(&state, mode != FULLSCREEN_MODE);
        
    // create a separate render thread so the rendering is not blocked by the Message Pump
    CreateThread(NULL, 0, &render_thread, &state, 0, NULL);
    
    // start the message pump
    for (;;)
    {
        MSG message;
        if (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);

            if (message.message == WM_QUIT)
            {
                break;
            }

            continue;
        }
    }
    
    ExitProcess(0);
}
