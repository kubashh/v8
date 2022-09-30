0x5554df352180     0  488bfa               REX.W movq rdi,rdx
0x5554df352183     3  488bd6               REX.W movq rdx,rsi
0x5554df352186     6  33f6                 xorl rsi,rsi
                  [ Frame: INTERNAL
                    [ EnterFrame
0x5554df352188     8  55                   push rbp
0x5554df352189     9  4889e5               REX.W movq rbp,rsp
0x5554df35218c     c  6a2c                 push 0x2c
                    ]
0x5554df35218e     e  498bb5f0460000       REX.W movq rsi,[r13+0x46f0] (external value (Isolate::context_address))
0x5554df352195    15  57                   push rdi
0x5554df352196    16  498bc0               REX.W movq rax,r8
0x5554df352199    19  498bd9               REX.W movq rbx,r9
0x5554df35219c    1c  4c8bc9               REX.W movq r9,rcx
                    [ StackOverflowCheck
0x5554df35219f    1f  4989e2               REX.W movq r10,rsp
0x5554df3521a2    22  4d2b5510             REX.W subq r10,[r13+0x10] (external value (StackGuard::address_of_real_jslimit()))
0x5554df3521a6    26  49c1fa03             REX.W sarq r10, 3
0x5554df3521aa    2a  4c3bd0               REX.W cmpq r10,rax
0x5554df3521ad    2d  7e02                 jle 0x5554df3521b1  (JSEntryTrampoline)
                    ]
0x5554df3521af    2f  eb0f                 jmp 0x5554df3521c0  (JSEntryTrampoline)
                    [ CallRuntime
0x5554df3521b1    31  33c0                 xorl rax,rax
0x5554df3521b3    33  498b9d28380000       REX.W movq rbx,[r13+0x3828] (external reference (Runtime::ThrowStackOverflow))
                      [ Inlined  Trampoline for call to CEntry_Return1_DontSaveFPRegs_ArgvOnStack_NoBuiltinExit
0x5554df3521ba    3a  e8c1f94900           call 0x5554df7f1b80  (CEntry_Return1_DontSaveFPRegs_ArgvOnStack_NoBuiltinExit)
                      ]
                    ]
0x5554df3521bf    3f  cc                   int3l
0x5554df3521c0    40  488d48ff             REX.W leaq rcx,[rax-0x1]
0x5554df3521c4    44  e907000000           jmp 0x5554df3521d0  (JSEntryTrampoline)
0x5554df3521c9    49  4c8b14cb             REX.W movq r10,[rbx+rcx*8]
0x5554df3521cd    4d  41ff32               push [r10]
0x5554df3521d0    50  48ffc9               REX.W decq rcx
0x5554df3521d3    53  7df4                 jge 0x5554df3521c9  (JSEntryTrampoline)
0x5554df3521d5    55  4151                 push r9
                    [ Inlined  Trampoline for call to Call_ReceiverIsAny
0x5554df3521d7    57  e8641dfbff           call 0x5554df303f40  (Call_ReceiverIsAny)



0x5554df35b9c0     0  448b570b             movl r10,[rdi+0xb]
                  [ DecompressTaggedPointer
0x5554df35b9c4     4  478b641603           movl r12,[r14+r10*1+0x3]
                    [ DecompressTaggedValueIf8GbHeap
                      [ ExtractHeapObjectTag
0x5554df35b9c9     9  4c89e3               REX.W movq rbx,r12
0x5554df35b9cc     c  49ba0300000000000000 REX.W movq r10,0x3
0x5554df35b9d6    16  4923da               REX.W andq rbx,r10
                      ]
0x5554df35b9d9    19  4183e4fc             andl r12,0xfc
0x5554df35b9dd    1d  49d1e4               REX.W shlq r12, 1
0x5554df35b9e0    20  4c0be3               REX.W orq r12,rbx
                    ]
0x5554df35b9e3    23  4d03e6               REX.W addq r12,r14
                  ]
                  [ GetSharedFunctionInfoBytecodeOrBaseline
                    [ DecompressTaggedPointer
0x5554df35b9e6    26  458b5424ff           movl r10,[r12-0x1]
                      [ DecompressTaggedValueIf8GbHeap
                        [ ExtractHeapObjectTag
0x5554df35b9eb    2b  498bda               REX.W movq rbx,r10
0x5554df35b9ee    2e  4c8b15d9ffffff       REX.W movq r10,[rip+0xffffffd9]
0x5554df35b9f5    35  4923da               REX.W andq rbx,r10
                        ]
0x5554df35b9f8    38  4183e2fc             andl r10,0xfc
0x5554df35b9fc    3c  49d1e2               REX.W shlq r10, 1
0x5554df35b9ff    3f  4c0bd3               REX.W orq r10,rbx
                      ]
0x5554df35ba02    42  4d03d6               REX.W addq r10,r14
                    ]
0x5554df35ba05    45  6641817a07ee00       cmpw [r10+0x7],0xee
0x5554df35ba0c    4c  0f8522000000         jnz 0x5554df35ba34  (InterpreterEntryTrampoline)
0x5554df35ba12    52  458b542417           movl r10,[r12+0x17]
0x5554df35ba17    57  4983e20f             REX.W andq r10,0xf
0x5554df35ba1b    5b  4183fa0b             cmpl r10,0xb
0x5554df35ba1f    5f  740d                 jz 0x5554df35ba2e  (InterpreterEntryTrampoline)