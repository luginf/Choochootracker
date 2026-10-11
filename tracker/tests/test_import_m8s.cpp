#include "doctest.h"

#include <import/import_m8s.h>
#include <export/export_m8s.h>
#include <chipnomad_lib.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

const size_t M8S_SIZE = 0x13A3E + 128 * 215 + 4096;

std::vector<uint8_t> makeM8S(uint8_t major = 2) {
  std::vector<uint8_t> d(M8S_SIZE, 0);
  memcpy(d.data(), "M8VERSION", 9);
  d[10] = 0x51;
  d[11] = major;
  memcpy(&d[14], "/Songs/", 7);
  float bpm = 120.0f;
  memcpy(&d[14 + 128 + 1], &bpm, sizeof(bpm));
  memcpy(&d[14 + 128 + 6], "TESTSONG", 8);
  memset(&d[0x2EE], 0xFF, 256 * 8);
  for (int p = 0; p < 255; p++)
    for (int s = 0; s < 16; s++) {
      uint8_t* step = &d[0xAEE + (p * 16 + s) * 9];
      memset(step, 0xFF, 9);
    }
  memset(&d[0x9A5E], 0xFF, 255 * 32);
  return d;
}

bool writeFile(const char* path, const std::vector<uint8_t>& d) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  fwrite(d.data(), 1, d.size(), f);
  fclose(f);
  return true;
}

}

TEST_SUITE("import_m8s") {

TEST_CASE("imports song, chain, transpose and phrase notes with the same indices") {
  auto d = makeM8S();
  d[0x2EE + 2 * 8 + 1] = 3;                 // song row 2, track 1 -> chain 3
  d[0x9A5E + 3 * 32 + 0] = 7;               // chain 3 step 0 -> phrase 7
  d[0x9A5E + 3 * 32 + 1] = 0xFE;            // transpose -2
  uint8_t* s0 = &d[0xAEE + (7 * 16 + 0) * 9];
  s0[0] = 0x3C; s0[1] = 0x64; s0[2] = 0x05; // note 60, vel 100, instrument 5
  uint8_t* s1 = &d[0xAEE + (7 * 16 + 4) * 9];
  s1[0] = 0x80;                             // note off
  memcpy(&d[0x13A3E + 5 * 215 + 1], "PULSEBASS", 9);
  d[0x13A3E + 5 * 215 + 10] = 0xFF;

  const char* path = "test_import_m8s_basic.m8s";
  REQUIRE(writeFile(path, d));

  Project p;
  projectInit(&p);
  REQUIRE(projectLoadM8S(&p, path) == 0);
  remove(path);

  CHECK(strcmp(p.title, "TESTSONG") == 0);
  CHECK(p.song[2][1] == 3);
  CHECK(p.song[0][0] == EMPTY_VALUE_16);
  CHECK(p.chains[3].rows[0].phrase == 7);
  CHECK((int8_t)p.chains[3].rows[0].transpose == -2);
  CHECK(p.chains[3].rows[1].phrase == EMPTY_VALUE_16);
  CHECK(p.phrases[7].rows[0].note == 48); // M8 60 = MIDI 60 = pitch index 48
  CHECK(p.phrases[7].rows[0].volume == 0x64);
  CHECK(p.phrases[7].rows[0].instrument == 5);
  CHECK(p.phrases[7].rows[1].note == EMPTY_VALUE_8);
  CHECK(p.phrases[7].rows[4].note == NOTE_OFF);
  CHECK(strcmp(p.instruments[5].name, "PULSEBASS") == 0);
  CHECK(p.instruments[5].type == InstrumentType::AY1);
  // 120 BPM, 4 steps per beat, 6 ticks per step -> 48 ticks/s
  CHECK(p.tickRate == doctest::Approx(48.0f));
  projectFree(&p);
}

TEST_CASE("rejects files that are not M8 songs") {
  auto d = makeM8S();
  memcpy(d.data(), "NOTM8SONG", 9);
  const char* path = "test_import_m8s_bad.m8s";
  REQUIRE(writeFile(path, d));
  Project p;
  projectInit(&p);
  CHECK(projectLoadM8S(&p, path) != 0);

  auto v = makeM8S(9);
  REQUIRE(writeFile(path, v));
  CHECK(projectLoadM8S(&p, path) != 0);

  std::vector<uint8_t> tiny(100, 0);
  REQUIRE(writeFile(path, tiny));
  CHECK(projectLoadM8S(&p, path) != 0);
  remove(path);
  projectFree(&p);
}


TEST_CASE("export writes structure and notes into the template and round-trips") {
  auto tmpl = makeM8S();
  tmpl[0x13A3E + 5 * 215 + 50] = 0x5A;  // instrument data must survive untouched
  tmpl[0x1A600] = 0xA5;                 // so must anything past the instruments
  tmpl[0xAEE] = 0x30;                   // template phrase content is replaced
  const char* tmplPath = "test_export_m8s_template.m8s";
  const char* outPath = "test_export_m8s_out.m8s";
  REQUIRE(writeFile(tmplPath, tmpl));

  Project src;
  projectInit(&src);
  strcpy(src.title, "ROUNDTRIP");
  src.tickRate = 60.0f; // 150 BPM
  src.song[1][2] = 4;
  src.chains[4].rows[0].phrase = 9;
  src.chains[4].rows[0].transpose = 0xFD; // -3
  src.phrases[9].rows[0].note = 36;       // MIDI 48
  src.phrases[9].rows[0].volume = 100;
  src.phrases[9].rows[0].instrument = 3;
  src.phrases[9].rows[5].note = NOTE_OFF;
  REQUIRE(projectExportM8S(&src, tmplPath, outPath) == 0);

  FILE* f = fopen(outPath, "rb");
  REQUIRE(f);
  std::vector<uint8_t> out(M8S_SIZE);
  REQUIRE(fread(out.data(), 1, out.size(), f) == out.size());
  fclose(f);
  CHECK(out.size() == tmpl.size());
  CHECK(out[0x13A3E + 5 * 215 + 50] == 0x5A);
  CHECK(out[0x1A600] == 0xA5);
  CHECK(out[0xAEE] == 0xFF);
  CHECK(out[0x2EE + 1 * 8 + 2] == 4);
  CHECK(out[0x2EE] == 0xFF);
  const uint8_t* s0 = &out[0xAEE + (9 * 16) * 9];
  CHECK(s0[0] == 48);
  CHECK(s0[1] == 100);
  CHECK(s0[2] == 3);
  CHECK(s0[3] == 0xFF);
  CHECK(out[0xAEE + (9 * 16 + 5) * 9] == 0x80);

  Project back;
  projectInit(&back);
  REQUIRE(projectLoadM8S(&back, outPath) == 0);
  CHECK(strcmp(back.title, "ROUNDTRIP") == 0);
  CHECK(back.tickRate == doctest::Approx(60.0f));
  CHECK(back.song[1][2] == 4);
  CHECK(back.chains[4].rows[0].phrase == 9);
  CHECK(back.chains[4].rows[0].transpose == 0xFD);
  CHECK(back.phrases[9].rows[0].note == 36);
  CHECK(back.phrases[9].rows[0].volume == 100);
  CHECK(back.phrases[9].rows[0].instrument == 3);
  CHECK(back.phrases[9].rows[5].note == NOTE_OFF);

  remove(tmplPath);
  remove(outPath);
  projectFree(&src);
  projectFree(&back);
}

TEST_CASE("export refuses phrases the M8 cannot hold and bad templates") {
  auto tmpl = makeM8S();
  const char* tmplPath = "test_export_m8s_template2.m8s";
  const char* outPath = "test_export_m8s_out2.m8s";
  REQUIRE(writeFile(tmplPath, tmpl));

  Project p;
  projectInit(&p);
  p.song[0][0] = 0;
  p.chains[0].rows[0].phrase = 300;
  CHECK(projectExportM8S(&p, tmplPath, outPath) != 0);

  p.chains[0].rows[0].phrase = 1;
  memcpy(tmpl.data(), "NOTM8SONG", 9);
  REQUIRE(writeFile(tmplPath, tmpl));
  CHECK(projectExportM8S(&p, tmplPath, outPath) != 0);
  CHECK(projectExportM8S(&p, "does-not-exist.m8s", outPath) != 0);

  remove(tmplPath);
  remove(outPath);
  projectFree(&p);
}

}
