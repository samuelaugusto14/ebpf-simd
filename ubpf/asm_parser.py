#!/usr/bin/env python
from __future__ import print_function
from parcon import *
from collections import namedtuple

hexchars = '0123456789abcdefABCDEF'

Reg = namedtuple("Reg", ["num"])
VReg = namedtuple("VReg", ["num"])
Imm = namedtuple("Imm", ["value"])
MemRef = namedtuple("MemRef", ["reg", "offset"])
Label = namedtuple("Label", ["name"])
LabelRef = namedtuple("LabelRef", ["name"])

def keywords(vs):
    return First(*[Keyword(SignificantLiteral(v)) for v in vs])

hexnum = SignificantLiteral('0x') + +CharIn(hexchars)
decnum = +Digit()
offset = (CharIn("+-") + Exact(hexnum | decnum))[flatten]["".join][lambda x: int(x, 0)]
imm = (-CharIn("+-") + Exact(hexnum | decnum))[flatten]["".join][lambda x: int(x, 0)][Imm]

# Label identifier: must start with letter or underscore, then alphanumeric or underscore
label_start_chars = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_'
label_cont_chars = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789'
label_name = Exact(CharIn(label_start_chars) + ZeroOrMore(CharIn(label_cont_chars)))[flatten]["".join]
label_def = (label_name + Literal(':'))[lambda x: Label(x[0] if isinstance(x, tuple) else x)]
label_ref = label_name[LabelRef]

reg = Literal('%') + Literal('r') + integer[int][Reg]
vreg = Exact(Literal('%v') + integer[int])[VReg]
memref = (Literal('[') + reg + Optional(offset, 0) + Literal(']'))[lambda x: MemRef(*x)]

unary_alu_ops = ['neg', 'neg32', 'le16', 'le32', 'le64', 'be16', 'be32', 'be64']
binary_alu_ops = ['add', 'sub', 'mul', 'div', 'or', 'and', 'lsh', 'rsh',
                  'mod', 'xor', 'mov', 'arsh']
binary_alu_ops.extend([x + '32' for x in binary_alu_ops])

alu_instruction = \
    (keywords(unary_alu_ops) + reg) | \
    (keywords(binary_alu_ops) + reg + "," + (reg | imm))

mem_sizes = ['w', 'h', 'b', 'dw']
mem_store_reg_ops = ['stx' + s for s in mem_sizes]
mem_store_imm_ops = ['st' + s for s in mem_sizes]
mem_load_ops = ['ldx' + s for s in mem_sizes]

mem_instruction = \
    (keywords(mem_store_reg_ops) + memref + "," + reg) | \
    (keywords(mem_store_imm_ops) + memref + "," + imm) | \
    (keywords(mem_load_ops) + reg + "," + memref) | \
    (keywords(["lddw"]) + reg + "," + imm)

jmp_cmp_ops = ['jeq', 'jgt', 'jge', 'jlt', 'jle', 'jset', 'jne', 'jsgt', 'jsge', 'jslt', 'jsle']
jmp_instruction = \
    (keywords(jmp_cmp_ops) + reg + "," + (reg | imm) + "," + (offset | label_ref)) | \
    (keywords(['ja']) + (offset | label_ref)) | \
    (keywords(['call']) + Keyword(SignificantLiteral('local')) + (imm | label_ref)) | \
    (keywords(['call']) + imm) | \
    (keywords(['exit'])[lambda x: (x, )])

simd_binary_lane_ops = [op + width for op in ('vadd', 'vsub', 'vmul', 'vlsh', 'vrsh', 'varsh')
                        for width in ('.32', '.64')]
simd_logic_ops = ['vand', 'vor', 'vxor']
simd_logic_lane_ops = [op + width for op in simd_logic_ops for width in ('.32', '.64')]
simd_broadcast_ops = ['vmov.32', 'vmov.64']
simd_unary_ops = ['vnot', 'vtobe.32', 'vtobe.64', 'vtole.32', 'vtole.64']
simd_compare_ops = ['vcmp.eq.32', 'vcmp.eq.64', 'vcmp.gt.s32', 'vcmp.gt.s64',
                    'vcmp.gt.u32', 'vcmp.gt.u64']

simd_imm_op = (keywords(simd_binary_lane_ops + simd_logic_lane_ops + simd_broadcast_ops + simd_compare_ops)
               + keywords(['k']))[' '.join]
simd_vector_src_op = (keywords(simd_binary_lane_ops + simd_compare_ops) + keywords(['x']))[' '.join]
simd_scalar_src_op = (keywords(simd_broadcast_ops) + keywords(['x']))[' '.join]
simd_vector_move_op = keywords(simd_logic_ops + ['vmov', 'vmovm.32', 'vmovm.64'])
simd_mask_extract_op = keywords(['vmovmsk.32', 'vmovmsk.64'])
simd_reduce_op = keywords(['vreduce.add.32', 'vreduce.add.64'])
simd_extract_op = keywords(['vextract.32', 'vextract.64'])
simd_insert_op = keywords(['vinsert.32', 'vinsert.64'])
simd_insert_imm_op = (simd_insert_op + keywords(['k']))[' '.join]
simd_insert_reg_op = (simd_insert_op + keywords(['x']))[' '.join]
simd_mem_load_op = keywords(['vld', 'vldm.32', 'vldm.64', 'vgather.32', 'vgather.64'])
simd_mem_store_op = keywords(['vst', 'vstm.32', 'vstm.64', 'vscatter.32', 'vscatter.64'])
simd_instruction = (
    (simd_imm_op + vreg + "," + imm)
    | ((simd_vector_src_op | simd_vector_move_op) + vreg + "," + vreg)
    | (simd_scalar_src_op + vreg + "," + reg)
    | (keywords(simd_unary_ops) + vreg)
    | (simd_mask_extract_op + reg)
    | (simd_reduce_op + reg + "," + vreg)
    | (simd_extract_op + reg + "," + vreg + "," + imm)
    | (simd_insert_imm_op + vreg + "," + imm + "," + imm)
    | (simd_insert_reg_op + vreg + "," + reg + "," + imm)
    | (simd_mem_load_op + vreg + "," + memref)
    | (simd_mem_store_op + memref + "," + vreg)
)

instruction = alu_instruction | mem_instruction | jmp_instruction | simd_instruction

# A line can be:
# 1. A label definition followed by optional instruction
# 2. Just an instruction
# The label_def includes the colon, so it's unambiguous
line_item = (label_def + Optional(instruction)) | instruction

start = ZeroOrMore(line_item + Optional(Literal(';'))) + End()

def parse(source):
    return start.parse_string(source)

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Assembly parser", formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('file', type=argparse.FileType('r'), default='-')
    args = parser.parse_args()
    result = parse(args.file.read())
    for inst in result:
        print(repr(inst))
