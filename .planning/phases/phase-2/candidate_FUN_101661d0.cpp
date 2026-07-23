// ============================================================================
// Candidate C++ for FUN_101661d0 (ModelAnimation::playInfo)
// Binary: Shadow Fight 2 (ARM 32-bit LE v8) at 0x101661d0–0x10166647
// ============================================================================
//
// Pipeline: reads per-axis displacement Vec3 from animation container,
// transforms it by a per-axis float offset + sign*scale, stores result at
// this[0x98..0xA0], then calls FUN_10166690 to consume it.
//
// Subcontainer blending: if the animation has subcontainers (keyframes),
// transforms each keyframe's Vec3 pair (at +12 and +24) and blends them
// by summing into temporary arrays, adding the main displacement.

// -- Utility function declarations (named from decompilation analysis) --

static void  vec3_zero(void* dst);                      // FUN_1028e470
static void  vec3_copy(void* dst, const void* src);      // FUN_1028e490 (12-byte copy)
static void  vec3_add(void* dst, const void* src);       // FUN_1028e6b0 (dst += src)
static void* element_at(void* container, uint32_t idx);  // FUN_10102c70 (stride 12)
static void  some_transform(void* a, void* dst, const void* src); // FUN_1028e890 (see NOTE 1)
static int   get_vector_int(void* vec, uint32_t idx);    // FUN_10164990 (int vector at this+0xdc)
static int   memcmp_str(const void* a, const void* b);   // FUN_1000cc00 (returns 1/0 in low byte)
static void  error_log(const char* msg);                 // FUN_101471b0
static void* container_lookup(void* c, const void* key); // FUN_1016da00
static int   container_idx(void* s);                     // FUN_1002ce60
static void* get_data_ptr(void* x);                      // FUN_10048b30
static int   get_frame_vec3_1(const void* frame);        // FUN_1016c5d0 (returns frame+12)
static int   get_frame_vec3_2(const void* frame);        // FUN_1016c5f0 (returns frame+24)
static void  guard_init(void* out);                      // FUN_101ec1c0
static void  temp_alloc(void** out, int count);          // FUN_101aefe0
static void  temp_free(void* p);                         // FUN_102f7780

// DAT_1065fb74 = zero Vec3 (12 zero bytes)
static const uint8_t ZERO_VEC3[12] = {0,0,0,0,0,0,0,0,0,0,0,0};

// -- Main function (address 0x101661d0) --

void ModelAnimation_playInfo(void* this_ptr)
{
    // ---- SEH prologue (MSVC ARM exception handling) ----
    // In the binary: ExceptionList is saved/restored via SEH chain
    // (omitted in C++ — assume noexcept or SEH-agnostic)
    // uStack_8 = 0xFFFFFFFF (try level), puStack_c = handler

    // iVar7 = *(int*)(*(int*)(this_ptr + 0x20) + 0x94)
    struct AnimState* state = *(struct AnimState**)(*(int**)((int)this_ptr + 0x20) + 0x94 / 4);

    // ---- Select animation source 1 (pvStack_1c) ----
    void* src_a;
    switch (state->anim_source_type_1) {  // state + 0xAC
    case 0:
    case 1:
        src_a = this_ptr;                       // self
        break;
    case 2:
        src_a = *(void**)((int)this_ptr + 0x48); // alternate anim data 2
        break;
    case 3:
        src_a = *(void**)((int)this_ptr + 0x44); // alternate anim data 1
        break;
    default:
        error_log("ModelAnimation::getPlayerAnimation - unknown type: %i");
        src_a = nullptr;
    }

    // ---- Select animation source 2 (pvStack_18) ----
    void* src_b;
    switch (state->anim_source_type_2) {  // state + 0xB0
    case 0:
    case 1:
        src_b = this_ptr;
        break;
    case 2:
        src_b = *(void**)((int)this_ptr + 0x48);
        break;
    case 3:
        src_b = *(void**)((int)this_ptr + 0x44);
        break;
    default:
        error_log("ModelAnimation::getPlayerAnimation - unknown type: %i");
        src_b = nullptr;
    }

    // ---- Output variables ----
    uint32_t displacement[3];   // auStack_38 (Vec3 as raw uint — sign bits manipulated)
    float offset;            // fStack_2c (per-axis float offset)
    float unused_float;      // fStack_28 (stored to, never read back — scratch)
    uint8_t result[12];      // auStack_50 (3 floats, transformed output)

    vec3_zero(displacement);
    offset = 0.0f;
    unused_float = 0.0f;     // binary: fStack_28 initialized by vec3_zero(&fStack_2c)

    if (src_b == nullptr)
        src_b = this_ptr;

    // ---- Compute Vec3 displacement from source animation ----
    switch (state->displacement_mode) {  // state + 0x68
    case 1: {
        // Animation loop-dependent pivot index
        uint32_t pivot_idx;
        if (*(uint8_t*)((int)src_a + 0x7C) == 0 || state->pivot_override > 0x7FFFFFFF)
            pivot_idx = state->pivot_default;       // state + 0x70
        else
            pivot_idx = state->pivot_override;      // state + 0x74
        goto read_vec3_from_container;
    }
    case 2: {
        // Sign-dependent axis displacement
        vec3_copy(displacement, ZERO_VEC3);         // init to zero
        int8_t sign = *(int8_t*)((int)this_ptr + 0x54);
        int match = memcmp_str(&state->axis_name_1, g_some_axis_str);  // state+0x88 vs DAT_103877fc
        if ((uint8_t)(sign == 1) == (match & 0xFF)) {
            displacement[0] = *(uint32_t*)((int)this_ptr + 0xE0);   // X displacement (raw bits)
        } else {
            displacement[0] = *(uint32_t*)((int)this_ptr + 0xE4);   // Y displacement (raw bits)
        }
        displacement[0] ^= 0x80000000u;  // flip sign bit (negate)
        break;
    }
    case 3: {
        // Plain zero displacement (just the offset)
        vec3_copy(displacement, ZERO_VEC3);
        break;
    }
    case 4: {
        // Direct pivot read from container
        uint32_t pivot_idx = *(uint32_t*)((int)this_ptr + 0x58);
read_vec3_from_container: ;
        void* container = element_at((void*)((int)this_ptr + 0xE8), 2);  // axis = 2
        void* pivot_vec3 = element_at(container, pivot_idx);
        vec3_copy(displacement, pivot_vec3);
        break;
    }
    }

    // ---- Compute float offset from source animation ----
    switch (state->offset_mode) {  // state + 0x6C
    case 1: {
        uint32_t idx;
        if (*(uint8_t*)((int)src_b + 0x7C) == 0 || state->offset_override > 0x7FFFFFFF)
            idx = state->offset_default;      // state + 0x78
        else
            idx = state->offset_override;     // state + 0x7C
        int frame_ptr = get_vector_int(src_b, idx);  // get frame from vector
        vec3_copy(&offset, (void*)get_frame_vec3_1((void*)frame_ptr));
        break;
    }
    case 2: {
        vec3_copy(&offset, ZERO_VEC3);
        int8_t sign = *(int8_t*)((int)this_ptr + 0x54);
        int match = memcmp_str(&state->axis_name_2, g_some_axis_str);  // state+0x94
        if ((uint8_t)(sign == 1) == (match & 0xFF)) {
            offset = *(float*)((int)this_ptr + 0xE0);
        } else {
            offset = *(float*)((int)this_ptr + 0xE4);
        }
        break;
    }
    case 3: {
        vec3_copy(&offset, (void*)((int)src_b + 0x98));
        break;
    }
    case 4: {
        vec3_copy(&offset, (void*)get_frame_vec3_1(*(int*)((int)src_b + 0x5C)));
        break;
    }
    }

    // ---- Apply sign * scale + offset ----
    int8_t sign = *(int8_t*)((int)this_ptr + 0x54);
    offset = (float)(int)sign * state->scale + offset;   // state + 0xB4
    // NOTE: The cast to int then float is the binary's exact behavior

    unused_float += state->additive;                     // state + 0xB8

    // Store final offset back to this
    *(float*)((int)this_ptr + 0x80) = offset;

    // ---- Transform displacement by offset ----
    // some_transform(&offset, result, displacement)
    // Applies the offset as a transform parameter to the displacement Vec3
    some_transform(&offset, result, displacement);
    vec3_copy((void*)((int)this_ptr + 0x98), result);

    // ---- Masked output to FUN_10166690 ----
    float out_x = 0.0f, out_y = 0.0f, out_z = 0.0f;
    if (state->use_z)  // state + 0x86 (byte)
        out_z = *(float*)((int)this_ptr + 0xA0);
    if (state->use_y)  // state + 0x85
        out_y = *(float*)((int)this_ptr + 0x9C);
    if (state->use_x)  // state + 0x84
        out_x = *(float*)((int)this_ptr + 0x98);

    FUN_10166690(this_ptr, out_x, out_y, out_z);

    // ---- Subcontainer blending ----
    // Only if subcontainer key range is non-empty
    if (state->subcontainer_begin != state->subcontainer_end) {  // state + 0xA0 vs 0xA4
        int sc_frame = container_lookup(
            *(void**)((int)this_ptr + 0xDC),
            state->subcontainer_begin);
        int sc_index = container_idx((void*)sc_frame);

        // Verify there are at least 3 entries in the frame array
        int frame_begin = *(int*)((int)this_ptr + 0xEC);
        int frame_end   = *(int*)((int)this_ptr + 0xF0);
        int frame_count = (frame_end - frame_begin) / 12;  // 12-byte stride

        int sc_entry_ptr;
        if (frame_count < 3) {
            error_log("Subcontainer index error %i");
            sc_entry_ptr = frame_begin;  // data_begin
        } else {
            sc_entry_ptr = frame_begin + 0x18;  // entry at index 2 (third entry)
        }

        // Compute entry count for the selected entry
        int entry_begin = *(int*)(sc_entry_ptr + 4);
        int entry_end   = *(int*)(sc_entry_ptr + 8);
        int entry_count = (entry_end - entry_begin) / 12;

        // Get Vec3 from the selected entry
        void* entry_vec;
        if (sc_index < entry_count) {
            entry_vec = (void*)(entry_begin + sc_index * 12);
        } else {
            error_log("Subcontainer index error %i");
            entry_vec = *(void**)(sc_entry_ptr + 4);  // data_begin
        }

        vec3_copy(displacement, entry_vec);  // auStack_38 = subcontainer Vec3

        // Transform by both frame Vec3 offsets
        float blend_a[3], blend_b[3];  // afStack_5c, afStack_68
        some_transform(displacement, blend_a, (void*)get_frame_vec3_1((void*)sc_frame));
        some_transform(displacement, blend_b, (void*)get_frame_vec3_2((void*)sc_frame));

        // Get source data and copy to temp buffer for iteration
        int* src_data = (int*)get_data_ptr(*(int*)((int)this_ptr + 0xDC));
        int src_end = src_data[1];
        int src_begin = src_data[0];
        int data_count = (src_end - src_begin) >> 2;  // divide by 4

        int* tmp_buf;
        guard_init(&unused_float);                     // FUN_101ec1c0 — scope guard
        temp_alloc((void**)&tmp_buf, data_count);      // FUN_101aefe0

        // Copy data
        int* write_ptr = tmp_buf;
        if ((void*)src_end != (void*)src_begin) {
            int copy_size = src_end - src_begin;
            void* copied = memmove(tmp_buf, (void*)src_begin, copy_size);
            write_ptr = (int*)((int)copied + copy_size);
        }
        int* buf_end = write_ptr;

        // Sum all keyframe transforms
        int entry_count2 = ((int)buf_end - (int)tmp_buf) >> 2;
        if (entry_count2 != 0) {
            int* read_ptr = tmp_buf;
            while (read_ptr < buf_end) {
                int frame_id = *read_ptr;  // first element is the frame id

                // NOTE: binary does FUN_1028e6b0(frame_vec, blend_array) → frame_vec += blend_array
                // (modifies frame data in-place; the blend arrays are the source)
                vec3_add((void*)get_frame_vec3_1((void*)frame_id), blend_a);
                vec3_add((void*)get_frame_vec3_2((void*)frame_id), blend_b);

                read_ptr++;
                // NOTE: each iteration reads one int from the buffer.
                // The loop iterates tmp_buf entries; each entry is a frame id.
            }
        }

        temp_free(tmp_buf);  // FUN_102f7780 — scope-guarded cleanup
    }

    // ---- SEH epilogue ----
    // ExceptionList restored (implicit with scoped exit in C++)
    return;
}
// ============================================================================
// NOTES:
//
// 1) some_transform (FUN_1028e890) has signature:
//    void* __thiscall some_transform(void* this, void* dst, const void* src)
//    where:
//      - Computes dst = this - src (component-wise, all 3 floats)
//      - Calls FUN_1028d850 to initialize dst, then sets dst_z = this_z - src_z
//      - The xy difference may use the same subtraction via FUN_1028d850 (#205 area)
//        but the effect is dst[xyz] = this[xyz] - src[xyz]
//    In the main path, `this` = &offset (address of a single float on stack), so
//    this[0]=offset, this[1..2] reads past declared stack variables (padding).
//
// 2) state = *(struct AnimState**)(*(int**)(this_ptr + 0x20) + 0x94 / 4)
//    Indirection chain: this+0x20 → internal struct → +0x94 → working state.
//
// 3) The subcontainer blending loop copies all frame IDs into a temp buffer,
//    transforms each by the displacement Vec3, and accumulates.
//    If there is no gap between subcontainer_begin/end, the whole block is skipped.
//
// 4) g_some_axis_str (DAT_103877fc) is a string reference used with
//    memcmp_str (FUN_1000cc00) to match axis names.
//    If the returned value matches (sign == 1), it selects this+0xE0 (X displacement);
//    otherwise this+0xE4 (Y displacement).
//
// 5) ZERO_VEC3 (DAT_1065fb74) is 12 zero bytes used as a Vec3 null constant.
//
// ============================================================================
