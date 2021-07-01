cbuffer constants : register (b0)
{
    float aspect_ratio;
    float timer;
}

struct vs_out
{
    float4 position : SV_POSITION;
    float2 texture_coord : TEX;
};

vs_out vs_main(uint vertex_index : SV_VERTEXID)
{
     vs_out output; 

     float2 texture_coord = float2(vertex_index & 1,vertex_index >> 1);
     output.position = float4((texture_coord.x - 0.5f) * 2, -(texture_coord.y - 0.5f) * 2, 0, 1);
     output.texture_coord = 0.5f * output.position.xy + 0.5f;

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
static const float MAX_DISTANCE = 100.0f;

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
    float3 material_info;
};

DistanceInfo make_distance_info(float2 data)
{
    DistanceInfo result;
    result.data = data;
    result.material_info = (float3)0.0f;

    return result;
}

DistanceInfo make_distance_info(float2 data,
                                float3 material_info)
{
    DistanceInfo result;
    result.data = data;
    result.material_info = material_info;

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

float hexagon_hash(float2 seed)
{
    return hash12(seed) * 0.5f + 0.25f;
}

// from https://www.shadertoy.com/view/MsVfz1
float hexagon_pylon(float2 p2, float pz, float r, float ht)
{
    float3 p = float3(p2.x, pz, p2.y);
    float3 b = float3(r, ht, r);
    
    // Hexagon.
    p.xz = abs(p.xz);
    p.xz = float2(p.x*.866025 + p.z*.5, p.z);
    
  	return length(max(abs(p) - b + .01, 0.)) - .01;
}

float2 hexagon_sdf(float2 p, float pH)
{
    const float2 s = float2(.866025, 1);
    
    // The hexagon centers: Two sets of repeat hexagons are required to fill in the space, and
    // the two sets are stored in a "vec4" in order to group some calculations together. The hexagon
    // center we'll eventually use will depend upon which is closest to the current point. Since 
    // the central hexagon point is unique, it doubles as the unique hexagon ID.
    float4 hC = floor(float4(p, p - float2(0, .5))/s.xyxy) + float4(0, 0, 0, .5);
    float4 hC2 = floor(float4(p - float2(.5, .25), p - float2(.5, .75))/s.xyxy) + float4(.5, .25, .5, .75);
    
    // Centering the coordinates with the hexagon centers above.
    float4 h = float4(p - (hC.xy + .5)*s, p - (hC.zw + .5)*s);
    float4 h2 = float4(p - (hC2.xy + .5)*s, p - (hC2.zw + .5)*s);
    
    // Hexagon height.
    float4 ht = float4(hexagon_hash(hC.xy), hexagon_hash(hC.zw),
                       hexagon_hash(hC2.xy), hexagon_hash(hC2.zw));
    
    // Restricting the heights to five levels... The ".02" was a hack to take out the lights
    // on the ground tiles, or something. :)
    ht = floor(ht*4.99)/4./2. + .02;

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
        0, 1, 2, 3
    };
    
    float2 oH = obj.x < obj.y ? float2(obj.x, offsets[0]) : float2(obj.y, offsets[2]);
    float2 oH2 = obj.z < obj.w ? float2(obj.z, offsets[1]) : float2(obj.w, offsets[3]);
    
    return oH.x < oH2.x ? oH : oH2;
    
}

DistanceInfo distance_function(float3 pos)
{
    float3 old_pos = pos;

    pos.y += .7f;
    pos.xy = mul(pos.xy, rotation_matrix(timer));
    pos.xz = mul(pos.xz, rotation_matrix(-timer));
    
    DistanceInfo distance;
    {
        distance = make_distance_info(windows_logo_3d_sdf(pos, 0.1f), 1.0f);
    }
    
	{
		float light_sdf = length(max(abs(old_pos - float3(2.0, 2.0f, 0)) -
						  float3(3.f, 0.01f, 3.f), 0.0f));

		distance = combine_sdf(distance,
							   make_distance_info(float2(light_sdf, 9.0f),
                                                  float3(1, 1, 1)));
	}

    {
        old_pos.y += 2.3f;
        old_pos.xz += float2(sin(timer * 2.0f), cos(timer * 2.0f)) * 0.01f;
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
                                          distance_to_closest.data.y),
                                   distance_to_closest.material_info);

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

float3 random_in_unit_sphere(inout float2 seed)
{
    return normalize(hash32(seed) * 2.0f - 1.0f);     
}

float4 ps_main(vs_out input) : SV_TARGET
{
    float2 coords = input.texture_coord;

    const float slider = 0.2f;
    float3 ray_pos = float3(0, .8 - slider, 3.4f);
    float3 look_at = float3(0, .6 - slider, 2.8f);
    
    float2 seed = coords.xy;
    float3 color_result = 0.0f;
    const int total_samples = 5;

    for (int j = 0; j < total_samples; ++j)
    {
        float3 total_emission = 0.0f;
        float3 total_attenuation = 0.0f;
        
        Ray ray = look_at_ray(ray_pos, look_at,
                              to_radians(60.0f),
                              coords + hash22(seed) * 0.002f);

        for (int i = 0; i < 6; ++i)
        {
            HitInfo hit_info = ray_march(ray);
        
            // we didn't hit anything draw a background
            if (hit_info.step_count == MAX_STEPS ||
                hit_info.distance.data.x >= MAX_DISTANCE)
            {
                break;
            }

            float3 hit_position =
                ray.pos + hit_info.distance.data.x * ray.dir;
            float3 hit_normal = calculate_normal(hit_position);

            int hit_index = int(hit_info.distance.data.y);
            if (hit_index > 8)
            {
				const float3 strength = hit_info.distance.material_info;
                total_emission += i == 0 ? strength : strength * total_attenuation;
                color_result += total_emission * 1.0f / float(total_samples);
                break;
            }
            else
            {
                float3 target = hit_normal +
                                random_in_unit_sphere(seed);

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
        }
    }

    return float4(pow(color_result, 1.0f / 2.2f), 1.0f);
}                           
