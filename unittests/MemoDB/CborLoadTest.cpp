#include "memodb/Node.h"

#include "gtest/gtest.h"

using namespace memodb;

namespace {

void test_load(const Node &expected, const std::vector<uint8_t> &cbor) {
  Node actual = Node::load_cbor(cbor);
  EXPECT_EQ(expected, actual);
}

TEST(CborLoadTest, Integer) {
  test_load(Node(0), {0x00});
  test_load(Node(1), {0x01});
  test_load(Node(10), {0x0a});
  test_load(Node(23), {0x17});
  test_load(Node(24), {0x18, 0x18});
  test_load(Node(25), {0x18, 0x19});
  test_load(Node(100), {0x18, 0x64});
  test_load(Node(1000), {0x19, 0x03, 0xe8});
  test_load(Node(1000000), {0x1a, 0x00, 0x0f, 0x42, 0x40});
  test_load(Node(1000000000000),
            {0x1b, 0x00, 0x00, 0x00, 0xe8, 0xd4, 0xa5, 0x10, 0x00});
  test_load(Node(-1), {0x20});
  test_load(Node(-10), {0x29});
  test_load(Node(-100), {0x38, 0x63});
  test_load(Node(-1000), {0x39, 0x03, 0xe7});

  test_load(Node(0), {0x1b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
}

TEST(CborLoadTest, Float) {
  auto check = [](double expected, const std::vector<uint8_t> &cbor) {
    Node value = Node::load_cbor(cbor);
    ASSERT_EQ(Kind::Float, value.kind());
    double actual = value.as_float();
    if (std::isnan(expected))
      ASSERT_TRUE(std::isnan(actual));
    else
      ASSERT_EQ(expected, actual);
  };
  check(0.0, {0xf9, 0x00, 0x00});
  check(-0.0, {0xf9, 0x80, 0x00});
  check(1.0, {0xf9, 0x3c, 0x00});
  check(1.1, {0xfb, 0x3f, 0xf1, 0x99, 0x99, 0x99, 0x99, 0x99, 0x9a});
  check(1.5, {0xf9, 0x3e, 0x00});
  check(65504.0, {0xf9, 0x7b, 0xff});
  check(100000.0, {0xfa, 0x47, 0xc3, 0x50, 0x00});
  check(3.4028234663852886e+38, {0xfa, 0x7f, 0x7f, 0xff, 0xff});
  check(1.0e+300, {0xfb, 0x7e, 0x37, 0xe4, 0x3c, 0x88, 0x00, 0x75, 0x9c});
  check(5.960464477539063e-8, {0xf9, 0x00, 0x01});
  check(0.00006103515625, {0xf9, 0x04, 0x00});
  check(-4.0, {0xf9, 0xc4, 0x00});
  check(-4.1, {0xfb, 0xc0, 0x10, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66});
  check(INFINITY, {0xf9, 0x7c, 0x00});
  check(NAN, {0xf9, 0x7e, 0x00});
  check(-INFINITY, {0xf9, 0xfc, 0x00});
  check(INFINITY, {0xfa, 0x7f, 0x80, 0x00, 0x00});
  check(NAN, {0xfa, 0x7f, 0xc0, 0x00, 0x00});
  check(-INFINITY, {0xfa, 0xff, 0x80, 0x00, 0x00});
  check(INFINITY, {0xfb, 0x7f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  check(NAN, {0xfb, 0x7f, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  check(-INFINITY, {0xfb, 0xff, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
}

TEST(CborLoadTest, Bool) {
  test_load(Node(false), {0xf4});
  test_load(Node(true), {0xf5});
}

TEST(CborLoadTest, Null) { test_load(Node{nullptr}, {0xf6}); }

TEST(CborLoadTest, Undefined) { test_load(Node{}, {0xf7}); }

TEST(CborLoadTest, Bytes) {
  using bytes = std::vector<uint8_t>;
  test_load(Node(bytes{}), {0x40});
  test_load(Node(bytes{0x01, 0x02, 0x03, 0x04}),
            {0x44, 0x01, 0x02, 0x03, 0x04});
  test_load(Node(bytes{0x01, 0x02, 0x03, 0x04, 0x05}),
            {0x5f, 0x42, 0x01, 0x02, 0x43, 0x03, 0x04, 0x05, 0xff});
}

TEST(CborLoadTest, String) {
  test_load(Node(""), {0x60});
  test_load(Node("a"), {0x61, 0x61});
  test_load(Node("IETF"), {0x64, 0x49, 0x45, 0x54, 0x46});
  test_load(Node("\"\\"), {0x62, 0x22, 0x5c});
  test_load(Node("\u00fc"), {0x62, 0xc3, 0xbc});
  test_load(Node("\u6c34"), {0x63, 0xe6, 0xb0, 0xb4});
  test_load(Node("\u6c34"), {0x63, 0xe6, 0xb0, 0xb4});
  test_load(Node("\U00010151"), {0x64, 0xf0, 0x90, 0x85, 0x91});
  test_load(Node("streaming"), {0x7f, 0x65, 0x73, 0x74, 0x72, 0x65, 0x61, 0x64,
                                0x6d, 0x69, 0x6e, 0x67, 0xff});
}

TEST(CborLoadTest, List) {
  test_load(Node(node_list_arg), {0x80});
  test_load(Node(node_list_arg, {1, 2, 3}), {0x83, 0x01, 0x02, 0x03});
  test_load(Node(node_list_arg,
                 {1, Node(node_list_arg, {2, 3}), Node(node_list_arg, {4, 5})}),
            {0x83, 0x01, 0x82, 0x02, 0x03, 0x82, 0x04, 0x05});
  test_load(
      Node(node_list_arg, {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13,
                           14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25}),
      {0x98, 0x19, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
       0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12,
       0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x18, 0x18, 0x19});

  test_load(Node(node_list_arg), {0x9f, 0xff});
  test_load(Node(node_list_arg,
                 {1, Node(node_list_arg, {2, 3}), Node(node_list_arg, {4, 5})}),
            {0x9f, 0x01, 0x82, 0x02, 0x03, 0x9f, 0x04, 0x05, 0xff, 0xff});
  test_load(Node(node_list_arg,
                 {1, Node(node_list_arg, {2, 3}), Node(node_list_arg, {4, 5})}),
            {0x9f, 0x01, 0x82, 0x02, 0x03, 0x82, 0x04, 0x05, 0xff});
  test_load(Node(node_list_arg,
                 {1, Node(node_list_arg, {2, 3}), Node(node_list_arg, {4, 5})}),
            {0x83, 0x01, 0x82, 0x02, 0x03, 0x9f, 0x04, 0x05, 0xff});
  test_load(Node(node_list_arg,
                 {1, Node(node_list_arg, {2, 3}), Node(node_list_arg, {4, 5})}),
            {0x83, 0x01, 0x9f, 0x02, 0x03, 0xff, 0x82, 0x04, 0x05});
  test_load(
      Node(node_list_arg, {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13,
                           14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25}),
      {0x9f, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
       0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
       0x14, 0x15, 0x16, 0x17, 0x18, 0x18, 0x18, 0x19, 0xff});
}

TEST(CborLoadTest, Map) {
  test_load(Node(node_map_arg), {0xa0});
  test_load(Node(node_map_arg,
                 {{"a", "A"}, {"b", "B"}, {"c", "C"}, {"d", "D"}, {"e", "E"}}),
            {0xa5, 0x61, 0x61, 0x61, 0x41, 0x61, 0x62, 0x61, 0x42, 0x61, 0x63,
             0x61, 0x43, 0x61, 0x64, 0x61, 0x44, 0x61, 0x65, 0x61, 0x45});
  test_load(
      Node(node_map_arg, {{"Fun", true}, {"Amt", -2}}),
      {0xbf, 0x63, 0x46, 0x75, 0x6e, 0xf5, 0x63, 0x41, 0x6d, 0x74, 0x21, 0xff});
}

TEST(CborLoadTest, Mixed) {
  test_load(Node(node_list_arg, {"a", Node(node_map_arg, {{"b", "c"}})}),
            {0x82, 0x61, 0x61, 0xa1, 0x61, 0x62, 0x61, 0x63});
  test_load(Node(node_map_arg, {{"a", 1}, {"b", Node(node_list_arg, {2, 3})}}),
            {0xa2, 0x61, 0x61, 0x01, 0x61, 0x62, 0x82, 0x02, 0x03});
  test_load(Node(node_map_arg, {{"a", 1}, {"b", Node(node_list_arg, {2, 3})}}),
            {0xbf, 0x61, 0x61, 0x01, 0x61, 0x62, 0x9f, 0x02, 0x03, 0xff, 0xff});
  test_load(Node(node_list_arg, {"a", Node(node_map_arg, {{"b", "c"}})}),
            {0x82, 0x61, 0x61, 0xbf, 0x61, 0x62, 0x61, 0x63, 0xff});
}

TEST(CborLoadTest, Ref) {
  test_load(*CID::fromBytes({0x01, 0x71, 0x00, 0x01, 0xf6}),
            {0xd8, 0x2a, 0x46, 0x00, 0x01, 0x71, 0x00, 0x01, 0xf6});
  test_load(*CID::fromBytes({0x01, 0x71, 0xa0, 0xe4, 0x02, 0x20, 0x03, 0x17,
                             0x0a, 0x2e, 0x75, 0x97, 0xb7, 0xb7, 0xe3, 0xd8,
                             0x4c, 0x05, 0x39, 0x1d, 0x13, 0x9a, 0x62, 0xb1,
                             0x57, 0xe7, 0x87, 0x86, 0xd8, 0xc0, 0x82, 0xf2,
                             0x9d, 0xcf, 0x4c, 0x11, 0x13, 0x14}),
            {0xd8, 0x2a, 0x58, 0x27, 0x00, 0x01, 0x71, 0xa0, 0xe4, 0x02, 0x20,
             0x03, 0x17, 0x0a, 0x2e, 0x75, 0x97, 0xb7, 0xb7, 0xe3, 0xd8, 0x4c,
             0x05, 0x39, 0x1d, 0x13, 0x9a, 0x62, 0xb1, 0x57, 0xe7, 0x87, 0x86,
             0xd8, 0xc0, 0x82, 0xf2, 0x9d, 0xcf, 0x4c, 0x11, 0x13, 0x14});
}

} // end anonymous namespace
