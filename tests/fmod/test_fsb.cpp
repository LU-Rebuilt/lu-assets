#include <gtest/gtest.h>
#include "fmod/fsb/fsb_reader.h"
#include "fmod/fev/fev_reader.h"

#include <array>
#include <cstring>
#include <vector>

using namespace lu::assets;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Build a minimal decrypted FSB4 header (48 bytes) with given fields.
static std::vector<uint8_t> make_fsb4_header(uint32_t num_samples,
                                              uint32_t shdrsize,
                                              uint32_t datasize,
                                              uint32_t ck0,
                                              uint32_t ck1) {
    std::vector<uint8_t> hdr(48, 0);
    // magic "FSB4"
    hdr[0]='F'; hdr[1]='S'; hdr[2]='B'; hdr[3]='4';
    auto w = [&](size_t off, uint32_t v) {
        std::memcpy(hdr.data() + off, &v, 4);
    };
    w(4,  num_samples);
    w(8,  shdrsize);
    w(12, datasize);
    w(16, 0x00040000u);  // version FSB4
    w(20, 0x00000044u);  // mode
    w(24, ck0);          // bank_checksums[0]
    w(28, ck1);          // bank_checksums[1]
    return hdr;
}

// Apply the FMOD cipher to 'n' bytes of a buffer (encryption direction).
// Encryption:  ciphertext[i] = bit_reverse(plaintext[i] XOR key[i % key_len])
// Decryption:  plaintext[i]  = bit_reverse(ciphertext[i]) XOR key[i % key_len]
// (The cipher is NOT self-inverse.)
static void encrypt_fsb_region(std::vector<uint8_t>& buf, size_t start, size_t end,
                                std::string_view key) {
    const size_t key_len = key.size();
    for (size_t i = start; i < end && i < buf.size(); ++i) {
        buf[i] = fsb_bit_reverse(buf[i] ^ static_cast<uint8_t>(key[i % key_len]));
    }
}

// Encrypt a complete FSB4 file (entire file) using the given key.
// The cipher covers the whole file — headers AND audio sample data.
static std::vector<uint8_t> encrypt_fsb4_file(const std::vector<uint8_t>& plain,
                                               std::string_view key) {
    std::vector<uint8_t> enc(plain);
    encrypt_fsb_region(enc, 0, enc.size(), key);
    return enc;
}

// ---------------------------------------------------------------------------
// Bit reversal tests
// ---------------------------------------------------------------------------

TEST(FSB, BitReverseBasic) {
    EXPECT_EQ(fsb_bit_reverse(0x01u), 0x80u);
    EXPECT_EQ(fsb_bit_reverse(0x80u), 0x01u);
    EXPECT_EQ(fsb_bit_reverse(0x02u), 0x40u);
    EXPECT_EQ(fsb_bit_reverse(0x40u), 0x02u);
    EXPECT_EQ(fsb_bit_reverse(0x00u), 0x00u);
    EXPECT_EQ(fsb_bit_reverse(0xFFu), 0xFFu);
    EXPECT_EQ(fsb_bit_reverse(0xF0u), 0x0Fu);
    EXPECT_EQ(fsb_bit_reverse(0x0Fu), 0xF0u);
}

TEST(FSB, BitReverseInvolution) {
    // bit_reverse applied twice should return original value for all bytes
    for (int b = 0; b < 256; ++b) {
        EXPECT_EQ(fsb_bit_reverse(fsb_bit_reverse(static_cast<uint8_t>(b))),
                  static_cast<uint8_t>(b))
            << "bit_reverse not involutory for byte " << b;
    }
}

// ---------------------------------------------------------------------------
// Decryption tests
// ---------------------------------------------------------------------------

TEST(FSB, DecryptHeaderKnownBytes) {
    // Verified against actual LU FSB file newcontent_AFV_Streaming.fsb.
    // First 4 ciphertext bytes: EE C6 0E 00
    // Key: "1024442297"
    // Expected plaintext: 46 53 42 34 = "FSB4"
    std::string_view key = FSB_LU_KEY;  // "1024442297"

    EXPECT_EQ(fsb_bit_reverse(0xEEu) ^ static_cast<uint8_t>(key[0]), 0x46u); // 'F'
    EXPECT_EQ(fsb_bit_reverse(0xC6u) ^ static_cast<uint8_t>(key[1]), 0x53u); // 'S'
    EXPECT_EQ(fsb_bit_reverse(0x0Eu) ^ static_cast<uint8_t>(key[2]), 0x42u); // 'B'
    EXPECT_EQ(fsb_bit_reverse(0x00u) ^ static_cast<uint8_t>(key[3]), 0x34u); // '4'
}

TEST(FSB, DecryptRoundTrip) {
    // Encrypt a minimal FSB4 file (header + sample headers), then decrypt and verify.
    const std::string_view key = "TESTKEY";
    // shdrsize=0 means no sample headers — only the 48-byte main header is encrypted.
    auto plain = make_fsb4_header(0, 0, 10000, 0xDEADBEEFu, 0xCAFEBABEu);
    auto enc = encrypt_fsb4_file(plain, key);

    // Magic should be scrambled after encryption
    EXPECT_NE(enc[0], 'F');

    // Decrypt in-place
    std::vector<uint8_t> dec(enc);
    bool ok = fsb_decrypt(dec, key);
    EXPECT_TRUE(ok);
    EXPECT_EQ(dec[0], 'F');
    EXPECT_EQ(dec[1], 'S');
    EXPECT_EQ(dec[2], 'B');
    EXPECT_EQ(dec[3], '4');
}

TEST(FSB, DecryptDecryptsEntireFile) {
    // The cipher covers the ENTIRE file — headers AND audio sample data.
    const std::string_view key = "TESTKEY";
    auto plain = make_fsb4_header(0, 0, 16, 0, 0);  // shdrsize=0, datasize=16
    // Append 16 bytes of "audio data"
    for (int i = 0; i < 16; ++i) plain.push_back(static_cast<uint8_t>(i + 1));
    auto enc = encrypt_fsb4_file(plain, key);

    std::vector<uint8_t> dec(enc);
    fsb_decrypt(dec, key);

    // Entire file should be restored to plaintext
    for (size_t i = 0; i < dec.size(); ++i) {
        EXPECT_EQ(dec[i], plain[i]) << "byte " << i << " not correctly decrypted";
    }
}

// ---------------------------------------------------------------------------
// Bank checksum extraction
// ---------------------------------------------------------------------------

TEST(FSB, ReadBankChecksumsFromDecryptedHeader) {
    const uint32_t ck0 = 0x44781062u;  // From newcontent_AFV_Streaming.fev bank
    const uint32_t ck1 = 0x469097CEu;
    auto hdr = make_fsb4_header(6, 496, 25332960, ck0, ck1);

    auto result = fsb_read_bank_checksums(hdr);
    EXPECT_EQ(result[0], ck0);
    EXPECT_EQ(result[1], ck1);
}

TEST(FSB, ReadBankChecksumsRejectsNonFSB4) {
    // If magic isn't FSB4, returns {0, 0}
    auto hdr = make_fsb4_header(0, 0, 0, 0xDEADu, 0xBEEFu);
    hdr[0] = 'X';  // corrupt magic

    auto result = fsb_read_bank_checksums(hdr);
    EXPECT_EQ(result[0], 0u);
    EXPECT_EQ(result[1], 0u);
}

TEST(FSB, ReadBankChecksumsTooShort) {
    std::vector<uint8_t> short_hdr = {0x46, 0x53, 0x42, 0x34};  // just "FSB4"
    auto result = fsb_read_bank_checksums(short_hdr);
    EXPECT_EQ(result[0], 0u);
    EXPECT_EQ(result[1], 0u);
}

TEST(FSB, ExtractBankChecksumsFromEncryptedData) {
    // Build a plaintext header, encrypt it, then verify extraction decrypts and reads correctly.
    // Use shdrsize=0 so the encrypted file is just 48 bytes (checksums in main header only).
    const std::string_view key = "1234567890";
    const uint32_t ck0 = 0xAABBCCDDu;
    const uint32_t ck1 = 0x11223344u;
    auto plain = make_fsb4_header(0, 0, 50000, ck0, ck1);
    auto enc = encrypt_fsb4_file(plain, key);

    auto result = fsb_extract_bank_checksums(enc, key);
    EXPECT_EQ(result[0], ck0);
    EXPECT_EQ(result[1], ck1);
}

TEST(FSB, ExtractBankChecksumsFromAlreadyDecryptedData) {
    // If the data is already decrypted (starts with FSB4 magic), no decryption is applied.
    const uint32_t ck0 = 0x12345678u;
    const uint32_t ck1 = 0x9ABCDEFu;
    auto plain = make_fsb4_header(5, 400, 20000, ck0, ck1);

    auto result = fsb_extract_bank_checksums(plain, FSB_LU_KEY);
    EXPECT_EQ(result[0], ck0);
    EXPECT_EQ(result[1], ck1);
}

// ---------------------------------------------------------------------------
// FEV bank checksum verification
// ---------------------------------------------------------------------------

TEST(FSB, FevVerifyBankChecksumMatch) {
    const uint32_t ck0 = 0x44781062u;
    const uint32_t ck1 = 0x469097CEu;

    FevBank bank;
    bank.fsb_checksum[0] = ck0;
    bank.fsb_checksum[1] = ck1;
    bank.name = "TestBank";

    auto hdr = make_fsb4_header(6, 496, 25332960, ck0, ck1);

    EXPECT_TRUE(fev_verify_bank_checksum(bank, hdr));
}

TEST(FSB, FevVerifyBankChecksumMismatch) {
    FevBank bank;
    bank.fsb_checksum[0] = 0xDEADBEEFu;
    bank.fsb_checksum[1] = 0xCAFEBABEu;
    bank.name = "WrongBank";

    // FSB has different checksums
    auto hdr = make_fsb4_header(6, 496, 25332960, 0x11111111u, 0x22222222u);

    EXPECT_FALSE(fev_verify_bank_checksum(bank, hdr));
}

TEST(FSB, FevVerifyBankChecksumOnlyOneMismatches) {
    const uint32_t ck0 = 0xAABBCCDDu;
    const uint32_t ck1 = 0x11223344u;

    FevBank bank;
    bank.fsb_checksum[0] = ck0;
    bank.fsb_checksum[1] = ck1;

    // ck0 matches but ck1 does not
    auto hdr = make_fsb4_header(5, 400, 20000, ck0, 0xFFFFFFFFu);
    EXPECT_FALSE(fev_verify_bank_checksum(bank, hdr));

    // ck1 matches but ck0 does not
    auto hdr2 = make_fsb4_header(5, 400, 20000, 0xFFFFFFFFu, ck1);
    EXPECT_FALSE(fev_verify_bank_checksum(bank, hdr2));

    // both match
    auto hdr3 = make_fsb4_header(5, 400, 20000, ck0, ck1);
    EXPECT_TRUE(fev_verify_bank_checksum(bank, hdr3));
}

// ---------------------------------------------------------------------------
// Full FSB4 parse
// ---------------------------------------------------------------------------

TEST(FSB, ParseDecryptedFSB4Header) {
    // Build a minimal FSB4 file with 2 sample headers
    std::vector<uint8_t> data;
    auto append_u32 = [&](uint32_t v) {
        uint8_t buf[4]; std::memcpy(buf, &v, 4);
        data.insert(data.end(), buf, buf+4);
    };
    auto append_u16 = [&](uint16_t v) {
        uint8_t buf[2]; std::memcpy(buf, &v, 2);
        data.insert(data.end(), buf, buf+2);
    };
    auto append_f32 = [&](float v) {
        uint8_t buf[4]; std::memcpy(buf, &v, 4);
        data.insert(data.end(), buf, buf+4);
    };
    auto append_zeros = [&](size_t n) {
        data.insert(data.end(), n, 0);
    };
    auto append_name = [&](const char* name) {
        // 30-byte null-padded name field
        size_t start = data.size();
        data.insert(data.end(), name, name + std::min((size_t)29, strlen(name)));
        while (data.size() - start < 30) data.push_back(0);
    };

    // FSB4 header (48 bytes): 8 known u32 fields (32 bytes) + 16 bytes unknown metadata
    data.insert(data.end(), {'F','S','B','4'});
    append_u32(2);           // num_samples
    append_u32(160);         // sample_header_size = 2 × 80
    append_u32(44100 * 4);   // data_size (arbitrary)
    append_u32(0x00040000);  // version
    append_u32(0x44);        // mode
    append_u32(0xDEADBEEF);  // bank_checksums[0]
    append_u32(0xCAFEBABE);  // bank_checksums[1]
    append_zeros(16);        // bytes 32-47: additional header metadata (not yet RE'd)

    // Sample 1 (80 bytes)
    append_u16(80);           // header_size
    append_name("Explosion");
    append_u32(44100 * 2);    // length_samples
    append_u32(8000);         // compressed_size
    append_u32(0);            // loop_start
    append_u32(0xFFFFFFFF);   // loop_end
    append_u32(0x02);         // mode (FMOD_LOOP_OFF)
    append_u32(44100);        // default_freq
    append_u16(255);          // default_vol
    append_u16(128);          // default_pan
    append_u16(128);          // default_pri
    append_u16(1);            // num_channels
    append_f32(1.0f);         // min_distance
    append_f32(1000.0f);      // max_distance
    append_u32(100);          // var_freq
    append_u16(0);            // var_vol
    append_u16(0);            // var_pan

    // Sample 2 (80 bytes)
    append_u16(80);
    append_name("Ambient_Loop");
    append_u32(44100 * 10);
    append_u32(32768);
    append_u32(0);
    append_u32(44100 * 10 - 1);
    append_u32(0x02);
    append_u32(22050);
    append_u16(200);
    append_u16(128);
    append_u16(64);
    append_u16(2);
    append_f32(0.5f);
    append_f32(500.0f);
    append_u32(105);
    append_u16(5);
    append_u16(10);

    FsbFile fsb = fsb_parse(data);

    EXPECT_EQ(fsb.magic, FSB4_MAGIC);
    EXPECT_EQ(fsb.num_samples, 2u);
    EXPECT_EQ(fsb.sample_header_size, 160u);
    EXPECT_EQ(fsb.version, 0x00040000u);
    EXPECT_EQ(fsb.bank_checksums[0], 0xDEADBEEFu);
    EXPECT_EQ(fsb.bank_checksums[1], 0xCAFEBABEu);
    EXPECT_EQ(fsb.data_offset, 48u + 160u);

    ASSERT_EQ(fsb.samples.size(), 2u);

    EXPECT_EQ(fsb.samples[0].name, "Explosion");
    EXPECT_EQ(fsb.samples[0].default_freq, 44100u);
    EXPECT_EQ(fsb.samples[0].num_channels, 1u);
    // default_vol raw=255 → normalized 255/255.0 = 1.0
    EXPECT_FLOAT_EQ(fsb.samples[0].default_vol, 1.0f);
    // default_pan raw=128 → normalized 128-128 = 0 (center)
    EXPECT_EQ(fsb.samples[0].default_pan, static_cast<int16_t>(0));
    EXPECT_FLOAT_EQ(fsb.samples[0].min_distance, 1.0f);
    EXPECT_FLOAT_EQ(fsb.samples[0].max_distance, 1000.0f);
    EXPECT_EQ(fsb.samples[0].var_freq, 100u);
    EXPECT_EQ(fsb.samples[0].var_vol, 0u);
    EXPECT_EQ(fsb.samples[0].var_pan, 0u);

    EXPECT_EQ(fsb.samples[1].name, "Ambient_Loop");
    EXPECT_EQ(fsb.samples[1].default_freq, 22050u);
    EXPECT_EQ(fsb.samples[1].num_channels, 2u);
    // default_vol raw=200 → normalized 200/255.0 ≈ 0.7843
    EXPECT_NEAR(fsb.samples[1].default_vol, 200.0f / 255.0f, 1e-5f);
    // default_pan raw=128 → normalized 128-128 = 0 (center)
    EXPECT_EQ(fsb.samples[1].default_pan, static_cast<int16_t>(0));
    EXPECT_FLOAT_EQ(fsb.samples[1].min_distance, 0.5f);
    EXPECT_FLOAT_EQ(fsb.samples[1].max_distance, 500.0f);
    EXPECT_EQ(fsb.samples[1].var_freq, 105u);
    EXPECT_EQ(fsb.samples[1].var_vol, 5u);
    EXPECT_EQ(fsb.samples[1].var_pan, 10u);
}

TEST(FSB, DecryptIncludesSampleHeaders) {
    // Verify that fsb_decrypt also decrypts sample headers (bytes 48..48+shdrsize).
    // Build a plaintext FSB4 with one sample header (80 bytes), then encrypt the whole
    // header+sample region, decrypt it, and check that the sample name is recovered.
    const std::string_view key = "TESTKEY";

    std::vector<uint8_t> plain;
    auto append_u32 = [&](uint32_t v) {
        uint8_t buf[4]; std::memcpy(buf, &v, 4);
        plain.insert(plain.end(), buf, buf+4);
    };
    auto append_u16 = [&](uint16_t v) {
        uint8_t buf[2]; std::memcpy(buf, &v, 2);
        plain.insert(plain.end(), buf, buf+2);
    };

    // FSB4 main header (48 bytes): magic + 11 u32 fields (bytes 32-47 are padding zeros)
    plain.insert(plain.end(), {'F','S','B','4'});
    append_u32(1);    // num_samples
    append_u32(80);   // shdrsize = 1 × 80
    append_u32(1000); // datasize
    append_u32(0x00040000u); // version
    append_u32(0u);   // mode
    append_u32(0u);   // ck0
    append_u32(0u);   // ck1
    // Padding: bytes 32-47 (4 u32 zeros to reach 48-byte header)
    append_u32(0u); append_u32(0u); append_u32(0u); append_u32(0u);

    // Sample header (80 bytes)
    append_u16(80);   // header_size
    // Name: 30 bytes null-padded
    const char* name = "TestSample";
    plain.insert(plain.end(), name, name + 10);
    plain.insert(plain.end(), 20, 0);  // pad to 30 bytes
    // Remaining 78 - 32 = 48 bytes of fields
    for (int i = 0; i < 12; ++i) append_u32(0u);

    // Encrypt header + sample headers
    auto enc = encrypt_fsb4_file(plain, key);

    // Sample name should be unreadable before decryption
    std::string name_enc(enc.begin() + 50, enc.begin() + 60);
    EXPECT_NE(name_enc, "TestSample");

    // After decryption, sample name should be readable
    fsb_decrypt(enc, key);
    std::string name_dec(enc.begin() + 50, enc.begin() + 60);
    EXPECT_EQ(name_dec, "TestSample");
}

TEST(FSB, ParseRejectsEncryptedData) {
    // Encrypted files should throw after failing magic check
    const std::string_view key = "TESTKEY";
    auto plain = make_fsb4_header(0, 0, 0, 0, 0);
    auto enc = encrypt_fsb4_file(plain, key);

    EXPECT_THROW(fsb_parse(enc), FsbError);
}

TEST(FSB, ParseSetsEncryptedFlag) {
    // Verify the error message mentions "encrypted" for bad magic
    std::vector<uint8_t> garbage(48, 0xAA);  // not FSB4/FSB5 magic
    try {
        fsb_parse(garbage);
        FAIL() << "Expected FsbError";
    } catch (const FsbError& e) {
        std::string msg(e.what());
        EXPECT_NE(msg.find("encrypted"), std::string::npos);
    }
}
