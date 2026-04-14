#include <gtest/gtest.h>
#include "netdevil/zone/lvl/lvl_reader.h"

#include <cstring>
#include <vector>

using namespace lu::assets;

namespace {

std::vector<uint8_t> build_test_lvl() {
    std::vector<uint8_t> data;
    auto write_u8 = [&](uint8_t v) { data.push_back(v); };
    auto write_u16 = [&](uint16_t v) {
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&v),
                    reinterpret_cast<uint8_t*>(&v) + 2);
    };
    auto write_u32 = [&](uint32_t v) {
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&v),
                    reinterpret_cast<uint8_t*>(&v) + 4);
    };
    auto write_u64 = [&](uint64_t v) {
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&v),
                    reinterpret_cast<uint8_t*>(&v) + 8);
    };
    auto write_f32 = [&](float v) {
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&v),
                    reinterpret_cast<uint8_t*>(&v) + 4);
    };

    // FileInfo chunk (ID 1000)
    size_t chunk1_start = data.size();
    write_u32(LVL_CHUNK_MAGIC);
    write_u32(1000);       // chunk ID
    write_u16(1);          // version
    write_u16(0);          // type
    // total_size and start_pos - fill in after
    size_t total_size_pos = data.size();
    write_u32(0);          // placeholder total_size
    size_t start_pos_pos = data.size();
    write_u32(0);          // placeholder start_pos

    // Patch start_pos
    uint32_t data_start = static_cast<uint32_t>(data.size());
    memcpy(data.data() + start_pos_pos, &data_start, 4);

    write_u32(40);         // file version

    uint32_t total_size = static_cast<uint32_t>(data.size() - chunk1_start - 20 + 20);
    memcpy(data.data() + total_size_pos, &total_size, 4);

    // SceneObjectData chunk (ID 2001)
    size_t chunk2_start = data.size();
    write_u32(LVL_CHUNK_MAGIC);
    write_u32(2001);
    write_u16(1);
    write_u16(0);
    size_t total_size_pos2 = data.size();
    write_u32(0);
    size_t start_pos_pos2 = data.size();
    write_u32(0);

    uint32_t data_start2 = static_cast<uint32_t>(data.size());
    memcpy(data.data() + start_pos_pos2, &data_start2, 4);

    // 1 object
    write_u32(1);

    // Object data (version 40: has nodeType and glomId)
    write_u64(12345);      // object_id
    write_u32(1000);       // lot
    write_u32(0);          // node type (version >= 38)
    write_u32(0);          // glom id (version >= 32)
    write_f32(1.0f);       // position x
    write_f32(2.0f);       // position y
    write_f32(3.0f);       // position z
    write_f32(1.0f);       // rotation w (file order WXYZ)
    write_f32(0.0f);       // rotation x
    write_f32(0.0f);       // rotation y
    write_f32(0.0f);       // rotation z
    write_f32(1.5f);       // scale
    write_u32(0);          // ldf config length (empty)
    write_u32(0);          // unknown trailing

    uint32_t total_size2 = static_cast<uint32_t>(data.size() - chunk2_start - 20 + 20);
    memcpy(data.data() + total_size_pos2, &total_size2, 4);

    return data;
}

} // anonymous namespace

TEST(LVL, ParseSingleObject) {
    auto data = build_test_lvl();
    auto lvl = lvl_parse(data);

    EXPECT_EQ(lvl.version, 40u);
    ASSERT_EQ(lvl.objects.size(), 1u);

    const auto& obj = lvl.objects[0];
    EXPECT_EQ(obj.object_id, 12345u);
    EXPECT_EQ(obj.lot, 1000u);
    EXPECT_FLOAT_EQ(obj.position.x, 1.0f);
    EXPECT_FLOAT_EQ(obj.position.y, 2.0f);
    EXPECT_FLOAT_EQ(obj.position.z, 3.0f);
    EXPECT_FLOAT_EQ(obj.rotation.w, 1.0f);
    EXPECT_FLOAT_EQ(obj.rotation.x, 0.0f);
    EXPECT_FLOAT_EQ(obj.scale, 1.5f);
}

TEST(LVL, EmptyData) {
    std::vector<uint8_t> empty;
    auto lvl = lvl_parse(empty);
    EXPECT_EQ(lvl.objects.size(), 0u);
}
