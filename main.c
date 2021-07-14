#include <stdint.h>
#include <stdbool.h>

#pragma warning(push, 0)
#define UNICODE
#include <Windows.h>

#define COBJMACROS
#include <dxgi.h>
#include <d3d11_4.h>
#include <d3dcompiler.h>
#pragma warning(pop)

#ifdef RELEASE_BUILD
static
#include "pixel_shader.h"

static
#include "post_pixel_shader.h"

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
#define ARRAY_COUNT(...) (sizeof((__VA_ARGS__)) / sizeof(*(__VA_ARGS__)))

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
    ID3D11Texture2D *texture;
    ID3D11RenderTargetView *texture_view;
    ID3D11ShaderResourceView *texture_shader_view; 
} RenderTexture;

typedef struct
{
    HWND window_handle;
    ID3D11Device *device;
    ID3D11DeviceContext *device_context;
    
    IDXGISwapChain2 *swap_chain;
    
    ID3D11RenderTargetView *frame_buffer_view;
    
    ID3D11VertexShader *vertex_shader;
    ID3D11PixelShader *pixel_shader;
    ID3D11PixelShader *post_pixel_shader;
    
    ID3D11Buffer *constant_buffer;

    RenderTexture render_textures[2];
    ID3D11SamplerState *render_texture_sampler;

    HANDLE frame_latency_waitable_object;
    
    int width;
    int height;
} State;


static void get_client_size(HWND const window,
                            int *const x, int *const y)
{
    RECT rect;
    GetClientRect(window, &rect);

    *x = rect.right - rect.left;
    *y = rect.bottom - rect.top;
}

__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;


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

    // adjust width and height to actual client size
    get_client_size(this->window_handle, &this->width, &this->height);
    
    ShowWindow(this->window_handle, SW_SHOWDEFAULT);
}

static void state_create_d3d_textures(State *const this)
{
    for (int i = 0; i < 2; ++i)
    {
        this->device->lpVtbl->CreateTexture2D(this->device,
                                              &(D3D11_TEXTURE2D_DESC)
                                              {
                                                  .Width = this->width,
                                                  .Height = this->height,
                                                  .MipLevels = 1,
                                                  .ArraySize = 1,
                                                  .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                                                  .SampleDesc.Count = 1,
                                                  .Usage = D3D11_USAGE_DEFAULT,
                                                  .BindFlags = (D3D11_BIND_RENDER_TARGET |
                                                                D3D11_BIND_SHADER_RESOURCE),
                                              }, NULL,
                                              &this->render_textures[i].texture);

        ID3D11Device_CreateRenderTargetView(this->device,
                                            (ID3D11Resource*)this->render_textures[i].texture,
                                            NULL, &this->render_textures[i].texture_view);
        
        ID3D11Device_CreateShaderResourceView(this->device,
                                              (ID3D11Resource*)this->render_textures[i].texture,
                                              NULL, &this->render_textures[i].texture_shader_view);
    }
}

static void state_destroy_d3d_textures(State *const this)
{
    for (int i = 0; i < 2; ++i)
    {
        ID3D11RenderTargetView_Release(this->render_textures[i].texture_view);
        ID3D11ShaderResourceView_Release(this->render_textures[i].texture_shader_view);
        ID3D11Texture2D_Release(this->render_textures[i].texture);
    }
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
                      ARRAY_COUNT(feature_levels),
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
                                        &IID_IDXGIFactory2,
                                        (void **)&dxgi_factory);

        dxgi_adapter->lpVtbl->Release(dxgi_adapter);
        dxgi_device->lpVtbl->Release(dxgi_device);
    }

    IDXGISwapChain1 *swap_chain1;
    IDXGIFactory2_CreateSwapChainForHwnd(dxgi_factory, (IUnknown *) this->device,
                                         this->window_handle,
                                         (&(DXGI_SWAP_CHAIN_DESC1)
                                          {
                                              .Width = 0, // use window width
                                              .Height = 0, // use window height
                                              .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                                              .Stereo = FALSE,
                                              .SampleDesc.Count = 1,
                                              .SampleDesc.Quality = 0,
                                              .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
                                              .BufferCount = 2,
                                              .Scaling = DXGI_SCALING_STRETCH,
                                              .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
                                              .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
                                              .Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT,
                                          }), NULL, NULL, &swap_chain1);

    IDXGISwapChain1_QueryInterface(swap_chain1,
                                   &IID_IDXGISwapChain2,
                                   (void **)&this->swap_chain);
    
    this->frame_latency_waitable_object =
        IDXGISwapChain2_GetFrameLatencyWaitableObject(this->swap_chain);
    
    if (!is_windowed)
    {
        this->swap_chain->lpVtbl->SetFullscreenState(this->swap_chain, false, NULL);
    }
    else
    {
        // disable alt-enter
        dxgi_factory->lpVtbl->MakeWindowAssociation(dxgi_factory,
                                                    this->window_handle,
                                                    DXGI_MWA_NO_ALT_ENTER);
    }
    
    dxgi_factory->lpVtbl->Release(dxgi_factory);
    
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

#define COMPILE_PIXEL_SHADER(entry_point, shader)                       \
    do                                                                  \
    {                                                                   \
                                                                        \
        ID3DBlob *shader_blob;                                          \
        result =  D3DCompileFromFile(L"shaders.hlsl", NULL,             \
                                     D3D_COMPILE_STANDARD_FILE_INCLUDE, \
                                     entry_point, "ps_5_0",             \
                                     D3DCOMPILE_ENABLE_STRICTNESS |     \
                                     D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, \
                                     &shader_blob, &error_blob);        \
                                                                        \
        if (FAILED(result))                                             \
        {                                                               \
            MessageBoxA(this->window_handle,                            \
                        ID3D10Blob_GetBufferPointer(error_blob),        \
                        "error:", MB_OK);                               \
            ExitProcess(1);                                             \
        }                                                               \
                                                                        \
        void *const data = ID3D10Blob_GetBufferPointer(shader_blob);    \
        size_t const size = ID3D10Blob_GetBufferSize(shader_blob);      \
                                                                        \
        this->device->lpVtbl->CreatePixelShader(this->device,           \
                                                data, (DWORD)size,      \
                                                NULL, &shader);         \
                                                                        \
        ID3D10Blob_Release(shader_blob);                                \
    } while(false)
    
    COMPILE_PIXEL_SHADER("ps_main", this->pixel_shader);
    COMPILE_PIXEL_SHADER("post_ps_main", this->post_pixel_shader);
    
#else
    this->device->lpVtbl->CreatePixelShader(this->device,
                                            g_ps_main, sizeof g_ps_main,
                                            NULL, &this->pixel_shader);
    
    this->device->lpVtbl->CreatePixelShader(this->device,
                                            g_post_ps_main,
                                            sizeof g_post_ps_main,
                                            NULL, &this->post_pixel_shader);
#endif
 
#ifndef RELEASE_BUILD
    vertex_shader_blob->lpVtbl->Release(vertex_shader_blob);
#endif
    
    this->device->lpVtbl->CreateBuffer(this->device,
                                       &(D3D11_BUFFER_DESC) {
                                           .ByteWidth = (int unsigned) sizeof(ShaderConstants),
                                           .Usage = D3D11_USAGE_DYNAMIC,
                                           .BindFlags  = D3D11_BIND_CONSTANT_BUFFER,
                                           .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE
                                       }, NULL, &this->constant_buffer);

    state_create_d3d_textures(this);
    ID3D11Device_CreateSamplerState(this->device,
                                    (&(D3D11_SAMPLER_DESC)
                                     {
                                         .Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
                                         .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                         .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
                                         .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                     }), &this->render_texture_sampler);

    
    // set the size of the portion of the window that we can draw to
    this->device_context->lpVtbl->RSSetViewports(this->device_context, 1,
                                                 &(D3D11_VIEWPORT) {
                                                     .Width = (float)this->width,
                                                     .Height = (float)this->height,
                                                     .MinDepth = 0.0f,
                                                     .MaxDepth = 1.0f,
                                                 });
}

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

    ID3D11DeviceContext_ClearState(this->device_context);
    
    state_destroy_d3d_textures(this);
    state_create_d3d_textures(this);
    
    // release frame_buffer view
    this->frame_buffer_view->lpVtbl->Release(this->frame_buffer_view);
    
    this->swap_chain->lpVtbl->ResizeBuffers(this->swap_chain, 0, 0, 0,
                                            DXGI_FORMAT_UNKNOWN,
                                            DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
    
    ID3D11Texture2D *window_buffer;
    this->swap_chain->lpVtbl->GetBuffer(this->swap_chain, 0,
                                        &IID_ID3D11Texture2D,
                                        (void **) &window_buffer);
     
    this->device->lpVtbl->CreateRenderTargetView(this->device,
                                                 (ID3D11Resource *)window_buffer,
                                                 NULL, &this->frame_buffer_view);
    
    // release the buffer
    window_buffer->lpVtbl->Release(window_buffer);

    // set the size of the portion of the window that we can draw to
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
    // clear background color to black
    this->device_context->lpVtbl->ClearRenderTargetView(this->device_context,
                                                        this->frame_buffer_view,
                                                        (float[4]) {[3] = 1.0f});

    ID3D11DeviceContext_OMSetRenderTargets(this->device_context, 2,
                                           ((ID3D11RenderTargetView*[]) {
                                               this->render_textures[0].texture_view,
                                               this->render_textures[1].texture_view
                                           }), NULL);
    
    this->device_context->lpVtbl->IASetPrimitiveTopology(this->device_context,
                                                         D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    
    this->device_context->lpVtbl->VSSetShader(this->device_context,
                                              this->vertex_shader, NULL, 0);
    
    this->device_context->lpVtbl->PSSetShader(this->device_context,
                                              this->pixel_shader, NULL, 0);
    
    this->device_context->lpVtbl->PSSetConstantBuffers(this->device_context, 0,
                                                       1, &this->constant_buffer);
    
    // draw the shaders
    this->device_context->lpVtbl->Draw(this->device_context, 4, 0);

    // unbind render target
    this->device_context->lpVtbl->OMSetRenderTargets(this->device_context, 2,
                                                     (ID3D11RenderTargetView*[2]){0},
                                                     NULL);
    
    // bind swapchain render target
    this->device_context->lpVtbl->OMSetRenderTargets(this->device_context, 1,
                                                     &this->frame_buffer_view,
                                                     NULL);
   
    this->device_context->lpVtbl->PSSetShader(this->device_context,
                                              this->post_pixel_shader, NULL, 0);

    this->device_context->lpVtbl->PSSetSamplers(this->device_context, 0, 1,
                                                &this->render_texture_sampler);
    
    ID3D11DeviceContext_PSSetShaderResources(this->device_context, 0, 2,
                                             ((ID3D11ShaderResourceView*[])
                                             {
                                                 this->render_textures[0].texture_shader_view,
                                                 this->render_textures[1].texture_shader_view,
                                             }));
    
    // draw the shaders
    this->device_context->lpVtbl->Draw(this->device_context, 4, 0);

    ID3D11DeviceContext_PSSetShaderResources(this->device_context, 0, 2,
                                             (ID3D11ShaderResourceView*[2]){0});
    
    // swap the front/back buffer
    this->swap_chain->lpVtbl->Present(this->swap_chain, 0, 0);
}

#ifdef SHADER_HOT_RELOAD
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
    
    ID3D11PixelShader **pixel_shaders[] =
    {
        &this->pixel_shader,
        &this->post_pixel_shader
    };
    
    char const *pixel_shader_entrys[] = {
        "ps_main", "post_ps_main",
    };
    
    ID3DBlob *error_blob = NULL;
    ID3DBlob *pixel_shader_blobs[2] = {0};

    for(size_t i = 0;;)
    {
        HRESULT result = D3DCompileFromFile(L"shaders.hlsl", NULL,
                                            D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                            pixel_shader_entrys[i], "ps_5_0",
                                            D3DCOMPILE_ENABLE_STRICTNESS |
                                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                                            pixel_shader_blobs + i, &error_blob);
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
        
        (*pixel_shaders[i])->lpVtbl->Release(*pixel_shaders[i]);
        this->device->lpVtbl->CreatePixelShader(this->device,
                                                pixel_shader_blobs[i]->lpVtbl->
                                                GetBufferPointer(pixel_shader_blobs[i]),
                                                pixel_shader_blobs[i]->lpVtbl->
                                                GetBufferSize(pixel_shader_blobs[i]),
                                                NULL, pixel_shaders[i]);

        pixel_shader_blobs[i]->lpVtbl->Release(pixel_shader_blobs[i]);


        if (++i >= ARRAY_COUNT(pixel_shaders)) break;
    }
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

#ifdef SHADER_HOT_RELOAD
    WIN32_FILE_ATTRIBUTE_DATA old_file_attribute_data;
    GetFileAttributesExW(L"shaders.hlsl",
                         GetFileExInfoStandard,
                         &old_file_attribute_data);
#endif
    
    for(;;)
    {
        LARGE_INTEGER current_counter;
        QueryPerformanceCounter(&current_counter);
        
        int64_t const counter_duration =
            current_counter.QuadPart - start_counter.QuadPart; 
        
        float const current_time =
            (float)((double)counter_duration / (double)performance_frequency.QuadPart);

        
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

        
        while(WaitForSingleObject(state->frame_latency_waitable_object, 0) == WAIT_TIMEOUT)
        {
        }

        {
            RECT rect;
            GetClientRect(state->window_handle, &rect);

            state_handle_resize(state,
                                rect.right - rect.left,
                                rect.bottom - rect.top);
        }
        
        state_draw(state);
        
#ifdef SHADER_HOT_RELOAD
        state_reload_shader(state, &old_file_attribute_data);
#endif
    }
}

static uint32_t parse_u32(wchar_t const *string)
{
    uint32_t result = 0;
    while (*string != L'\0')
    {
        result *= 10;
        result += *string - L'0';
        ++string;
    }

    return result;
}

// references:
// https://docs.nvidia.com/gameworks/content/gameworkslibrary/coresdk/nvapi/modules.html
// https://stackoverflow.com/questions/13291783/how-to-get-the-id-memory-address-of-dll-function

#ifndef NO_FPS_OVERLAY
static void try_enable_fps_overlay(ID3D11Device *const device)
{
    HMODULE nvapi = LoadLibraryW(L"nvapi64.dll");
    if (nvapi == NULL) return;

    enum
    {
        NVAPI_OK = 0,
    };

    typedef int (__cdecl *NvAPI_InitializeFn)(void);
    typedef void *(__cdecl *NvApi_QueryInterfaceFn)(uint32_t);
    typedef int (__cdecl *NvAPI_D3D_SetFPSIndicatorStateFn)(IUnknown *, uint8_t);

    NvApi_QueryInterfaceFn NvApi_QueryInterface =
        (NvApi_QueryInterfaceFn)GetProcAddress(nvapi, "nvapi_QueryInterface");

    if (NvApi_QueryInterface == NULL) goto nvapi_unload;
    
#define NVAPI_LOAD_FN(name, id)                                      \
    name##Fn name;                                                   \
    do                                                               \
    {                                                                \
        if((name = (name##Fn)NvApi_QueryInterface(id)) == NULL)      \
        {                                                            \
            goto nvapi_unload;                                       \
        }                                                            \
    } while(false)

    NVAPI_LOAD_FN(NvAPI_Initialize, 0x150E828);
    NVAPI_LOAD_FN(NvAPI_D3D_SetFPSIndicatorState, 0x0A776E8DB);

    if (NvAPI_Initialize() != NVAPI_OK) goto nvapi_unload;
    if (NvAPI_D3D_SetFPSIndicatorState((IUnknown*)device, true) != NVAPI_OK)
    {
        goto nvapi_unload;
    }
    
#undef NVAPI_LOAD_FN
    
nvapi_unload:
    FreeLibrary(nvapi);
}
#endif

void entry(void);
void entry(void)
{
    int argc;
    wchar_t **const argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    if (argc < 2) return;

#ifndef NO_FPS_OVERLAY
    bool have_fps_overlay = false;
#endif
    
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
                if (argument[1] == L'\0')
                {
                    mode = WINDOW_MODE;
                }
                
                break;
            }

#ifndef NO_FPS_OVERLAY
            case 'f':
            case 'F':
            {
                if ((argument[1] == L'0' || argument[1] == L'1') &&
                    argument[2] == L'\0')
                {
                    have_fps_overlay = argument[1] - L'0' != 0;
                }
                
                break;
            }
#endif
        }
    }

    if (mode == NOTHING_MODE || mode == DIALOG_MODE)
    {
        return;
    }

    // cause all the other screens to go black when in fullscreen mode
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

#ifndef NO_FPS_OVERLAY
    if (have_fps_overlay)
    {
        try_enable_fps_overlay(state.device);
    }
#endif
        
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
