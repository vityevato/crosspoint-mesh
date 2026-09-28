#include "MeshCoreGpsParse.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

using namespace MeshProto;

void putInt24(std::vector<uint8_t>& out, int32_t v) {
  out.push_back(static_cast<uint8_t>(v >> 16));
  out.push_back(static_cast<uint8_t>(v >> 8));
  out.push_back(static_cast<uint8_t>(v));
}

// CayenneLPP GPS entry: [channel][136][lat 3][lon 3][alt 3]
void putGps(std::vector<uint8_t>& out, uint8_t channel, float lat, float lon, float alt) {
  out.push_back(channel);
  out.push_back(LPP_GPS);
  putInt24(out, static_cast<int32_t>(lat * 10000.0f));
  putInt24(out, static_cast<int32_t>(lon * 10000.0f));
  putInt24(out, static_cast<int32_t>(alt * 100.0f));
}

void putVoltage(std::vector<uint8_t>& out, uint8_t channel, uint16_t centivolts) {
  out.push_back(channel);
  out.push_back(116);
  out.push_back(static_cast<uint8_t>(centivolts >> 8));
  out.push_back(static_cast<uint8_t>(centivolts));
}

void putTemperature(std::vector<uint8_t>& out, uint8_t channel, int16_t deciDegrees) {
  out.push_back(channel);
  out.push_back(103);
  out.push_back(static_cast<uint8_t>(deciDegrees >> 8));
  out.push_back(static_cast<uint8_t>(deciDegrees));
}

}  // namespace

TEST(MeshCoreGpsParse, LppSizeTableMatchesFirmware) {
  EXPECT_EQ(lppDataSize(136), 9u);  // GPS
  EXPECT_EQ(lppDataSize(116), 2u);  // voltage
  EXPECT_EQ(lppDataSize(103), 2u);  // temperature
  EXPECT_EQ(lppDataSize(113), 6u);  // accelerometer
  EXPECT_EQ(lppDataSize(134), 6u);  // gyrometer
  EXPECT_EQ(lppDataSize(100), 4u);  // generic sensor
  EXPECT_EQ(lppDataSize(135), 3u);  // colour
  EXPECT_EQ(lppDataSize(240), 8u);  // polyline
  EXPECT_EQ(lppDataSize(0), 1u);    // digital input
  EXPECT_EQ(lppDataSize(200), 1u);  // unknown → firmware default skip
}

TEST(MeshCoreGpsParse, CustomVarsGpsOn) {
  const uint8_t frame[] = {0x15, 'g', 'p', 's', ':', '1'};
  bool hasGps = false;
  bool enabled = false;
  ASSERT_TRUE(parseGpsCustomVars(frame, sizeof(frame), hasGps, enabled));
  EXPECT_TRUE(hasGps);
  EXPECT_TRUE(enabled);
}

TEST(MeshCoreGpsParse, CustomVarsGpsOff) {
  const uint8_t frame[] = {0x15, 'g', 'p', 's', ':', '0'};
  bool hasGps = true;
  bool enabled = true;
  ASSERT_TRUE(parseGpsCustomVars(frame, sizeof(frame), hasGps, enabled));
  EXPECT_TRUE(hasGps);
  EXPECT_FALSE(enabled);
}

TEST(MeshCoreGpsParse, CustomVarsGpsWithOtherEntries) {
  const char payload[] = "gps:1,gps_interval:60";
  std::vector<uint8_t> frame = {0x15};
  frame.insert(frame.end(), payload, payload + sizeof(payload) - 1);
  bool hasGps = false;
  bool enabled = false;
  ASSERT_TRUE(parseGpsCustomVars(frame.data(), frame.size(), hasGps, enabled));
  EXPECT_TRUE(hasGps);
  EXPECT_TRUE(enabled);
}

TEST(MeshCoreGpsParse, CustomVarsWithoutGpsKey) {
  const char payload[] = "bme:1";
  std::vector<uint8_t> frame = {0x15};
  frame.insert(frame.end(), payload, payload + sizeof(payload) - 1);
  bool hasGps = true;
  bool enabled = true;
  ASSERT_TRUE(parseGpsCustomVars(frame.data(), frame.size(), hasGps, enabled));
  EXPECT_FALSE(hasGps);
  EXPECT_FALSE(enabled);
}

TEST(MeshCoreGpsParse, CustomVarsEmptyPayloadIsValidNoGps) {
  const uint8_t frame[] = {0x15};
  bool hasGps = true;
  bool enabled = true;
  ASSERT_TRUE(parseGpsCustomVars(frame, sizeof(frame), hasGps, enabled));
  EXPECT_FALSE(hasGps);
  EXPECT_FALSE(enabled);
}

TEST(MeshCoreGpsParse, CustomVarsEmptyFrameIsInvalid) {
  bool hasGps = false;
  bool enabled = false;
  EXPECT_FALSE(parseGpsCustomVars(nullptr, 0, hasGps, enabled));
}

TEST(MeshCoreGpsParse, LppSkipsOtherEntriesToFindGps) {
  std::vector<uint8_t> lpp;
  putVoltage(lpp, TELEM_CHANNEL_SELF, 410);
  putGps(lpp, TELEM_CHANNEL_SELF, 55.7558f, 37.6173f, 150.0f);
  putTemperature(lpp, TELEM_CHANNEL_SELF, 210);

  double lat = 0, lon = 0, alt = 0;
  bool hasGps = false;
  ASSERT_TRUE(parseLppGps(lpp.data(), lpp.size(), lat, lon, alt, hasGps));
  EXPECT_TRUE(hasGps);
  EXPECT_NEAR(lat, 55.7558f, 0.0002f);
  EXPECT_NEAR(lon, 37.6173f, 0.0002f);
  EXPECT_NEAR(alt, 150.0f, 0.01f);
}

TEST(MeshCoreGpsParse, LppNegativeCoordinatesSignExtend) {
  std::vector<uint8_t> lpp;
  putGps(lpp, TELEM_CHANNEL_SELF, -33.8688f, -70.6693f, -5.5f);

  double lat = 0, lon = 0, alt = 0;
  bool hasGps = false;
  ASSERT_TRUE(parseLppGps(lpp.data(), lpp.size(), lat, lon, alt, hasGps));
  EXPECT_TRUE(hasGps);
  EXPECT_NEAR(lat, -33.8688f, 0.0002f);
  EXPECT_NEAR(lon, -70.6693f, 0.0002f);
  EXPECT_NEAR(alt, -5.5f, 0.01f);
}

TEST(MeshCoreGpsParse, LppWithoutGpsEntry) {
  std::vector<uint8_t> lpp;
  putVoltage(lpp, TELEM_CHANNEL_SELF, 410);
  putTemperature(lpp, TELEM_CHANNEL_SELF, 210);

  double lat = 0, lon = 0, alt = 0;
  bool hasGps = true;
  ASSERT_TRUE(parseLppGps(lpp.data(), lpp.size(), lat, lon, alt, hasGps));
  EXPECT_FALSE(hasGps);
}

TEST(MeshCoreGpsParse, LppTruncatedEntryFails) {
  std::vector<uint8_t> lpp;
  putGps(lpp, TELEM_CHANNEL_SELF, 55.7558f, 37.6173f, 0.0f);
  lpp.pop_back();  // cut the altitude short

  double lat = 0, lon = 0, alt = 0;
  bool hasGps = false;
  EXPECT_FALSE(parseLppGps(lpp.data(), lpp.size(), lat, lon, alt, hasGps));
}

TEST(MeshCoreGpsParse, TelemetryResponseParsesSelfFrame) {
  const uint8_t selfKey[6] = {0xED, 0xE0, 0x88, 0xE2, 0x69, 0x44};
  std::vector<uint8_t> frame = {0x8B, 0x00};
  frame.insert(frame.end(), selfKey, selfKey + 6);
  putGps(frame, TELEM_CHANNEL_SELF, 55.7558f, 37.6173f, 0.0f);

  double lat = 0, lon = 0, alt = 0;
  bool hasGps = false;
  ASSERT_TRUE(parseTelemetryResponse(frame.data(), frame.size(), selfKey, lat, lon, alt, hasGps));
  EXPECT_TRUE(hasGps);
  EXPECT_NEAR(lat, 55.7558f, 0.0002f);
  EXPECT_NEAR(lon, 37.6173f, 0.0002f);
}

TEST(MeshCoreGpsParse, TelemetryResponseRejectsOtherSender) {
  const uint8_t selfKey[6] = {0xED, 0xE0, 0x88, 0xE2, 0x69, 0x44};
  const uint8_t otherKey[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
  std::vector<uint8_t> frame = {0x8B, 0x00};
  frame.insert(frame.end(), otherKey, otherKey + 6);
  putGps(frame, TELEM_CHANNEL_SELF, 55.7558f, 37.6173f, 0.0f);

  double lat = 0, lon = 0, alt = 0;
  bool hasGps = false;
  EXPECT_FALSE(parseTelemetryResponse(frame.data(), frame.size(), selfKey, lat, lon, alt, hasGps));
  EXPECT_FALSE(hasGps);
}

TEST(MeshCoreGpsParse, TelemetryResponseTooShort) {
  const uint8_t frame[] = {0x8B, 0x00, 0x01};
  double lat = 0, lon = 0, alt = 0;
  bool hasGps = false;
  EXPECT_FALSE(parseTelemetryResponse(frame, sizeof(frame), nullptr, lat, lon, alt, hasGps));
}
