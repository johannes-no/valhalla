#ifndef VALHALLA_BALDR_CUSTOM_ATTRIBUTES_TILE_H_
#define VALHALLA_BALDR_CUSTOM_ATTRIBUTES_TILE_H_

#include <valhalla/baldr/graphmemory.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

namespace valhalla {
namespace baldr {

// Binary format: 4-byte edge_count followed by edge_count floats.
#pragma pack(push, 1)
struct CustomAttributesTileHeader {
  uint32_t edge_count;
};
#pragma pack(pop)

static_assert(sizeof(CustomAttributesTileHeader) == 4,
              "CustomAttributesTileHeader must be exactly 4 bytes");

/**
 * Provides O(1) mmap'd access to per-edge custom float attributes.
 *
 * The .cab binary format:
 *   - 4-byte CustomAttributesTileHeader (edge_count u32)
 *   - edge_count floats (one value per directed edge)
 */
class CustomAttributesTile {
public:
  CustomAttributesTile(const CustomAttributesTile&) = delete;
  CustomAttributesTile& operator=(const CustomAttributesTile&) = delete;

  CustomAttributesTile(CustomAttributesTile&&) = default;
  CustomAttributesTile& operator=(CustomAttributesTile&&) = default;

  CustomAttributesTile(std::nullptr_t) : memory_(nullptr), header_(nullptr), data_(nullptr) {
  }

  /**
   * Construct from raw memory, validating edge_count against the tile.
   *
   * @param memory               Ownership of the mmap'd .cab data
   * @param expected_edge_count  Expected number of directed edges in this tile
   * @throws std::runtime_error  if header edge_count doesn't match or memory is too small
   */
  CustomAttributesTile(std::unique_ptr<const GraphMemory> memory, uint32_t expected_edge_count)
      : memory_(std::move(memory)), header_(nullptr), data_(nullptr) {
    if (!memory_) {
      return;
    }

    if (memory_->size < sizeof(CustomAttributesTileHeader)) {
      throw std::runtime_error("CustomAttributesTile: memory too small for header (size=" +
                               std::to_string(memory_->size) + ")");
    }

    header_ = reinterpret_cast<const volatile CustomAttributesTileHeader*>(memory_->data);

    if (header_->edge_count != expected_edge_count) {
      throw std::runtime_error(
          "CustomAttributesTile: edge_count mismatch (expected=" +
          std::to_string(expected_edge_count) + ", got=" + std::to_string(header_->edge_count) +
          ")");
    }

    const size_t required_size =
        sizeof(CustomAttributesTileHeader) + static_cast<size_t>(header_->edge_count) * sizeof(float);
    if (memory_->size < required_size) {
      throw std::runtime_error(
          "CustomAttributesTile: memory too small for data (required=" +
          std::to_string(required_size) + ", got=" + std::to_string(memory_->size) + ")");
    }

    data_ =
        reinterpret_cast<const volatile float*>(memory_->data + sizeof(CustomAttributesTileHeader));
  }

  /**
   * O(1) attribute access by edge index.
   *
   * @param edge_index  Index of the directed edge (0-based)
   * @return The float value, or 0.0f if uninitialized or out of bounds
   */
  float value(uint32_t edge_index) const volatile {
    if (!data_ || !header_ || edge_index >= header_->edge_count) {
      return 0.0f;
    }
    return data_[edge_index];
  }

  uint32_t edge_count() const volatile {
    return header_ ? header_->edge_count : 0u;
  }

  explicit operator bool() const volatile {
    return data_ != nullptr;
  }

protected:
  std::unique_ptr<const GraphMemory> memory_;

private:
  // These are const pointers to data structures - once assigned, the pointer values won't change.
  // The pointer targets are marked as const volatile because they can be modified by code outside
  // our control (another process accessing a mmap'd file for example).
  const volatile CustomAttributesTileHeader* header_;
  const volatile float* data_;
};

} // namespace baldr
} // namespace valhalla

#endif // VALHALLA_BALDR_CUSTOM_ATTRIBUTES_TILE_H_
