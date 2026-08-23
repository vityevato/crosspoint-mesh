#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "MeshCoreContactUrlParser.h"
#include "MeshCoreShareUrl.h"

namespace {

// 32 bytes of "public key" used across the tests.
const std::vector<uint8_t> kPubkey = [] {
  std::vector<uint8_t> k(32);
  for (size_t i = 0; i < k.size(); ++i) k[i] = static_cast<uint8_t>(i);
  return k;
}();

std::string keyHex() {
  std::string hex;
  char buf[3];
  for (uint8_t b : kPubkey) {
    snprintf(buf, sizeof(buf), "%02x", b);
    hex += buf;
  }
  return hex;
}

std::string contactUrl(const char* name, int type) {
  std::string url = "meshcore://contact/add?name=";
  url += name;
  url += "&public_key=";
  url += keyHex();
  url += "&type=";
  url += std::to_string(type);
  return url;
}

// Build a hex-encoded "biz card" ADVERT packet (Format 2):
// [header][path_length][pubkey 32B][timestamp 4B][signature 64B][flags 1B][name].
std::string bizCard(uint8_t flags, const char* name) {
  std::vector<uint8_t> pkt;
  pkt.push_back(0x12);  // header: direct route | payload type 0x04 (ADVERT) << 2
  pkt.push_back(0x00);  // path_length = 0 (0 hops)
  for (int i = 0; i < 32; ++i) pkt.push_back(static_cast<uint8_t>(i * 3 + 1));
  for (int i = 0; i < 4; ++i) pkt.push_back(0xAA);   // timestamp
  for (int i = 0; i < 64; ++i) pkt.push_back(0xBB);  // signature
  pkt.push_back(flags);
  for (const char* p = name; *p != '\0'; ++p) pkt.push_back(static_cast<uint8_t>(*p));

  std::string hex;
  char buf[3];
  for (uint8_t b : pkt) {
    snprintf(buf, sizeof(buf), "%02x", b);
    hex += buf;
  }
  return "meshcore://" + hex;
}

bool parse(const std::string& url, MeshCoreContact& out) { return parseMeshCoreContactUrl(url.c_str(), out); }

TEST(MeshCoreContactUrlParser, SaveRoundTripParsesAsCompanion) {
  // The exact URL saveAdvertToFile() writes (Companion -> type=1).
  char url[384] = {};
  ASSERT_GT(
      meshcore::buildMeshCoreContactShareUrl("my node", kPubkey.data(), MeshNodeType::COMPANION, url, sizeof(url)), 0);

  MeshCoreContact c{};
  ASSERT_TRUE(parse(url, c));
  EXPECT_EQ(c.type, MeshNodeType::COMPANION);
  EXPECT_EQ(std::string(c.name), "my node");
  EXPECT_EQ(memcmp(c.publicKey, kPubkey.data(), 32), 0);
}

TEST(MeshCoreContactUrlParser, WireTypeOneIsCompanion) {
  MeshCoreContact c{};
  ASSERT_TRUE(parse(contactUrl("Friend", 1), c));
  EXPECT_EQ(c.type, MeshNodeType::COMPANION);
}

TEST(MeshCoreContactUrlParser, AllWireTypesMapToInternalEnum) {
  struct Case {
    int wire;
    MeshNodeType type;
  };
  const Case kCases[] = {
      {1, MeshNodeType::COMPANION},
      {2, MeshNodeType::REPEATER},
      {3, MeshNodeType::ROOM_SERVER},
      {4, MeshNodeType::SENSOR},
  };
  for (const auto& tc : kCases) {
    MeshCoreContact c{};
    ASSERT_TRUE(parse(contactUrl("node", tc.wire), c)) << "wire type " << tc.wire;
    EXPECT_EQ(c.type, tc.type) << "wire type " << tc.wire;
  }
}

TEST(MeshCoreContactUrlParser, AbsentTypeDefaultsToCompanion) {
  const std::string url = "meshcore://contact/add?name=Friend&public_key=" + keyHex();
  MeshCoreContact c{};
  ASSERT_TRUE(parse(url, c));
  EXPECT_EQ(c.type, MeshNodeType::COMPANION);
}

TEST(MeshCoreContactUrlParser, NameIsUrlDecoded) {
  MeshCoreContact c{};
  ASSERT_TRUE(parse(contactUrl("Web+Server", 1), c));
  EXPECT_EQ(std::string(c.name), "Web Server");
}

TEST(MeshCoreContactUrlParser, PublicKeyIsHexDecoded) {
  MeshCoreContact c{};
  ASSERT_TRUE(parse(contactUrl("node", 1), c));
  EXPECT_EQ(memcmp(c.publicKey, kPubkey.data(), 32), 0);
}

TEST(MeshCoreContactUrlParser, InvalidUrlsAreRejected) {
  MeshCoreContact c{};
  EXPECT_FALSE(parse("http://contact/add?name=x&public_key=" + keyHex() + "&type=1", c));
  EXPECT_FALSE(parse("meshcore://", c));
  EXPECT_FALSE(parse("meshcore://contact/add?name=x", c));                // no public_key
  EXPECT_FALSE(parse("meshcore://contact/add?public_key=ff&type=1", c));  // key too short
}

TEST(MeshCoreContactUrlParser, BizCardCompanionParsesAsCompanion) {
  MeshCoreContact c{};
  ASSERT_TRUE(parse(bizCard(0x01, "Bob"), c));
  EXPECT_EQ(c.type, MeshNodeType::COMPANION);
  EXPECT_EQ(std::string(c.name), "Bob");
}

TEST(MeshCoreContactUrlParser, BizCardRoomServerParsesAsRoomServer) {
  // flags low nibble 0x03 is the numeric type 3 (Room Server), not bit flags.
  MeshCoreContact c{};
  ASSERT_TRUE(parse(bizCard(0x03, "Lobby"), c));
  EXPECT_EQ(c.type, MeshNodeType::ROOM_SERVER);
}

TEST(MeshCoreContactUrlParser, BizCardSensorParsesAsSensor) {
  MeshCoreContact c{};
  ASSERT_TRUE(parse(bizCard(0x04, "weather-1"), c));
  EXPECT_EQ(c.type, MeshNodeType::SENSOR);
}

TEST(MeshCoreContactUrlParser, LongNameIsTruncatedToBufferSize) {
  // MeshCoreContact::name holds 32 chars + NUL. Longer names must be truncated,
  // never overflowed (out.name[33]).
  MeshCoreContact c{};
  const std::string longName(60, 'x');
  ASSERT_TRUE(parse(contactUrl(longName.c_str(), 1), c));
  EXPECT_EQ(std::string(c.name), std::string(32, 'x'));
  EXPECT_EQ(strlen(c.name), 32u);
}

TEST(MeshCoreContactUrlParser, BizCardLongNameIsTruncatedToBufferSize) {
  MeshCoreContact c{};
  const std::string longName(60, 'y');
  ASSERT_TRUE(parse(bizCard(0x01, longName.c_str()), c));
  EXPECT_EQ(std::string(c.name), std::string(32, 'y'));
  EXPECT_EQ(strlen(c.name), 32u);
}

}  // namespace
