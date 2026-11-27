// Unit test for s40kb.h: g++ -std=gnu++17 -I. test_s40kb.cpp -o /tmp/t && /tmp/t ../s40launcher/assets/kb/pinyin.txt
#include <cstdio>
#include "s40kb.h"

static int fails = 0;
static std::string typeKeys(const std::string& keys, const std::string& method, bool newStyle = false) {
  s40kb::Keyboard k;
  k.cfg.vi = true; k.cfg.lang = "vi"; k.cfg.viMethod = method; k.cfg.newToneStyle = newStyle;
  for (char c : keys) k.typeChar(std::string(1, c));
  return k.text;
}
static void check(const std::string& keys, const std::string& method, const std::string& want, bool newStyle = false) {
  std::string got = typeKeys(keys, method, newStyle);
  bool ok = got == want;
  if (!ok) fails++;
  printf("%s %-6s %-22s -> %-18s %s\n", ok ? "OK  " : "FAIL", method.c_str(), keys.c_str(), got.c_str(), ok ? "" : ("(want " + want + ")").c_str());
}

int main(int argc, char** argv) {
  check("tieengs vieejt", "telex", "tiếng việt");
  check("dduowcj", "telex", "được");
  check("nguwowif", "telex", "người");
  check("Vieejt Nam", "telex", "Việt Nam");
  check("hoaf", "telex", "hòa");
  check("hoaf", "telex", "hoà", true);
  check("toans hocj", "telex", "toán học");
  check("quas", "telex", "quá");
  check("gif", "telex", "gì");
  check("giaf", "telex", "già");
  check("ddi", "telex", "đi");
  check("thuowr", "telex", "thuở");
  check("huow", "telex", "huơ");
  check("muwowif", "telex", "mười");
  check("tuowi", "telex", "tươi");
  check("nguowif", "telex", "người");
  check("khuyeens", "telex", "khuyến");
  check("aaa", "telex", "aa");
  check("ass", "telex", "as");
  check("w", "telex", "ư");
  check("tuowng", "telex", "tương");
  check("hoaf", "telex", "hòa");
  check("hoafn", "telex", "hoàn");
  check("quowr", "telex", "quở");
  check("ddaay laf mootj caau", "telex", "đây là một câu");
  check("Xin chaof cacs banj", "telex", "Xin chào các bạn");
  check("tie61ng vie65t", "vni", "tiếng việt");
  check("d9u7o7c5", "vni", "được");
  check("nguo7i2", "vni", "người");
  check("ho2a", "vni", "hòa");
  check("Ha2 No65i", "vni", "Hà Nội");
  if (argc > 1) {
    s40kb::Pinyin py;
    py.load(argv[1]);
    s40kb::Keyboard k;
    k.py = &py; k.cfg.zh = true; k.cfg.lang = "zh";
    for (char c : std::string("nihao")) k.typeChar(std::string(1, c));
    auto c = k.candidates();
    printf("pinyin 'nihao' -> head '%s', %zu candidates: ", py.head(k.comp).c_str(), c.size());
    for (size_t i = 0; i < c.size() && i < 8; i++) printf("%s", c[i].c_str());
    k.commitCandidate(0);
    auto c2 = k.candidates();
    printf(" | after pick: text '%s', comp '%s', next: ", k.text.c_str(), k.comp.c_str());
    for (size_t i = 0; i < c2.size() && i < 6; i++) printf("%s", c2[i].c_str());
    k.commitCandidate(0);
    printf(" | text '%s'\n", k.text.c_str());
    if (k.text != "你好") fails++;
    s40kb::Keyboard z; z.py = &py; z.cfg.zh = true; z.cfg.lang = "zh";
    for (char c : std::string("zh")) z.typeChar(std::string(1, c));
    auto c3 = z.candidates();
    printf("pinyin 'zh' (incomplete) -> %zu candidates: ", c3.size());
    for (size_t i = 0; i < c3.size() && i < 10; i++) printf("%s", c3[i].c_str());
    printf("\n");
  }
  printf("%s (%d failures)\n", fails ? "FAILED" : "ALL OK", fails);
  return fails != 0;
}
