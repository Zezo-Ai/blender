/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: Apache-2.0 */

#include "testing/testing.h"

#include <fmt/format.h>

#include "BLI_array.hh"
#include "BLI_bit_span_ops.hh"
#include "BLI_bit_vector.hh"
#include "BLI_index_mask.hh"
#include "BLI_index_ranges_builder.hh"
#include "BLI_rand.hh"
#include "BLI_set.hh"
#include "BLI_timeit.hh"

#include "BLI_strict_flags.h" /* IWYU pragma: keep. Keep last. */

namespace blender::index_mask::tests {

TEST(index_mask, IndicesToMask)
{
  IndexMaskMemory memory;
  Array<int> data = {
      5, 100, 16383, 16384, 16385, 20000, 20001, 50000, 50001, 50002, 100000, 101000};
  IndexMask mask = IndexMask::from_indices<int>(data, memory);

  EXPECT_EQ(mask.first(), 5);
  EXPECT_EQ(mask.last(), 101000);
  EXPECT_EQ(mask.min_array_size(), 101001);
  EXPECT_EQ(mask.bounds(), IndexRange::from_begin_end_inclusive(5, 101000));
}

TEST(index_mask, FromBitsManual)
{
  IndexMaskMemory memory;
  const uint64_t bits =
      0b0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'1111'0010'0000;
  const IndexMask mask = IndexMask::from_bits(BitSpan(&bits, IndexRange(2, 40)), memory);
  Array<int> indices(5);
  mask.to_indices<int>(indices);
  EXPECT_EQ(indices[0], 3);
  EXPECT_EQ(indices[1], 6);
  EXPECT_EQ(indices[2], 7);
  EXPECT_EQ(indices[3], 8);
  EXPECT_EQ(indices[4], 9);
}

TEST(index_mask, FromBitsSimple)
{
  IndexMaskMemory memory;
  BitVector bit_vec(200, true);
  bit_vec[0].reset();
  bit_vec[100].reset();
  const IndexMask mask = IndexMask::from_bits(bit_vec, memory);

  EXPECT_EQ(mask.size(), 198);
  EXPECT_FALSE(mask.contains(0));
  EXPECT_FALSE(mask.contains(100));
}

TEST(index_mask, FromBitsWithUniverse)
{
  IndexMaskMemory memory;
  BitVector bit_vec(200, true);
  bit_vec[6].reset();
  bit_vec[100].reset();

  const IndexMask universe = IndexMask::from_indices<int>({4, 6, 7, 8, 9, 100, 101, 102}, memory);
  const IndexMask mask = IndexMask::from_bits(universe, bit_vec, memory);
  EXPECT_EQ(mask.size(), 6);
  EXPECT_EQ(mask[0], 4);
  EXPECT_EQ(mask[1], 7);
  EXPECT_EQ(mask[2], 8);
  EXPECT_EQ(mask[3], 9);
  EXPECT_EQ(mask[4], 101);
  EXPECT_EQ(mask[5], 102);
}

TEST(index_mask, FromBitsSparse)
{
  BitVector bit_vec(100'000, false);
  bit_vec[5].set();
  bit_vec[100].set();
  bit_vec[200].set();
  bit_vec[500].set();
  bit_vec[800].set();
  bit_vec[10'000].set();
  bit_vec[10'002].set();
  bit_vec[50'000].set();
  bit_vec[70'000].set();
  bit_vec[70'002].set();
  bit_vec[70'004].set();
  bit_vec[70'005].set();

  IndexMaskMemory memory;
  const IndexMask mask = IndexMask::from_bits(bit_vec, memory);
  EXPECT_EQ(mask.size(), 12);
  EXPECT_EQ(mask[0], 5);
  EXPECT_EQ(mask[1], 100);
  EXPECT_EQ(mask[2], 200);
  EXPECT_EQ(mask[3], 500);
  EXPECT_EQ(mask[4], 800);
  EXPECT_EQ(mask[5], 10'000);
  EXPECT_EQ(mask[6], 10'002);
  EXPECT_EQ(mask[7], 50'000);
  EXPECT_EQ(mask[8], 70'000);
  EXPECT_EQ(mask[9], 70'002);
  EXPECT_EQ(mask[10], 70'004);
  EXPECT_EQ(mask[11], 70'005);
}

TEST(index_mask, FromBoolsSparse)
{
  Vector<bool> bools(10'000'000, false);
  bools[5] = true;
  bools[100] = true;
  bools[200] = true;
  bools[500] = true;
  bools[800] = true;
  bools[10'000] = true;
  bools[10'002] = true;
  bools[50'000] = true;
  bools[70'000] = true;
  bools[70'002] = true;
  bools[70'004] = true;
  bools[70'005] = true;

  IndexMaskMemory memory;
  const IndexMask mask = IndexMask::from_bools(bools, memory);
  EXPECT_EQ(mask.size(), 12);
  EXPECT_EQ(mask[0], 5);
  EXPECT_EQ(mask[1], 100);
  EXPECT_EQ(mask[2], 200);
  EXPECT_EQ(mask[3], 500);
  EXPECT_EQ(mask[4], 800);
  EXPECT_EQ(mask[5], 10'000);
  EXPECT_EQ(mask[6], 10'002);
  EXPECT_EQ(mask[7], 50'000);
  EXPECT_EQ(mask[8], 70'000);
  EXPECT_EQ(mask[9], 70'002);
  EXPECT_EQ(mask[10], 70'004);
  EXPECT_EQ(mask[11], 70'005);
}

TEST(index_mask, FromBitsAlternating)
{
  int64_t size = 100'000;
  BitVector<> bits(size, false);
  for (const int64_t i : IndexRange(size / 2)) {
    bits[i * 2].set();
  }

  IndexMaskMemory memory;
  const IndexMask mask = IndexMask::from_bits(bits, memory);
  EXPECT_EQ(mask.size(), size / 2);
  EXPECT_EQ(mask[0], 0);
  EXPECT_EQ(mask[1], 2);
  EXPECT_EQ(mask[2], 4);
  EXPECT_EQ(mask[3], 6);
}

/* The benchmark is too slow to run during normal test runs. */
#if 0

static BitVector<> build_bits_with_uniform_distribution(const int bits_num,
                                                        const int set_bits_num,
                                                        const uint32_t seed = 0)
{
  if (set_bits_num > bits_num / 2) {
    BitVector bit_vec = build_bits_with_uniform_distribution(bits_num, bits_num - set_bits_num);
    bits::invert(bit_vec);
    return bit_vec;
  }
  BitVector bit_vec(bits_num, false);
  RandomNumberGenerator rng(seed);
  int counter = 0;
  while (counter < set_bits_num) {
    const int i = rng.get_int32(int(bits_num));
    MutableBitRef bit = bit_vec[i];
    if (!bit) {
      bit.set();
      counter++;
    }
  }
  return bit_vec;
}

static void benchmark_uniform_bit_distribution(const int bits_num,
                                               const int set_bits_num,
                                               const int iterations)
{
  const bool machine_readable = true;
  BitVector bit_vec = build_bits_with_uniform_distribution(bits_num, set_bits_num);
  std::locale loc("en_US.UTF-8");
  timeit::Nanoseconds min_duration{INT64_MAX};
  for ([[maybe_unused]] const int64_t i : IndexRange(iterations)) {
    IndexMaskMemory memory;
    timeit::TimePoint start = timeit::Clock::now();
    const IndexMask mask = IndexMask::from_bits(bit_vec, memory);
    timeit::TimePoint end = timeit::Clock::now();
    const timeit::Nanoseconds duration = end - start;
    // const double ms = double(duration.count()) / 1'000'000.0;
    // std::cout << fmt::format(loc, "{:15L} / {:L}: {:.4} ms\n", set_bits_num, bits_num, ms);
    min_duration = std::min(min_duration, duration);
    EXPECT_EQ(mask.size(), set_bits_num);
  }
  const double ms = double(min_duration.count()) / 1'000'000.0;
  if (machine_readable) {
    std::cout << fmt::format("{},{:.6}\n", set_bits_num, ms);
  }
  else {
    std::cout << fmt::format(loc, "{:15L} / {:L}: {:.4} ms\n", set_bits_num, bits_num, ms);
  }
}

TEST(index_mask, FromBitsBenchmark)
{
  const int size = 100'000'000;
  const int iterations = 5;
  Vector<int> set_bit_nums;
  set_bit_nums.append(0);
  int current = 100;
  while (current < size / 2) {
    set_bit_nums.append(current);
    set_bit_nums.append(size - current);
    current = int(current * 1.3);
  }
  set_bit_nums.append(size);
  std::sort(set_bit_nums.begin(), set_bit_nums.end());

  for (const int set_bit_num : set_bit_nums) {
    benchmark_uniform_bit_distribution(size, set_bit_num, iterations);
  }
}

/* Benchmark. */
#endif

TEST(index_mask, FromBitsFuzzy)
{
  RandomNumberGenerator rng(0);
  for ([[maybe_unused]] const int64_t iteration : IndexRange(10)) {
    const int size = rng.get_int32(100'000) + 1;
    const int set_bits = rng.get_int32(size);

    /* Remove part of the beginning and end of the bits to test unaligned bit spans. */
    const int64_t slice_inset = rng.get_int32(size / 10);
    const IndexRange slice = IndexRange::from_begin_end(slice_inset, size - slice_inset);

    BitVector<> bits(size, false);
    for ([[maybe_unused]] const int64_t set_bit_i : IndexRange(set_bits)) {
      int index = rng.get_int32(size);
      /* This is like linear probing which results in a somewhat random distribution but also leads
       * to having longer ranges every now and then. */
      while (true) {
        if (!bits[index]) {
          bits[index].set();
          break;
        }
        index = (index + 1) % size;
      }
    }

    IndexMaskMemory memory;

    /* The universe is partially a range and partially only even/odd indices.
     * For example: [1, 2, 3, 4, 6, 8, 10, 12]. */
    const int64_t slice_half_size = slice.size() / 2;
    const IndexMask universe = IndexMask::from_union(
                                   IndexRange(slice_half_size),
                                   IndexMask::from_repeating(
                                       IndexRange(1), slice_half_size, 2, slice_half_size, memory),
                                   memory)
                                   .slice_content(slice.index_range());

    const IndexMask mask = IndexMask::from_bits(universe, BitSpan(bits).slice(slice), memory);

    BitVector<> inverted_bits{bits};
    bits::invert(inverted_bits);
    const IndexMask inverted_mask = IndexMask::from_bits(
        universe, BitSpan(inverted_bits).slice(slice), memory);

    const IndexMask double_inverted_mask = inverted_mask.complement(universe, memory);
    EXPECT_EQ(mask, double_inverted_mask);
  }
}

TEST(index_mask, FromBitsDense)
{
  BitVector bit_vec(1'000, true);
  bit_vec[5].reset();
  bit_vec[200].reset();
  bit_vec[201].reset();
  bit_vec[500].reset();
  bit_vec[502].reset();
  bit_vec[504].reset();
  bit_vec[506].reset();

  IndexMaskMemory memory;
  const IndexMask mask = IndexMask::from_bits(bit_vec, memory);
  EXPECT_EQ(mask.size(), 993);
  EXPECT_FALSE(mask.contains(5));
  EXPECT_FALSE(mask.contains(200));
  EXPECT_FALSE(mask.contains(201));
  EXPECT_FALSE(mask.contains(500));
  EXPECT_FALSE(mask.contains(502));
  EXPECT_FALSE(mask.contains(504));
  EXPECT_FALSE(mask.contains(506));
}

TEST(index_mask, FromSize)
{
  {
    const IndexMask mask(5);
    Vector<IndexMaskSegment> segments;
    mask.foreach_segment([&](const IndexMaskSegment segment) { segments.append(segment); });
    EXPECT_EQ(segments.size(), 1);
    EXPECT_EQ(segments[0].size(), 5);
    EXPECT_EQ(mask.first(), 0);
    EXPECT_EQ(mask.last(), 4);
    EXPECT_EQ(mask.min_array_size(), 5);
    EXPECT_EQ(mask.bounds(), IndexRange(5));
  }
  {
    const IndexMask mask(max_segment_size);
    Vector<IndexMaskSegment> segments;
    mask.foreach_segment([&](const IndexMaskSegment segment) { segments.append(segment); });
    EXPECT_EQ(segments.size(), 1);
    EXPECT_EQ(segments[0].size(), max_segment_size);
    EXPECT_EQ(mask.first(), 0);
    EXPECT_EQ(mask.last(), max_segment_size - 1);
    EXPECT_EQ(mask.min_array_size(), max_segment_size);
    EXPECT_EQ(mask.bounds(), IndexRange(max_segment_size));
  }
}

TEST(index_mask, FromUnion)
{
  {
    IndexMaskMemory memory;
    Array<int> data_a = {1, 2};
    IndexMask mask_a = IndexMask::from_indices<int>(data_a, memory);
    Array<int> data_b = {2, 20000, 20001};
    IndexMask mask_b = IndexMask::from_indices<int>(data_b, memory);

    IndexMask mask_union = IndexMask::from_union(mask_a, mask_b, memory);

    EXPECT_EQ(mask_union.size(), 4);
    EXPECT_EQ(mask_union[0], 1);
    EXPECT_EQ(mask_union[1], 2);
    EXPECT_EQ(mask_union[2], 20000);
    EXPECT_EQ(mask_union[3], 20001);
  }
  {
    IndexMaskMemory memory;
    Array<int> data_a = {1, 2, 3};
    IndexMask mask_a = IndexMask::from_indices<int>(data_a, memory);
    Array<int> data_b = {20000, 20001, 20002};
    IndexMask mask_b = IndexMask::from_indices<int>(data_b, memory);

    IndexMask mask_union = IndexMask::from_union(mask_a, mask_b, memory);

    EXPECT_EQ(mask_union.size(), 6);
    EXPECT_EQ(mask_union[0], 1);
    EXPECT_EQ(mask_union[1], 2);
    EXPECT_EQ(mask_union[2], 3);
    EXPECT_EQ(mask_union[3], 20000);
    EXPECT_EQ(mask_union[4], 20001);
    EXPECT_EQ(mask_union[5], 20002);
  }
  {
    IndexMaskMemory memory;
    IndexMask mask_union = IndexMask::from_union({}, memory);
    EXPECT_TRUE(mask_union.is_empty());
  }
  {
    IndexMaskMemory memory;
    IndexMask mask_union = IndexMask::from_union({IndexRange::from_begin_end(0, 10000),
                                                  IndexRange::from_begin_end(20000, 30000),
                                                  IndexRange::from_begin_end(40000, 50000)},
                                                 memory);
    EXPECT_EQ(mask_union.size(), 30000);
  }
}

TEST(index_mask, FromDifference)
{
  {
    IndexMaskMemory memory;
    Array<int> data_a = {1, 2, 3};
    IndexMask mask_a = IndexMask::from_indices<int>(data_a, memory);
    Array<int> data_b = {2, 20000, 20001};
    IndexMask mask_b = IndexMask::from_indices<int>(data_b, memory);

    IndexMask mask_difference = IndexMask::from_difference(mask_a, mask_b, memory);

    EXPECT_EQ(mask_difference.size(), 2);
    EXPECT_EQ(mask_difference[0], 1);
    EXPECT_EQ(mask_difference[1], 3);
  }
  {
    IndexMaskMemory memory;
    Array<int> data_a = {1, 2, 3};
    IndexMask mask_a = IndexMask::from_indices<int>(data_a, memory);
    Array<int> data_b = {1, 2, 3};
    IndexMask mask_b = IndexMask::from_indices<int>(data_b, memory);

    IndexMask mask_difference = IndexMask::from_difference(mask_a, mask_b, memory);

    EXPECT_TRUE(mask_difference.is_empty());
  }
}

TEST(index_mask, FromIntersection)
{
  {
    IndexMaskMemory memory;
    Array<int> data_a = {1, 2, 20000};
    IndexMask mask_a = IndexMask::from_indices<int>(data_a, memory);
    Array<int> data_b = {2, 20000, 20001};
    IndexMask mask_b = IndexMask::from_indices<int>(data_b, memory);

    IndexMask mask_intersection = IndexMask::from_intersection(mask_a, mask_b, memory);

    EXPECT_EQ(mask_intersection.size(), 2);
    EXPECT_EQ(mask_intersection[0], 2);
    EXPECT_EQ(mask_intersection[1], 20000);
  }
  {
    IndexMaskMemory memory;
    Array<int> data_a = {1, 2, 3};
    IndexMask mask_a = IndexMask::from_indices<int>(data_a, memory);
    Array<int> data_b = {20000, 20001, 20002};
    IndexMask mask_b = IndexMask::from_indices<int>(data_b, memory);

    IndexMask mask_intersection = IndexMask::from_intersection(mask_a, mask_b, memory);

    EXPECT_TRUE(mask_intersection.is_empty());
  }
}

TEST(index_mask, DefaultConstructor)
{
  IndexMask mask;
  EXPECT_EQ(mask.size(), 0);
  EXPECT_EQ(mask.min_array_size(), 0);
  EXPECT_EQ(mask.bounds(), IndexRange());
}

TEST(index_mask, ForeachRange)
{
  IndexMaskMemory memory;
  const IndexMask mask = IndexMask::from_indices<int>({2, 3, 4, 10, 40, 41}, memory);
  Vector<IndexRange> ranges;
  mask.foreach_range([&](const IndexRange range) { ranges.append(range); });

  EXPECT_EQ(ranges.size(), 3);
  EXPECT_EQ(ranges[0], IndexRange(2, 3));
  EXPECT_EQ(ranges[1], IndexRange(10, 1));
  EXPECT_EQ(ranges[2], IndexRange(40, 2));
}

TEST(index_mask, ToRange)
{
  IndexMaskMemory memory;
  {
    const IndexMask mask = IndexMask::from_indices<int>({4, 5, 6, 7}, memory);
    EXPECT_TRUE(mask.to_range().has_value());
    EXPECT_EQ(*mask.to_range(), IndexRange(4, 4));
  }
  {
    const IndexMask mask = IndexMask::from_indices<int>({}, memory);
    EXPECT_TRUE(mask.to_range().has_value());
    EXPECT_EQ(*mask.to_range(), IndexRange());
  }
  {
    const IndexMask mask = IndexMask::from_indices<int>({0, 1, 3, 4}, memory);
    EXPECT_FALSE(mask.to_range().has_value());
  }
  {
    const IndexRange range{16000, 40000};
    const IndexMask mask{range};
    EXPECT_TRUE(mask.to_range().has_value());
    EXPECT_EQ(*mask.to_range(), range);
  }
}

TEST(index_mask, ToBits)
{
  IndexMaskMemory memory;
  {
    const IndexMask mask = IndexMask::from_indices<int>({4, 5, 6, 7}, memory);
    BitVector<> bits(mask.min_array_size());
    mask.to_bits(bits);
    EXPECT_EQ(bits[0].test(), false);
    EXPECT_EQ(bits[1].test(), false);
    EXPECT_EQ(bits[2].test(), false);
    EXPECT_EQ(bits[3].test(), false);
    EXPECT_EQ(bits[4].test(), true);
    EXPECT_EQ(bits[5].test(), true);
    EXPECT_EQ(bits[6].test(), true);
    EXPECT_EQ(bits[7].test(), true);
  }
  {
    const IndexMask mask = IndexMask::from_indices<int>({4, 5, 6, 7}, memory);
    BitVector<> bits(mask.min_array_size());
    bits[0].set();
    bits[2].set();
    mask.set_bits(bits);
    EXPECT_EQ(bits[0].test(), true);
    EXPECT_EQ(bits[1].test(), false);
    EXPECT_EQ(bits[2].test(), true);
    EXPECT_EQ(bits[3].test(), false);
    EXPECT_EQ(bits[4].test(), true);
    EXPECT_EQ(bits[5].test(), true);
    EXPECT_EQ(bits[6].test(), true);
    EXPECT_EQ(bits[7].test(), true);
  }
}

TEST(index_mask, FromRange)
{
  const auto test_range = [](const IndexRange range) {
    const IndexMask mask = range;
    EXPECT_EQ(mask.to_range(), range);
  };

  test_range({0, 0});
  test_range({0, 10});
  test_range({0, 16384});
  test_range({16320, 64});
  test_range({16384, 64});
  test_range({0, 100000});
  test_range({100000, 100000});
  test_range({688064, 64});
}

TEST(index_mask, FromPredicate)
{
  IndexMaskMemory memory;
  {
    const IndexRange range{20'000, 50'000};
    const IndexMask mask = IndexMask::from_predicate(
        IndexRange(100'000), GrainSize(1024), memory, [&](const int64_t i) {
          return range.contains(i);
        });
    EXPECT_EQ(mask.to_range(), range);
  }
  {
    const Vector<int64_t> indices = {0, 500, 20'000, 50'000};
    const IndexMask mask = IndexMask::from_predicate(
        IndexRange(100'000), GrainSize(1024), memory, [&](const int64_t i) {
          return indices.contains(i);
        });
    EXPECT_EQ(mask.size(), indices.size());
    Vector<int64_t> new_indices(mask.size());
    mask.to_indices<int64_t>(new_indices);
    EXPECT_EQ(indices, new_indices);
  }
}

TEST(index_mask, IndexIteratorConversionFuzzy)
{
  RandomNumberGenerator rng;

  Vector<int64_t> indices;
  indices.append(5);
  for ([[maybe_unused]] const int64_t i : IndexRange(1000)) {
    for ([[maybe_unused]] const int64_t j :
         IndexRange(indices.last() + 1 + rng.get_int32(1000), rng.get_int32(64)))
    {
      indices.append(j);
    }
  }

  IndexMaskMemory memory;
  const IndexMask mask = IndexMask::from_indices<int64_t>(indices, memory);
  EXPECT_EQ(mask.size(), indices.size());

  for ([[maybe_unused]] const int64_t _ : IndexRange(100)) {
    const int64_t index = rng.get_int32(int(indices.size()));
    const RawMaskIterator it = mask.index_to_iterator(index);
    EXPECT_EQ(mask[it], indices[index]);
    const int64_t new_index = mask.iterator_to_index(it);
    EXPECT_EQ(index, new_index);
  }

  for ([[maybe_unused]] const int64_t _ : IndexRange(100)) {
    const int64_t start = rng.get_int32(int(indices.size() - 1));
    const int64_t size = 1 + rng.get_int32(int(indices.size() - start - 1));
    const IndexMask sub_mask = mask.slice(start, size);
    const int64_t index = rng.get_int32(int(sub_mask.size()));
    const RawMaskIterator it = sub_mask.index_to_iterator(index);
    EXPECT_EQ(sub_mask[it], indices[start + index]);
    const int64_t new_index = sub_mask.iterator_to_index(it);
    EXPECT_EQ(index, new_index);
  }

  for ([[maybe_unused]] const int64_t _ : IndexRange(100)) {
    const int64_t index = rng.get_int32(int(indices.size() - 1000));
    for (const int64_t offset : {0, 1, 2, 100, 500}) {
      const int64_t index_to_search = indices[index] + offset;
      const bool contained = std::binary_search(indices.begin(), indices.end(), index_to_search);
      const std::optional<RawMaskIterator> it = mask.find(index_to_search);
      EXPECT_EQ(contained, it.has_value());
      if (contained) {
        EXPECT_EQ(index_to_search, mask[*it]);
      }
    }
  }
}

TEST(index_mask, FromPredicateFuzzy)
{
  RandomNumberGenerator rng;
  Set<int> values;

  for ([[maybe_unused]] const int64_t _ : IndexRange(10000)) {
    values.add(rng.get_int32(100'000));
  }

  IndexMaskMemory memory;
  const IndexMask mask = IndexMask::from_predicate(
      IndexRange(110'000), GrainSize(1024), memory, [&](const int64_t i) {
        return values.contains(int(i));
      });
  EXPECT_EQ(mask.size(), values.size());
  for (const int index : values) {
    EXPECT_TRUE(mask.contains(index));
  }
  mask.foreach_index([&](const int64_t index, const int64_t pos) {
    EXPECT_TRUE(values.contains(int(index)));
    EXPECT_EQ(index, mask[pos]);
  });
}

TEST(index_mask, Complement)
{
  IndexMaskMemory memory;
  {
    const IndexMask mask(0);
    const IndexMask complement = mask.complement(IndexRange(100), memory);
    EXPECT_EQ(100 - mask.size(), complement.size());
    complement.foreach_index([&](const int64_t i) { EXPECT_FALSE(mask.contains(i)); });
    mask.foreach_index([&](const int64_t i) { EXPECT_FALSE(complement.contains(i)); });
  }
  {
    const IndexMask mask(10000);
    const IndexMask complement = mask.complement(IndexRange(10000), memory);
    EXPECT_EQ(10000 - mask.size(), complement.size());
    complement.foreach_index([&](const int64_t i) { EXPECT_FALSE(mask.contains(i)); });
    mask.foreach_index([&](const int64_t i) { EXPECT_FALSE(complement.contains(i)); });
  }
  {
    const IndexMask mask(IndexRange(100, 900));
    const IndexMask complement = mask.complement(IndexRange(1000), memory);
    EXPECT_EQ(1000 - mask.size(), complement.size());
    complement.foreach_index([&](const int64_t i) { EXPECT_FALSE(mask.contains(i)); });
    mask.foreach_index([&](const int64_t i) { EXPECT_FALSE(complement.contains(i)); });
  }
  {
    const IndexMask mask(IndexRange(0, 900));
    const IndexMask complement = mask.complement(IndexRange(1000), memory);
    EXPECT_EQ(1000 - mask.size(), complement.size());
    complement.foreach_index([&](const int64_t i) { EXPECT_FALSE(mask.contains(i)); });
    mask.foreach_index([&](const int64_t i) { EXPECT_FALSE(complement.contains(i)); });
  }
}

TEST(index_mask, ComplementFuzzy)
{
  RandomNumberGenerator rng;

  const int64_t mask_size = 100;
  const int64_t iter_num = 100;
  const int64_t universe_size = 110;

  for (const int64_t iter : IndexRange(iter_num)) {
    Set<int> values;
    for ([[maybe_unused]] const int64_t _ : IndexRange(iter)) {
      values.add(rng.get_int32(mask_size));
    }
    IndexMaskMemory memory;
    const IndexMask mask = IndexMask::from_predicate(
        IndexRange(mask_size), GrainSize(1024), memory, [&](const int64_t i) {
          return values.contains(int(i));
        });

    const IndexMask complement = mask.complement(IndexRange(universe_size), memory);
    EXPECT_EQ(universe_size - mask.size(), complement.size());
    complement.foreach_index([&](const int64_t i) { EXPECT_FALSE(mask.contains(i)); });
    mask.foreach_index([&](const int64_t i) { EXPECT_FALSE(complement.contains(i)); });
  }
}

TEST(index_mask, OffsetIndexRangeFind)
{
  IndexMask mask = IndexRange(1, 2);
  auto result = mask.find(1);
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(mask.iterator_to_index(*result), 0);
  EXPECT_EQ(mask[0], 1);
}

TEST(index_mask, FindLargerEqual)
{
  IndexMaskMemory memory;
  {
    const IndexMask mask = IndexMask::from_initializers(
        {0, 1, 3, 6, IndexRange(50, 50), IndexRange(100'000, 30)}, memory);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(0)), 0);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(1)), 1);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(2)), 2);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(3)), 2);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(4)), 3);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(5)), 3);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(6)), 3);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(7)), 4);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(10)), 4);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(40)), 4);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(49)), 4);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(50)), 4);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(60)), 14);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(70)), 24);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(99)), 53);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(100)), 54);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(1'000)), 54);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(10'000)), 54);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(50'000)), 54);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(100'000)), 54);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(100'001)), 55);
    EXPECT_FALSE(mask.find_larger_equal(101'000).has_value());
  }
  {
    const IndexMask mask{IndexRange(10'000, 30'000)};
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(0)), 0);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(50)), 0);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(9'999)), 0);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(10'000)), 0);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(10'001)), 1);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(39'998)), 29'998);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_larger_equal(39'999)), 29'999);
    EXPECT_FALSE(mask.find_larger_equal(40'000).has_value());
    EXPECT_FALSE(mask.find_larger_equal(40'001).has_value());
    EXPECT_FALSE(mask.find_larger_equal(100'000).has_value());
  }
}

TEST(index_mask, FindSmallerEqual)
{
  IndexMaskMemory memory;
  {
    const IndexMask mask = IndexMask::from_initializers(
        {0, 1, 3, 6, IndexRange(50, 50), IndexRange(100'000, 30)}, memory);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(0)), 0);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(1)), 1);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(2)), 1);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(3)), 2);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(4)), 2);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(5)), 2);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(6)), 3);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(7)), 3);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(10)), 3);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(40)), 3);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(49)), 3);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(50)), 4);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(60)), 14);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(70)), 24);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(99)), 53);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(100)), 53);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(1'000)), 53);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(10'000)), 53);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(50'000)), 53);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(100'000)), 54);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(100'001)), 55);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(101'000)), 83);
  }
  {
    const IndexMask mask{IndexRange(10'000, 30'000)};
    EXPECT_FALSE(mask.find_smaller_equal(0).has_value());
    EXPECT_FALSE(mask.find_smaller_equal(1).has_value());
    EXPECT_FALSE(mask.find_smaller_equal(50).has_value());
    EXPECT_FALSE(mask.find_smaller_equal(9'999).has_value());
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(10'000)), 0);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(10'001)), 1);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(39'998)), 29'998);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(39'999)), 29'999);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(40'000)), 29'999);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(40'001)), 29'999);
    EXPECT_EQ(mask.iterator_to_index(*mask.find_smaller_equal(100'000)), 29'999);
  }
}

TEST(index_mask, SliceContent)
{
  IndexMaskMemory memory;
  {
    const IndexMask mask;
    EXPECT_TRUE(mask.slice_content(IndexRange(50, 10)).is_empty());
  }
  {
    const IndexMask mask{IndexRange(10, 90)};
    const IndexMask a = mask.slice_content(IndexRange(30));
    EXPECT_EQ(a.size(), 20);
    const IndexMask b = mask.slice_content(IndexRange(10, 90));
    EXPECT_EQ(b.size(), 90);
    const IndexMask c = mask.slice_content(IndexRange(80, 100));
    EXPECT_EQ(c.size(), 20);
    const IndexMask d = mask.slice_content(IndexRange(1000, 100));
    EXPECT_EQ(d.size(), 0);
  }
  {
    const IndexMask mask = IndexMask::from_initializers(
        {4, 5, 100, 1'000, 10'000, 20'000, 25'000, 100'000}, memory);
    EXPECT_EQ(mask.slice_content(IndexRange(10)).size(), 2);
    EXPECT_EQ(mask.slice_content(IndexRange(200)).size(), 3);
    EXPECT_EQ(mask.slice_content(IndexRange(2'000)).size(), 4);
    EXPECT_EQ(mask.slice_content(IndexRange(10'000)).size(), 4);
    EXPECT_EQ(mask.slice_content(IndexRange(10'001)).size(), 5);
    EXPECT_EQ(mask.slice_content(IndexRange(1'000'000)).size(), 8);
    EXPECT_EQ(mask.slice_content(IndexRange(10'000, 100'000)).size(), 4);
    EXPECT_EQ(mask.slice_content(IndexRange(1'001, 100'000)).size(), 4);
    EXPECT_EQ(mask.slice_content(IndexRange(1'000, 100'000)).size(), 5);
    EXPECT_EQ(mask.slice_content(IndexRange(1'000, 99'000)).size(), 4);
    EXPECT_EQ(mask.slice_content(IndexRange(1'000, 10'000)).size(), 2);
  }
}

TEST(index_mask, EqualsRangeSelf)
{
  IndexMask mask = IndexRange(16384);
  EXPECT_EQ(mask, mask);
}

TEST(index_mask, EqualsRange)
{
  IndexMask mask_a = IndexRange(16384);
  IndexMask mask_b = IndexRange(16384);
  EXPECT_EQ(mask_a, mask_b);
}

TEST(index_mask, EqualsRangeLarge)
{
  IndexMask mask_a = IndexRange(96384);
  IndexMask mask_b = IndexRange(96384);
  EXPECT_EQ(mask_a, mask_b);
}

TEST(index_mask, EqualsRangeBegin)
{
  IndexMask mask_a = IndexRange(102, 16384 - 102);
  IndexMask mask_b = IndexRange(102, 16384 - 102);
  EXPECT_EQ(mask_a, mask_b);
}

TEST(index_mask, EqualsRangeEnd)
{
  IndexMask mask_a = IndexRange(16384 + 1);
  IndexMask mask_b = IndexRange(16384 + 1);
  EXPECT_EQ(mask_a, mask_b);
}

TEST(index_mask, NonEqualsRange)
{
  IndexMask mask_a = IndexRange(16384);
  IndexMask mask_b = IndexRange(1, 16384);
  EXPECT_NE(mask_a, mask_b);
}

TEST(index_mask, EqualsSelf)
{
  IndexMaskMemory memory;
  IndexMask mask = IndexMask::from_union(IndexRange(16384), IndexRange(16384 * 3, 533), memory);
  EXPECT_EQ(mask, mask);
}

TEST(index_mask, Equals)
{
  IndexMaskMemory memory;
  IndexMask mask_a = IndexMask::from_union(IndexRange(16384), IndexRange(16384 * 3, 533), memory);
  IndexMask mask_b = IndexMask::from_union(IndexRange(16384), IndexRange(16384 * 3, 533), memory);
  EXPECT_EQ(mask_a, mask_b);
}

TEST(index_mask, NonEquals)
{
  IndexMaskMemory memory;
  IndexMask mask_a = IndexMask::from_union(IndexRange(16384), IndexRange(16384 * 3, 533), memory);
  IndexMask mask_b = IndexMask::from_union(
      IndexRange(55, 16384), IndexRange(16384 * 5, 533), memory);
  EXPECT_NE(mask_a, mask_b);
}

TEST(index_mask, NotEqualsRangeAndIndices)
{
  IndexMaskMemory memory;
  IndexMask mask_a = IndexMask::from_union(
      IndexRange(2040), IndexMask::from_indices<int>({2072, 2073, 2075}, memory), memory);
  IndexMask mask_b = IndexMask::from_union(
      IndexRange(2040), IndexMask::from_indices<int>({2072, 2073 + 1, 2075}, memory), memory);

  EXPECT_NE(mask_a, mask_b);
}

static bool mask_segments_equals(const IndexMaskSegment &a, const IndexMaskSegment &b)
{
  if (a.size() != b.size()) {
    return false;
  }
  for (const int64_t i : a.index_range()) {
    if (a[i] != b[i]) {
      return false;
    }
  }
  return true;
}

TEST(index_mask, ZippedForeachSelf)
{
  IndexMaskMemory memory;
  IndexMask mask = IndexMask::from_initializers({IndexRange(500), 555, 699, 222, 900, 100},
                                                memory);
  {
    int calls_num = 0;
    IndexMask::foreach_segment_zipped({mask}, [&](Span<IndexMaskSegment> segments) {
      EXPECT_FALSE(segments.is_empty());
      calls_num++;
      return true;
    });
    EXPECT_EQ(calls_num, 2);
  }

  {
    int calls_num = 0;
    IndexMask::foreach_segment_zipped({mask, mask}, [&](Span<IndexMaskSegment> segments) {
      EXPECT_FALSE(segments.is_empty());
      EXPECT_TRUE(mask_segments_equals(segments[0], segments[1]));
      calls_num++;
      return true;
    });
    EXPECT_EQ(calls_num, 2);
  }

  {
    int calls_num = 0;
    IndexMask::foreach_segment_zipped({mask, mask, mask}, [&](Span<IndexMaskSegment> segments) {
      EXPECT_FALSE(segments.is_empty());
      EXPECT_TRUE(mask_segments_equals(segments[0], segments[1]));
      EXPECT_TRUE(mask_segments_equals(segments[0], segments[2]));
      calls_num++;
      return true;
    });
    EXPECT_EQ(calls_num, 2);
  }

  {
    int calls_num = 0;
    IndexMask::foreach_segment_zipped(
        {mask, mask, mask, mask}, [&](Span<IndexMaskSegment> segments) {
          EXPECT_FALSE(segments.is_empty());
          EXPECT_TRUE(mask_segments_equals(segments[0], segments[1]));
          EXPECT_TRUE(mask_segments_equals(segments[0], segments[2]));
          EXPECT_TRUE(mask_segments_equals(segments[0], segments[3]));
          calls_num++;
          return true;
        });
    EXPECT_EQ(calls_num, 2);
  }
}

TEST(index_mask, ZippedForeachSameSegments)
{
  IndexMaskMemory memory;
  IndexMask mask_a = IndexMask::from_initializers({0, 1, 2}, memory);
  IndexMask mask_b = IndexMask::from_initializers({3, 4, 5}, memory);
  IndexMask mask_c = IndexMask::from_initializers({6, 7, 8}, memory);
  {
    int calls_num = 0;
    IndexMask::foreach_segment_zipped({mask_a}, [&](Span<IndexMaskSegment> segments) {
      EXPECT_FALSE(segments.is_empty());
      calls_num++;
      return true;
    });
    EXPECT_EQ(calls_num, 1);
  }
  {
    int calls_num = 0;
    IndexMask::foreach_segment_zipped({mask_a, mask_b}, [&](Span<IndexMaskSegment> segments) {
      EXPECT_FALSE(segments.is_empty());
      EXPECT_EQ(segments[0].size(), segments[1].size());
      EXPECT_FALSE(mask_segments_equals(segments[0], segments[1]));
      calls_num++;
      return true;
    });
    EXPECT_EQ(calls_num, 1);
  }
  {
    int calls_num = 0;
    IndexMask::foreach_segment_zipped(
        {mask_a, mask_b, mask_c}, [&](Span<IndexMaskSegment> segments) {
          EXPECT_FALSE(segments.is_empty());
          EXPECT_EQ(segments[0].size(), segments[1].size());
          EXPECT_EQ(segments[0].size(), segments[2].size());
          EXPECT_FALSE(mask_segments_equals(segments[0], segments[1]));
          EXPECT_FALSE(mask_segments_equals(segments[0], segments[2]));
          EXPECT_FALSE(mask_segments_equals(segments[1], segments[2]));
          calls_num++;
          return true;
        });
    EXPECT_EQ(calls_num, 1);
  }
}

TEST(index_mask, ZippedForeachEqual)
{
  Span<int16_t> indices(get_static_indices_array());

  IndexMaskMemory memory;
  IndexMask mask_a = IndexMask::from_segments(
      {{0, indices.take_front(5)}, {5, indices.take_front(5)}}, memory);
  IndexMask mask_b = IndexMask::from_segments(
      {{0, indices.take_front(3)}, {3, indices.take_front(4)}, {7, indices.take_front(3)}},
      memory);
  IndexMask mask_c = IndexMask::from_segments({{0, indices.take_front(10)}}, memory);

  int index = 0;
  Array<IndexMaskSegment> reference_segments{{0, indices.take_front(3)},
                                             {3, indices.take_front(2)},
                                             {5, indices.take_front(2)},
                                             {7, indices.take_front(3)}};

  IndexMask::foreach_segment_zipped(
      {mask_a, mask_b, mask_c}, [&](Span<IndexMaskSegment> segments) {
        EXPECT_TRUE(mask_segments_equals(reference_segments[index], segments[0]));
        EXPECT_TRUE(mask_segments_equals(reference_segments[index], segments[1]));
        EXPECT_TRUE(mask_segments_equals(reference_segments[index], segments[2]));
        index++;
        return true;
      });
  EXPECT_EQ(index, 4);
}

TEST(index_mask, FromRepeatingEmpty)
{
  IndexMaskMemory memory;
  const IndexMask mask = IndexMask::from_repeating(IndexMask(), 100, 0, 10, memory);
  EXPECT_TRUE(mask.is_empty());
}

TEST(index_mask, FromRepeatingSingle)
{
  IndexMaskMemory memory;
  const IndexMask mask = IndexMask::from_repeating(IndexMask(1), 5, 10, 2, memory);
  EXPECT_EQ(mask, IndexMask::from_initializers({2, 12, 22, 32, 42}, memory));
}

TEST(index_mask, FromRepeatingSame)
{
  IndexMaskMemory memory;
  const IndexMask mask = IndexMask::from_indices<int>({4, 6, 7}, memory);
  const IndexMask repeated_mask = IndexMask::from_repeating(mask, 1, 100, 0, memory);
  EXPECT_EQ(mask, repeated_mask);
}

TEST(index_mask, FromRepeatingMultiple)
{
  IndexMaskMemory memory;
  const IndexMask mask = IndexMask::from_repeating(
      IndexMask::from_indices<int>({5, 6, 7, 50}, memory), 3, 100, 1000, memory);
  EXPECT_EQ(mask[0], 1005);
  EXPECT_EQ(mask[1], 1006);
  EXPECT_EQ(mask[2], 1007);
  EXPECT_EQ(mask[3], 1050);
  EXPECT_EQ(mask[4], 1105);
  EXPECT_EQ(mask[5], 1106);
  EXPECT_EQ(mask[6], 1107);
  EXPECT_EQ(mask[7], 1150);
  EXPECT_EQ(mask[8], 1205);
  EXPECT_EQ(mask[9], 1206);
  EXPECT_EQ(mask[10], 1207);
  EXPECT_EQ(mask[11], 1250);
}

TEST(index_mask, FromRepeatingRangeFromSingle)
{
  IndexMaskMemory memory;
  const IndexMask mask = IndexMask::from_repeating(IndexMask(IndexRange(1)), 50'000, 1, 0, memory);
  EXPECT_EQ(*mask.to_range(), IndexRange(50'000));
}

TEST(index_mask, FromRepeatingRangeFromRange)
{
  IndexMaskMemory memory;
  const IndexMask mask = IndexMask::from_repeating(
      IndexMask(IndexRange(100)), 50'000, 100, 100, memory);
  EXPECT_EQ(*mask.to_range(), IndexRange(100, 5'000'000));
}

TEST(index_mask, FromRepeatingEverySecond)
{
  IndexMaskMemory memory;
  const IndexMask mask = IndexMask::from_repeating(IndexMask(1), 500'000, 2, 0, memory);
  EXPECT_EQ(mask[0], 0);
  EXPECT_EQ(mask[1], 2);
  EXPECT_EQ(mask[2], 4);
  EXPECT_EQ(mask[3], 6);
  EXPECT_EQ(mask[20'000], 40'000);
}

TEST(index_mask, FromRepeatingMultipleRanges)
{
  IndexMaskMemory memory;
  const IndexMask mask = IndexMask::from_repeating(
      IndexMask::from_initializers({IndexRange(0, 100), IndexRange(10'000, 100)}, memory),
      5,
      100'000,
      0,
      memory);
  EXPECT_EQ(mask[0], 0);
  EXPECT_EQ(mask[1], 1);
  EXPECT_EQ(mask[2], 2);
  EXPECT_EQ(mask[100], 10'000);
  EXPECT_EQ(mask[101], 10'001);
  EXPECT_EQ(mask[102], 10'002);
  EXPECT_EQ(mask[200], 100'000);
  EXPECT_EQ(mask[201], 100'001);
  EXPECT_EQ(mask[202], 100'002);
  EXPECT_EQ(mask[300], 110'000);
  EXPECT_EQ(mask[301], 110'001);
  EXPECT_EQ(mask[302], 110'002);
}

TEST(index_mask, FromRepeatingNoRepetitions)
{
  IndexMaskMemory memory;
  const IndexMask mask = IndexMask::from_repeating(IndexMask(IndexRange(5)), 0, 100, 0, memory);
  EXPECT_TRUE(mask.is_empty());
}

TEST(index_mask, FromEveryNth)
{
  IndexMaskMemory memory;
  {
    const IndexMask mask = IndexMask::from_every_nth(2, 5, 0, memory);
    EXPECT_EQ(mask, IndexMask::from_initializers({0, 2, 4, 6, 8}, memory));
  }
  {
    const IndexMask mask = IndexMask::from_every_nth(3, 5, 100, memory);
    EXPECT_EQ(mask, IndexMask::from_initializers({100, 103, 106, 109, 112}, memory));
  }
  {
    const IndexMask mask = IndexMask::from_every_nth(4, 5, 0, memory);
    EXPECT_EQ(mask, IndexMask::from_initializers({0, 4, 8, 12, 16}, memory));
  }
  {
    const IndexMask mask = IndexMask::from_every_nth(10, 5, 100, memory);
    EXPECT_EQ(mask, IndexMask::from_initializers({100, 110, 120, 130, 140}, memory));
  }
  {
    const IndexMask mask = IndexMask::from_every_nth(1, 5, 100, memory);
    EXPECT_EQ(mask, IndexMask::from_initializers({100, 101, 102, 103, 104}, memory));
  }
  {
    const IndexMask mask = IndexMask::from_every_nth(100'000, 5, 0, memory);
    EXPECT_EQ(mask, IndexMask::from_initializers({0, 100'000, 200'000, 300'000, 400'000}, memory));
  }
}

TEST(index_mask, Shift)
{
  IndexMaskMemory memory;
  {
    const IndexMask mask;
    const IndexMask shifted_mask = mask.shift(10, memory);
    EXPECT_TRUE(shifted_mask.is_empty());
    EXPECT_EQ(mask, shifted_mask);
  }
  {
    const IndexMask mask{IndexRange(100, 10)};
    const IndexMask shifted_mask = mask.shift(1000, memory);
    EXPECT_EQ(shifted_mask.size(), 10);
    EXPECT_EQ(shifted_mask[0], 1100);
    EXPECT_EQ(shifted_mask[9], 1109);
  }
  {
    const IndexMask mask = IndexMask::from_initializers({4, 6, 7, IndexRange(100, 100)}, memory);
    const IndexMask shifted_mask = mask.shift(1000, memory).shift(-1000, memory);
    EXPECT_EQ(mask, shifted_mask);
  }
  {
    const IndexMask mask{IndexRange(100, 10)};
    const IndexMask shifted_mask = mask.shift(0, memory);
    EXPECT_EQ(mask, shifted_mask);
  }
}

TEST(index_mask, SliceAndShift)
{
  IndexMaskMemory memory;
  {
    const IndexMask mask{IndexRange(100, 10)};
    const IndexMask new_mask = mask.slice_and_shift(5, 5, 1000, memory);
    EXPECT_EQ(new_mask.size(), 5);
    EXPECT_EQ(new_mask[0], 1105);
    EXPECT_EQ(new_mask[1], 1106);
  }
  {
    const IndexMask mask = IndexMask::from_indices<int>({10, 100, 1'000, 10'000, 100'000}, memory);
    const IndexMask new_mask = mask.slice_and_shift(IndexRange(1, 4), -100, memory);
    EXPECT_EQ(new_mask.size(), 4);
    EXPECT_EQ(new_mask[0], 0);
    EXPECT_EQ(new_mask[1], 900);
    EXPECT_EQ(new_mask[2], 9'900);
    EXPECT_EQ(new_mask[3], 99'900);
  }
  {
    const IndexMask mask = IndexMask::from_indices<int>({10, 100}, memory);
    const IndexMask new_mask = mask.slice_and_shift(1, 0, 100, memory);
    EXPECT_TRUE(new_mask.is_empty());
  }
}

TEST(index_mask, IndexRangeToMaskSegments)
{
  auto test_range = [](const IndexRange range) {
    Vector<IndexMaskSegment> segments;
    index_range_to_mask_segments(range, segments);
    IndexMaskMemory memory;
    const IndexMask mask = IndexMask::from_segments(segments, memory);
    const std::optional<IndexRange> new_range = mask.to_range();
    EXPECT_TRUE(new_range.has_value());
    EXPECT_EQ(range, *new_range);
  };

  test_range(IndexRange::from_begin_size(1'000, 0));

  test_range(IndexRange::from_begin_end_inclusive(0, 10));
  test_range(IndexRange::from_begin_end_inclusive(0, 10'000));
  test_range(IndexRange::from_begin_end_inclusive(0, 100'000));
  test_range(IndexRange::from_begin_end_inclusive(0, 1'000'000));

  test_range(IndexRange::from_begin_end_inclusive(50'000, 1'000'000));
  test_range(IndexRange::from_begin_end_inclusive(999'999, 1'000'000));
  test_range(IndexRange::from_begin_end_inclusive(1'000'000, 1'000'000));
}

TEST(index_mask, FromRanges)
{
  IndexMaskMemory memory;
  Array<int> data = {5, 100, 400, 500, 100'000, 200'000};
  OffsetIndices<int> offsets(data);

  {
    const IndexMask mask = IndexMask::from_ranges(offsets, offsets.index_range(), memory);
    EXPECT_EQ(mask.size(), 199'995);
    EXPECT_EQ(*mask.to_range(), IndexRange::from_begin_end(5, 200'000));
  }
  {
    const IndexMask mask = IndexMask::from_ranges(offsets, IndexRange(0), memory);
    EXPECT_TRUE(mask.is_empty());
  }
  {
    const IndexMask mask = IndexMask::from_ranges(offsets, IndexRange(1), memory);
    EXPECT_EQ(*mask.to_range(), IndexRange::from_begin_end(5, 100));
  }
  {
    const IndexMask offsets_mask = IndexMask::from_indices(Span<int>({1, 4}), memory);
    const IndexMask mask = IndexMask::from_ranges(offsets, offsets_mask, memory);
    EXPECT_EQ(mask,
              IndexMask::from_initializers({IndexRange::from_begin_end(100, 400),
                                            IndexRange::from_begin_end(100'000, 200'000)},
                                           memory));
  }
}

}  // namespace blender::index_mask::tests
