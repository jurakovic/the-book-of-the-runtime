// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

/******************************************************************************
 * This file is auto-generated from a template file by the GenerateTests.csx  *
 * script in tests\src\JIT\HardwareIntrinsics\X86\Shared. In order to make    *
 * changes, please update the corresponding template and run according to the *
 * directions listed in the file.                                             *
 ******************************************************************************/

using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Intrinsics;
using Xunit;

namespace JIT.HardwareIntrinsics.General._Vector256
{
    public static partial class Program
    {
        [Fact]
        public static void ConditionalSelectUInt64()
        {
            var test = new VectorTernaryOpTest__ConditionalSelectUInt64();

            // Validates basic functionality works, using Unsafe.Read
            test.RunBasicScenario_UnsafeRead();

            // Validates calling via reflection works, using Unsafe.Read
            test.RunReflectionScenario_UnsafeRead();

            // Validates passing a static member works
            test.RunClsVarScenario();

            // Validates passing a local works, using Unsafe.Read
            test.RunLclVarScenario_UnsafeRead();

            // Validates passing the field of a local class works
            test.RunClassLclFldScenario();

            // Validates passing an instance member of a class works
            test.RunClassFldScenario();

            // Validates passing the field of a local struct works
            test.RunStructLclFldScenario();

            // Validates passing an instance member of a struct works
            test.RunStructFldScenario();

            if (!test.Succeeded)
            {
                throw new Exception("One or more scenarios did not complete as expected.");
            }
        }
    }

    public sealed unsafe class VectorTernaryOpTest__ConditionalSelectUInt64
    {
        private struct DataTable
        {
            private byte[] inArray1;
            private byte[] inArray2;
            private byte[] inArray3;
            private byte[] outArray;

            private GCHandle inHandle1;
            private GCHandle inHandle2;
            private GCHandle inHandle3;
            private GCHandle outHandle;

            private ulong alignment;

            public DataTable(UInt64[] inArray1, UInt64[] inArray2, UInt64[] inArray3, UInt64[] outArray, int alignment)
            {
                int sizeOfinArray1 = inArray1.Length * Unsafe.SizeOf<UInt64>();
                int sizeOfinArray2 = inArray2.Length * Unsafe.SizeOf<UInt64>();
                int sizeOfinArray3 = inArray3.Length * Unsafe.SizeOf<UInt64>();
                int sizeOfoutArray = outArray.Length * Unsafe.SizeOf<UInt64>();
                if ((alignment != 32 && alignment != 16 && alignment != 8) || (alignment * 2) < sizeOfinArray1 || (alignment * 2) < sizeOfinArray2 || (alignment * 2) < sizeOfinArray3 || (alignment * 2) < sizeOfoutArray)
                {
                    throw new ArgumentException("Invalid value of alignment");
                }

                this.inArray1 = new byte[alignment * 2];
                this.inArray2 = new byte[alignment * 2];
                this.inArray3 = new byte[alignment * 2];
                this.outArray = new byte[alignment * 2];

                this.inHandle1 = GCHandle.Alloc(this.inArray1, GCHandleType.Pinned);
                this.inHandle2 = GCHandle.Alloc(this.inArray2, GCHandleType.Pinned);
                this.inHandle3 = GCHandle.Alloc(this.inArray3, GCHandleType.Pinned);
                this.outHandle = GCHandle.Alloc(this.outArray, GCHandleType.Pinned);

                this.alignment = (ulong)alignment;

                Unsafe.CopyBlockUnaligned(ref Unsafe.AsRef<byte>(inArray1Ptr), ref Unsafe.As<UInt64, byte>(ref inArray1[0]), (uint)sizeOfinArray1);
                Unsafe.CopyBlockUnaligned(ref Unsafe.AsRef<byte>(inArray2Ptr), ref Unsafe.As<UInt64, byte>(ref inArray2[0]), (uint)sizeOfinArray2);
                Unsafe.CopyBlockUnaligned(ref Unsafe.AsRef<byte>(inArray3Ptr), ref Unsafe.As<UInt64, byte>(ref inArray3[0]), (uint)sizeOfinArray3);
            }

            public void* inArray1Ptr => Align((byte*)(inHandle1.AddrOfPinnedObject().ToPointer()), alignment);
            public void* inArray2Ptr => Align((byte*)(inHandle2.AddrOfPinnedObject().ToPointer()), alignment);
            public void* inArray3Ptr => Align((byte*)(inHandle3.AddrOfPinnedObject().ToPointer()), alignment);
            public void* outArrayPtr => Align((byte*)(outHandle.AddrOfPinnedObject().ToPointer()), alignment);

            public void Dispose()
            {
                inHandle1.Free();
                inHandle2.Free();
                inHandle3.Free();
                outHandle.Free();
            }

            private static unsafe void* Align(byte* buffer, ulong expectedAlignment)
            {
                return (void*)(((ulong)buffer + expectedAlignment - 1) & ~(expectedAlignment - 1));
            }
        }

        private struct TestStruct
        {
            public Vector256<UInt64> _fld1;
            public Vector256<UInt64> _fld2;
            public Vector256<UInt64> _fld3;

            public static TestStruct Create()
            {
                var testStruct = new TestStruct();

                for (var i = 0; i < Op1ElementCount; i++) { _data1[i] = TestLibrary.Generator.GetUInt64(); }
                Unsafe.CopyBlockUnaligned(ref Unsafe.As<Vector256<UInt64>, byte>(ref testStruct._fld1), ref Unsafe.As<UInt64, byte>(ref _data1[0]), (uint)Unsafe.SizeOf<Vector256<UInt64>>());
                for (var i = 0; i < Op2ElementCount; i++) { _data2[i] = TestLibrary.Generator.GetUInt64(); }
                Unsafe.CopyBlockUnaligned(ref Unsafe.As<Vector256<UInt64>, byte>(ref testStruct._fld2), ref Unsafe.As<UInt64, byte>(ref _data2[0]), (uint)Unsafe.SizeOf<Vector256<UInt64>>());
                for (var i = 0; i < Op3ElementCount; i++) { _data3[i] = TestLibrary.Generator.GetUInt64(); }
                Unsafe.CopyBlockUnaligned(ref Unsafe.As<Vector256<UInt64>, byte>(ref testStruct._fld3), ref Unsafe.As<UInt64, byte>(ref _data3[0]), (uint)Unsafe.SizeOf<Vector256<UInt64>>());

                return testStruct;
            }

            public void RunStructFldScenario(VectorTernaryOpTest__ConditionalSelectUInt64 testClass)
            {
                var result = Vector256.ConditionalSelect(_fld1, _fld2, _fld3);

                Unsafe.Write(testClass._dataTable.outArrayPtr, result);
                testClass.ValidateResult(_fld1, _fld2, _fld3, testClass._dataTable.outArrayPtr);
            }
        }

        private static readonly int LargestVectorSize = 32;

        private static readonly int Op1ElementCount = Unsafe.SizeOf<Vector256<UInt64>>() / sizeof(UInt64);
        private static readonly int Op2ElementCount = Unsafe.SizeOf<Vector256<UInt64>>() / sizeof(UInt64);
        private static readonly int Op3ElementCount = Unsafe.SizeOf<Vector256<UInt64>>() / sizeof(UInt64);
        private static readonly int RetElementCount = Unsafe.SizeOf<Vector256<UInt64>>() / sizeof(UInt64);

        private static UInt64[] _data1 = new UInt64[Op1ElementCount];
        private static UInt64[] _data2 = new UInt64[Op2ElementCount];
        private static UInt64[] _data3 = new UInt64[Op3ElementCount];

        private static Vector256<UInt64> _clsVar1;
        private static Vector256<UInt64> _clsVar2;
        private static Vector256<UInt64> _clsVar3;

        private Vector256<UInt64> _fld1;
        private Vector256<UInt64> _fld2;
        private Vector256<UInt64> _fld3;

        private DataTable _dataTable;

        static VectorTernaryOpTest__ConditionalSelectUInt64()
        {
            for (var i = 0; i < Op1ElementCount; i++) { _data1[i] = TestLibrary.Generator.GetUInt64(); }
            Unsafe.CopyBlockUnaligned(ref Unsafe.As<Vector256<UInt64>, byte>(ref _clsVar1), ref Unsafe.As<UInt64, byte>(ref _data1[0]), (uint)Unsafe.SizeOf<Vector256<UInt64>>());
            for (var i = 0; i < Op2ElementCount; i++) { _data2[i] = TestLibrary.Generator.GetUInt64(); }
            Unsafe.CopyBlockUnaligned(ref Unsafe.As<Vector256<UInt64>, byte>(ref _clsVar2), ref Unsafe.As<UInt64, byte>(ref _data2[0]), (uint)Unsafe.SizeOf<Vector256<UInt64>>());
            for (var i = 0; i < Op3ElementCount; i++) { _data3[i] = TestLibrary.Generator.GetUInt64(); }
            Unsafe.CopyBlockUnaligned(ref Unsafe.As<Vector256<UInt64>, byte>(ref _clsVar3), ref Unsafe.As<UInt64, byte>(ref _data3[0]), (uint)Unsafe.SizeOf<Vector256<UInt64>>());
        }

        public VectorTernaryOpTest__ConditionalSelectUInt64()
        {
            Succeeded = true;

            for (var i = 0; i < Op1ElementCount; i++) { _data1[i] = TestLibrary.Generator.GetUInt64(); }
            Unsafe.CopyBlockUnaligned(ref Unsafe.As<Vector256<UInt64>, byte>(ref _fld1), ref Unsafe.As<UInt64, byte>(ref _data1[0]), (uint)Unsafe.SizeOf<Vector256<UInt64>>());
            for (var i = 0; i < Op2ElementCount; i++) { _data2[i] = TestLibrary.Generator.GetUInt64(); }
            Unsafe.CopyBlockUnaligned(ref Unsafe.As<Vector256<UInt64>, byte>(ref _fld2), ref Unsafe.As<UInt64, byte>(ref _data2[0]), (uint)Unsafe.SizeOf<Vector256<UInt64>>());
            for (var i = 0; i < Op3ElementCount; i++) { _data3[i] = TestLibrary.Generator.GetUInt64(); }
            Unsafe.CopyBlockUnaligned(ref Unsafe.As<Vector256<UInt64>, byte>(ref _fld3), ref Unsafe.As<UInt64, byte>(ref _data3[0]), (uint)Unsafe.SizeOf<Vector256<UInt64>>());

            for (var i = 0; i < Op1ElementCount; i++) { _data1[i] = TestLibrary.Generator.GetUInt64(); }
            for (var i = 0; i < Op2ElementCount; i++) { _data2[i] = TestLibrary.Generator.GetUInt64(); }
            for (var i = 0; i < Op3ElementCount; i++) { _data3[i] = TestLibrary.Generator.GetUInt64(); }
            _dataTable = new DataTable(_data1, _data2, _data3, new UInt64[RetElementCount], LargestVectorSize);
        }

        public bool Succeeded { get; set; }

        public void RunBasicScenario_UnsafeRead()
        {
            TestLibrary.TestFramework.BeginScenario(nameof(RunBasicScenario_UnsafeRead));

            var result = Vector256.ConditionalSelect(
                Unsafe.Read<Vector256<UInt64>>(_dataTable.inArray1Ptr),
                Unsafe.Read<Vector256<UInt64>>(_dataTable.inArray2Ptr),
                Unsafe.Read<Vector256<UInt64>>(_dataTable.inArray3Ptr)
            );

            Unsafe.Write(_dataTable.outArrayPtr, result);
            ValidateResult(_dataTable.inArray1Ptr, _dataTable.inArray2Ptr, _dataTable.inArray3Ptr, _dataTable.outArrayPtr);
        }

        public void RunReflectionScenario_UnsafeRead()
        {
            TestLibrary.TestFramework.BeginScenario(nameof(RunReflectionScenario_UnsafeRead));

            var method = typeof(Vector256).GetMethod(nameof(Vector256.ConditionalSelect), new Type[] {
                typeof(Vector256<UInt64>),
                typeof(Vector256<UInt64>),
                typeof(Vector256<UInt64>)
            });

            if (method is null)
            {
                method = typeof(Vector256).GetMethod(nameof(Vector256.ConditionalSelect), 1, new Type[] {
                    typeof(Vector256<>).MakeGenericType(Type.MakeGenericMethodParameter(0)),
                    typeof(Vector256<>).MakeGenericType(Type.MakeGenericMethodParameter(0)),
                    typeof(Vector256<>).MakeGenericType(Type.MakeGenericMethodParameter(0))
                });
            }

            if (method.IsGenericMethodDefinition)
            {
                method = method.MakeGenericMethod(typeof(UInt64));
            }

            var result = method.Invoke(null, new object[] {
                Unsafe.Read<Vector256<UInt64>>(_dataTable.inArray1Ptr),
                Unsafe.Read<Vector256<UInt64>>(_dataTable.inArray2Ptr),
                Unsafe.Read<Vector256<UInt64>>(_dataTable.inArray3Ptr)
            });

            Unsafe.Write(_dataTable.outArrayPtr, (Vector256<UInt64>)(result));
            ValidateResult(_dataTable.inArray1Ptr, _dataTable.inArray2Ptr, _dataTable.inArray3Ptr, _dataTable.outArrayPtr);
        }

        public void RunClsVarScenario()
        {
            TestLibrary.TestFramework.BeginScenario(nameof(RunClsVarScenario));

            var result = Vector256.ConditionalSelect(
                _clsVar1,
                _clsVar2,
                _clsVar3
            );

            Unsafe.Write(_dataTable.outArrayPtr, result);
            ValidateResult(_clsVar1, _clsVar2, _clsVar3, _dataTable.outArrayPtr);
        }

        public void RunLclVarScenario_UnsafeRead()
        {
            TestLibrary.TestFramework.BeginScenario(nameof(RunLclVarScenario_UnsafeRead));

            var op1 = Unsafe.Read<Vector256<UInt64>>(_dataTable.inArray1Ptr);
            var op2 = Unsafe.Read<Vector256<UInt64>>(_dataTable.inArray2Ptr);
            var op3 = Unsafe.Read<Vector256<UInt64>>(_dataTable.inArray3Ptr);
            var result = Vector256.ConditionalSelect(op1, op2, op3);

            Unsafe.Write(_dataTable.outArrayPtr, result);
            ValidateResult(op1, op2, op3, _dataTable.outArrayPtr);
        }

        public void RunClassLclFldScenario()
        {
            TestLibrary.TestFramework.BeginScenario(nameof(RunClassLclFldScenario));

            var test = new VectorTernaryOpTest__ConditionalSelectUInt64();
            var result = Vector256.ConditionalSelect(test._fld1, test._fld2, test._fld3);

            Unsafe.Write(_dataTable.outArrayPtr, result);
            ValidateResult(test._fld1, test._fld2, test._fld3, _dataTable.outArrayPtr);
        }

        public void RunClassFldScenario()
        {
            TestLibrary.TestFramework.BeginScenario(nameof(RunClassFldScenario));

            var result = Vector256.ConditionalSelect(_fld1, _fld2, _fld3);

            Unsafe.Write(_dataTable.outArrayPtr, result);
            ValidateResult(_fld1, _fld2, _fld3, _dataTable.outArrayPtr);
        }

        public void RunStructLclFldScenario()
        {
            TestLibrary.TestFramework.BeginScenario(nameof(RunStructLclFldScenario));

            var test = TestStruct.Create();
            var result = Vector256.ConditionalSelect(test._fld1, test._fld2, test._fld3);

            Unsafe.Write(_dataTable.outArrayPtr, result);
            ValidateResult(test._fld1, test._fld2, test._fld3, _dataTable.outArrayPtr);
        }

        public void RunStructFldScenario()
        {
            TestLibrary.TestFramework.BeginScenario(nameof(RunStructFldScenario));

            var test = TestStruct.Create();
            test.RunStructFldScenario(this);
        }

        private void ValidateResult(Vector256<UInt64> op1, Vector256<UInt64> op2, Vector256<UInt64> op3, void* result, [CallerMemberName] string method = "")
        {
            UInt64[] inArray1 = new UInt64[Op1ElementCount];
            UInt64[] inArray2 = new UInt64[Op2ElementCount];
            UInt64[] inArray3 = new UInt64[Op3ElementCount];
            UInt64[] outArray = new UInt64[RetElementCount];

            Unsafe.WriteUnaligned(ref Unsafe.As<UInt64, byte>(ref inArray1[0]), op1);
            Unsafe.WriteUnaligned(ref Unsafe.As<UInt64, byte>(ref inArray2[0]), op2);
            Unsafe.WriteUnaligned(ref Unsafe.As<UInt64, byte>(ref inArray3[0]), op3);
            Unsafe.CopyBlockUnaligned(ref Unsafe.As<UInt64, byte>(ref outArray[0]), ref Unsafe.AsRef<byte>(result), (uint)Unsafe.SizeOf<Vector256<UInt64>>());

            ValidateResult(inArray1, inArray2, inArray3, outArray, method);
        }

        private void ValidateResult(void* op1, void* op2, void* op3, void* result, [CallerMemberName] string method = "")
        {
            UInt64[] inArray1 = new UInt64[Op1ElementCount];
            UInt64[] inArray2 = new UInt64[Op2ElementCount];
            UInt64[] inArray3 = new UInt64[Op3ElementCount];
            UInt64[] outArray = new UInt64[RetElementCount];

            Unsafe.CopyBlockUnaligned(ref Unsafe.As<UInt64, byte>(ref inArray1[0]), ref Unsafe.AsRef<byte>(op1), (uint)Unsafe.SizeOf<Vector256<UInt64>>());
            Unsafe.CopyBlockUnaligned(ref Unsafe.As<UInt64, byte>(ref inArray2[0]), ref Unsafe.AsRef<byte>(op2), (uint)Unsafe.SizeOf<Vector256<UInt64>>());
            Unsafe.CopyBlockUnaligned(ref Unsafe.As<UInt64, byte>(ref inArray3[0]), ref Unsafe.AsRef<byte>(op3), (uint)Unsafe.SizeOf<Vector256<UInt64>>());
            Unsafe.CopyBlockUnaligned(ref Unsafe.As<UInt64, byte>(ref outArray[0]), ref Unsafe.AsRef<byte>(result), (uint)Unsafe.SizeOf<Vector256<UInt64>>());

            ValidateResult(inArray1, inArray2, inArray3, outArray, method);
        }

        private void ValidateResult(UInt64[] firstOp, UInt64[] secondOp, UInt64[] thirdOp, UInt64[] result, [CallerMemberName] string method = "")
        {
            bool succeeded = true;

            if (result[0] != (ulong)((secondOp[0] & firstOp[0]) | (thirdOp[0] & ~firstOp[0])))
            {
                succeeded = false;
            }
            else
            {
                for (var i = 1; i < RetElementCount; i++)
                {
                    if (result[i] != (ulong)((secondOp[i] & firstOp[i]) | (thirdOp[i] & ~firstOp[i])))
                    {
                        succeeded = false;
                        break;
                    }
                }
            }

            if (!succeeded)
            {
                TestLibrary.TestFramework.LogInformation($"{nameof(Vector256)}.{nameof(Vector256.ConditionalSelect)}<UInt64>(Vector256<UInt64>, Vector256<UInt64>, Vector256<UInt64>): {method} failed:");
                TestLibrary.TestFramework.LogInformation($" firstOp: ({string.Join(", ", firstOp)})");
                TestLibrary.TestFramework.LogInformation($"secondOp: ({string.Join(", ", secondOp)})");
                TestLibrary.TestFramework.LogInformation($" thirdOp: ({string.Join(", ", thirdOp)})");
                TestLibrary.TestFramework.LogInformation($"  result: ({string.Join(", ", result)})");
                TestLibrary.TestFramework.LogInformation(string.Empty);

                Succeeded = false;
            }
        }
    }
}
