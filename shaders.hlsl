cbuffer constants : register (b0)
{
    float aspect_ratio;
    float timer;
    float pixel_width;
}

struct vs_out
{
    float4 position : SV_POSITION;
    float2 texture_coords : TEX;
};

vs_out vs_main(uint vertex_index : SV_VERTEXID)
{
     vs_out output; 

     float2 texture_coords = float2(vertex_index & 1, vertex_index >> 1);
     output.position = float4((texture_coords.x - 0.5f) * 2,
                              -(texture_coords.y - 0.5f) * 2, 0, 1);
     output.texture_coords = output.position.xy * 0.5f + 0.5f;

     return output;
}

float to_radians(float degree) { return degree * 0.017453f; }

// from https://www.shadertoy.com/view/Xt3cDn by nimitz
uint baseHash(uint2 p)
{
    p = 1103515245U*((p >> 1U)^(p.yx));
    uint h32 = 1103515245U*((p.x)^(p.y>>3U));
    return h32^(h32 >> 16);
}

float3 hash32(inout float2 uv)
{
    uint n = baseHash(asuint(uv+=0.1f));
    uint3 rz = uint3(n, n*16807U, n*48271U);
    return float3(rz & (uint3)0x7fffffffU)/float(0x7fffffff);
}

float2 hash22(inout float2 uv)
{
    uint n = baseHash(asuint(uv+=0.1f));
    uint2 rz = uint2(n, n*48271U);
    return float2(rz & (uint2)0x7fffffffU)/float(0x7fffffff);
}

float hash12(inout float2 uv)
{
    uint n = baseHash(asuint(uv+=0.1f));
    return float(n & 0x7fffffffU)/float(0x7fffffff);
}

static const int MAX_STEPS = 100;
static const float MIN_DISTANCE = 0.001f;
static const float MAX_DISTANCE = 8.0f;

struct Ray
{
    float3 pos;
    float3 dir;
};

Ray make_ray(float3 pos, float3 dir)
{
    Ray ray;
    ray.pos = pos;
    ray.dir = dir;
    return ray;
}

// https://steveharveynz.wordpress.com/2012/12/20/ray-tracer-part-two-creating-the-camera/
Ray look_at_ray(float3 eye_point,
                float3 look_at_point,
                float fov, float2 coords)
{
    static const float3 up = float3(0, 1, 0);
    
    float3 view_direction = look_at_point - eye_point;
    float3 u = cross(view_direction, up);
    float3 v = cross(u, view_direction);
    
    u = normalize(u);
    v = normalize(v);

    float view_plane_half_width = tan(fov / 2.0f);
    float view_plane_half_height = view_plane_half_width * aspect_ratio;

        
    float3 view_plane_bottom_left_point = look_at_point -
                                          v * view_plane_half_height -
                                          u * view_plane_half_width;

   float3 x_increment_vector = u * 2.0f * view_plane_half_width;
   float3 y_increment_vector = v * 2.0f * view_plane_half_height;

   float3 view_plane_point = view_plane_bottom_left_point +
                             coords.x * x_increment_vector +
                             coords.y * y_increment_vector;

   return make_ray(eye_point, normalize(view_plane_point - eye_point));
}

float2x2 rotation_matrix(float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    
    return float2x2(c, -s, s, c);
}

float sdf_box(float2 p, float2 b)
{
    float2 d = abs(p) - b;
    return length(max(d, 0.0f)) + min(max(d.x, d.y), 0.0f);
}

float2 windows_logo_sdf(float2 uv)
{
    float theta = atan2(uv.x, uv.y) - 0.2f;
    float radius = length(uv);

    uv = radius * float2(sin(theta), cos(theta));
    uv.y += sin(uv.x * acos(-1.0f)) * 0.1f;
   
    float d = sdf_box(uv, .78f);
    
    d = max(d, -(abs(uv.x) - 0.03f));
    d = max(d, -(abs(uv.y) - 0.03f));

    float color_index;
    if(uv.x < 0.0f && uv.y > 0.0f)
    {
        color_index = 0;
    }
    else if (uv.x > 0.0f && uv.y > 0.0f)
    {
        color_index = 1;
    }
    else if (uv.y < 0.0f && uv.x < 0.0f)
    {
        color_index = 2;
    }
    else if (uv.y < 0.0f && uv.x > 0.0f)
    {
        color_index = 3;
    }

    return float2(d, color_index);
}

float op_extrude(float3 p, float sdf_2d, float height)
{
    float2 w = float2(sdf_2d, abs(p.z) - height);
    return min(max(w.x, w.y), 0.0f) + length(max(w, 0.0f));
}

float2 windows_logo_3d_sdf(float3 pos, float extrude)
{
    float2 logo_sdf = windows_logo_sdf(pos.xy);
    return float2(op_extrude(pos, logo_sdf.x, extrude), logo_sdf.y);
}

struct DistanceInfo
{
    float2 data;
};

DistanceInfo make_distance_info(float2 data)
{
    DistanceInfo result;
    result.data = data;
    return result;
}

DistanceInfo combine_sdf(DistanceInfo a, DistanceInfo b)
{
    if (a.data.x < b.data.x)
    {
        return a;
    }
    else
    {
        return b;
    }
}

float hexagon_hash(float2 p)
{
    return (dot(sin(p * 4.0f - cos(p.yx * 1.4f) + timer), 0.25f) + .5f);
}

// from https://www.shadertoy.com/view/MsVfz1
float hexagon_pylon(float2 p2, float pz, float r, float ht)
{
    float3 p = float3(p2.x, pz, p2.y);
    float3 b = float3(r, ht, r);

    float angle = sin(timer);

    // Hexagon.
    p.xz = abs(p.xz);
    p.xz = float2(p.x * 0.866025f + p.z * 0.5f, p.z);
    
    return length(max(abs(p) - b + 0.005f, 0.)) - 0.005f;
}

float2 hexagon_sdf(float2 p, float pH)
{
    const float2 s = float2(.866025, 1);
    
    // The hexagon centers: Two sets of repeat hexagons are required to fill in the space, and
    // the two sets are stored in a "vec4" in order to group some calculations together. The hexagon
    // center we'll eventually use will depend upon which is closest to the current point. Since 
    // the central hexagon point is unique, it doubles as the unique hexagon ID.
    float4 hC = floor(float4(p, p - float2(0, .5))/s.xyxy) + float4(0, 0, 0, .5);
    float4 hC2 = floor(float4(p - float2(.5, .25), p - float2(.5, .75))/s.xyxy) +
                 float4(.5, .25, .5, .75);
    
    // Centering the coordinates with the hexagon centers above.
    float4 h = float4(p - (hC.xy + .5)*s, p - (hC.zw + .5)*s);
    float4 h2 = float4(p - (hC2.xy + .5)*s, p - (hC2.zw + .5)*s);
    
    // Hexagon height.
    float4 ht = float4(hexagon_hash(hC.xy), hexagon_hash(hC.zw),
                       hexagon_hash(hC2.xy), hexagon_hash(hC2.zw));

    // The pylon radius. Lower numbers leave gaps, and heigher numbers give overlap. There's not a 
    // lot of room for movement, so numbers above ".3," or so give artefacts.
    const float r = .25; // .21 to .3. 
    float4 obj = float4(hexagon_pylon(h.xy, pH, r, ht.x),
                       hexagon_pylon(h.zw, pH, r, ht.y), 
                       hexagon_pylon(h2.xy, pH, r, ht.z),
                       hexagon_pylon(h2.zw, pH, r, ht.w));
    
    
    // Nearest hexagon center (with respect to p) to the current point. In other words, when
    // "h.xy" is zero, we're at the center. We're also returning the corresponding hexagon ID -
    // in the form of the hexagonal central point.
    //
    h = obj.x<obj.y ? float4(h.xy, hC.xy) : float4(h.zw, hC.zw);
    h2 = obj.z<obj.w ? float4(h2.xy, hC2.xy) : float4(h2.zw, hC2.zw);

    float offsets[4] = {
        4, 4, 4, 4
    };
    
    float2 oH = obj.x < obj.y ? float2(obj.x, offsets[0]) : float2(obj.y, offsets[2]);
    float2 oH2 = obj.z < obj.w ? float2(obj.z, offsets[1]) : float2(obj.w, offsets[3]);
    
    return oH.x < oH2.x ? oH : oH2;
    
}

DistanceInfo distance_function(float3 pos)
{
    float3 old_pos = pos;

    pos.y += .5f;
    pos.xy = mul(pos.xy, rotation_matrix(timer));
    pos.xz = mul(pos.xz, rotation_matrix(timer));
    pos.xz += .1f * sin(timer);

    DistanceInfo distance;
    {
        distance = make_distance_info(windows_logo_3d_sdf(pos, 0.1f));
    }
    
    {
        float3 light_pos = old_pos;
        light_pos -= float3(0.0f, 2.0f, 0);
        light_pos.xz = mul(light_pos.xz, rotation_matrix(-timer));

        float light_sdf = length(max(abs(light_pos) -
                                 float3(1.f, 0.01f, 10.25f), 0.0f));

        distance = combine_sdf(distance,
                               make_distance_info(float2(light_sdf, 9.0f)));
    }

    {
        old_pos.zy = mul(old_pos.zy, rotation_matrix(sin(timer) * 0.3f));
        old_pos.xz = mul(old_pos.xz, rotation_matrix(timer * 0.5f));
        old_pos.y += 2.3;
        old_pos.z += timer;


        float2 hexagon_board = hexagon_sdf(old_pos.xz, -old_pos.y);
        
        distance =
            combine_sdf(distance,
                        make_distance_info(hexagon_board));

    }
                    
    return distance;
}

struct HitInfo
{
    DistanceInfo distance;
    int step_count;
};

struct HitInfo ray_march(Ray ray)
{
    float distance_traveled = 0.0f;
    
    int i = 0;
    for (;i < MAX_STEPS; ++i)
    {
        float3 current_position =  ray.pos + distance_traveled * ray.dir;
        DistanceInfo distance_to_closest = distance_function(current_position);

        if (abs(distance_to_closest.data.x) < MIN_DISTANCE)
        {
            HitInfo hit_info;
            hit_info.distance =
                make_distance_info(float2(distance_traveled,
                                          distance_to_closest.data.y));

            hit_info.step_count = i;
            
            return hit_info;
        }

        distance_traveled += distance_to_closest.data.x;
        if (distance_to_closest.data.x > MAX_DISTANCE)
        {
            break;
        }
    }

    HitInfo hit_info;   
    hit_info.distance = make_distance_info(float2(distance_traveled, -1));
    hit_info.step_count = i;

    return hit_info;
}

// from https://www.iquilezles.org/www/articles/normalsSDF/normalsSDF.htm
float3 calculate_normal(float3 p)
{
    const float eps = 0.0001f; // or some other value
    const float2 h = float2(eps, 0);

    return normalize(float3(distance_function(p + h.xyy).data.x -
                            distance_function(p - h.xyy).data.x,
                            distance_function(p + h.yxy).data.x -
                            distance_function(p - h.yxy).data.x,
                            distance_function(p + h.yyx).data.x -
                            distance_function(p - h.yyx).data.x));
}

float3 random_in_unit_sphere(inout float2 seed, float3 nor)
{
    float2 r = hash22(seed);

    float3 uu = normalize(cross(nor, float3(0.0, 1.0, 1.0)));
    float3 vv = cross(uu, nor);
    
    float ra = sqrt(r.y);
    float rx = ra * cos(6.2831f * r.x); 
    float ry = ra * sin(6.2831f *r.x);
    float rz = sqrt(1.0f - r.y);
    return normalize((float3)(rx*uu + ry*vv + rz*nor));
}

struct ps_out
{
    float4 color : SV_Target0;
    float4 normal : SV_Target1;
};

float pow2(float value) { return value * value; }

ps_out ps_main(vs_out input)
{
    float2 coords = input.texture_coords;

    const float slider = 0.9f;
    float3 ray_pos = float3(0, .8 - slider, 3.4f);
    float3 look_at = float3(0, .6 - slider, 2.85f);
    
    float2 seed = coords.xy;
    const int total_samples = 3;
    int max_bounces = 4;
    
    ps_out result;
    result.color = result.normal = 0.0f;

    float2 pixel_size =
        float2(1.0f / (1.0f / pixel_width * aspect_ratio), pixel_width);

    int j = 0;
    for (; j < total_samples; ++j)
    {
        float3 total_emission = 0.0f;
        float3 total_attenuation = 0.0f;
        
        Ray ray = look_at_ray(ray_pos, look_at,
                              to_radians(60.0f),
                              coords + hash22(seed) * pixel_size);

        for (int i = 0; i < max_bounces; ++i)
        {
            HitInfo hit_info = ray_march(ray);
        
            // we didn't hit anything draw a background
            if (hit_info.step_count == MAX_STEPS ||
                hit_info.distance.data.x >= MAX_DISTANCE)
            {
                if (i == 0) total_attenuation = 1.0f;
                
                float3 background =
                    pow2(abs(ray.dir.y + 0.3f)  + hash12(seed) * 0.1f) * 0.25f;
                    
                result.color += 
                    float4(background * total_attenuation, 0);
                    
                break;
            }

            float3 hit_position = ray.pos + hit_info.distance.data.x * ray.dir;
            float3 hit_normal = calculate_normal(hit_position);
            
            int hit_index = int(hit_info.distance.data.y);
            if (hit_index > 8)
            {
                const float3 strength = 0.9f;
                total_emission = i == 0 ? strength : strength * total_attenuation;

                result.color += float4(total_emission, 0);
                result.normal += float4(hit_normal, 0);
                break;
            }
            else
            {
                float3 target = hit_normal +
                                random_in_unit_sphere(seed, hit_normal);

                ray.pos = hit_position + hit_normal * 0.003f;
                ray.dir = normalize(target);

                float3 attenuation;
                switch(hit_index)
                {
                    case 0:
                    {
                        attenuation = float3(.9f, .05f, 0);
                        break;
                    }

                    case 1:
                    {
                        attenuation = float3(0, 0.7f, 0);
                        break;
                    }

                    case 2:
                    {
                        attenuation = float3(.0, .15, 1.0);
                        break;
                    }

                    case 3:
                    {
                        attenuation = float3(1, 1, 0);
                        break;
                    }

                    default:
                    {
                        attenuation = 1.0f;
                        break;
                    }
                }

                total_attenuation = i == 0                         ?
                                    attenuation                    :
                                    total_attenuation * attenuation;
            }
            
            if (i == 0 && j == 0)
            {
                result.normal += float4(hit_normal, 0);
            }

            if (dot(total_attenuation, total_attenuation) < 0.01f)
            {
                break;
            }
        }
    }

    result.color /= float(j == 0 ? 1 : j);
    result.normal /= float(j == 0 ? 1 : j);
        
    return result;
}

Texture2D color_texture : register(t0);
Texture2D normal_texture : register(t1);
SamplerState texture_sampler : register(s0);

float4 sample_color_texture(float2 pos)
{
    return color_texture.Sample(texture_sampler, pos);
}

float4 sample_normal_texture(float2 pos)
{
    return normal_texture.Sample(texture_sampler, pos);
}

// based on https://www.shadertoy.com/view/ldKBzG
float4 post_ps_main(vs_out input) : SV_TARGET
{
    float2 coords = float2(input.texture_coords.x,
                           1.0f - input.texture_coords.y);
    
    float2 offset[25] = {
        float2(-2,-2),
        float2(-1,-2),
        float2(0,-2),
        float2(1,-2),
        float2(2,-2),
    
        float2(-2,-1),
        float2(-1,-1),
        float2(0,-1),
        float2(1,-1),
        float2(2,-1),
    
        float2(-2,0),
        float2(-1,0),
        float2(0,0),
        float2(1,0),
        float2(2,0),
    
        float2(-2,1),
        float2(-1,1),
        float2(0,1),
        float2(1,1),
        float2(2,1),
    
        float2(-2,2),
        float2(-1,2),
        float2(0,2),
        float2(1,2),
        float2(2,2),
    };
    
    float kernel[25] = {
        1.0f/256.0f,
        1.0f/64.0f,
        3.0f/128.0f,
        1.0f/64.0f,
        1.0f/256.0f,
    
        1.0f/64.0f,
        1.0f/16.0f,
        3.0f/32.0f,
        1.0f/16.0f,
        1.0f/64.0f,
    
        3.0f/128.0f,
        3.0f/32.0f,
        9.0f/64.0f,
        3.0f/32.0f,
        3.0f/128.0f,
    
        1.0f/64.0f,
        1.0f/16.0f,
        3.0f/32.0f,
        1.0f/16.0f,
        1.0f/64.0f,
    
        1.0f/256.0f,
        1.0f/64.0f,
        3.0f/128.0f,
        1.0f/64.0f,
        1.0f/256.0f,
    };

    float4 sum = 0.0f;
    float total_weight = 0.0f;
    float4 center_color = sample_color_texture(coords);    
    float4 center_normal = sample_normal_texture(coords);
    float2 pixel_size =
        float2(1.0f / (1.0f / pixel_width * aspect_ratio), pixel_width);

    for (int i = 0; i < 25; i += 1)
    {
        float2 uv = coords + offset[i] * pixel_size;

        float4 sample_color = sample_color_texture(uv);
        float4 color_difference = center_color - sample_color;

        float color_dist = dot(color_difference, color_difference);
        float color_weight = min(exp(-color_dist), 1.0f);
        
        float4 sample_normal = sample_normal_texture(uv);
        float4 normal_difference = center_normal - sample_normal;

        float normal_dist = dot(normal_difference, normal_difference);
        float normal_weight = min(exp(-normal_dist * 2.0f), 1.0f);

        float weight = normal_weight * color_weight;
        sum += sample_color * weight * kernel[i];
        total_weight += weight * kernel[i];        
    }
    
    return pow(sum / total_weight, 1.0f / 2.2f); 
}
