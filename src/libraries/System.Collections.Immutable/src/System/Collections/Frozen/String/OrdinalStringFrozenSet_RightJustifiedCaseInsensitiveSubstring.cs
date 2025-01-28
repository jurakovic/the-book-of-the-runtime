﻿// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.Collections.Generic;

namespace System.Collections.Frozen
{
    internal sealed partial class OrdinalStringFrozenSet_RightJustifiedCaseInsensitiveSubstring : OrdinalStringFrozenSet
    {
        internal OrdinalStringFrozenSet_RightJustifiedCaseInsensitiveSubstring(
            string[] entries,
            IEqualityComparer<string> comparer,
            int minimumLength,
            int maximumLengthDiff,
            int hashIndex,
            int hashCount)
            : base(entries, comparer, minimumLength, maximumLengthDiff, hashIndex, hashCount)
        {
        }

        // See comment in OrdinalStringFrozenSet for why these overrides exist. Do not remove.
        private protected override int FindItemIndex(string item) => base.FindItemIndex(item);

        private protected override bool Equals(string? x, string? y) => StringComparer.OrdinalIgnoreCase.Equals(x, y);
        private protected override bool Equals(ReadOnlySpan<char> x, string? y) => EqualsOrdinalIgnoreCase(x, y);
        private protected override int GetHashCode(string s) => Hashing.GetHashCodeOrdinalIgnoreCase(s.AsSpan(s.Length + HashIndex, HashCount));
        private protected override int GetHashCode(ReadOnlySpan<char> s) => Hashing.GetHashCodeOrdinalIgnoreCase(s.Slice(s.Length + HashIndex, HashCount));
    }
}
