vld             %v0, [%r1]
vcmp.gt.s32 k   %v0, 49

vmov            %v1, %v0
vadd.32 k       %v1, 10
vmovm.32        %v0, %v1
vst             [%r1+32], %v0

vmovmsk.32      %r3
stxw            [%r1+64], %r3
vreduce.add.32  %r4, %v0
stxw            [%r1+68], %r4

mov             %r0, 0
exit
