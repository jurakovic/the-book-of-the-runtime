// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

// Generated by Fuzzlyn v2.4 on 2024-12-01 16:32:26
// Run on X64 Linux
// Seed: 7861295224295601455-vectort,vector128,vector256,x86aes,x86avx,x86avx2,x86avx512bw,x86avx512bwvl,x86avx512cd,x86avx512cdvl,x86avx512dq,x86avx512dqvl,x86avx512f,x86avx512fvl,x86avx512fx64,x86bmi1,x86bmi1x64,x86bmi2,x86bmi2x64,x86fma,x86lzcnt,x86lzcntx64,x86pclmulqdq,x86popcnt,x86popcntx64,x86sse,x86ssex64,x86sse2,x86sse2x64,x86sse3,x86sse41,x86sse41x64,x86sse42,x86sse42x64,x86ssse3,x86x86base
// Reduced from 115.8 KiB to 0.9 KiB in 00:02:27
// Hits JIT assert in Release:
// Assertion failed 'newLclValue.BothDefined()' in 'Program:Main(Fuzzlyn.ExecutionServer.IRuntime)' during 'Do value numbering' (IL size 61; hash 0xade6b36b; FullOpts)
// 
//     File: /__w/1/s/src/coreclr/jit/valuenum.cpp Line: 6138
// 
using System;
using System.Numerics;
using System.Runtime.CompilerServices;
using System.Runtime.Intrinsics;
using System.Runtime.Intrinsics.X86;
using Xunit;

public class C0
{
    public uint F0;
}

public struct S0
{
    public C0 F2;
}

public class C3
{
    public byte F0;
}

public class Runtime_110306
{
    public static S0 s_3;
    
    [Fact]
    public static void TestEntryPoint()
    {
        if (!Avx512F.VL.IsSupported)
        {
            return;
        }

        try
        {
            TestMain();
        }
        catch
        {
        }
    }
    
    private static void TestMain()
    {        
        var vr5 = Vector256.Create(1, 0, 0, 0);
        Vector256<long> vr15 = Vector256.Create<long>(0);
        Vector256<long> vr8 = Avx512F.VL.CompareNotEqual(vr5, vr15);
        long vr9 = 0;
        var vr10 = new C3();
        vr8 = M3();
        long vr11 = vr9;
        var vr12 = s_3.F2.F0;
        vr8 = vr8;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    public static Vector256<long> M3()
    {
        return default;
    }
}