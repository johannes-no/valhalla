#include "baldr/custom_attributes_tile.h"

#include <gtest/gtest.h>

namespace {
class UnmanagedGraphMemory : public valhalla::baldr::GraphMemory {
public:
  UnmanagedGraphMemory(char* const buf, const size_t len) {
    data = buf;
    size = len;
  }
};
} // namespace

TEST(CustomAttributesTile, TileConstruction) {
  using namespace valhalla::baldr;

#pragma pack(push, 1)
  struct TestTile {
    CustomAttributesTileHeader header;
    float value0;
    float value1;
    float value2;
  };
#pragma pack(pop)

  TestTile testdata{};
  testdata.header.edge_count = 3;
  testdata.value0 = 0.1f;
  testdata.value1 = 0.5f;
  testdata.value2 = 1.0f;

  auto memory =
      std::make_unique<UnmanagedGraphMemory>(reinterpret_cast<char*>(&testdata), sizeof(TestTile));
  CustomAttributesTile tile(std::move(memory), 3);

  EXPECT_EQ(tile.edge_count(), 3u);
  EXPECT_FLOAT_EQ(tile.value(0), 0.1f);
  EXPECT_FLOAT_EQ(tile.value(1), 0.5f);
  EXPECT_FLOAT_EQ(tile.value(2), 1.0f);
}

TEST(CustomAttributesTile, OutOfBoundsReturnsZero) {
  using namespace valhalla::baldr;

#pragma pack(push, 1)
  struct TestTile {
    CustomAttributesTileHeader header;
    float value0;
  };
#pragma pack(pop)

  TestTile testdata{};
  testdata.header.edge_count = 1;
  testdata.value0 = 0.7f;

  auto memory =
      std::make_unique<UnmanagedGraphMemory>(reinterpret_cast<char*>(&testdata), sizeof(TestTile));
  CustomAttributesTile tile(std::move(memory), 1);

  EXPECT_FLOAT_EQ(tile.value(0), 0.7f);
  EXPECT_FLOAT_EQ(tile.value(1), 0.0f);
  EXPECT_FLOAT_EQ(tile.value(999), 0.0f);
}

TEST(CustomAttributesTile, NullTileConstruction) {
  using namespace valhalla::baldr;
  CustomAttributesTile tile(nullptr, 0);

  EXPECT_FLOAT_EQ(tile.value(0), 0.0f);
  EXPECT_FLOAT_EQ(tile.value(99), 0.0f);
}

TEST(CustomAttributesTile, EdgeCountMismatchThrows) {
  using namespace valhalla::baldr;

#pragma pack(push, 1)
  struct TestTile {
    CustomAttributesTileHeader header;
    float value0;
    float value1;
  };
#pragma pack(pop)

  TestTile testdata{};
  testdata.header.edge_count = 2;
  testdata.value0 = 0.3f;
  testdata.value1 = 0.6f;

  auto memory =
      std::make_unique<UnmanagedGraphMemory>(reinterpret_cast<char*>(&testdata), sizeof(TestTile));
  // expected_edge_count doesn't match header.edge_count → should throw
  EXPECT_THROW(CustomAttributesTile(std::move(memory), 5), std::runtime_error);
}

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
