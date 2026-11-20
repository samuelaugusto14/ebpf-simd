from .asm_parser import parse, Imm, Label, LabelRef, MemRef, Reg, VReg
import struct
try:
    from StringIO import StringIO as io
except ImportError:
    from io import BytesIO as io

Inst = struct.Struct("BBHI")

MEM_SIZES = {
    'w': 0,
    'h': 1,
    'b': 2,
    'dw': 3,
}

MEM_LOAD_OPS = { 'ldx' + k: (0x61 | (v << 3)) for k, v in list(MEM_SIZES.items()) }
MEM_LOAD_SX_OPS = { 'ldxs' + k: (0x81 | (v << 3)) for k, v in list(MEM_SIZES.items()) if k != 'dw' }
MEM_STORE_IMM_OPS = { 'st' + k: (0x62 | (v << 3))  for k, v in list(MEM_SIZES.items()) }
MEM_STORE_REG_OPS = { 'stx' + k: (0x63 | (v << 3)) for k, v in list(MEM_SIZES.items()) }

UNARY_ALU_OPS = {
    'neg': 8,
}

BINARY_ALU_OPS = {
    'add': 0,
    'sub': 1,
    'mul': 2,
    'div': 3,
    'or': 4,
    'and': 5,
    'lsh': 6,
    'rsh': 7,
    'mod': 9,
    'xor': 10,
    'mov': 11,
    'arsh': 12,
}

# Signed division and modulo operations (same opcode as div/mod but with offset=1)
SIGNED_BINARY_ALU_OPS = {
    'sdiv': 3,
    'smod': 9,
}

UNARY_ALU32_OPS = { k + '32': v for k, v in list(UNARY_ALU_OPS.items()) }
BINARY_ALU32_OPS = { k + '32': v for k, v in list(BINARY_ALU_OPS.items()) }
SIGNED_BINARY_ALU32_OPS = { k + '32': v for k, v in list(SIGNED_BINARY_ALU_OPS.items()) }

END_OPS = {
    'le16': (0xd4, 16),
    'le32': (0xd4, 32),
    'le64': (0xd4, 64),
    'be16': (0xdc, 16),
    'be32': (0xdc, 32),
    'be64': (0xdc, 64),
    'bswap16': (0xd7, 16),
    'bswap32': (0xd7, 32),
    'bswap64': (0xd7, 64),
}

JMP_CMP_OPS = {
    'jeq': 1,
    'jgt': 2,
    'jge': 3,
    'jset': 4,
    'jne': 5,
    'jsgt': 6,
    'jsge': 7,
    'jlt': 10,
    'jle': 11,
    'jslt': 12,
    'jsle': 13,
}

JMP_MISC_OPS = {
    'ja': 0,
    'call': 8,
    'exit': 9,
}

class SimdEncoding(object):
    def __init__(self, opcode, src_type=None, dst_type=VReg, offset=0, lane=None):
        self.opcode = opcode
        self.src_type = src_type
        self.dst_type = dst_type
        self.offset = offset
        # When present, lane is (field name, lane count).
        self.lane = lane


SIMD_OPS = {
    # Arithmetic
    'vadd.32 k': SimdEncoding(0xe4, Imm),
    'vadd.32 x': SimdEncoding(0xec, VReg),
    'vadd.64 k': SimdEncoding(0xe7, Imm),
    'vadd.64 x': SimdEncoding(0xef, VReg),
    'vsub.32 k': SimdEncoding(0xf4, Imm),
    'vsub.32 x': SimdEncoding(0xfc, VReg),
    'vsub.64 k': SimdEncoding(0xf7, Imm),
    'vsub.64 x': SimdEncoding(0xff, VReg),
    'vmul.32 k': SimdEncoding(0x82, Imm),
    'vmul.32 x': SimdEncoding(0x8a, VReg),
    'vmul.64 k': SimdEncoding(0x92, Imm),
    'vmul.64 x': SimdEncoding(0x9a, VReg),

    # Bitwise
    'vand': SimdEncoding(0x0b, VReg),
    'vand.32 k': SimdEncoding(0x03, Imm),
    'vand.64 k': SimdEncoding(0x13, Imm),
    'vor': SimdEncoding(0x2b, VReg),
    'vor.32 k': SimdEncoding(0x23, Imm),
    'vor.64 k': SimdEncoding(0x33, Imm),
    'vxor': SimdEncoding(0x4b, VReg),
    'vxor.32 k': SimdEncoding(0x43, Imm),
    'vxor.64 k': SimdEncoding(0x53, Imm),
    'vnot': SimdEncoding(0x5b),

    # Shifts
    'vlsh.32 k': SimdEncoding(0xa2, Imm),
    'vlsh.32 x': SimdEncoding(0xaa, VReg),
    'vlsh.64 k': SimdEncoding(0xb2, Imm),
    'vlsh.64 x': SimdEncoding(0xba, VReg),
    'vrsh.32 k': SimdEncoding(0xc2, Imm),
    'vrsh.32 x': SimdEncoding(0xca, VReg),
    'vrsh.64 k': SimdEncoding(0xd2, Imm),
    'vrsh.64 x': SimdEncoding(0xda, VReg),
    'varsh.32 k': SimdEncoding(0xe2, Imm),
    'varsh.32 x': SimdEncoding(0xea, VReg),
    'varsh.64 k': SimdEncoding(0xf2, Imm),
    'varsh.64 x': SimdEncoding(0xfa, VReg),

    # Vector moves and broadcasts
    'vmov.32 k': SimdEncoding(0x60, Imm),
    'vmov.32 x': SimdEncoding(0x68, Reg),
    'vmov.64 k': SimdEncoding(0x70, Imm),
    'vmov.64 x': SimdEncoding(0x78, Reg),
    'vmov': SimdEncoding(0x80, VReg),

    # Byte order conversions
    'vtobe.32': SimdEncoding(0xa0),
    'vtobe.64': SimdEncoding(0xb0),
    'vtole.32': SimdEncoding(0xa8),
    'vtole.64': SimdEncoding(0xb8),

    # Lane access and reduction
    'vextract.32': SimdEncoding(0xc8, VReg, dst_type=Reg, lane=('imm', 8)),
    'vextract.64': SimdEncoding(0xd8, VReg, dst_type=Reg, lane=('imm', 4)),
    'vinsert.32 k': SimdEncoding(0x08, Imm, lane=('offset', 8)),
    'vinsert.32 x': SimdEncoding(0xe8, Reg, lane=('offset', 8)),
    'vinsert.64 k': SimdEncoding(0x10, Imm, lane=('offset', 4)),
    'vinsert.64 x': SimdEncoding(0xf8, Reg, lane=('offset', 4)),
    'vreduce.add.32': SimdEncoding(0xe0, VReg, dst_type=Reg),
    'vreduce.add.64': SimdEncoding(0xf0, VReg, dst_type=Reg),

    # Masked moves and mask extraction
    'vmovm.32': SimdEncoding(0x88, VReg),
    'vmovm.64': SimdEncoding(0x98, VReg),
    'vmovmsk.32': SimdEncoding(0xc0, dst_type=Reg),
    'vmovmsk.64': SimdEncoding(0xd0, dst_type=Reg),

    # Comparisons write V15 implicitly. GT uses offset=1 for signed and 0 for unsigned.
    'vcmp.eq.32 k': SimdEncoding(0xe6, Imm),
    'vcmp.eq.32 x': SimdEncoding(0xee, VReg),
    'vcmp.eq.64 k': SimdEncoding(0xe5, Imm),
    'vcmp.eq.64 x': SimdEncoding(0xed, VReg),
    'vcmp.gt.s32 k': SimdEncoding(0xf6, Imm, offset=1),
    'vcmp.gt.s32 x': SimdEncoding(0xfe, VReg, offset=1),
    'vcmp.gt.s64 k': SimdEncoding(0xf5, Imm, offset=1),
    'vcmp.gt.s64 x': SimdEncoding(0xfd, VReg, offset=1),
    'vcmp.gt.u32 k': SimdEncoding(0xf6, Imm),
    'vcmp.gt.u32 x': SimdEncoding(0xfe, VReg),
    'vcmp.gt.u64 k': SimdEncoding(0xf5, Imm),
    'vcmp.gt.u64 x': SimdEncoding(0xfd, VReg),

    # Memory operands encode a scalar base and a signed byte offset.
    'vld': SimdEncoding(0xa1, MemRef),
    'vldm.32': SimdEncoding(0xa9, MemRef),
    'vldm.64': SimdEncoding(0xb9, MemRef),
    'vst': SimdEncoding(0x83, VReg, dst_type=MemRef),
    'vstm.32': SimdEncoding(0x8b, VReg, dst_type=MemRef),
    'vstm.64': SimdEncoding(0x9b, VReg, dst_type=MemRef),
    'vgather.32': SimdEncoding(0xc1, MemRef),
    'vgather.64': SimdEncoding(0xd1, MemRef),
    'vscatter.32': SimdEncoding(0xa3, VReg, dst_type=MemRef),
    'vscatter.64': SimdEncoding(0xb3, VReg, dst_type=MemRef),
}

def pack(opcode, dst, src, offset, imm):
    return Inst.pack(opcode & 0xff, (dst | (src << 4)) & 0xff, offset & 0xffff, imm & 0xffffffff)

def assemble_binop(op, cls, ops, dst, src, offset):
    opcode = cls | (ops[op] << 4)
    if isinstance(src, Imm):
        return pack(opcode, dst.num, 0, offset, src.value)
    else:
        return pack(opcode | 0x08, dst.num, src.num, offset, 0)

def simd_register_number(op, reg, reg_type, role):
    if reg_type is VReg:
        prefix, limit = '%v', 15
    else:
        prefix = '%r'
        limit = 9 if role == 'destination' else 10
    if not 0 <= reg.num <= limit:
        raise ValueError('{}: {} must be in {}0..{}{}'.format(op, role, prefix, prefix, limit))
    return reg.num

def simd_memory_fields(op, reference):
    base = simd_register_number(op, reference.reg, Reg, 'base')
    offset = reference.offset
    if not -(1 << 15) <= offset < (1 << 15):
        raise ValueError('{}: memory offset must be in -32768..32767'.format(op))
    return base, offset

def assemble_simd(op, dst, src=None, lane=None):
    encoding = SIMD_OPS[op]
    src_num = 0
    imm = 0
    offset = encoding.offset

    if encoding.dst_type is MemRef:
        dst_num, offset = simd_memory_fields(op, dst)
    else:
        dst_num = simd_register_number(op, dst, encoding.dst_type, 'destination')

    if encoding.src_type is Imm:
        imm = src.value
        if not -(1 << 31) <= imm < (1 << 32):
            raise ValueError('{}: immediate does not fit in 32 bits'.format(op))
    elif encoding.src_type is MemRef:
        src_num, offset = simd_memory_fields(op, src)
    elif encoding.src_type is not None:
        src_num = simd_register_number(op, src, encoding.src_type, 'source')

    if encoding.lane is not None:
        field, count = encoding.lane
        if not 0 <= lane.value < count:
            raise ValueError('{}: lane must be in 0..{}'.format(op, count - 1))
        if field == 'imm':
            imm = lane.value
        else:
            offset = lane.value

    return pack(encoding.opcode, dst_num, src_num, offset, imm)

def assemble_one(inst):
    op = inst[0]
    if op in MEM_LOAD_OPS:
        opcode = MEM_LOAD_OPS[op]
        return pack(opcode, inst[1].num, inst[2].reg.num, inst[2].offset, 0)
    elif op in MEM_LOAD_SX_OPS:
        opcode = MEM_LOAD_SX_OPS[op]
        return pack(opcode, inst[1].num, inst[2].reg.num, inst[2].offset, 0)
    elif op == "lddw":
        a = pack(0x18, inst[1].num, 0, 0, inst[2].value)
        b = pack(0, 0, 0, 0, inst[2].value >> 32)
        return a + b
    elif op in MEM_STORE_IMM_OPS:
        opcode = MEM_STORE_IMM_OPS[op]
        return pack(opcode, inst[1].reg.num, 0, inst[1].offset, inst[2].value)
    elif op in MEM_STORE_REG_OPS:
        opcode = MEM_STORE_REG_OPS[op]
        return pack(opcode, inst[1].reg.num, inst[2].num, inst[1].offset, 0)
    elif op in UNARY_ALU_OPS:
        opcode = 0x07 | (UNARY_ALU_OPS[op] << 4)
        return pack(opcode, inst[1].num, 0, 0, 0)
    elif op in UNARY_ALU32_OPS:
        opcode = 0x04 | (UNARY_ALU32_OPS[op] << 4)
        return pack(opcode, inst[1].num, 0, 0, 0)
    elif op in BINARY_ALU_OPS:
        return assemble_binop(op, 0x07, BINARY_ALU_OPS, inst[1], inst[2], 0)
    elif op in BINARY_ALU32_OPS:
        return assemble_binop(op, 0x04, BINARY_ALU32_OPS, inst[1], inst[2], 0)
    elif op in SIGNED_BINARY_ALU_OPS:
        return assemble_binop(op, 0x07, SIGNED_BINARY_ALU_OPS, inst[1], inst[2], 1)
    elif op in SIGNED_BINARY_ALU32_OPS:
        return assemble_binop(op, 0x04, SIGNED_BINARY_ALU32_OPS, inst[1], inst[2], 1)
    elif op in SIMD_OPS:
        return assemble_simd(*inst)
    elif op in END_OPS:
        opcode, imm = END_OPS[op]
        return pack(opcode, inst[1].num, 0, 0, imm)
    elif op in JMP_CMP_OPS:
        return assemble_binop(op, 0x05, JMP_CMP_OPS, inst[1], inst[2], inst[3])
    elif op in JMP_MISC_OPS:
        opcode = 0x05 | (JMP_MISC_OPS[op] << 4)
        if op == 'ja':
            return pack(opcode, 0, 0, inst[1], 0)
        elif op == 'call':
            # Check if it's a local call (3-tuple with 'local' marker)
            if len(inst) > 2 and inst[1] == 'local':
                # call local: set src_reg=1, imm=relative offset
                return pack(opcode, 0, 1, 0, inst[2].value)
            else:
                # Regular call: src_reg=0, imm=function index
                return pack(opcode, 0, 0, 0, inst[1].value)
        elif op == 'exit':
            return pack(opcode, 0, 0, 0, 0)
    else:
        raise ValueError("unexpected instruction %r" % op)

def resolve_label_ref(label_ref, current_idx, labels):
    """
    Resolve a label reference to an offset.
    
    Args:
        label_ref: LabelRef or int (offset)
        current_idx: Current instruction index
        labels: Dictionary mapping label names to instruction indices
    
    Returns:
        Resolved offset as an integer
    """
    if isinstance(label_ref, LabelRef):
        if label_ref.name not in labels:
            raise ValueError("Undefined label: %s" % label_ref.name)
        target_idx = labels[label_ref.name]
        offset = target_idx - current_idx - 1
        return offset
    else:
        # Already an offset
        return label_ref

def resolve_labels_in_inst(inst, current_idx, labels):
    """
    Replace label references in an instruction with calculated offsets.
    
    Args:
        inst: Instruction tuple
        current_idx: Current instruction index
        labels: Dictionary mapping label names to instruction indices
    
    Returns:
        Instruction tuple with labels resolved to offsets.
        Note: Return tuple size varies by instruction type as different
        BPF instructions have different operand counts:
        - Jump comparisons (jeq, jgt, etc.): 4-tuple (op, reg, reg/imm, offset)
        - Unconditional jump (ja): 2-tuple ('ja', offset)
        - Local calls: 3-tuple ('call', 'local', imm)
        - Other instructions: returned unchanged
    """
    if not inst:
        return inst
    
    op = inst[0]
    
    # Handle jump comparison operations (jeq, jgt, etc.)
    if op in JMP_CMP_OPS:
        # inst = (op, reg, reg/imm, offset/labelref)
        target = inst[3]
        resolved_offset = resolve_label_ref(target, current_idx, labels)
        return (inst[0], inst[1], inst[2], resolved_offset)
    
    # Handle unconditional jump (ja)
    elif op == 'ja':
        # inst = ('ja', offset/labelref)
        target = inst[1]
        resolved_offset = resolve_label_ref(target, current_idx, labels)
        return ('ja', resolved_offset)
    
    # Handle call instruction with 'local' keyword
    elif op == 'call' and len(inst) > 2 and inst[1] == 'local':
        # inst = ('call', 'local', imm/labelref)
        target = inst[2]
        if isinstance(target, LabelRef):
            # Reuse generic label resolution logic
            offset = resolve_label_ref(target, current_idx, labels)
            resolved_imm = Imm(offset)
            return ('call', 'local', resolved_imm)
        else:
            # Already an Imm; preserve the 'local' marker
            return ('call', 'local', target)
    
    # No labels to resolve in this instruction
    return inst

def assemble(source):
    """
    Assemble BPF assembly source code to bytecode.
    
    This uses a two-pass approach:
    1. First pass: collect labels and build instruction list
    2. Second pass: resolve label references and assemble
    
    Args:
        source: Assembly source code as a string
    
    Returns:
        Assembled bytecode as bytes
    """
    parsed = parse(source)
    
    # Pass 1: Collect labels and instructions
    labels = {}
    instructions = []
    instruction_idx = 0
    
    for item in parsed:
        if not item:
            # Empty line
            continue
        
        # item can be:
        # 1. A tuple with (Label, 'opcode', ...) when label and instruction on same line
        # 2. Just Label when only label on the line
        # 3. A tuple ('opcode', ...) when just an instruction
        label = None
        inst = None
        
        # Check for Label first, before checking for tuple (Label is a namedtuple subclass)
        if isinstance(item, Label):
            # Standalone label
            label = item
        elif isinstance(item, tuple) and len(item) > 0:
            # Check if first element is a Label
            if isinstance(item[0], Label):
                label = item[0]
                # The rest is the instruction
                if len(item) > 1:
                    inst = item[1:]
            else:
                # It's just an instruction tuple
                inst = item
        else:
            # Shouldn't happen, but just in case
            inst = item
        
        # Process label if found
        if label:
            if label.name in labels:
                raise ValueError("Duplicate label: %s" % label.name)
            labels[label.name] = instruction_idx
        
        # Process instruction if found
        if inst:
            instructions.append(inst)
            # LDDW takes 2 instruction slots
            if inst[0] == 'lddw':
                instruction_idx += 2
            else:
                instruction_idx += 1
    
    # Pass 2: Resolve labels and assemble
    output = io()
    instruction_idx = 0
    for inst in instructions:
        resolved_inst = resolve_labels_in_inst(inst, instruction_idx, labels)
        output.write(assemble_one(resolved_inst))
        # LDDW takes 2 instruction slots
        if inst[0] == 'lddw':
            instruction_idx += 2
        else:
            instruction_idx += 1
    
    return output.getvalue()
