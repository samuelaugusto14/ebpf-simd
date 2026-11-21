mix_src_ip:
ldxw            %r2, [%r1]
mul32           %r2, -0x3361d2af
mov32           %r7, %r2
lsh32           %r2, 15
rsh32           %r7, 17
or32            %r2, %r7
mul32           %r2, 0x1b873593

mix_dst_ip:
ldxw            %r3, [%r1+4]
mul32           %r3, -0x3361d2af
mov32           %r7, %r3
lsh32           %r3, 15
rsh32           %r7, 17
or32            %r3, %r7
mul32           %r3, 0x1b873593

mix_src_port:
ldxw            %r4, [%r1+8]
mul32           %r4, -0x3361d2af
mov32           %r7, %r4
lsh32           %r4, 15
rsh32           %r7, 17
or32            %r4, %r7
mul32           %r4, 0x1b873593

mix_dst_port:
ldxw            %r5, [%r1+12]
mul32           %r5, -0x3361d2af
mov32           %r7, %r5
lsh32           %r5, 15
rsh32           %r7, 17
or32            %r5, %r7
mul32           %r5, 0x1b873593

mix_protocol:
ldxw            %r6, [%r1+16]
mul32           %r6, -0x3361d2af
mov32           %r7, %r6
lsh32           %r6, 15
rsh32           %r7, 17
or32            %r6, %r7
mul32           %r6, 0x1b873593

load_row_seeds:
mov             %r7, %r1
add             %r7, 32788
vld             %v0, [%r7]

hash_src_ip:
vmov.32 x       %v2, %r2
vxor            %v0, %v2
vmov            %v1, %v0
vlsh.32 k       %v0, 13
vrsh.32 k       %v1, 19
vor             %v0, %v1
vmul.32 k       %v0, 5
vadd.32 k       %v0, 0xe6546b64

hash_dst_ip:
vmov.32 x       %v2, %r3
vxor            %v0, %v2
vmov            %v1, %v0
vlsh.32 k       %v0, 13
vrsh.32 k       %v1, 19
vor             %v0, %v1
vmul.32 k       %v0, 5
vadd.32 k       %v0, 0xe6546b64

hash_src_port:
vmov.32 x       %v2, %r4
vxor            %v0, %v2
vmov            %v1, %v0
vlsh.32 k       %v0, 13
vrsh.32 k       %v1, 19
vor             %v0, %v1
vmul.32 k       %v0, 5
vadd.32 k       %v0, 0xe6546b64

hash_dst_port:
vmov.32 x       %v2, %r5
vxor            %v0, %v2
vmov            %v1, %v0
vlsh.32 k       %v0, 13
vrsh.32 k       %v1, 19
vor             %v0, %v1
vmul.32 k       %v0, 5
vadd.32 k       %v0, 0xe6546b64

hash_protocol:
vmov.32 x       %v2, %r6
vxor            %v0, %v2
vmov            %v1, %v0
vlsh.32 k       %v0, 13
vrsh.32 k       %v1, 19
vor             %v0, %v1
vmul.32 k       %v0, 5
vadd.32 k       %v0, 0xe6546b64

finalize_hashes:
vxor.32 k       %v0, 20
vmov            %v1, %v0
vrsh.32 k       %v1, 16
vxor            %v0, %v1
vmul.32 k       %v0, 0x85ebca6b
vmov            %v1, %v0
vrsh.32 k       %v1, 13
vxor            %v0, %v1
vmul.32 k       %v0, 0xc2b2ae35
vmov            %v1, %v0
vrsh.32 k       %v1, 16
vxor            %v0, %v1

build_counter_indices:
vand.32 k       %v0, 1023
vmov.32 k       %v14, 0
vinsert.32 k    %v14, 1024, 1
vinsert.32 k    %v14, 2048, 2
vinsert.32 k    %v14, 3072, 3
vinsert.32 k    %v14, 4096, 4
vinsert.32 k    %v14, 5120, 5
vinsert.32 k    %v14, 6144, 6
vinsert.32 k    %v14, 7168, 7
vadd.32 x       %v14, %v0

increment_one_counter_per_row:
vcmp.eq.32 x    %v0, %v0
vgather.32      %v3, [%r1+20]
vadd.32 k       %v3, 1
vscatter.32     [%r1+20], %v3

mov             %r0, 0
exit
