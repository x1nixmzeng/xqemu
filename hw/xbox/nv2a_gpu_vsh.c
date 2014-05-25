/*
 * QEMU Geforce NV2A GPU vertex shader translation
 *
 * Copyright (c) 2014 Jannik Vogel
 * Copyright (c) 2012 espes
 *
 * Based on:
 * Cxbx, VertexShader.cpp
 * Copyright (c) 2004 Aaron Robinson <caustik@caustik.com>
 *                    Kingofc <kingofc@freenet.de>
 * Dxbx, uPushBuffer.pas
 * Copyright (c) 2007 Shadow_tj, PatrickvL
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#include "hw/xbox/nv2a_gpu_vsh.h"

#define VSH_D3DSCM_CORRECTION 96

#define VSH_TOKEN_SIZE 4

typedef enum {
    FLD_ILU = 0,
    FLD_MAC,
    FLD_CONST,
    FLD_V,
    // Input A
    FLD_A_NEG,
    FLD_A_SWZ_X,
    FLD_A_SWZ_Y,
    FLD_A_SWZ_Z,
    FLD_A_SWZ_W,
    FLD_A_R,
    FLD_A_MUX,
    // Input B
    FLD_B_NEG,
    FLD_B_SWZ_X,
    FLD_B_SWZ_Y,
    FLD_B_SWZ_Z,
    FLD_B_SWZ_W,
    FLD_B_R,
    FLD_B_MUX,
    // Input C
    FLD_C_NEG,
    FLD_C_SWZ_X,
    FLD_C_SWZ_Y,
    FLD_C_SWZ_Z,
    FLD_C_SWZ_W,
    FLD_C_R_HIGH,
    FLD_C_R_LOW,
    FLD_C_MUX,
    // Output
    FLD_OUT_MAC_MASK,
    FLD_OUT_R,
    FLD_OUT_ILU_MASK,
    FLD_OUT_O_MASK,
    FLD_OUT_ORB,
    FLD_OUT_ADDRESS,
    FLD_OUT_MUX,
    // Relative addressing
    FLD_A0X,
    // Final instruction
    FLD_FINAL
} VshFieldName;


typedef enum {
    PARAM_UNKNOWN = 0,
    PARAM_R,
    PARAM_V,
    PARAM_C
} VshParameterType;

typedef enum {
    OUTPUT_C = 0,
    OUTPUT_O
} VshOutputType;

typedef enum {
    OMUX_MAC = 0,
    OMUX_ILU
} VshOutputMux;

typedef enum {
    ILU_NOP = 0,
    ILU_MOV,
    ILU_RCP,
    ILU_RCC,
    ILU_RSQ,
    ILU_EXP,
    ILU_LOG,
    ILU_LIT
} VshILU;

typedef enum {
    MAC_NOP,
    MAC_MOV,
    MAC_MUL,
    MAC_ADD,
    MAC_MAD,
    MAC_DP3,
    MAC_DPH,
    MAC_DP4,
    MAC_DST,
    MAC_MIN,
    MAC_MAX,
    MAC_SLT,
    MAC_SGE,
    MAC_ARL
} VshMAC;

typedef enum {
    SWIZZLE_X = 0,
    SWIZZLE_Y,
    SWIZZLE_Z,
    SWIZZLE_W
} VshSwizzle;


typedef struct VshFieldMapping {
    VshFieldName field_name;
    uint8_t subtoken;
    uint8_t start_bit;
    uint8_t bit_length;
} VshFieldMapping;

static const VshFieldMapping field_mapping[] = {
    // Field Name         DWORD BitPos BitSize
    {  FLD_ILU,              1,   25,     3 },
    {  FLD_MAC,              1,   21,     4 },
    {  FLD_CONST,            1,   13,     8 },
    {  FLD_V,                1,    9,     4 },
    // INPUT A
    {  FLD_A_NEG,            1,    8,     1 },
    {  FLD_A_SWZ_X,          1,    6,     2 },
    {  FLD_A_SWZ_Y,          1,    4,     2 },
    {  FLD_A_SWZ_Z,          1,    2,     2 },
    {  FLD_A_SWZ_W,          1,    0,     2 },
    {  FLD_A_R,              2,   28,     4 },
    {  FLD_A_MUX,            2,   26,     2 },
    // INPUT B
    {  FLD_B_NEG,            2,   25,     1 },
    {  FLD_B_SWZ_X,          2,   23,     2 },
    {  FLD_B_SWZ_Y,          2,   21,     2 },
    {  FLD_B_SWZ_Z,          2,   19,     2 },
    {  FLD_B_SWZ_W,          2,   17,     2 },
    {  FLD_B_R,              2,   13,     4 },
    {  FLD_B_MUX,            2,   11,     2 },
    // INPUT C
    {  FLD_C_NEG,            2,   10,     1 },
    {  FLD_C_SWZ_X,          2,    8,     2 },
    {  FLD_C_SWZ_Y,          2,    6,     2 },
    {  FLD_C_SWZ_Z,          2,    4,     2 },
    {  FLD_C_SWZ_W,          2,    2,     2 },
    {  FLD_C_R_HIGH,         2,    0,     2 },
    {  FLD_C_R_LOW,          3,   30,     2 },
    {  FLD_C_MUX,            3,   28,     2 },
    // Output
    {  FLD_OUT_MAC_MASK,     3,   24,     4 },
    {  FLD_OUT_R,            3,   20,     4 },
    {  FLD_OUT_ILU_MASK,     3,   16,     4 },
    {  FLD_OUT_O_MASK,       3,   12,     4 },
    {  FLD_OUT_ORB,          3,   11,     1 },
    {  FLD_OUT_ADDRESS,      3,    3,     8 },
    {  FLD_OUT_MUX,          3,    2,     1 },
    // Other
    {  FLD_A0X,              3,    1,     1 },
    {  FLD_FINAL,            3,    0,     1 }
};


typedef struct VshOpcodeParams {
    bool A;
    bool B;
    bool C;
} VshOpcodeParams;

static const VshOpcodeParams ilu_opcode_params[] = {
    /* ILU OP       ParamA ParamB ParamC */
    /* ILU_NOP */ { false, false, false }, // Dxbx note : Unused
    /* ILU_MOV */ { false, false, true  },
    /* ILU_RCP */ { false, false, true  },
    /* ILU_RCC */ { false, false, true  },
    /* ILU_RSQ */ { false, false, true  },
    /* ILU_EXP */ { false, false, true  },
    /* ILU_LOG */ { false, false, true  },
    /* ILU_LIT */ { false, false, true  },
};

static const VshOpcodeParams mac_opcode_params[] = {
    /* MAC OP      ParamA  ParamB ParamC */
    /* MAC_NOP */ { false, false, false }, // Dxbx note : Unused
    /* MAC_MOV */ { true,  false, false },
    /* MAC_MUL */ { true,  true,  false },
    /* MAC_ADD */ { true,  false, true  },
    /* MAC_MAD */ { true,  true,  true  },
    /* MAC_DP3 */ { true,  true,  false },
    /* MAC_DPH */ { true,  true,  false },
    /* MAC_DP4 */ { true,  true,  false },
    /* MAC_DST */ { true,  true,  false },
    /* MAC_MIN */ { true,  true,  false },
    /* MAC_MAX */ { true,  true,  false },
    /* MAC_SLT */ { true,  true,  false },
    /* MAC_SGE */ { true,  true,  false },
    /* MAC_ARL */ { true,  false, false },
};


#if 0
static const char* mask_str[] = {
            // xyzw xyzw
    "",     // 0000 ____
    ".waaa",   // 0001 ___w
    ".zaaa",   // 0010 __z_
    ".zwaa",  // 0011 __zw
    ".yaaa",   // 0100 _y__
    ".ywaa",  // 0101 _y_w
    ".yzaa",  // 0110 _yz_
    ".yzwa", // 0111 _yzw
    ".xaaa",   // 1000 x___
    ".xwaa",  // 1001 x__w
    ".xzaa",  // 1010 x_z_
    ".xzwa", // 1011 x_zw
    ".xyaa",  // 1100 xy__
    ".xywa", // 1101 xy_w
    ".xyza", // 1110 xyz_
    ""//.xyzw  1111 xyzw
};
#else
static const char* mask_str[] = {
            // xyzw xyzw
    "",     // 0000 ____
    ",w",   // 0001 ___w
    ",z",   // 0010 __z_
    ",zw",  // 0011 __zw
    ",y",   // 0100 _y__
    ",yw",  // 0101 _y_w
    ",yz",  // 0110 _yz_
    ",yzw", // 0111 _yzw
    ",x",   // 1000 x___
    ",xw",  // 1001 x__w
    ",xz",  // 1010 x_z_
    ",xzw", // 1011 x_zw
    ",xy",  // 1100 xy__
    ",xyw", // 1101 xy_w
    ",xyz", // 1110 xyz_
    ",xyzw"//.xyzw  1111 xyzw
};
#endif

/* Note: OpenGL seems to be case-sensitive, and requires upper-case opcodes! */
static const char* mac_opcode[] = {
    "NOP",
    "MOV",
    "MUL",
    "ADD",
    "MAD",
    "DP3",
    "DPH",
    "DP4",
    "DST",
    "MIN",
    "MAX",
    "SLT",
    "SGE",
    "ARL A0.x", // Dxbx note : Alias for "mov a0.x"
};

static const char* ilu_opcode[] = {
    "NOP",
    "MOV",
    "RCP",
    "RCC",
    "RSQ",
    "EXP",
    "LOG",
    "LIT",
};

static bool ilu_force_scalar[] = {
    false,
    false,
    true,
    true,
    true,
    true,
    true,
    false,
};

static const char* out_reg_name[] = {
    "oPos",
    "???",
    "???",
    "oD0",
    "oD1",
    "oFog",
    "oPts",
    "oB0",
    "oB1",
    "oT0",
    "oT1",
    "oT2",
    "oT3",
    "???",
    "???",
    "A0.x",
};



// Retrieves a number of bits in the instruction token
static int vsh_get_from_token(uint32_t *shader_token,
                              uint8_t subtoken,
                              uint8_t start_bit,
                              uint8_t bit_length)
{
    return (shader_token[subtoken] >> start_bit) & ~(0xFFFFFFFF << bit_length);
}
static uint8_t vsh_get_field(uint32_t *shader_token, VshFieldName field_name)
{

    return (uint8_t)(vsh_get_from_token(shader_token,
                                        field_mapping[field_name].subtoken,
                                        field_mapping[field_name].start_bit,
                                        field_mapping[field_name].bit_length));
}


// Converts the C register address to disassembly format
static int16_t convert_c_register(const int16_t c_reg)
{
    int16_t r = ((((c_reg >> 5) & 7) - 3) * 32) + (c_reg & 31);
    r += VSH_D3DSCM_CORRECTION; /* to map -96..95 to 0..191 */
    return r; //FIXME: = c_reg?!
}



static QString* decode_swizzle(uint32_t *shader_token,
                               VshFieldName swizzle_field)
{
    const char* swizzle_str = "xyzw";
    VshSwizzle x, y, z, w;

    /* some microcode instructions force a scalar value */
    if (swizzle_field == FLD_C_SWZ_X
        && ilu_force_scalar[vsh_get_field(shader_token, FLD_ILU)]) {
        x = y = z = w = vsh_get_field(shader_token, swizzle_field);
    } else {
        x = vsh_get_field(shader_token, swizzle_field++);
        y = vsh_get_field(shader_token, swizzle_field++);
        z = vsh_get_field(shader_token, swizzle_field++);
        w = vsh_get_field(shader_token, swizzle_field);
    }

    if (x == SWIZZLE_X && y == SWIZZLE_Y
        && z == SWIZZLE_Z && w == SWIZZLE_W) {
        /* Don't print the swizzle if it's .xyzw */
        return qstring_from_str(""); // Will turn ".xyzw" into "."
    /* Don't print duplicates */
    } else if (x == y && y == z && z == w) {
        return qstring_from_str((char[]){'.', swizzle_str[x], '\0'});
#if 0
    } else if (x == y && z == w) {
        return qstring_from_str((char[]){'.',
            swizzle_str[x], swizzle_str[y], '\0'}); //FIXME: !!!! Would turn ".xxyy" into ".xy" ?! !!!!
    /* } else if (z == w) {
        return qstring_from_str((char[]){'.',
            swizzle_str[x], swizzle_str[y], swizzle_str[z], '\0'}); */
#endif
    } else {
        return qstring_from_str((char[]){'.',
                                       swizzle_str[x], swizzle_str[y],
                                       swizzle_str[z], swizzle_str[w],
                                       '\0'}); // Normal swizzle mask
    }
}

static QString* decode_opcode_input(uint32_t *shader_token,
                                    VshParameterType param,
                                    VshFieldName neg_field,
                                    int reg_num)
{
    /* This function decodes a vertex shader opcode parameter into a string.
     * Input A, B or C is controlled via the Param and NEG fieldnames,
     * the R-register address for each input is already given by caller. */

    QString *ret_str = qstring_new();


    if (vsh_get_field(shader_token, neg_field) > 0) {
        qstring_append_chr(ret_str, '-');
    }

    /* PARAM_R uses the supplied reg_num, but the other two need to be
     * determined */
    char tmp[40];
    switch (param) {
    case PARAM_R:
        snprintf(tmp, sizeof(tmp), "R%d", reg_num);
        break;
    case PARAM_V:
        reg_num = vsh_get_field(shader_token, FLD_V);
        snprintf(tmp, sizeof(tmp), "v%d", reg_num);
        break;
    case PARAM_C:
        reg_num = convert_c_register(vsh_get_field(shader_token, FLD_CONST));
        if (vsh_get_field(shader_token, FLD_A0X) > 0) {
            snprintf(tmp, sizeof(tmp), "c[A0+%d]", reg_num); //FIXME: does this really require the "correction" doe in convert_c_register?!
        } else {
            snprintf(tmp, sizeof(tmp), "c[%d]", reg_num);
        }
        break;
    default:
        printf("Param: 0x%x\n", param);
        assert(false);
    }
    qstring_append(ret_str, tmp);

    {
        /* swizzle bits are next to the neg bit */
        QString *swizzle_str = decode_swizzle(shader_token, neg_field+1);
        qstring_append(ret_str, qstring_get_str(swizzle_str));
        QDECREF(swizzle_str);
    }

    return ret_str;
}


static QString* decode_opcode(uint32_t *shader_token,
                              VshOutputMux out_mux,
                              uint32_t mask,
                              const char* opcode,
                              QString *inputs)
{
    QString *ret = qstring_new();
    int reg_num = vsh_get_field(shader_token, FLD_OUT_R);

    /* Test for paired opcodes (in other words : Are both <> NOP?) */
    if (out_mux == OMUX_MAC
          &&  vsh_get_field(shader_token, FLD_ILU) != ILU_NOP
          && reg_num == 1) {
        /* Ignore paired MAC opcodes that write to R1 */
        mask = 0;
    } else if (out_mux == OMUX_ILU
               && vsh_get_field(shader_token, FLD_MAC) != MAC_NOP) {
        /* Paired ILU opcodes can only write to R1 */
        reg_num = 1;
    }

    if (mask > 0) {
        if (strcmp(opcode, mac_opcode[MAC_ARL]) == 0) {
            qstring_append(ret, "  ARL(a0");
            qstring_append(ret, qstring_get_str(inputs));
            qstring_append(ret, ";\n");
        } else {
            qstring_append(ret, "  ");
            qstring_append(ret, opcode);
            qstring_append(ret, "(");
            qstring_append(ret, "R");
            qstring_append_int(ret, reg_num);
            qstring_append(ret, mask_str[mask]);
            qstring_append(ret, qstring_get_str(inputs));
            qstring_append(ret, ");\n");
        }
    }

    /* See if we must add a muxed opcode too: */
    if (vsh_get_field(shader_token, FLD_OUT_MUX) == out_mux
        /* Only if it's not masked away: */
        && vsh_get_field(shader_token, FLD_OUT_O_MASK) != 0) {

        qstring_append(ret, "  ");
        qstring_append(ret, opcode);
        qstring_append(ret, "(");

        if (vsh_get_field(shader_token, FLD_OUT_ORB) == OUTPUT_C) {
            /* TODO : Emulate writeable const registers */
            qstring_append(ret, "c");
            qstring_append_int(ret,
                convert_c_register(
                    vsh_get_field(shader_token, FLD_OUT_ADDRESS)));
        } else {
            qstring_append(ret,
                out_reg_name[
                    vsh_get_field(shader_token, FLD_OUT_ADDRESS) & 0xF]);
        }
        qstring_append(ret,
            mask_str[
                vsh_get_field(shader_token, FLD_OUT_O_MASK)]);
        qstring_append(ret, qstring_get_str(inputs));
        qstring_append(ret, ");\n");
    }

    return ret;
}


static QString* decode_token(uint32_t *shader_token)
{
    QString *ret;

    /* Since it's potentially used twice, decode input C once: */
    QString *input_c =
        decode_opcode_input(shader_token,
                            vsh_get_field(shader_token, FLD_C_MUX),
                            FLD_C_NEG,
                            (vsh_get_field(shader_token, FLD_C_R_HIGH) << 2)
                                | vsh_get_field(shader_token, FLD_C_R_LOW));

    /* See what MAC opcode is written to (if not masked away): */
    VshMAC mac = vsh_get_field(shader_token, FLD_MAC);
    if (mac != MAC_NOP) {
        QString *inputs_mac = qstring_new();
        if (mac_opcode_params[mac].A) {
            QString *input_a =
                decode_opcode_input(shader_token,
                                    vsh_get_field(shader_token, FLD_A_MUX),
                                    FLD_A_NEG,
                                    vsh_get_field(shader_token, FLD_A_R));
            qstring_append(inputs_mac, ", ");
            qstring_append(inputs_mac, qstring_get_str(input_a));
            QDECREF(input_a);
        }
        if (mac_opcode_params[mac].B) {
            QString *input_b =
                decode_opcode_input(shader_token,
                                    vsh_get_field(shader_token, FLD_B_MUX),
                                    FLD_B_NEG,
                                    vsh_get_field(shader_token, FLD_B_R));
            qstring_append(inputs_mac, ", ");
            qstring_append(inputs_mac, qstring_get_str(input_b));
            QDECREF(input_b);
        }
        if (mac_opcode_params[mac].C) {
            qstring_append(inputs_mac, ", ");
            qstring_append(inputs_mac, qstring_get_str(input_c));
        }

        /* Then prepend these inputs with the actual opcode, mask, and input : */
        ret = decode_opcode(shader_token,
                            OMUX_MAC,
                            vsh_get_field(shader_token, FLD_OUT_MAC_MASK),
                            mac_opcode[mac],
                            inputs_mac);
        QDECREF(inputs_mac);
    } else {
        ret = qstring_new();
    }

    /* See if a ILU opcode is present too: */
    VshILU ilu = vsh_get_field(shader_token, FLD_ILU);
    if (ilu != ILU_NOP) {
        QString *inputs_c = qstring_from_str(", ");
        qstring_append(inputs_c, qstring_get_str(input_c));

        /* Append the ILU opcode, mask and (the already determined) input C: */
        QString *ilu_op =
            decode_opcode(shader_token,
                          OMUX_ILU,
                          vsh_get_field(shader_token, FLD_OUT_ILU_MASK),
                          ilu_opcode[ilu],
                          inputs_c);

        qstring_append(ret, qstring_get_str(ilu_op));

        QDECREF(inputs_c);
        QDECREF(ilu_op);
    }

    QDECREF(input_c);

    return ret;
}

/* Vertex shader header, mapping Xbox1 registers to the ARB syntax (original
 * version by KingOfC). Note about the use of 'conventional' attributes in here:
 * Since we prefer to use only one shader for both immediate and deferred mode
 * rendering, we alias all attributes to conventional inputs as much as possible.
 * Only when there's no conventional attribute available, we use generic
 * attributes. So in the following header, we use conventional attributes first,
 * and generic attributes for the rest of the vertex attribute slots. This makes
 * it possible to support immediate and deferred mode rendering with the same
 * shader, and the use of the OpenGL fixed-function pipeline without a shader.
 */
static const char* vsh_header =
    "#version 110\n"
    "\n"
    //FIXME: I just assumed this is true for all registers?!
    "vec4 R0 = vec4(0.0,0.0,0.0,1.0);\n"
    "vec4 R1 = vec4(0.0,0.0,0.0,1.0);\n"
    "vec4 R2 = vec4(0.0,0.0,0.0,1.0);\n"
    "vec4 R3 = vec4(0.0,0.0,0.0,1.0);\n"
    "vec4 R4 = vec4(0.0,0.0,0.0,1.0);\n"
    "vec4 R5 = vec4(0.0,0.0,0.0,1.0);\n"
    "vec4 R6 = vec4(0.0,0.0,0.0,1.0);\n"
    "vec4 R7 = vec4(0.0,0.0,0.0,1.0);\n"
    "vec4 R8 = vec4(0.0,0.0,0.0,1.0);\n"
    "vec4 R9 = vec4(0.0,0.0,0.0,1.0);\n"
    "vec4 R10 = vec4(0.0,0.0,0.0,1.0);\n"
    "vec4 R11 = vec4(0.0,0.0,0.0,1.0);\n"
    "vec4 R12 = vec4(0.0,0.0,0.0,1.0);\n"
    "\n"
    //FIXME: What is a0 initialized as?
    "int A0 = 0;\n"
    "\n"
#if 0
    "ATTRIB v0 = vertex.position;" // (See "conventional" note above)
    "ATTRIB v1 = vertex.%s;" // Note : We replace this with "weight" or "attrib[1]" depending GL_ARB_vertex_blend
    "ATTRIB v2 = vertex.normal;"
    "ATTRIB v3 = vertex.color.primary;"
    "ATTRIB v4 = vertex.color.secondary;"
    "ATTRIB v5 = vertex.fogcoord;"
    "ATTRIB v6 = vertex.attrib[6];"
    "ATTRIB v7 = vertex.attrib[7];"
    "ATTRIB v8 = vertex.texcoord[0];"
    "ATTRIB v9 = vertex.texcoord[1];"
    "ATTRIB v10 = vertex.texcoord[2];"
    "ATTRIB v11 = vertex.texcoord[3];"
#else
    "attribute vec4 v0;\n"
    "attribute vec4 v1;\n"
    "attribute vec4 v2;\n"
    "attribute vec4 v3;\n"
    "attribute vec4 v4;\n"
    "attribute vec4 v5;\n"
    "attribute vec4 v6;\n"
    "attribute vec4 v7;\n"
    "attribute vec4 v8;\n"
    "attribute vec4 v9;\n"
    "attribute vec4 v10;\n"
    "attribute vec4 v11;\n"
#endif
    "attribute vec4 v12;\n"
    "attribute vec4 v13;\n"
    "attribute vec4 v14;\n"
    "attribute vec4 v15;\n"

    "\n"

/*
//FIXME: temp var?
    "OUTPUT oPos = result.position;\n"
    "OUTPUT oD0 = result.color.front.primary;\n"
    "OUTPUT oD1 = result.color.front.secondary;\n"
    "OUTPUT oB0 = result.color.back.primary;\n"
    "OUTPUT oB1 = result.color.back.secondary;\n"
    "OUTPUT oPts = result.pointsize;\n"
    "OUTPUT oFog = result.fogcoord;\n"
    "OUTPUT oT0 = result.texcoord[0];\n"
    "OUTPUT oT1 = result.texcoord[1];\n"
    "OUTPUT oT2 = result.texcoord[2];\n"
    "OUTPUT oT3 = result.texcoord[3];\n"
*/
    "#define oPos R12 /* oPos is a mirror of R12 */\n"
    "vec4 oD0 = vec4(0.0,0.0,0.0,1.0);\n"
    "vec4 oD1 = vec4(0.0,0.0,0.0,1.0);\n"
    "vec4 oB0 = vec4(0.0,0.0,0.0,1.0);\n"
    "vec4 oB1 = vec4(0.0,0.0,0.0,1.0);\n"
    "vec4 oPts = vec4(0.0,0.0,0.0,1.0);\n"
    "vec4 oFog = vec4(0.0,0.0,0.0,1.0);\n"
    "vec4 oT0 = vec4(0.0,0.0,0.0,1.0);\n"
    "vec4 oT1 = vec4(0.0,0.0,0.0,1.0);\n"
    "vec4 oT2 = vec4(0.0,0.0,0.0,1.0);\n"
    "vec4 oT3 = vec4(0.0,0.0,0.0,1.0);\n"

    "\n"

    /* All constants in 1 array declaration */
//FIXME: it's probably wise to change the c[x] to c##x later because it forces us to allocate and reupload around 100*4*4 bytes (~1.5kB) of useless data on/to the GPU :P
   "uniform vec4 c[192];\n"
   "#define viewport_scale c[58] /* This seems to be hardwired? See comment in nv2a_gpu.c */\n"
   "#define viewport_offset c[59] /* Same as above */\n"
   "uniform vec2 cliprange;\n"

/*


FIXME: !!!!!! MAJOR BUG !!!!!!
JayFoxRox: mhhh I believe there is a bug in my glsl stuff too I didn't even think about before
JayFoxRox: but if mask is yz it would result in: dest.yz = OP().yz when it should be dest.yz = OP().xy



*/

// Code from pages linked here http://msdn.microsoft.com/en-us/library/windows/desktop/bb174703%28v=vs.85%29.aspx
// and also https://www.opengl.org/registry/specs/NV/vertex_program1_1.txt
// Some code was also written from scratch because it seemed easy - if you are bored verify the behaviour!
    "\n"
    /* Oh boy.. Let's hope these are optimized away! */
    "/* Converts number of components of rvalue to lvalue */\n"
    "float components(float l, vec4 r) { return r.x; }\n"
    "vec2 components(vec2 l, vec4 r) { return r.xy; }\n"
    "vec3 components(vec3 l, vec4 r) { return r.xyz; }\n"
    "vec4 components(vec4 l, vec4 r) { return r.xyzw; }\n"
    "\n"
    "#define MOV(dest,mask, src) dest.mask = components(dest.mask,_MOV(vec4(src)))\n"
    "vec4 _MOV(vec4 src)\n" 
    "{\n"
    "  return src;\n"
    "}\n"
    "\n"
    "#define MUL(dest,mask, src0, src1) dest.mask = components(dest.mask,_MUL(vec4(src0), vec4(src1)))\n"
    "vec4 _MUL(vec4 src0, vec4 src1)\n" 
    "{\n"
    "  return src0 * src1;\n"
    "}\n"
    "\n"
    "#define ADD(dest,mask, src0, src1) dest.mask = components(dest.mask,_ADD(vec4(src0), vec4(src1)))\n"
    "vec4 _ADD(vec4 src0, vec4 src1)\n" 
    "{\n"
    "  return src0 + src1;\n"
    "}\n"
    "\n"
    "#define MAD(dest,mask, src0, src1, src2) dest.mask = components(dest.mask,_MAD(vec4(src0), vec4(src1), vec4(src2)))\n"
    "vec4 _MAD(vec4 src0, vec4 src1, vec4 src2)\n" 
    "{\n"
    "  return src0 * src1 + src2;\n"
    "}\n"
    "\n"
    "#define DP3(dest,mask, src0, src1) dest.mask = components(dest.mask,_DP3(vec4(src0), vec4(src1)))\n"
    "vec4 _DP3(vec4 src0, vec4 src1)\n" 
    "{\n"
    "  return vec4(dot(src0.xyz, src1.xyz));\n"
    "}\n"
    "\n"
    "#define DPH(dest,mask, src0, src1) dest.mask = components(dest.mask,_DPH(vec4(src0), vec4(src1)))\n"
    "vec4 _DPH(vec4 src0, vec4 src1)\n" 
    "{\n"
    "  return vec4(dot(vec4(src0.xyz, 1.0), src1));\n"
    "}\n"
    "\n"
    "#define DP4(dest,mask, src0, src1) dest.mask = components(dest.mask,_DP4(vec4(src0), vec4(src1)))\n"
    "vec4 _DP4(vec4 src0, vec4 src1)\n" 
    "{\n"
    "  return vec4(dot(src0, src1));\n"
    "}\n"
    "\n"
    "#define DST(dest,mask, src0, src1) dest.mask = components(dest.mask,_DST(vec4(src0), vec4(src1)))\n"
    "vec4 _DST(vec4 src0, vec4 src1)\n" 
    "{\n"
    "  return vec4(1.0,\n"
    "              src0.y * src1.y,\n"
    "              src0.z,\n"
    "              src1.w);\n"
    "}\n"
    "\n"
    "#define MIN(dest,mask, src0, src1) dest.mask = components(dest.mask,_MIN(vec4(src0), vec4(src1)))\n"
    "vec4 _MIN(vec4 src0, vec4 src1)\n" 
    "{\n"
    "  return min(src0, src1);\n"
    "}\n"
    "\n"
    "#define MAX(dest,mask, src0, src1) dest.mask = components(dest.mask,_MAX(vec4(src0), vec4(src1)))\n"
    "vec4 _MAX(vec4 src0, vec4 src1)\n" 
    "{\n"
    "  return max(src0, src1);\n"
    "}\n"
    "\n"
    "#define SLT(dest,mask, src0, src1) dest.mask = components(dest.mask,_SLT(vec4(src0), vec4(src1)))\n"
    "vec4 _SLT(vec4 src0, vec4 src1)\n" 
    "{\n"
    "  return vec4(src0.x < src1.x ? 1.0 : 0.0,\n"
    "              src0.y < src1.y ? 1.0 : 0.0,\n"
    "              src0.z < src1.z ? 1.0 : 0.0,\n"
    "              src0.w < src1.w ? 1.0 : 0.0);\n"
    "}\n"
    "\n"
    "#define ARL(dest,mask, src) dest = _ARL(vec4(src).x)\n"
    "int _ARL(float src)\n" 
    "{\n"
    "  return int(src);\n"
    "}\n"
    "\n"
    "#define SGE(dest,mask, src0, src1) dest.mask = components(dest.mask,_SGE(vec4(src0), vec4(src1)))\n"
    "vec4 _SGE(vec4 src0, vec4 src1)\n" 
    "{\n"
    "  return vec4(src0.x >= src1.x ? 1.0 : 0.0,\n"
    "              src0.y >= src1.y ? 1.0 : 0.0,\n"
    "              src0.z >= src1.z ? 1.0 : 0.0,\n"
    "              src0.w >= src1.w ? 1.0 : 0.0);\n"
    "}\n"
    "\n"
    "#define RCP(dest,mask, src) dest.mask = components(dest.mask,_RCP(vec4(src).x))\n"
    "vec4 _RCP(float src)\n" 
    "{\n"
    "  return vec4(1.0 / src);\n"
    "}\n"
    "\n"
    "#define RCC(dest,mask, src) dest.mask = components(dest.mask,_RCC(vec4(src).x))\n"
    "vec4 _RCC(float src)\n" 
    "{\n"
    "  float t = 1.0 / src;\n"
    "  if (t > 0.0) {\n"
    "    t = min(t, 1.884467e+019);\n"
    "    t = max(t, 5.42101e-020);\n"
    "  } else {\n"
    "    t = max(t, -1.884467e+019);\n"
    "    t = min(t, -5.42101e-020);\n"
    "  }\n"
    "  return vec4(t);\n"
    "}\n"
    "\n"
    "#define RSQ(dest,mask, src) dest.mask = components(dest.mask,_RSQ(vec4(src).x))\n"
    "vec4 _RSQ(float src)\n" 
    "{\n"
    "  return vec4(1.0 / sqrt(src));\n"
    "}\n"
    "\n"
    "#define EXP(dest,mask, src) dest.mask = components(dest.mask,_EXP(vec4(src).x))\n"
    "vec4 _EXP(float src)\n" 
    "{\n"
    "  return vec4(exp2(src));\n"
    "}\n"
    "\n"
    "#define LOG(dest,mask, src) dest.mask = components(dest.mask,_LOG(vec4(src).x))\n"
    "vec4 _LOG(float src)\n" 
    "{\n"
    "  return vec4(log2(src));\n"
    "}\n"
    "\n"
    "#define LIT(dest,mask, src) dest.mask = components(dest.mask,_LIT(vec4(src)))\n"
    "vec4 _LIT(vec4 src)\n" 
    "{\n"
    "  vec4 t = vec4(1.0, 0.0, 0.0, 1.0);\n"
    "  float power = src.w;\n"
#if 0
    //XXX: Limitation for 8.8 fixed point
    "  power = max(power, -127.9961);\n"
    "  power = min(power, 127.9961);\n"
#endif
    "  if (src.x > 0.0) {\n"
    "    t.y = src.x;\n"
    "    if (src.y > 0.0) {\n"
    //XXX: Allowed approximation is EXP(power * LOG(src.y))
    "      t.z = pow(src.y, power);\n"
    "    }\n"
    "  }\n"
    "  return t;\n"
    "}\n";

QString* vsh_translate(uint16_t version,
                       uint32_t *tokens, unsigned int tokens_length)
{
    QString *body = qstring_from_str("\n");
    QString *header = qstring_from_str(vsh_header);
                          
#ifdef DEBUG_NV2A_GPU_SHADER_FEEDBACK
    qstring_append(header,
                   "\n"
                   "/* Debug stuff */\n"
                   "varying vec4 debug_v0;\n"
                   "varying vec4 debug_v1;\n"
                   "varying vec4 debug_v2;\n"
                   "varying vec4 debug_v3;\n"
                   "varying vec4 debug_v4;\n"
                   "varying vec4 debug_v5;\n"
                   "varying vec4 debug_v6;\n"
                   "varying vec4 debug_v7;\n"
                   "varying vec4 debug_v8;\n"
                   "varying vec4 debug_v9;\n"
                   "varying vec4 debug_v10;\n"
                   "varying vec4 debug_v11;\n"
                   "varying vec4 debug_v12;\n"
                   "varying vec4 debug_v13;\n"
                   "varying vec4 debug_v14;\n"
                   "varying vec4 debug_v15;\n"
                   "varying vec4 debug_oPos;\n"
                   "varying vec4 debug_oD0;\n"
                   "varying vec4 debug_oD1;\n"
                   "varying vec4 debug_oB0;\n"
                   "varying vec4 debug_oB1;\n"
                   "varying vec4 debug_oPts;\n"
                   "varying vec4 debug_oFog;\n"
                   "varying vec4 debug_oT0;\n"
                   "varying vec4 debug_oT1;\n"
                   "varying vec4 debug_oT2;\n"
                   "varying vec4 debug_oT3;\n"
                   "\n"
                   "#define DEBUG_VAR(slot,var) debug_ ## slot ## _ ## var = var;\n"
                   "#define DEBUG(slot) \\\n"
                   "  DEBUG_VAR(slot,R0) \\\n"
                   "  DEBUG_VAR(slot,R1) \\\n"
                   "  DEBUG_VAR(slot,R2) \\\n"
                   "  DEBUG_VAR(slot,R3) \\\n"
                   "  DEBUG_VAR(slot,R4) \\\n"
                   "  DEBUG_VAR(slot,R5) \\\n"
                   "  DEBUG_VAR(slot,R6) \\\n"
                   "  DEBUG_VAR(slot,R7) \\\n"
                   "  DEBUG_VAR(slot,R8) \\\n"
                   "  DEBUG_VAR(slot,R9) \\\n"
                   "  DEBUG_VAR(slot,R10) \\\n"
                   "  DEBUG_VAR(slot,R11) \\\n"
                   "  DEBUG_VAR(slot,R12)\n"
                   "\n"
                   "#define DEBUG_VARYING_VAR(slot,var) varying vec4 debug_ ## slot ## _ ## var;\n"
                   "#define DEBUG_VARYING(slot) \\\n"
                   "  DEBUG_VARYING_VAR(slot,R0) \\\n"
                   "  DEBUG_VARYING_VAR(slot,R1) \\\n"
                   "  DEBUG_VARYING_VAR(slot,R2) \\\n"
                   "  DEBUG_VARYING_VAR(slot,R3) \\\n"
                   "  DEBUG_VARYING_VAR(slot,R4) \\\n"
                   "  DEBUG_VARYING_VAR(slot,R5) \\\n"
                   "  DEBUG_VARYING_VAR(slot,R6) \\\n"
                   "  DEBUG_VARYING_VAR(slot,R7) \\\n"
                   "  DEBUG_VARYING_VAR(slot,R8) \\\n"
                   "  DEBUG_VARYING_VAR(slot,R9) \\\n"
                   "  DEBUG_VARYING_VAR(slot,R10) \\\n"
                   "  DEBUG_VARYING_VAR(slot,R11) \\\n"
                   "  DEBUG_VARYING_VAR(slot,R12)\n"
                   "\n");
    qstring_append(body,
                   "  /* Debug input */\n"
                   "  debug_v0 = v0;\n"
                   "  debug_v1 = v1;\n"
                   "  debug_v2 = v2;\n"
                   "  debug_v3 = v3;\n"
                   "  debug_v4 = v4;\n"
                   "  debug_v5 = v5;\n"
                   "  debug_v6 = v6;\n"
                   "  debug_v7 = v7;\n"
                   "  debug_v8 = v8;\n"
                   "  debug_v9 = v9;\n"
                   "  debug_v10 = v10;\n"
                   "  debug_v11 = v11;\n"
                   "  debug_v12 = v12;\n"
                   "  debug_v13 = v13;\n"
                   "  debug_v14 = v14;\n"
                   "  debug_v15 = v15;\n"
                   "\n");
#endif


    bool has_final = false;
    uint32_t *cur_token = tokens;
    while (cur_token-tokens < tokens_length) {
        unsigned int slot = (cur_token-tokens) / VSH_TOKEN_SIZE;
        QString *token_str = decode_token(cur_token);
        qstring_append_fmt(body,
                           "  /* Slot %d: 0x%08X 0x%08X 0x%08X 0x%08X */\n",
                           slot,
                           cur_token[0],cur_token[1],cur_token[2],cur_token[3]);
        qstring_append(body, qstring_get_str(token_str));
#ifdef DEBUG_NV2A_GPU_SHADER_FEEDBACK
        qstring_append_fmt(header,"DEBUG_VARYING(%d)\n",slot);
        qstring_append_fmt(body,"  DEBUG(%d)\n",slot);
#endif
        qstring_append(body, "\n");
        QDECREF(token_str);

        if (vsh_get_field(cur_token, FLD_FINAL)) {
            printf("Final at %u\n",slot);
            has_final = true;
            break;
        }
        cur_token += VSH_TOKEN_SIZE;
    }
    assert(has_final);

    /* Note : Since we replaced oPos with r12 in the above decoding,
     * we have to assign oPos at the end; This can be done in two ways;
     * 1) When the shader is complete (including transformations),
     *    we could just do a 'MOV oPos, R12;' and be done with it.
     */
    qstring_append(body,
/*
    '# Dxbx addition : Transform the vertex to clip coordinates :'
    "DP4 R0.x, mvp[0], R12;"
    "DP4 R0.y, mvp[1], R12;"
    "DP4 R0.z, mvp[2], R12;"
    "DP4 R0.w, mvp[3], R12;"
    "MOV R12, R0;"
*/


        /* the shaders leave the result in screen space, while
         * opengl expects it in clip coordinates.
         * Use the magic viewport constants for now,
         * but they're not necessarily present.
         * Same idea as above I think, but dono what the mvp stuff is about...
        */
#ifdef DEBUG_NV2A_GPU_SHADER_FEEDBACK
        "  /* Debug output */\n"
        "  debug_oPos = oPos;\n"
        "  debug_oD0 = oD0;\n"
        "  debug_oD1 = oD1;\n"
        "  debug_oB0 = oB0;\n"
        "  debug_oB1 = oB1;\n"
        "  debug_oPts = oPts;\n"
        "  debug_oFog = oFog;\n"
        "  debug_oT0 = oT0;\n"
        "  debug_oT1 = oT1;\n"
        "  debug_oT2 = oT2;\n"
        "  debug_oT3 = oT3;\n"
        "\n"
#endif
#if 1
        "  /* Un-screenspace transform */\n"
        "  R12.xyz = R12.xyz - viewport_offset.xyz;\n"
        "  vec3 tmp = vec3(1.0)\n;"

        /* FIXME: old comment was "scale_z = view_z == 0 ? 1 : (1 / view_z)" */
        "  if (viewport_scale.x != 0.0) { tmp.x /= viewport_scale.x; }\n"
        "  if (viewport_scale.y != 0.0) { tmp.y /= viewport_scale.y; }\n"
        "  if (viewport_scale.z != 0.0) { tmp.z /= viewport_scale.z; }\n"

        "  R12.xyz = R12.xyz * tmp.xyz;\n"
#if 1
        "  R12.xyz *= R12.w;\n" //This breaks 2D? Maybe w is zero?
#else
        "  R12.w = 1.0;\n" //This breaks 2D? Maybe w is zero?
#endif
        "\n"
#else
//FIXME: Use surface width / height / zeta max
      "R12.z /= 16777215.0;\n" // Z[0;1]
      "R12.z *= (cliprange.y - cliprange.x) / 16777215.0;\n" // Scale so [0;zmax] -> [0;cliprange_size]
      "R12.z -= cliprange.x / 16777215.0;\n" // Move down so [clipmin_min;clipmin_max]
      // X = [0;surface_width]; Y = [surface_height;0]; Z = [0;1]; W = ???
      "R12.xyz = R12.xyz / vec3(640.0,480.0,1.0);\n"
      // X,Z = [0;1]; Y = [1;0]; W = ???
      "R12.xyz = R12.xyz * vec3(2.0) - vec3(1.0);\n"
      "R12.y *= -1.0;\n"
      "R12.w = 1.0;\n"
      // X,Y,Z = [-1;+1]; W = 1
        "\n"
#endif
        /* undo the perspective divide? */
        //"MUL R12.xyz, R12, R12.w;\n"

        /* Z coord [0;1]->[-1;1] mapping, see comment in transform_projection
         * in state.c
         *
         * Basically we want (in homogeneous coordinates) z = z * 2 - 1. However,
         * shaders are run before the homogeneous divide, so we have to take the w
         * into account: z = ((z / w) * 2 - 1) * w, which is the same as
         * z = z * 2 - w.
         */
        //"# Apply Z coord mapping\n"
        //"ADD R12.z, R12.z, R12.z;\n"
        //"ADD R12.z, R12.z, -R12.w;\n"
        "  /* Set outputs */\n"
        "  gl_Position = oPos;\n"
        "  gl_FrontColor = oD0;\n"
        "  gl_FrontSecondaryColor = oD1;\n"
        "  gl_BackColor = oB0;\n"
        "  gl_BackSecondaryColor = oB1;\n"
        "  gl_PointSize = oPts.x;\n"
        "  gl_FogFragCoord = oFog.x;\n"
        "  gl_TexCoord[0] = oT0;\n"
        "  gl_TexCoord[1] = oT1;\n"
        "  gl_TexCoord[2] = oT2;\n"
        "  gl_TexCoord[3] = oT3;\n"
        "\n"
    );

    QString *ret = qstring_new();
    qstring_append(ret, qstring_get_str(header));
    qstring_append(ret,"\n"
                       "void main(void)\n"
                       "{\n");
    qstring_append(ret, qstring_get_str(body));
    qstring_append(ret,"}\n");
    QDECREF(header);
    QDECREF(body);
    return ret;
}
