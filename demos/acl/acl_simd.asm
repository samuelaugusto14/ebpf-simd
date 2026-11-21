broadcast_key:
ldxw            %r2, [%r1]
vmov.32 x       %v0, %r2
ldxw            %r2, [%r1+4]
vmov.32 x       %v1, %r2
ldxw            %r2, [%r1+8]
vmov.32 x       %v2, %r2
ldxw            %r2, [%r1+12]
vmov.32 x       %v3, %r2
ldxw            %r2, [%r1+16]
vmov.32 x       %v4, %r2

count_groups:
ldxw            %r3, [%r1+20]
add32           %r3, 7
rsh32           %r3, 3
add             %r1, 24

match_source_ip:
vmov            %v5, %v0
vld             %v6, [%r1]
vand            %v5, %v6
vld             %v6, [%r1+32]
vcmp.eq.32 x    %v5, %v6
vmov            %v7, %v15

match_destination_ip:
vmov            %v5, %v1
vld             %v6, [%r1+64]
vand            %v5, %v6
vld             %v6, [%r1+96]
vcmp.eq.32 x    %v5, %v6
vand            %v7, %v15

match_protocol:
vmov            %v5, %v2
vld             %v6, [%r1+128]
vand            %v5, %v6
vld             %v6, [%r1+160]
vcmp.eq.32 x    %v5, %v6
vand            %v7, %v15

match_source_port:
vmov            %v5, %v3
vld             %v6, [%r1+192]
vand            %v5, %v6
vld             %v6, [%r1+224]
vcmp.eq.32 x    %v5, %v6
vand            %v7, %v15

match_destination_port:
vmov            %v5, %v4
vld             %v6, [%r1+256]
vand            %v5, %v6
vld             %v6, [%r1+288]
vcmp.eq.32 x    %v5, %v6
vand            %v15, %v7

check_active_matches:
vmovmsk.32      %r2
ldxw            %r4, [%r1+352]
and32           %r2, %r4
jne             %r2, 0, first_matching_lane

next_group:
add32           %r3, -1
jeq             %r3, 0, default_pass
add             %r1, 356
ja              match_source_ip

first_matching_lane:
mov32           %r3, 0
jset            %r2, 0x0f, select_pair
rsh32           %r2, 4
add32           %r3, 4
select_pair:
jset            %r2, 0x03, select_lane
rsh32           %r2, 2
add32           %r3, 2
select_lane:
jset            %r2, 0x01, load_result
add32           %r3, 1
load_result:
lsh32           %r3, 2
add             %r1, %r3
ldxw            %r0, [%r1+320]
exit

default_pass:
mov32           %r0, 0
exit
