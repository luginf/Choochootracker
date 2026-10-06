#include "doctest.h"
#include "fm_catalog.h"
#include <cstdio>
TEST_CASE("FM catalog supports ten thousand metadata entries transactionally") {
  FILE* f=fopen("test_fm_catalog.tsv","wb");REQUIRE(f);fputs("CCT-CHIP-CATALOG\t1\n",f);
  for(int i=0;i<10000;++i)fprintf(f,"24\t101\tScale fixture\tUnsorted\tSynthetic %05d\tsynthetic-%05d.cni\n",i,i);fclose(f);
  std::vector<FMPresetEntry> entries;REQUIRE(loadFMCatalog("test_fm_catalog.tsv",entries));CHECK(entries.size()==10000);CHECK(entries.back().name=="Synthetic 09999");
  f=fopen("test_fm_catalog.tsv","wb");REQUIRE(f);fputs("CCT-CHIP-CATALOG\t1\n24\t101\tBad\tUnsorted\tBad\t../escape.cni\n",f);fclose(f);
  CHECK_FALSE(loadFMCatalog("test_fm_catalog.tsv",entries));CHECK(entries.size()==10000);std::remove("test_fm_catalog.tsv");
}
TEST_CASE("FM bundled catalogue loads all currently packaged records") {
  std::vector<FMPresetEntry> entries;
  REQUIRE(loadFMCatalog("packaging/common/instruments/chips/catalog.tsv",entries));CHECK(entries.size()==1120);
}
